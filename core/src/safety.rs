//! Command safety: syntax verification and "dangerous command" detection
//! (ported from utils/commandParser.js and main.js `terminal.isDangerousCommand`).

use crate::proc::{default_shell, run};
use crate::store::cstr;
use std::ffi::c_char;
use std::time::Duration;

/// True if `command` parses as valid sh (`/bin/sh -n -c`). Empty/blank is false.
pub fn verify_command(command: &str) -> bool {
    if command.trim().is_empty() { return false; }
    run("/bin/sh", &["-n", "-c", command], None, Duration::from_secs(2)).map(|o| o.ok).unwrap_or(false)
}

/// `\b(git|cd)\b` without a regex dependency.
pub(crate) fn mentions_git_or_cd(text: &str) -> bool {
    let b = text.as_bytes();
    let word = |c: u8| c.is_ascii_alphanumeric() || c == b'_';
    for w in ["git", "cd"] {
        let mut from = 0;
        while let Some(i) = text[from..].find(w) {
            let s = from + i;
            let e = s + w.len();
            if (s == 0 || !word(b[s - 1])) && (e >= b.len() || !word(b[e])) { return true; }
            from = s + 1;
        }
    }
    false
}

/// `git`/`cd` are dangerous to auto-rerun, directly or through an alias/function that expands to them.
pub fn is_dangerous(command: &str) -> bool {
    let trimmed = command.trim();
    let first = match trimmed.split_whitespace().next() { Some(f) => f, None => return false };
    if first == "git" || first == "cd" { return true; }
    // The JS interpolated `first` into a shell string unescaped. Only look up plain command names.
    if !first.chars().all(|c| c.is_ascii_alphanumeric() || "_.+-".contains(c)) { return false; }
    let script = format!("alias {first}; which {first}; type {first}");
    // JS had no timeout; an interactive shell can hang on rc files, so cap it.
    let out = run(&default_shell(), &["-i", "-c", &script], None, Duration::from_secs(5));
    out.map(|o| mentions_git_or_cd(&o.stdout.to_lowercase())).unwrap_or(false)
}

#[no_mangle]
pub extern "C" fn tp_verify_command(cmd: *const c_char) -> i32 { verify_command(&cstr(cmd)) as i32 }

#[no_mangle]
pub extern "C" fn tp_is_dangerous(cmd: *const c_char) -> i32 { is_dangerous(&cstr(cmd)) as i32 }

#[cfg(test)]
mod tests {
    use super::*;

    // Ported from legacy-electron/tests/terminal.test.js
    #[test]
    fn valid_command() { assert!(verify_command("ls -la")); }
    #[test]
    fn syntax_error() { assert!(!verify_command("if [ -z \"$VAR\" ]; then echo \"missing fi\"")); }
    #[test]
    fn blank_is_invalid() { assert!(!verify_command("   ")); assert!(!verify_command("")); }
    #[test]
    fn null_pointer_is_invalid() { assert_eq!(tp_verify_command(std::ptr::null()), 0); }

    #[test]
    fn direct_git_and_cd_are_dangerous() {
        assert!(is_dangerous("git status"));
        assert!(is_dangerous("  cd /tmp"));
        assert!(is_dangerous("cd"));
        assert!(!is_dangerous(""));
        assert!(!is_dangerous("   "));
    }

    #[test]
    fn word_boundaries() {
        assert!(mentions_git_or_cd("alias gs='git status'"));
        assert!(mentions_git_or_cd("ll is an alias for cd"));
        assert!(mentions_git_or_cd("/usr/bin/git"));
        assert!(!mentions_git_or_cd("digit abcd cdrom github"));
    }

    #[test]
    fn plain_command_not_dangerous_and_odd_names_skipped() {
        assert!(!is_dangerous("echo hello"));
        assert!(!is_dangerous("$(rm -rf /) x")); // not looked up, never executed
    }
}
