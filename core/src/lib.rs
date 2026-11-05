//! TerminalPro core: Rust logic exposed to the Qt shell through a C ABI
//! (see `../terminalpro_core.h`). Core never references Qt.

pub mod ai;
pub mod git;
pub mod history;
pub mod launch;
pub mod proc;
pub mod pty;
pub mod safety;
pub mod session;
pub mod settings;
pub mod store;

use std::ffi::c_char;

/// Static, NUL-terminated core version string. Never freed by the caller.
#[no_mangle]
pub extern "C" fn tp_core_version() -> *const c_char {
    concat!(env!("CARGO_PKG_VERSION"), "\0").as_ptr() as *const c_char
}

#[cfg(test)]
mod tests {
    #[test]
    fn version_is_nonempty() {
        let v = unsafe { std::ffi::CStr::from_ptr(super::tp_core_version()) };
        assert!(!v.to_bytes().is_empty());
    }
}
