#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include "plumterm_core.h"

// Thin C++ wrapper over the Rust core C ABI. The only place that talks to it.
namespace CoreBridge {

inline QString version() { return QString::fromUtf8(tp_core_version()); }

namespace detail {
// Take ownership of a core-allocated string; null stays null.
inline QByteArray take(char *s) {
    if (!s) return {};
    QByteArray out(s);
    tp_string_free(s);
    return out;
}
inline QJsonValue takeJson(char *s) {
    const QByteArray raw = take(s);
    if (raw.isEmpty()) return QJsonValue(QJsonValue::Null);
    // The core always emits one JSON value; wrap in an array so scalars/objects parse the same way.
    const auto doc = QJsonDocument::fromJson("[" + raw + "]");
    return doc.array().first();
}
inline QByteArray u8(const QString &s) { return s.toUtf8(); }
}

// --- pty ---
inline QString ptyCwd(quint64 id) { return QString::fromUtf8(detail::take(tp_pty_cwd(id))); }

// --- settings / session ---
inline QJsonValue settings() { return detail::takeJson(tp_settings_get_json()); }
inline bool setSettings(const QString &patchJson) { return tp_settings_set_json(detail::u8(patchJson)) == 1; }
inline QString loadSession() { return QString::fromUtf8(detail::take(tp_session_load_json())); } // empty if none
inline bool saveSession(const QString &json) { return tp_session_save_json(detail::u8(json)) == 1; }

// --- safety / launchers / history ---
inline bool verifyCommand(const QString &cmd) { return tp_verify_command(detail::u8(cmd)) == 1; }
inline bool isDangerous(const QString &cmd) { return tp_is_dangerous(detail::u8(cmd)) == 1; }
inline QJsonValue openIn(const QString &kind, const QString &path) {
    return detail::takeJson(tp_open_in(detail::u8(kind), detail::u8(path)));
}
inline QJsonValue shellHistory(uint32_t limit = 25) { return detail::takeJson(tp_shell_history(limit)); }
inline QString lastShellCommand() { return QString::fromUtf8(detail::take(tp_last_shell_command())); }

// --- git ---
namespace git {
inline QJsonValue status(const QString &cwd) { return detail::takeJson(tp_git_status(detail::u8(cwd))); }
inline QJsonValue branches(const QString &cwd) { return detail::takeJson(tp_git_branches(detail::u8(cwd))); }
inline QJsonValue changedFiles(const QString &cwd) { return detail::takeJson(tp_git_changed_files(detail::u8(cwd))); }
inline QJsonValue unpushed(const QString &cwd) { return detail::takeJson(tp_git_unpushed(detail::u8(cwd))); }
inline QJsonValue pr(const QString &cwd) { return detail::takeJson(tp_git_pr(detail::u8(cwd))); }
// Null string when cwd is not in a repository.
inline QString fileDiff(const QString &cwd, const QString &file) {
    return QString::fromUtf8(detail::take(tp_git_file_diff(detail::u8(cwd), detail::u8(file))));
}
inline QJsonValue discard(const QString &cwd, const QString &file) {
    return detail::takeJson(tp_git_discard(detail::u8(cwd), detail::u8(file)));
}
inline QJsonValue commit(const QString &cwd, const QString &filesJson, const QString &message, bool push, bool pullFirst) {
    return detail::takeJson(tp_git_commit(detail::u8(cwd), detail::u8(filesJson), detail::u8(message), push, pullFirst));
}
inline QJsonValue push(const QString &cwd, bool pullFirst) { return detail::takeJson(tp_git_push(detail::u8(cwd), pullFirst)); }
}

// --- ai ---
inline QJsonValue askAI(const QString &token, const QString &prompt) {
    return detail::takeJson(tp_ask_ai(detail::u8(token), detail::u8(prompt)));
}
inline QJsonValue generateCommitMessage(const QString &token, const QString &cwd, const QString &filesJson) {
    return detail::takeJson(tp_generate_commit_message(detail::u8(token), detail::u8(cwd), detail::u8(filesJson)));
}

}
