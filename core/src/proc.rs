//! Run external commands with a timeout and captured output (shared by safety, launch, git, ai).

use std::io::Read;
use std::path::Path;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

pub struct Out {
    pub ok: bool,
    pub stdout: String,
    pub stderr: String,
}

/// Run `prog args` (optionally in `cwd`) and wait up to `timeout`. None if it could not start or timed out.
pub fn run(prog: &str, args: &[&str], cwd: Option<&Path>, timeout: Duration) -> Option<Out> {
    run_env(prog, args, cwd, &[], timeout)
}

/// Like [`run`], with extra environment variables.
pub fn run_env(prog: &str, args: &[&str], cwd: Option<&Path>, env: &[(&str, &str)], timeout: Duration) -> Option<Out> {
    let mut cmd = Command::new(prog);
    hide_console(&mut cmd);
    cmd.envs(env.iter().copied());
    cmd.args(args).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
    if let Some(c) = cwd { cmd.current_dir(c); }
    let mut child = cmd.spawn().ok()?;
    let mut so = child.stdout.take()?;
    let mut se = child.stderr.take()?;
    let t1 = std::thread::spawn(move || { let mut b = Vec::new(); let _ = so.read_to_end(&mut b); b });
    let t2 = std::thread::spawn(move || { let mut b = Vec::new(); let _ = se.read_to_end(&mut b); b });
    let start = Instant::now();
    let status = loop {
        match child.try_wait() {
            Ok(Some(s)) => break s,
            Ok(None) if start.elapsed() < timeout => std::thread::sleep(Duration::from_millis(5)),
            _ => { let _ = child.kill(); let _ = child.wait(); return None; }
        }
    };
    Some(Out {
        ok: status.success(),
        stdout: String::from_utf8_lossy(&t1.join().unwrap_or_default()).into_owned(),
        stderr: String::from_utf8_lossy(&t2.join().unwrap_or_default()).into_owned(),
    })
}

// A GUI app has no console, so without this every console child would flash its own window.
#[cfg(windows)]
fn hide_console(cmd: &mut Command) {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    cmd.creation_flags(CREATE_NO_WINDOW);
}

#[cfg(not(windows))]
fn hide_console(_cmd: &mut Command) {}

/// Start `prog args` without waiting for it, for GUI apps that keep running. True if it started.
#[cfg(windows)]
pub fn spawn_detached(prog: &Path, args: &[&str]) -> bool {
    let mut cmd = Command::new(prog);
    hide_console(&mut cmd);
    cmd.args(args).stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null()).spawn().is_ok()
}

/// First `name` found on PATH.
#[cfg(windows)]
pub fn find_in_path(name: &str) -> Option<std::path::PathBuf> {
    let path = std::env::var_os("PATH")?;
    std::env::split_paths(&path).map(|d| d.join(name)).find(|p| p.is_file())
}

/// PowerShell 7 when installed, else the Windows PowerShell that ships with the OS. `$SHELL` is
/// ignored: Git Bash sets it to a POSIX path that Windows cannot launch.
#[cfg(windows)]
pub fn default_shell() -> String {
    if find_in_path("pwsh.exe").is_some() { "pwsh.exe".into() } else { "powershell.exe".into() }
}

/// Pick a shell that exists on this machine: $SHELL, then zsh/bash, then sh.
#[cfg(not(windows))]
pub fn default_shell() -> String {
    let env = std::env::var("SHELL").ok();
    let cands = [env.as_deref(), Some("/bin/zsh"), Some("/usr/bin/zsh"), Some("/bin/bash"), Some("/usr/bin/bash"), Some("/bin/sh")];
    cands.iter().flatten().find(|c| !c.is_empty() && Path::new(c).exists()).map(|s| s.to_string()).unwrap_or_else(|| "/bin/sh".into())
}
