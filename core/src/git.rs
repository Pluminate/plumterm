//! Git operations by shelling out to `git` / `gh`, with the same commands as legacy-electron/main.js
//! (arguments are passed as argv, never through a shell). Results cross the ABI as JSON.

use crate::proc::{run_env, Out};
use crate::store::{cstr, to_c};
use serde_json::{json, Value};
use std::ffi::c_char;
use std::path::{Path, PathBuf};
use std::time::Duration;

const LOCAL: Duration = Duration::from_secs(30);
const NET: Duration = Duration::from_secs(120);
const POLL: Duration = Duration::from_secs(5);
const ENV: &[(&str, &str)] = &[("GIT_TERMINAL_PROMPT", "0")];

/// Run git. Err carries stderr (or a reason) on failure. A stale `.git/index.lock` is removed and
/// the command retried once, like the Electron app's `runGit`.
fn git(root: &Path, args: &[&str], timeout: Duration) -> Result<Out, String> {
    git_retry(root, args, timeout, true)
}

fn git_retry(root: &Path, args: &[&str], timeout: Duration, retry: bool) -> Result<Out, String> {
    match run_env("git", args, Some(root), ENV, timeout) {
        None => Err("git failed to run or timed out".into()),
        Some(o) if o.ok => Ok(o),
        Some(o) => {
            if retry && o.stderr.contains("index.lock") {
                let lock = root.join(".git").join("index.lock");
                if lock.exists() && std::fs::remove_file(&lock).is_ok() {
                    return git_retry(root, args, timeout, false);
                }
            }
            Err(if o.stderr.trim().is_empty() { "git command failed".into() } else { o.stderr })
        }
    }
}

/// Nearest ancestor of `start` containing `.git`.
pub fn find_root(start: &Path) -> Option<PathBuf> {
    let mut dir = start;
    loop {
        if dir.join(".git").exists() { return Some(dir.to_path_buf()); }
        dir = dir.parent()?;
    }
}

fn is_push_rejected(msg: &str) -> bool {
    msg.contains("non-fast-forward") || msg.contains("rejected") || msg.contains("fetch first")
}

pub fn current_branch(root: &Path) -> Option<String> {
    let o = run_env("git", &["--no-optional-locks", "symbolic-ref", "--short", "-q", "HEAD"], Some(root), ENV, POLL)?;
    let b = o.stdout.trim().to_string();
    if o.ok && !b.is_empty() { Some(b) } else { None }
}

fn first_number_before(text: &str, word: &str) -> u64 {
    // "3 files changed, 10 insertions(+)" -> number immediately before `word`
    text.find(word).and_then(|i| text[..i].trim_end().rsplit(' ').next()?.parse().ok()).unwrap_or(0)
}

/// `{root, branch, gitStats: {files, insertions, deletions}|null, isBehindRemote}` or null outside a repo.
pub fn status(cwd: &Path) -> Value {
    let Some(root) = find_root(cwd) else { return Value::Null };
    let branch = current_branch(&root);
    let short = run_env("git", &["--no-optional-locks", "diff", "HEAD", "--shortstat"], Some(&root), ENV, POLL);
    let porc = run_env("git", &["--no-optional-locks", "status", "--porcelain"], Some(&root), ENV, POLL);
    let stats = match (short, porc) {
        (Some(s), Some(p)) => {
            let untracked = p.stdout.lines().filter(|l| l.starts_with("??")).count() as u64;
            let files = first_number_before(&s.stdout, "file") + untracked;
            if files > 0 {
                json!({"files": files,
                       "insertions": first_number_before(&s.stdout, "insertion"),
                       "deletions": first_number_before(&s.stdout, "deletion")})
            } else { Value::Null }
        }
        _ => Value::Null,
    };
    let behind = branch.as_deref().map(|b| {
        let range = format!("HEAD..origin/{b}");
        run_env("git", &["rev-list", "--count", &range], Some(&root), ENV, POLL)
            .filter(|o| o.ok)
            .and_then(|o| o.stdout.trim().parse::<u64>().ok())
            .unwrap_or(0) > 0
    }).unwrap_or(false);
    json!({"root": root, "branch": branch, "gitStats": stats, "isBehindRemote": behind})
}

pub fn branches(cwd: &Path) -> Vec<String> {
    let Some(root) = find_root(cwd) else { return vec![] };
    git(&root, &["branch", "--format=%(refname:short)"], LOCAL).map(|o| {
        o.stdout.lines().map(str::trim).filter(|l| !l.is_empty()).map(String::from).collect()
    }).unwrap_or_default()
}

/// Staged + unstaged tracked changes plus untracked files: `[{status, file}]`.
pub fn changed_files(cwd: &Path) -> Vec<Value> {
    let Some(root) = find_root(cwd) else { return vec![] };
    let (Ok(diff), Ok(untracked)) = (
        git(&root, &["diff", "HEAD", "--name-status"], LOCAL),
        git(&root, &["ls-files", "--others", "--exclude-standard"], LOCAL),
    ) else { return vec![] };
    let mut out = Vec::new();
    for line in diff.stdout.lines().filter(|l| !l.trim().is_empty()) {
        let parts: Vec<&str> = line.split('\t').collect();
        let status = parts[0].chars().next().map(String::from).unwrap_or_default();
        out.push(json!({"status": status, "file": parts[parts.len() - 1]})); // renames: destination
    }
    for f in untracked.stdout.lines().filter(|l| !l.trim().is_empty()) {
        out.push(json!({"status": "?", "file": f}));
    }
    out
}

/// Diff for one file: vs HEAD, else staged, else untracked as fully added. None outside a repo.
pub fn file_diff(cwd: &Path, file: &str) -> Option<String> {
    let root = find_root(cwd)?;
    let mut diff = git(&root, &["diff", "HEAD", "--", file], LOCAL).map(|o| o.stdout).unwrap_or_default();
    if diff.trim().is_empty() {
        diff = git(&root, &["diff", "--cached", "--", file], LOCAL).map(|o| o.stdout).unwrap_or_default();
    }
    if diff.trim().is_empty() {
        // exits 1 when the files differ, so read stdout regardless of status
        diff = run_env("git", &["diff", "--no-index", "/dev/null", file], Some(&root), ENV, LOCAL)
            .map(|o| o.stdout).unwrap_or_default();
    }
    Some(diff)
}

/// Commits ahead of origin/<branch> (or all of HEAD when there is no upstream): `[{hash, message}]`.
pub fn unpushed(cwd: &Path) -> Vec<Value> {
    let Some(root) = find_root(cwd) else { return vec![] };
    let Ok(b) = git(&root, &["rev-parse", "--abbrev-ref", "HEAD"], LOCAL) else { return vec![] };
    let range = format!("origin/{}..HEAD", b.stdout.trim());
    let out = git(&root, &["log", &range, "--oneline"], LOCAL)
        .or_else(|_| git(&root, &["log", "HEAD", "--oneline"], LOCAL));
    let Ok(out) = out else { return vec![] };
    out.stdout.lines().filter(|l| !l.trim().is_empty()).map(|l| {
        let (hash, msg) = l.split_once(' ').unwrap_or((l, ""));
        json!({"hash": hash, "message": msg})
    }).collect()
}

pub fn discard(cwd: &Path, file: &str) -> Value {
    let Some(root) = find_root(cwd) else { return json!({"error": "Not a git repository"}) };
    match git(&root, &["restore", "--", file], LOCAL) {
        Ok(_) => json!({"success": true}),
        Err(e) => json!({"error": e}),
    }
}

/// `git add <files>`, commit, optionally pull then push. Mirrors `terminal.commitFiles` replies.
pub fn commit(cwd: &Path, files: &[String], message: &str, push: bool, pull_first: bool) -> Value {
    let Some(root) = find_root(cwd) else { return json!({"error": "Not a git repository"}) };
    let mut add = vec!["add", "--"];
    add.extend(files.iter().map(String::as_str));
    if let Err(e) = git(&root, &add, LOCAL) { return json!({"error": e}); }
    if let Err(e) = git(&root, &["commit", "-m", message], LOCAL) { return json!({"error": e}); }
    if push {
        if pull_first {
            if let Err(e) = git(&root, &["pull"], NET) { return json!({"error": e}); }
        }
        if let Err(e) = git(&root, &["push"], NET) {
            if is_push_rejected(&e) {
                return json!({"success": true, "committed": true, "pushError": "Remote has updates", "needsPull": true});
            }
            return json!({"success": true, "committed": true, "pushError": e});
        }
    }
    json!({"success": true})
}

/// Mirrors `terminal.push`.
pub fn push(cwd: &Path, pull_first: bool) -> Value {
    if pull_first {
        if let Err(e) = git(cwd, &["pull"], NET) { return json!({"error": e, "success": false}); }
    }
    match git(cwd, &["push"], NET) {
        Ok(_) => json!({"success": true}),
        Err(e) if is_push_rejected(&e) => json!({"error": "Remote has updates", "needsPull": true, "success": false}),
        Err(e) => json!({"error": e, "success": false}),
    }
}

/// Locate the `gh` CLI: known install paths first, then a login shell lookup, else plain "gh".
pub fn gh_path() -> String {
    for p in ["/opt/homebrew/bin/gh", "/usr/local/bin/gh", "/usr/bin/gh"] {
        if Path::new(p).exists() { return p.into(); }
    }
    for sh in ["/bin/bash", "/bin/zsh"] {
        if let Some(o) = run_env(sh, &["-l", "-c", "which gh"], None, &[], Duration::from_secs(2)) {
            let p = o.stdout.trim();
            if o.ok && !p.is_empty() { return p.into(); }
        }
    }
    "gh".into()
}

/// `{number, url, title}` of the current branch's PR via `gh pr view`, or null.
pub fn pr_info(cwd: &Path) -> Value {
    let root = find_root(cwd).unwrap_or_else(|| cwd.to_path_buf());
    let Some(o) = run_env(&gh_path(), &["pr", "view", "--json", "number,url,title"], Some(&root), &[], Duration::from_secs(8))
        else { return Value::Null };
    if !o.ok || o.stdout.trim().is_empty() { return Value::Null; }
    serde_json::from_str::<Value>(&o.stdout).ok().map(|v| json!({
        "number": v["number"].as_u64().unwrap_or(0),
        "url": v["url"].as_str().unwrap_or(""),
        "title": v["title"].as_str().unwrap_or(""),
    })).unwrap_or(Value::Null)
}

// ---------- C ABI (all JSON replies freed with tp_string_free) ----------

fn p(c: *const c_char) -> PathBuf { PathBuf::from(cstr(c)) }

#[no_mangle]
pub extern "C" fn tp_git_status(cwd: *const c_char) -> *mut c_char { to_c(status(&p(cwd)).to_string()) }
#[no_mangle]
pub extern "C" fn tp_git_branches(cwd: *const c_char) -> *mut c_char { to_c(json!(branches(&p(cwd))).to_string()) }
#[no_mangle]
pub extern "C" fn tp_git_changed_files(cwd: *const c_char) -> *mut c_char { to_c(json!(changed_files(&p(cwd))).to_string()) }
#[no_mangle]
pub extern "C" fn tp_git_unpushed(cwd: *const c_char) -> *mut c_char { to_c(json!(unpushed(&p(cwd))).to_string()) }
#[no_mangle]
pub extern "C" fn tp_git_pr(cwd: *const c_char) -> *mut c_char { to_c(pr_info(&p(cwd)).to_string()) }
/// Diff text, or NULL when `cwd` is not in a repository.
#[no_mangle]
pub extern "C" fn tp_git_file_diff(cwd: *const c_char, file: *const c_char) -> *mut c_char {
    match file_diff(&p(cwd), &cstr(file)) { Some(d) => to_c(d), None => std::ptr::null_mut() }
}
#[no_mangle]
pub extern "C" fn tp_git_discard(cwd: *const c_char, file: *const c_char) -> *mut c_char {
    to_c(discard(&p(cwd), &cstr(file)).to_string())
}
/// `files_json` is a JSON array of paths.
#[no_mangle]
pub extern "C" fn tp_git_commit(cwd: *const c_char, files_json: *const c_char, message: *const c_char, push_after: i32, pull_first: i32) -> *mut c_char {
    let files: Vec<String> = serde_json::from_str(&cstr(files_json)).unwrap_or_default();
    to_c(commit(&p(cwd), &files, &cstr(message), push_after != 0, pull_first != 0).to_string())
}
#[no_mangle]
pub extern "C" fn tp_git_push(cwd: *const c_char, pull_first: i32) -> *mut c_char {
    to_c(push(&p(cwd), pull_first != 0).to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::store::test_dir;

    fn sh(dir: &Path, args: &[&str]) {
        let o = run_env("git", args, Some(dir), &[], Duration::from_secs(20)).expect("git runs");
        assert!(o.ok, "git {:?} failed: {}", args, o.stderr);
    }

    fn repo(tag: &str) -> PathBuf {
        let d = test_dir(tag);
        sh(&d, &["init", "-q", "-b", "main"]);
        sh(&d, &["config", "user.email", "t@example.com"]);
        sh(&d, &["config", "user.name", "T"]);
        sh(&d, &["config", "commit.gpgsign", "false"]);
        std::fs::write(d.join("a.txt"), "one\n").unwrap();
        sh(&d, &["add", "."]);
        sh(&d, &["commit", "-q", "-m", "init"]);
        d
    }

    #[test]
    fn root_branch_status_and_changes() {
        let d = repo("git-status");
        std::fs::create_dir_all(d.join("sub")).unwrap();
        assert_eq!(find_root(&d.join("sub")).unwrap(), d);
        assert_eq!(current_branch(&d).unwrap(), "main");
        assert!(status(&d)["gitStats"].is_null());

        std::fs::write(d.join("a.txt"), "one\ntwo\n").unwrap();
        std::fs::write(d.join("new.txt"), "x\n").unwrap();
        let s = status(&d);
        assert_eq!(s["branch"], "main");
        assert_eq!(s["gitStats"]["files"], 2);
        assert_eq!(s["gitStats"]["insertions"], 1);
        assert_eq!(s["gitStats"]["deletions"], 0);
        assert_eq!(s["isBehindRemote"], false);

        let ch = changed_files(&d);
        assert!(ch.contains(&json!({"status": "M", "file": "a.txt"})));
        assert!(ch.contains(&json!({"status": "?", "file": "new.txt"})));
        assert_eq!(branches(&d), vec!["main"]);
        assert!(status(&test_dir("git-none")).is_null());
    }

    #[test]
    fn diff_tracked_and_untracked() {
        let d = repo("git-diff");
        std::fs::write(d.join("a.txt"), "one\ntwo\n").unwrap();
        std::fs::write(d.join("new.txt"), "hello\n").unwrap();
        assert!(file_diff(&d, "a.txt").unwrap().contains("+two"));
        assert!(file_diff(&d, "new.txt").unwrap().contains("+hello"));
        assert!(file_diff(&test_dir("git-diff-none"), "a.txt").is_none());
    }

    #[test]
    fn commit_discard_unpushed() {
        let d = repo("git-commit");
        std::fs::write(d.join("b.txt"), "b\n").unwrap();
        let r = commit(&d, &["b.txt".into()], "add \"b\" file", false, false);
        assert_eq!(r, json!({"success": true}));
        let log = unpushed(&d); // no upstream: falls back to the whole history
        assert_eq!(log[0]["message"], "add \"b\" file");
        assert_eq!(log.len(), 2);

        std::fs::write(d.join("a.txt"), "changed\n").unwrap();
        assert_eq!(discard(&d, "a.txt"), json!({"success": true}));
        assert_eq!(std::fs::read_to_string(d.join("a.txt")).unwrap(), "one\n");
        assert!(discard(&test_dir("git-disc-none"), "a.txt")["error"].is_string());
        assert!(commit(&d, &["nope.txt".into()], "m", false, false)["error"].is_string());
    }

    #[test]
    fn push_flow_and_needs_pull() {
        let d = repo("git-push");
        let bare = test_dir("git-bare");
        sh(&bare, &["init", "-q", "--bare", "-b", "main"]);
        sh(&d, &["remote", "add", "origin", bare.to_str().unwrap()]);
        sh(&d, &["push", "-q", "-u", "origin", "main"]);

        std::fs::write(d.join("c.txt"), "c\n").unwrap();
        assert_eq!(commit(&d, &["c.txt".into()], "c", true, false), json!({"success": true}));
        assert!(unpushed(&d).is_empty());

        // a second clone pushes first, so our next push is rejected
        let other = test_dir("git-other");
        std::fs::remove_dir_all(&other).unwrap();
        let parent = other.parent().unwrap();
        sh(parent, &["clone", "-q", bare.to_str().unwrap(), other.to_str().unwrap()]);
        sh(&other, &["config", "user.email", "o@example.com"]);
        sh(&other, &["config", "user.name", "O"]);
        std::fs::write(other.join("o.txt"), "o\n").unwrap();
        sh(&other, &["add", "."]);
        sh(&other, &["commit", "-q", "-m", "other"]);
        sh(&other, &["push", "-q"]);

        std::fs::write(d.join("d.txt"), "d\n").unwrap();
        let r = commit(&d, &["d.txt".into()], "d", true, false);
        assert_eq!(r["committed"], true);
        assert_eq!(r["needsPull"], true);
        let p1 = push(&d, false);
        assert_eq!(p1["needsPull"], true);
        sh(&d, &["config", "pull.rebase", "false"]);
        assert_eq!(push(&d, true), json!({"success": true}));
    }

    #[test]
    fn stale_index_lock_is_cleared() {
        let d = repo("git-lock");
        std::fs::write(d.join("l.txt"), "l\n").unwrap();
        std::fs::write(d.join(".git/index.lock"), "").unwrap();
        assert_eq!(commit(&d, &["l.txt".into()], "l", false, false), json!({"success": true}));
    }

    #[test]
    fn counts_parse() {
        let s = " 3 files changed, 10 insertions(+), 2 deletions(-)";
        assert_eq!(first_number_before(s, "file"), 3);
        assert_eq!(first_number_before(s, "insertion"), 10);
        assert_eq!(first_number_before(s, "deletion"), 2);
        assert_eq!(first_number_before("", "file"), 0);
    }
}
