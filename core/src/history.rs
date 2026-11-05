//! Shell history (ported from main.js getLastShellCommand / app.getShellHistory).

use crate::store::to_c;
use std::collections::HashSet;
use std::ffi::c_char;
use std::path::Path;

/// zsh extended history line `: <ts>:<dur>;<command>` -> command.
fn zsh_cmd(line: &str) -> Option<&str> {
    let rest = line.strip_prefix(':')?.trim_start();
    let (ts, rest) = rest.split_once(':')?;
    if ts.is_empty() || !ts.bytes().all(|b| b.is_ascii_digit()) { return None; }
    let (dur, cmd) = rest.split_once(';')?;
    if dur.is_empty() || !dur.bytes().all(|b| b.is_ascii_digit()) { return None; }
    if cmd.is_empty() { None } else { Some(cmd) }
}

fn read_lines_rev(p: &Path) -> Option<Vec<String>> {
    let bytes = std::fs::read(p).ok()?;
    let text = String::from_utf8_lossy(&bytes);
    Some(text.split('\n').rev().map(|s| s.trim_end_matches('\r').to_string()).collect())
}

pub fn last_command(home: &Path) -> Option<String> {
    if let Some(lines) = read_lines_rev(&home.join(".zsh_history")) {
        for l in &lines {
            if let Some(c) = zsh_cmd(l) { let c = c.trim(); if !c.is_empty() { return Some(c.into()); } }
        }
    }
    if let Some(lines) = read_lines_rev(&home.join(".bash_history")) {
        for l in &lines { let c = l.trim(); if !c.is_empty() { return Some(c.into()); } }
    }
    None
}

/// Most recent unique commands (excluding `cd`), oldest first; zsh first, bash fills the remainder.
pub fn recent(home: &Path, limit: usize) -> Vec<String> {
    let mut seen = HashSet::new();
    let mut out: Vec<String> = Vec::new();
    let mut take = |cmd: &str, out: &mut Vec<String>| {
        let c = cmd.trim();
        if !c.is_empty() && !seen.contains(c) && !c.starts_with("cd ") && c != "cd" {
            seen.insert(c.to_string());
            out.push(c.to_string());
        }
    };
    if let Some(lines) = read_lines_rev(&home.join(".zsh_history")) {
        for l in &lines {
            if out.len() >= limit { break; }
            if let Some(c) = zsh_cmd(l) { take(c, &mut out); }
        }
    }
    if out.len() < limit {
        if let Some(lines) = read_lines_rev(&home.join(".bash_history")) {
            for l in &lines { if out.len() >= limit { break; } take(l, &mut out); }
        }
    }
    out.reverse();
    out
}

fn home() -> std::path::PathBuf { std::path::PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".into())) }

/// JSON array of recent commands (free with `tp_string_free`).
#[no_mangle]
pub extern "C" fn tp_shell_history(limit: u32) -> *mut c_char {
    to_c(serde_json::to_string(&recent(&home(), limit as usize)).unwrap())
}

/// Last command or NULL (free with `tp_string_free`).
#[no_mangle]
pub extern "C" fn tp_last_shell_command() -> *mut c_char {
    match last_command(&home()) { Some(c) => to_c(c), None => std::ptr::null_mut() }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::store::test_dir;

    #[test]
    fn zsh_history_dedupes_skips_cd_and_orders_oldest_first() {
        let h = test_dir("hist-zsh");
        std::fs::write(h.join(".zsh_history"),
            ": 1:0;ls\n: 2:0;cd /tmp\n: 3:0;make\n: 4:0;cd\n: 5:0;ls\n: 6:0;git status\n").unwrap();
        assert_eq!(recent(&h, 25), vec!["make", "ls", "git status"]);
        assert_eq!(recent(&h, 2), vec!["ls", "git status"]);
        assert_eq!(last_command(&h).unwrap(), "git status");
    }

    #[test]
    fn bash_fallback_fills_remainder() {
        let h = test_dir("hist-bash");
        std::fs::write(h.join(".zsh_history"), ": 1:0;zshcmd\n").unwrap();
        std::fs::write(h.join(".bash_history"), "bashcmd\nzshcmd\n\n").unwrap();
        assert_eq!(recent(&h, 25), vec!["bashcmd", "zshcmd"]);
    }

    #[test]
    fn last_command_bash_only_and_none() {
        let h = test_dir("hist-last");
        assert!(last_command(&h).is_none());
        std::fs::write(h.join(".bash_history"), "a\nb\n\n").unwrap();
        assert_eq!(last_command(&h).unwrap(), "b");
    }

    #[test]
    fn non_matching_zsh_lines_ignored() {
        assert_eq!(zsh_cmd(": 12:3;echo hi;there"), Some("echo hi;there"));
        assert_eq!(zsh_cmd("plain"), None);
        assert_eq!(zsh_cmd(": x:0;ls"), None);
    }
}
