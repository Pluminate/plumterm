#pragma once
// C ABI of the TerminalPro Rust core. Keep in sync with core/src/*.rs.
// Callbacks run on a reader thread; the host must marshal onto its GUI thread.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *tp_core_version(void); // static string, do not free

typedef void (*tp_data_cb)(void *user, uint64_t id, const uint8_t *data, size_t len);
typedef void (*tp_exit_cb)(void *user, uint64_t id);
uint64_t tp_pty_spawn(const char *cwd, uint16_t cols, uint16_t rows, tp_data_cb on_data, tp_exit_cb on_exit, void *user); // 0 on failure
void tp_pty_write(uint64_t id, const uint8_t *data, size_t len);
void tp_pty_resize(uint64_t id, uint16_t cols, uint16_t rows);
void tp_pty_kill(uint64_t id);
char *tp_pty_cwd(uint64_t id); // shell working directory or NULL; free with tp_string_free

// ---- Strings returned as `char *` are owned by the caller: free with tp_string_free. ----
void tp_string_free(char *s);

// ---- Settings / session (JSON, schema-compatible with the Electron app) ----
char *tp_settings_get_json(void);                 // full settings with defaults applied
int tp_settings_set_json(const char *patch_json); // merge a partial object + persist; 1 ok / 0 fail
char *tp_session_load_json(void);                 // NULL when no saved session
int tp_session_save_json(const char *json);       // 1 ok / 0 invalid JSON or write failure

// ---- Safety, launchers, history ----
int tp_verify_command(const char *cmd);           // 1 if valid sh syntax
int tp_is_dangerous(const char *cmd);             // 1 for git/cd or aliases of them
char *tp_open_in(const char *kind, const char *path); // kind: vscode|intellij|githubdesktop|finder -> JSON
char *tp_shell_history(uint32_t limit);           // JSON array, oldest first
char *tp_last_shell_command(void);                // NULL when none

// ---- Git (JSON replies) ----
char *tp_git_status(const char *cwd);             // {root,branch,gitStats,isBehindRemote} or null
char *tp_git_branches(const char *cwd);           // ["main", ...]
char *tp_git_changed_files(const char *cwd);      // [{status,file}]
char *tp_git_unpushed(const char *cwd);           // [{hash,message}]
char *tp_git_pr(const char *cwd);                 // {number,url,title} or null (needs gh)
char *tp_git_file_diff(const char *cwd, const char *file); // NULL outside a repo
char *tp_git_discard(const char *cwd, const char *file);
char *tp_git_commit(const char *cwd, const char *files_json, const char *message, int push, int pull_first);
char *tp_git_push(const char *cwd, int pull_first);

// ---- AI (JSON replies) ----
char *tp_ask_ai(const char *token, const char *prompt);
char *tp_generate_commit_message(const char *token, const char *cwd, const char *files_json);

#ifdef __cplusplus
}
#endif
