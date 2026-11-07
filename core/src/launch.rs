//! Open a path in external apps (ported from main.js openInVSCode/IntelliJ/GithubDesktop/Finder).
//! Results are JSON like the Electron IPC replies: `{"installed":bool,"opened":bool}`.

use crate::store::{cstr, to_c};
use std::ffi::c_char;

#[cfg(not(windows))]
mod platform {
    use crate::proc::run;
    use serde_json::{json, Value};
    use std::path::Path;
    use std::time::Duration;

    const T: Duration = Duration::from_secs(10);

    fn open_app(app: &str, path: &str) -> bool {
        run("open", &["-a", app, path], None, T).map(|o| o.ok).unwrap_or(false)
    }

    pub fn vscode(path: &str) -> Value {
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

    pub fn intellij(path: &str) -> Value {
        let apps = [("/Applications/IntelliJ IDEA.app", "IntelliJ IDEA"),
                    ("/Applications/IntelliJ IDEA Community Edition.app", "IntelliJ IDEA Community Edition")];
        match apps.iter().find(|(p, _)| Path::new(p).exists()) {
            None => json!({"installed": false, "opened": false}),
            Some((_, name)) => json!({"installed": true, "opened": open_app(name, path)}),
        }
    }

    pub fn github_desktop(path: &str) -> Value {
        if !Path::new("/Applications/GitHub Desktop.app").exists() { return json!({"installed": false}); }
        json!({"installed": true, "opened": open_app("GitHub Desktop", path)})
    }

    pub fn file_manager(path: &str) -> Value {
        let tool = if cfg!(target_os = "macos") { "open" } else { "xdg-open" };
        json!({"opened": run(tool, &[path], None, T).map(|o| o.ok).unwrap_or(false)})
    }
}

#[cfg(windows)]
mod platform {
    use crate::proc::{find_in_path, spawn_detached};
    use serde_json::{json, Value};
    use std::path::{Path, PathBuf};

    fn env_dir(var: &str) -> Option<PathBuf> { std::env::var_os(var).map(PathBuf::from) }

    fn launch(app: Option<PathBuf>, path: &str) -> Value {
        match app {
            None => json!({"installed": false, "opened": false}),
            Some(exe) => json!({"installed": true, "opened": Path::new(path).exists() && spawn_detached(&exe, &[path])}),
        }
    }

    pub fn vscode(path: &str) -> Value {
        let known = [env_dir("LOCALAPPDATA").map(|d| d.join("Programs")), env_dir("ProgramFiles")];
        let app = find_in_path("code.cmd").or_else(|| known.iter().flatten()
            .map(|d| d.join("Microsoft VS Code").join("bin").join("code.cmd"))
            .find(|p| p.is_file()));
        launch(app, path)
    }

    pub fn intellij(path: &str) -> Value {
        // Install folders carry the version, e.g. `JetBrains\IntelliJ IDEA 2026.1\bin\idea64.exe`.
        let roots = [env_dir("ProgramFiles").map(|d| d.join("JetBrains")), env_dir("LOCALAPPDATA").map(|d| d.join("Programs"))];
        let installed = roots.iter().flatten()
            .filter_map(|r| std::fs::read_dir(r).ok())
            .flatten().flatten()
            .filter(|e| e.file_name().to_string_lossy().starts_with("IntelliJ IDEA"))
            .map(|e| e.path().join("bin").join("idea64.exe"))
            .find(|p| p.is_file());
        launch(installed.or_else(|| find_in_path("idea64.exe")).or_else(|| find_in_path("idea.cmd")), path)
    }

    pub fn github_desktop(path: &str) -> Value {
        let app = env_dir("LOCALAPPDATA").map(|d| d.join("GitHubDesktop").join("bin").join("github.bat")).filter(|p| p.is_file());
        launch(app, path)
    }

    pub fn file_manager(path: &str) -> Value {
        // Explorer rejects forward slashes and exits non-zero even on success, so only check it started.
        let native = path.replace('/', "\\");
        json!({"opened": Path::new(path).exists() && spawn_detached(Path::new("explorer.exe"), &[&native])})
    }
}

/// `kind`: vscode | intellij | githubdesktop | finder.
pub fn open_in(kind: &str, path: &str) -> serde_json::Value {
    match kind {
        "vscode" => platform::vscode(path),
        "intellij" => platform::intellij(path),
        "githubdesktop" => platform::github_desktop(path),
        "finder" => platform::file_manager(path),
        _ => serde_json::json!({"installed": false, "opened": false, "error": "unknown kind"}),
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

    #[cfg(windows)]
    #[test]
    fn windows_openers_refuse_a_missing_path() {
        for kind in ["vscode", "intellij", "githubdesktop", "finder"] {
            assert_eq!(open_in(kind, "C:/definitely/not/here")["opened"], false, "{kind}");
        }
    }
}
