//! Gemini AI integration for terminal command suggestion (Ask AI) and commit message generation.
//! Shells out to system `curl` for robust SSL/TLS and corporate proxy support without heavy OpenSSL crates.

use crate::git::{file_diff, find_root};
use crate::proc::run;
use crate::store::{cstr, to_c};
use serde_json::{json, Value};
use std::ffi::c_char;
use std::path::Path;
use std::time::Duration;

const MODELS: &[&str] = &[
    "gemini-2.5-flash",
    "gemini-2.5-pro",
    "gemini-2.5-flash-lite",
    "gemini-1.5-flash",
];

const SYSTEM_PROMPT: &str = "\
You are a terminal assistant. Your job is to convert natural language requests into valid, safe, and efficient zsh/bash commands.
RULES:
1. Return ONLY the command to execute. No markdown, no triple backticks, no explanations.
2. If multiple steps are needed, combine them with '&&' or ';'.
3. Assume standard macOS utilities (e.g. valid flags for BSD ls, sed, awk).
4. If the request implies danger (delete all files), use proper flags but assume user knows what they are doing.
5. Output format: Just the command string.";

fn clean_command(text: &str) -> String {
    let mut s = text.trim();
    if s.starts_with("```") {
        if let Some(first_nl) = s.find('\n') {
            s = &s[first_nl + 1..];
        }
        if s.ends_with("```") {
            s = &s[..s.len() - 3];
        }
    }
    s.trim().to_string()
}

fn call_gemini(token: &str, system_inst: &str, user_prompt: &str) -> Result<String, String> {
    let token = token.trim();
    if token.is_empty() {
        return Err("Gemini API key is not configured in Settings.".into());
    }

    let payload = json!({
        "contents": [{
            "parts": [{ "text": format!("{}\n\nRequest: {}", system_inst, user_prompt) }]
        }],
        "generationConfig": {
            "temperature": 0.2,
            "maxOutputTokens": 1024
        }
    }).to_string();

    let mut last_err = String::from("No models succeeded");

    for model in MODELS {
        let url = format!(
            "https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent?key={}",
            model, token
        );

        let out = run(
            "/usr/bin/curl",
            &[
                "-s",
                "-X", "POST",
                "-H", "Content-Type: application/json",
                "-d", &payload,
                &url,
            ],
            None,
            Duration::from_secs(15),
        );

        let Some(out) = out else {
            last_err = "Network request timed out".into();
            continue;
        };

        let Ok(val) = serde_json::from_str::<Value>(&out.stdout) else {
            last_err = format!("Failed to parse response: {}", out.stdout);
            continue;
        };

        if let Some(err) = val.get("error") {
            let msg = err["message"].as_str().unwrap_or("API error");
            last_err = format!("{}: {}", model, msg);
            continue;
        }

        if let Some(candidates) = val.get("candidates").and_then(|c| c.as_array()) {
            if let Some(parts) = candidates.get(0)
                .and_then(|c| c.get("content"))
                .and_then(|ct| ct.get("parts"))
                .and_then(|p| p.as_array())
            {
                if let Some(text) = parts.get(0).and_then(|pt| pt["text"].as_str()) {
                    let cleaned = clean_command(text);
                    if !cleaned.is_empty() {
                        return Ok(cleaned);
                    }
                }
            }
        }
    }

    Err(last_err)
}

pub fn ask_ai(token: &str, prompt: &str) -> Result<String, String> {
    call_gemini(token, SYSTEM_PROMPT, prompt)
}

pub fn generate_commit_message(token: &str, cwd: &Path, files: &[String]) -> Result<String, String> {
    let root = find_root(cwd).ok_or_else(|| "Not a git repository".to_string())?;

    let mut diff_context = String::new();
    for f in files {
        if let Some(d) = file_diff(&root, f) {
            let clean = d.lines()
                .filter(|l| !l.starts_with("@@") && !l.starts_with("index ") && !l.starts_with("---") && !l.starts_with("+++") && !l.starts_with("diff --git"))
                .collect::<Vec<_>>()
                .join("\n");
            let capped = if clean.len() > 1500 { &clean[..1500] } else { &clean };
            diff_context.push_str(&format!("## {}\n{}\n\n", f, capped));
        }
    }

    let prompt = format!(
        "Analyze these git diffs and write a SHORT, CLEAR commit message. Return ONLY the message text, nothing else.\n\nFiles: {}\n\n{}\n\nMessage rules:\n- ONE single line, no newlines\n- 50-75 characters\n- Start with capital verb: Add, Remove, Fix, Update, Improve, Refactor, etc.\n- Plain English, no prefixes like feat: or fix:\n- No quotes, backticks, or trailing punctuation.",
        files.join(", "),
        diff_context
    );

    let res = call_gemini(token, "You are an expert git commit message writer.", &prompt)?;
    Ok(res.replace('\n', " ").trim().to_string())
}

// C ABI
#[no_mangle]
pub extern "C" fn tp_ask_ai(token: *const c_char, prompt: *const c_char) -> *mut c_char {
    let r = ask_ai(&cstr(token), &cstr(prompt));
    let val = match r {
        Ok(cmd) => json!({ "success": true, "command": cmd }),
        Err(err) => json!({ "success": false, "error": err }),
    };
    to_c(val.to_string())
}

#[no_mangle]
pub extern "C" fn tp_generate_commit_message(token: *const c_char, cwd: *const c_char, files_json: *const c_char) -> *mut c_char {
    let files: Vec<String> = serde_json::from_str(&cstr(files_json)).unwrap_or_default();
    let r = generate_commit_message(&cstr(token), Path::new(&cstr(cwd)), &files);
    let val = match r {
        Ok(msg) => json!({ "success": true, "message": msg }),
        Err(err) => json!({ "success": false, "error": err }),
    };
    to_c(val.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clean_command_strips_markdown() {
        assert_eq!(clean_command("```bash\nls -la\n```"), "ls -la");
        assert_eq!(clean_command("```\ngit status\n```"), "git status");
        assert_eq!(clean_command("echo hello"), "echo hello");
    }

    #[test]
    fn empty_token_errors() {
        let res = ask_ai("", "list files");
        assert!(res.is_err());
    }
}
