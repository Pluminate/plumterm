//! Pseudo-terminal sessions. Output is pushed to the host through a C callback
//! invoked from a reader thread; the host must marshal it onto its own thread.

use portable_pty::{native_pty_system, Child, CommandBuilder, MasterPty, PtySize};
use std::collections::HashMap;
use std::ffi::{c_char, c_void, CStr};
use std::io::{Read, Write};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};

pub type DataCb = extern "C" fn(user: *mut c_void, id: u64, data: *const u8, len: usize);
pub type ExitCb = extern "C" fn(user: *mut c_void, id: u64);

struct Session {
    master: Box<dyn MasterPty + Send>,
    writer: Box<dyn Write + Send>,
    child: Box<dyn Child + Send + Sync>,
}

static NEXT_ID: AtomicU64 = AtomicU64::new(1);
fn sessions() -> &'static Mutex<HashMap<u64, Session>> {
    static S: OnceLock<Mutex<HashMap<u64, Session>>> = OnceLock::new();
    S.get_or_init(|| Mutex::new(HashMap::new()))
}

// Raw pointer made Send so the reader thread can hand it back to the host untouched.
struct User(*mut c_void);
unsafe impl Send for User {}

#[cfg(not(windows))]
fn shell_command() -> CommandBuilder {
    let mut cmd = CommandBuilder::new(std::env::var("SHELL").unwrap_or_else(|_| "/bin/sh".into()));
    cmd.arg("-l"); // login shell: GUI-launched apps otherwise miss the user's PATH
    cmd
}

// `Set-Location` does not change PowerShell's process directory, so the OS cannot tell us the
// current folder. Wrap the user's prompt (their profile has already run) to report it as OSC 7.
#[cfg(windows)]
const REPORT_CWD: &str = "$global:__plumPrompt = $function:prompt; function global:prompt { \
    $l = Get-Location; \
    if ($l.Provider.Name -eq 'FileSystem') { \
        [Console]::Write([char]27 + ']7;' + ([Uri]$l.ProviderPath).AbsoluteUri + [char]7) \
    }; \
    & $global:__plumPrompt }";

#[cfg(windows)]
fn shell_command() -> CommandBuilder {
    let mut cmd = CommandBuilder::new(crate::proc::default_shell());
    cmd.args(["-NoLogo", "-NoExit", "-Command", REPORT_CWD]);
    cmd
}

// ConPTY keeps its output pipe open after the shell exits, so the reader never sees EOF. Closing
// the pseudo console once the shell is gone ends the reader, which then reports the exit.
#[cfg(windows)]
fn close_when_shell_exits(id: u64) {
    std::thread::spawn(move || loop {
        std::thread::sleep(std::time::Duration::from_millis(200));
        let exited = match sessions().lock().unwrap().get_mut(&id) {
            None => return, // already killed
            Some(s) => !matches!(s.child.try_wait(), Ok(None)),
        };
        if exited {
            let session = sessions().lock().unwrap().remove(&id);
            drop(session); // outside the lock: closing a pseudo console can block
            return;
        }
    });
}

pub fn spawn(cwd: &str, cols: u16, rows: u16, on_data: DataCb, on_exit: ExitCb, user: *mut c_void) -> Option<u64> {
    let pair = native_pty_system()
        .openpty(PtySize { rows: rows.max(1), cols: cols.max(1), pixel_width: 0, pixel_height: 0 })
        .ok()?;
    let mut cmd = shell_command();
    cmd.cwd(cwd);
    cmd.env("TERM", "xterm-256color");
    cmd.env("COLORTERM", "truecolor");
    let child = pair.slave.spawn_command(cmd).ok()?;
    drop(pair.slave);
    let mut reader = pair.master.try_clone_reader().ok()?;
    let writer = pair.master.take_writer().ok()?;
    let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
    sessions().lock().unwrap().insert(id, Session { master: pair.master, writer, child });
    #[cfg(windows)]
    close_when_shell_exits(id);

    let user = User(user);
    std::thread::spawn(move || {
        let user = user;
        let mut buf = [0u8; 16 * 1024];
        loop {
            match reader.read(&mut buf) {
                Ok(0) | Err(_) => break,
                Ok(n) => on_data(user.0, id, buf.as_ptr(), n),
            }
        }
        on_exit(user.0, id);
    });
    Some(id)
}

pub fn write(id: u64, data: &[u8]) {
    if let Some(s) = sessions().lock().unwrap().get_mut(&id) {
        let _ = s.writer.write_all(data).and_then(|_| s.writer.flush());
    }
}

pub fn resize(id: u64, cols: u16, rows: u16) {
    if let Some(s) = sessions().lock().unwrap().get(&id) {
        let _ = s.master.resize(PtySize { rows: rows.max(1), cols: cols.max(1), pixel_width: 0, pixel_height: 0 });
    }
}

/// Current working directory of the session's shell (None if unknown).
pub fn cwd(id: u64) -> Option<String> {
    let pid = sessions().lock().unwrap().get(&id)?.child.process_id()?;
    process_cwd(pid)
}

#[cfg(target_os = "macos")]
fn process_cwd(pid: u32) -> Option<String> {
    let mut info: libc::proc_vnodepathinfo = unsafe { std::mem::zeroed() };
    let size = std::mem::size_of::<libc::proc_vnodepathinfo>() as libc::c_int;
    let n = unsafe { libc::proc_pidinfo(pid as libc::c_int, libc::PROC_PIDVNODEPATHINFO, 0, &mut info as *mut _ as *mut c_void, size) };
    if n != size { return None; }
    let path = unsafe { CStr::from_ptr(info.pvi_cdir.vip_path.as_ptr() as *const c_char) };
    let s = path.to_string_lossy().into_owned();
    if s.is_empty() { None } else { Some(s) }
}

#[cfg(all(unix, not(target_os = "macos")))]
fn process_cwd(pid: u32) -> Option<String> {
    std::fs::read_link(format!("/proc/{pid}/cwd")).ok().map(|p| p.to_string_lossy().into_owned())
}

#[cfg(not(unix))]
fn process_cwd(_pid: u32) -> Option<String> { None }

pub fn kill(id: u64) {
    let session = sessions().lock().unwrap().remove(&id);
    if let Some(mut s) = session {
        let _ = s.child.kill();
        // Closing a pseudo console can block until its host process exits, so never do it on the
        // caller's thread: that is the UI thread, including while the app is quitting.
        #[cfg(windows)]
        std::thread::spawn(move || drop(s));
    }
}

pub unsafe fn cstr<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() { None } else { CStr::from_ptr(p).to_str().ok() }
}

#[no_mangle]
pub unsafe extern "C" fn tp_pty_spawn(
    cwd: *const c_char, cols: u16, rows: u16, on_data: DataCb, on_exit: ExitCb, user: *mut c_void,
) -> u64 {
    cstr(cwd).and_then(|c| spawn(c, cols, rows, on_data, on_exit, user)).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn tp_pty_write(id: u64, data: *const u8, len: usize) {
    if !data.is_null() {
        write(id, std::slice::from_raw_parts(data, len));
    }
}

#[no_mangle]
pub extern "C" fn tp_pty_resize(id: u64, cols: u16, rows: u16) { resize(id, cols, rows) }

/// Shell's current directory (free with `tp_string_free`), or NULL.
#[no_mangle]
pub extern "C" fn tp_pty_cwd(id: u64) -> *mut c_char {
    match cwd(id) { Some(c) => crate::store::to_c(c), None => std::ptr::null_mut() }
}

#[no_mangle]
pub extern "C" fn tp_pty_kill(id: u64) { kill(id) }

#[cfg(test)]
mod tests {
    use super::*;

    // ConPTY asks for the cursor position at startup and waits for the answer, as TerminalScreen gives.
    fn answer_cursor_query(id: u64, output: &[u8]) {
        if output.windows(4).any(|w| w == b"\x1b[6n") {
            write(id, b"\x1b[1;1R");
        }
    }

    #[test]
    fn pty_runs_a_command_and_reports_exit() {
        use std::sync::mpsc::{channel, Sender};
        extern "C" fn on_data(u: *mut c_void, _id: u64, d: *const u8, n: usize) {
            let tx = unsafe { &*(u as *const Sender<Option<Vec<u8>>>) };
            let _ = tx.send(Some(unsafe { std::slice::from_raw_parts(d, n) }.to_vec()));
        }
        extern "C" fn on_exit(u: *mut c_void, _id: u64) {
            let tx = unsafe { &*(u as *const Sender<Option<Vec<u8>>>) };
            let _ = tx.send(None);
        }
        let (tx, rx) = channel::<Option<Vec<u8>>>();
        let tx = Box::new(tx);
        let tmp = std::env::temp_dir();
        let id = spawn(tmp.to_str().unwrap(), 80, 24, on_data, on_exit, &*tx as *const _ as *mut _).expect("spawn");
        let script: &[u8] = if cfg!(windows) { b"echo pl-marker-$(20+22); exit\r" } else { b"echo pl-marker-$((20+22)); exit\n" };
        write(id, script);
        let mut out = String::new();
        while let Ok(m) = rx.recv_timeout(std::time::Duration::from_secs(10)) {
            match m {
                Some(b) => {
                    answer_cursor_query(id, &b);
                    out.push_str(&String::from_utf8_lossy(&b));
                }
                None => break,
            }
        }
        assert!(out.contains("pl-marker-42"), "output was: {out}");
    }

    #[cfg(windows)]
    #[test]
    fn prompt_reports_cwd_as_osc7_and_follows_cd() {
        use std::sync::mpsc::{channel, Sender};
        extern "C" fn on_data(u: *mut c_void, _id: u64, d: *const u8, n: usize) {
            let tx = unsafe { &*(u as *const Sender<Vec<u8>>) };
            let _ = tx.send(unsafe { std::slice::from_raw_parts(d, n) }.to_vec());
        }
        extern "C" fn on_exit(_: *mut c_void, _: u64) {}
        let (tx, rx) = channel::<Vec<u8>>();
        let tx = Box::new(tx);
        let dir = crate::store::test_dir("osc7 folder");
        let id = spawn(dir.to_str().unwrap(), 200, 24, on_data, on_exit, &*tx as *const _ as *mut _).expect("spawn");
        write(id, b"cd sub-does-not-exist; cd ..\r");
        let parent = dir.parent().unwrap().file_name().unwrap().to_str().unwrap().to_string();
        let mut out = String::new();
        let want_start = "\x1b]7;file:///";
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
        // Started in "osc7 folder" (percent-encoded), then `cd ..` must be reported too.
        while std::time::Instant::now() < deadline {
            if let Ok(b) = rx.recv_timeout(std::time::Duration::from_millis(250)) {
                answer_cursor_query(id, &b);
                out.push_str(&String::from_utf8_lossy(&b));
            }
            let reports: Vec<&str> = out.split(want_start).skip(1).collect();
            if reports.iter().any(|r| r.contains("osc7%20folder")) && reports.iter().any(|r| r.split('\x07').next().unwrap().ends_with(&parent)) {
                kill(id);
                return;
            }
        }
        kill(id);
        panic!("cwd reports missing; output was: {out:?}");
    }

    #[cfg(unix)]
    #[test]
    fn reports_shell_cwd() {
        extern "C" fn nop_data(_: *mut c_void, _: u64, _: *const u8, _: usize) {}
        extern "C" fn nop_exit(_: *mut c_void, _: u64) {}
        let id = spawn("/tmp", 80, 24, nop_data, nop_exit, std::ptr::null_mut()).expect("spawn");
        let mut got = None;
        for _ in 0..100 { // login shell may need a moment to chdir/exec
            got = cwd(id);
            if got.as_deref().map(|c| c.ends_with("/tmp")).unwrap_or(false) { break; }
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
        kill(id);
        assert!(got.unwrap().ends_with("/tmp"));
    }
}
