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

/// Pick a shell that exists on this machine: $SHELL, then zsh/bash, then sh.
pub fn default_shell() -> String {
    let env = std::env::var("SHELL").ok();
    let cands = [env.as_deref(), Some("/bin/zsh"), Some("/usr/bin/zsh"), Some("/bin/bash"), Some("/usr/bin/bash"), Some("/bin/sh")];
    cands.iter().flatten().find(|c| !c.is_empty() && Path::new(c).exists()).map(|s| s.to_string()).unwrap_or_else(|| "/bin/sh".into())
}
