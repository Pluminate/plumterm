//! Open a path in external apps (ported from main.js openInVSCode/IntelliJ/GithubDesktop/Finder).
//! Results are JSON like the Electron IPC replies: `{"installed":bool,"opened":bool}`.

use crate::proc::run;
use crate::store::{cstr, to_c};
use serde_json::json;
use std::ffi::c_char;
use std::path::Path;
use std::time::Duration;

const T: Duration = Duration::from_secs(10);

fn open_app(app: &str, path: &str) -> bool {
    run("open", &["-a", app, path], None, T).map(|o| o.ok).unwrap_or(false)
}

fn vscode(path: &str) -> serde_json::Value {
    // `code` is tried through the login-less PATH first, like the JS exec(`code "…"`).
    if run("code", &[path], None, T).map(|o| o.ok).unwrap_or(false) {
        return json!({"installed": true, "opened": true});
    }
    let installed = ["/Applications/Visual Studio Code.app", "/usr/local/bin/code", "/opt/homebrew/bin/code"]
        .iter().any(|p| Path::new(p).exists());
    if installed && open_app("Visual Studio Code", path) {
        return json!({"installed": true, "opened": true});
    }
    json!({"installed": installed, "opened": false})
}

fn intellij(path: &str) -> serde_json::Value {
    let apps = [("/Applications/IntelliJ IDEA.app", "IntelliJ IDEA"),
                ("/Applications/IntelliJ IDEA Community Edition.app", "IntelliJ IDEA Community Edition")];
    match apps.iter().find(|(p, _)| Path::new(p).exists()) {
        None => json!({"installed": false, "opened": false}),
        Some((_, name)) => json!({"installed": true, "opened": open_app(name, path)}),
    }
}

fn github_desktop(path: &str) -> serde_json::Value {
    if !Path::new("/Applications/GitHub Desktop.app").exists() { return json!({"installed": false}); }
    json!({"installed": true, "opened": open_app("GitHub Desktop", path)})
}

fn finder(path: &str) -> serde_json::Value {
    let tool = if cfg!(target_os = "macos") { "open" } else { "xdg-open" };
    json!({"opened": run(tool, &[path], None, T).map(|o| o.ok).unwrap_or(false)})
}

/// `kind`: vscode | intellij | githubdesktop | finder.
pub fn open_in(kind: &str, path: &str) -> serde_json::Value {
    match kind {
        "vscode" => vscode(path),
        "intellij" => intellij(path),
        "githubdesktop" => github_desktop(path),
        "finder" => finder(path),
        _ => json!({"installed": false, "opened": false, "error": "unknown kind"}),
    }
}

/// Returns JSON (free with `tp_string_free`).
#[no_mangle]
pub extern "C" fn tp_open_in(kind: *const c_char, path: *const c_char) -> *mut c_char {
    to_c(open_in(&cstr(kind), &cstr(path)).to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unknown_kind_reports_error() {
        let v = open_in("nope", "/tmp");
        assert_eq!(v["opened"], false);
        assert_eq!(v["error"], "unknown kind");
    }

    #[test]
    fn missing_app_means_not_installed_or_not_opened() {
        // Never launches anything: a nonexistent path makes every opener fail or report not installed.
        let v = open_in("intellij", "/definitely/not/here");
        assert!(v["installed"] == false || v["opened"] == false);
    }
}
