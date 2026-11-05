//! Shared helpers for file-backed JSON stores and C-string ownership across the ABI.

use std::ffi::{c_char, CStr, CString};
use std::path::PathBuf;

/// Directory holding settings.json / session.json.
/// `TP_DATA_DIR` overrides (used by tests). Otherwise reuse the Electron app's folder when it
/// already holds data, so existing settings and sessions carry over.
pub fn data_dir() -> PathBuf {
    if let Ok(d) = std::env::var("TP_DATA_DIR") {
        if !d.is_empty() { return PathBuf::from(d); }
    }
    let home = std::env::var("HOME").unwrap_or_else(|_| ".".into());
    let base = if cfg!(target_os = "macos") {
        PathBuf::from(home).join("Library/Application Support")
    } else {
        std::env::var("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|_| PathBuf::from(home).join(".config"))
    };
    for name in ["TerminalPro", "terminal"] {
        let p = base.join(name);
        if p.join("settings.json").exists() || p.join("session.json").exists() { return p; }
    }
    base.join("TerminalPro")
}

/// Write via temp file + rename so a crash never leaves a half-written file.
pub fn atomic_write(path: &std::path::Path, contents: &str) -> std::io::Result<()> {
    if let Some(parent) = path.parent() { std::fs::create_dir_all(parent)?; }
    let tmp = path.with_extension("json.tmp");
    std::fs::write(&tmp, contents)?;
    std::fs::rename(&tmp, path)
}

pub fn cstr(p: *const c_char) -> String {
    if p.is_null() { return String::new(); }
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

/// Hand a string to the host; free with `tp_string_free`.
pub fn to_c(s: String) -> *mut c_char {
    CString::new(s.replace('\0', "")).unwrap().into_raw()
}

#[no_mangle]
pub extern "C" fn tp_string_free(p: *mut c_char) {
    if !p.is_null() { unsafe { drop(CString::from_raw(p)); } }
}

#[cfg(test)]
pub fn test_dir(tag: &str) -> PathBuf {
    let d = std::env::temp_dir().join(format!("tp-test-{}-{}", tag, std::process::id()));
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}
