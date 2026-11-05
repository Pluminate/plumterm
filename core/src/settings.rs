//! settings.json, schema-compatible with the Electron app: a flat JSON object whose stored keys
//! are shallow-merged over the defaults (unknown keys are preserved).

use crate::store::{atomic_write, cstr, data_dir, to_c};
use serde_json::{json, Map, Value};
use std::ffi::c_char;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

pub fn defaults() -> Map<String, Value> {
    let v = json!({
        "globalHotkey": "CommandOrControl+Shift+T",
        "rerunShortcut": "CommandOrControl+R",
        "confirmOnQuit": false,
        "excludeDangerousRerun": true,
        "notifyOnCommandEnd": true,
        "notificationThreshold": 120,
        "geminiToken": "",
        "restoreSession": true,
        "ghDesktopShortcut": "",
        "vscodeShortcut": "",
        "intellijShortcut": "",
        "finderShortcut": "",
        "commitShortcut": "",
        "runShortcut": "CommandOrControl+S",
        "runCommands": {}
    });
    v.as_object().unwrap().clone()
}

fn path() -> PathBuf { data_dir().join("settings.json") }

/// Defaults overlaid with whatever is stored; missing/empty/corrupt file yields defaults.
pub fn load_from(p: &Path) -> Map<String, Value> {
    let mut out = defaults();
    if let Ok(text) = std::fs::read_to_string(p) {
        if !text.trim().is_empty() {
            if let Ok(Value::Object(m)) = serde_json::from_str::<Value>(&text) {
                for (k, v) in m { out.insert(k, v); }
            }
        }
    }
    out
}

/// Merge a partial object (like `settings.set`) into the stored settings and persist.
pub fn merge_into(p: &Path, patch: &str) -> Result<Map<String, Value>, String> {
    let Value::Object(patch) = serde_json::from_str::<Value>(patch).map_err(|e| e.to_string())?
        else { return Err("settings patch must be a JSON object".into()) };
    let mut cur = load_from(p);
    for (k, v) in patch { cur.insert(k, v); }
    atomic_write(p, &Value::Object(cur.clone()).to_string()).map_err(|e| e.to_string())?;
    Ok(cur)
}

static LOCK: Mutex<()> = Mutex::new(());

/// Full settings as JSON (free with `tp_string_free`).
#[no_mangle]
pub extern "C" fn tp_settings_get_json() -> *mut c_char {
    let _g = LOCK.lock().unwrap();
    to_c(Value::Object(load_from(&path())).to_string())
}

/// Merge a partial JSON object into settings and persist. Returns 1 on success, 0 on failure.
#[no_mangle]
pub extern "C" fn tp_settings_set_json(patch: *const c_char) -> i32 {
    let _g = LOCK.lock().unwrap();
    merge_into(&path(), &cstr(patch)).is_ok() as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::store::test_dir;

    #[test]
    fn missing_file_gives_defaults() {
        let d = test_dir("set-missing");
        let m = load_from(&d.join("settings.json"));
        assert_eq!(m["runShortcut"], "CommandOrControl+S");
        assert_eq!(m["notificationThreshold"], 120);
    }

    #[test]
    fn electron_sample_roundtrip_keeps_unknown_keys() {
        let d = test_dir("set-sample");
        let p = d.join("settings.json");
        std::fs::write(&p, r#"{"globalHotkey":"Alt+Space","futureKey":{"a":1},"runCommands":{"/x":"make"}}"#).unwrap();
        let m = load_from(&p);
        assert_eq!(m["globalHotkey"], "Alt+Space");
        assert_eq!(m["restoreSession"], true);
        let after = merge_into(&p, r#"{"confirmOnQuit":true}"#).unwrap();
        assert_eq!(after["confirmOnQuit"], true);
        assert_eq!(after["futureKey"]["a"], 1);
        assert_eq!(load_from(&p)["runCommands"]["/x"], "make");
    }

    #[test]
    fn corrupt_or_empty_falls_back() {
        let d = test_dir("set-corrupt");
        let p = d.join("settings.json");
        std::fs::write(&p, "  ").unwrap();
        assert_eq!(load_from(&p)["restoreSession"], true);
        std::fs::write(&p, "{not json").unwrap();
        assert_eq!(load_from(&p)["restoreSession"], true);
        assert!(merge_into(&p, "[1]").is_err());
    }
}
