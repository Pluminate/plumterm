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

fn default_shell() -> String {
    if let Ok(shell) = std::env::var("SHELL") {
        if !shell.is_empty() {
            return shell;
        }
    }
    #[cfg(windows)]
    {
        for cand in [r"C:\msys64\usr\bin\zsh.exe"] {
            if std::path::Path::new(cand).exists() {
                return cand.into();
            }
        }
        return "powershell.exe".into();
    }
    #[cfg(not(windows))]
    {
        "/bin/sh".into()
    }
}

fn is_posix_shell(shell: &str) -> bool {
    let name = std::path::Path::new(shell)
        .file_name()
        .and_then(|s| s.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    matches!(name.as_str(), "zsh" | "zsh.exe" | "bash" | "bash.exe" | "sh" | "sh.exe")
}

fn normalize_cwd(cwd: &str) -> String {
    #[cfg(windows)]
    {
        let mut p = cwd.trim().to_string();
        let bytes = p.as_bytes();
        if bytes.len() >= 3 && bytes[0] == b'/' && bytes[1].is_ascii_alphabetic() && bytes[2] == b':' {
            p = p[1..].to_string();
        } else if bytes.len() >= 2 && bytes[0] == b'/' && bytes[1].is_ascii_alphabetic() && (bytes.len() == 2 || bytes[2] == b'/') {
            let drive = (bytes[1] as char).to_ascii_uppercase();
            let rest = if bytes.len() > 2 { &p[2..] } else { "/" };
            p = format!("{drive}:{rest}");
        } else if p.starts_with("/home/") {
            p = format!("C:/msys64{p}");
        }
        if !p.is_empty() && std::path::Path::new(&p).exists() {
            return p;
        }
        if let Ok(up) = std::env::var("USERPROFILE") {
            if !up.is_empty() && std::path::Path::new(&up).exists() {
                return up;
            }
        }
    }
    cwd.to_string()
}

pub fn spawn(cwd: &str, cols: u16, rows: u16, on_data: DataCb, on_exit: ExitCb, user: *mut c_void) -> Option<u64> {
    let pair = native_pty_system()
        .openpty(PtySize { rows: rows.max(1), cols: cols.max(1), pixel_width: 0, pixel_height: 0 })
        .ok()?;
    let shell = default_shell();
    let posix = is_posix_shell(&shell);
    let mut cmd = CommandBuilder::new(&shell);
    if !cfg!(windows) {
        cmd.arg("-l"); // login shell: macOS/Linux GUI-launched apps otherwise miss the user's PATH
    }
    if cfg!(windows) && posix {
        // Avoid `-l` on Windows (MSYS2 /etc/profile adds ~2s of fork overhead on every new tab).
        // Instead, preserve the inherited Windows PATH and ensure the shell's bin dir is included.
        if let Some(bin_dir) = std::path::Path::new(&shell).parent().and_then(|p| p.to_str()) {
            let cur_path = std::env::var("PATH").unwrap_or_default();
            let has_bin = cur_path.split(';').any(|p| p.eq_ignore_ascii_case(bin_dir));
            if !has_bin {
                let new_path = if cur_path.is_empty() {
                    bin_dir.to_string()
                } else {
                    format!("{bin_dir};{cur_path}")
                };
                cmd.env("PATH", new_path);
            }
        }
        if std::env::var_os("MSYSTEM").is_none() {
            cmd.env("MSYSTEM", "MSYS");
        }
        if std::env::var_os("LANG").is_none() {
            cmd.env("LANG", "en_US.UTF-8");
        }
        cmd.env("CHERE_INVOKING", "1");
    }
    cmd.cwd(normalize_cwd(cwd));
    cmd.env("TERM", "xterm-256color");
    cmd.env("COLORTERM", "truecolor");
    let child = pair.slave.spawn_command(cmd).ok()?;
    drop(pair.slave);
    let mut reader = pair.master.try_clone_reader().ok()?;
    let writer = pair.master.take_writer().ok()?;
    let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
    sessions().lock().unwrap().insert(id, Session { master: pair.master, writer, child });

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

#[cfg(windows)]
fn process_cwd(pid: u32) -> Option<String> {
    use std::ffi::c_void;
    const PROCESS_QUERY_INFORMATION: u32 = 0x0400;
    const PROCESS_VM_READ: u32 = 0x0010;

    #[repr(C)]
    struct ProcessBasicInformation {
        reserved1: *mut c_void,
        peb_base_address: *mut c_void,
        reserved2: [*mut c_void; 2],
        unique_process_id: usize,
        reserved3: *mut c_void,
    }

    #[repr(C)]
    struct UnicodeString {
        length: u16,
        maximum_length: u16,
        buffer: *mut u16,
    }

    extern "system" {
        fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
        fn CloseHandle(h: *mut c_void) -> i32;
        fn ReadProcessMemory(proc: *mut c_void, base: *const c_void, buf: *mut c_void, size: usize, read: *mut usize) -> i32;
        fn NtQueryInformationProcess(proc: *mut c_void, info_class: u32, info: *mut c_void, len: u32, ret_len: *mut u32) -> i32;
    }

    unsafe {
        let h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 0, pid);
        if h.is_null() {
            return None;
        }
        struct Guard(*mut c_void);
        impl Drop for Guard {
            fn drop(&mut self) {
                unsafe { CloseHandle(self.0); }
            }
        }
        let _guard = Guard(h);

        let mut pbi: ProcessBasicInformation = std::mem::zeroed();
        let mut ret_len = 0u32;
        if NtQueryInformationProcess(h, 0, &mut pbi as *mut _ as *mut c_void, std::mem::size_of::<ProcessBasicInformation>() as u32, &mut ret_len) != 0
            || pbi.peb_base_address.is_null()
        {
            return None;
        }

        // PEB64: ProcessParameters pointer is at offset 0x20.
        let mut proc_params: *mut c_void = std::ptr::null_mut();
        let mut bytes_read = 0usize;
        if ReadProcessMemory(h, (pbi.peb_base_address as *const u8).add(0x20) as *const c_void, &mut proc_params as *mut _ as *mut c_void, std::mem::size_of::<*mut c_void>(), &mut bytes_read) == 0
            || proc_params.is_null()
        {
            return None;
        }

        // RTL_USER_PROCESS_PARAMETERS64: CurrentDirectory.DosPath (UNICODE_STRING) is at offset 0x38.
        let mut cur_dir: UnicodeString = std::mem::zeroed();
        if ReadProcessMemory(h, (proc_params as *const u8).add(0x38) as *const c_void, &mut cur_dir as *mut _ as *mut c_void, std::mem::size_of::<UnicodeString>(), &mut bytes_read) == 0
            || cur_dir.length == 0
            || cur_dir.buffer.is_null()
        {
            return None;
        }

        let char_count = (cur_dir.length / 2) as usize;
        let mut buf = vec![0u16; char_count];
        if ReadProcessMemory(h, cur_dir.buffer as *const c_void, buf.as_mut_ptr() as *mut c_void, cur_dir.length as usize, &mut bytes_read) == 0 {
            return None;
        }
        let s = String::from_utf16_lossy(&buf).trim_end_matches(['\\', '/']).to_string();
        if s.is_empty() { None } else { Some(s) }
    }
}

#[cfg(all(not(unix), not(windows)))]
fn process_cwd(_pid: u32) -> Option<String> { None }

pub fn kill(id: u64) {
    if let Some(mut s) = sessions().lock().unwrap().remove(&id) {
        let _ = s.child.kill();
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
        let id = spawn("/tmp", 80, 24, on_data, on_exit, &*tx as *const _ as *mut _).expect("spawn");
        write(id, b"echo pl-marker-$((20+22)); exit\n");
        let mut out = String::new();
        while let Ok(m) = rx.recv_timeout(std::time::Duration::from_secs(10)) {
            match m { Some(b) => out.push_str(&String::from_utf8_lossy(&b)), None => break }
        }
        assert!(out.contains("pl-marker-42"), "output was: {out}");
    }

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
