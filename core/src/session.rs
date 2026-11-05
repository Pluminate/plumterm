//! session.json: the tab/layout tree saved by the UI. Treated as opaque JSON (an array of tabs
//! carrying `layout`, `savedCwd`, `savedLastCommand`, ...) so the schema stays Electron-compatible.

use crate::store::{atomic_write, cstr, data_dir, to_c};
use serde_json::Value;
use std::ffi::c_char;
use std::path::{Path, PathBuf};

fn path() -> PathBuf { data_dir().join("session.json") }

/// Stored session as JSON text, or None if absent/unparsable.
pub fn load_from(p: &Path) -> Option<String> {
    let text = std::fs::read_to_string(p).ok()?;
    let v: Value = serde_json::from_str(&text).ok()?;
    Some(v.to_string())
}

pub fn save_to(p: &Path, json: &str) -> Result<(), String> {
    serde_json::from_str::<Value>(json).map_err(|e| e.to_string())?; // never persist garbage
    atomic_write(p, json).map_err(|e| e.to_string())
}

/// Saved session JSON (free with `tp_string_free`), or NULL when none exists.
#[no_mangle]
pub extern "C" fn tp_session_load_json() -> *mut c_char {
    match load_from(&path()) { Some(s) => to_c(s), None => std::ptr::null_mut() }
}

/// Persist the session. Returns 1 on success, 0 if the JSON is invalid or the write fails.
#[no_mangle]
pub extern "C" fn tp_session_save_json(json: *const c_char) -> i32 {
    save_to(&path(), &cstr(json)).is_ok() as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::store::test_dir;

    #[test]
    fn roundtrip_electron_session() {
        let d = test_dir("sess");
        let p = d.join("session.json");
        assert!(load_from(&p).is_none());
        let sample = r#"[{"id":1,"title":"a","layout":{"type":"pane","savedCwd":"/tmp","savedLastCommand":"ls"}}]"#;
        save_to(&p, sample).unwrap();
        let back: Value = serde_json::from_str(&load_from(&p).unwrap()).unwrap();
        assert_eq!(back[0]["layout"]["savedCwd"], "/tmp");
    }

    #[test]
    fn rejects_invalid_json_and_keeps_old_file() {
        let d = test_dir("sess-bad");
        let p = d.join("session.json");
        save_to(&p, "[]").unwrap();
        assert!(save_to(&p, "{oops").is_err());
        assert_eq!(load_from(&p).unwrap(), "[]");
    }
}
