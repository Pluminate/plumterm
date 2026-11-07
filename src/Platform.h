#pragma once
#include <QKeySequence>
#include <QString>

// Per-platform keyboard bindings and labels. Qt maps "Ctrl" to Cmd on macOS, so plain Ctrl+letter is
// free for app shortcuts there. Everywhere else those keys belong to the shell (Ctrl+C interrupts,
// Ctrl+W deletes a word), so app shortcuts move to Ctrl+Shift, like other terminals on those systems.
namespace Platform {

struct Binding {
    const char *macGlyph;  // as written in menu labels
    const char *macKey;
    const char *otherKey;  // empty: no shortcut outside macOS
};

inline constexpr Binding kBindings[] = {
    {"⌘T", "Ctrl+T", "Ctrl+Shift+T"},
    {"⌘N", "Ctrl+N", "Ctrl+Shift+N"},
    {"⌘⇧R", "Ctrl+Shift+R", "Ctrl+Shift+R"},
    {"⌘⇧D", "Ctrl+Shift+D", "Ctrl+Shift+D"},
    {"⌘W", "Ctrl+W", "Ctrl+Shift+W"},
    {"⌘⇧W", "Ctrl+Shift+W", "Ctrl+Alt+Shift+W"},
    {"⌘,", "Ctrl+,", "Ctrl+,"},
    {"⌘C", "Ctrl+C", "Ctrl+Shift+C"},
    {"⌘V", "Ctrl+V", "Ctrl+Shift+V"},
    {"⌘A", "Ctrl+A", "Ctrl+Shift+A"},
    {"⌘K", "Ctrl+K", "Ctrl+Shift+K"},
    {"⌘F", "Ctrl+F", "Ctrl+Shift+F"},
    {"⌘G", "Ctrl+G", "Ctrl+Shift+I"},
    {"⌘B", "Ctrl+B", "Ctrl+Shift+B"},
    {"⌘R", "Ctrl+R", "Ctrl+Shift+E"},
    {"⌃Tab", "Ctrl+Tab", "Ctrl+Tab"},
    {"⌃⇧Tab", "Ctrl+Shift+Tab", "Ctrl+Shift+Tab"},
    {"⌃⌘F", "Ctrl+Meta+F", "F11"},
    {"⌘M", "Ctrl+M", ""},
};

// The key sequence for a binding named by its macOS key.
inline QKeySequence key(const char *macKey) {
#ifdef Q_OS_MACOS
    return QKeySequence(QString::fromLatin1(macKey));
#else
    for (const Binding &b : kBindings) {
        if (qstrcmp(b.macKey, macKey) == 0) return QKeySequence(QString::fromLatin1(b.otherKey));
    }
    return QKeySequence(QString::fromLatin1(macKey));
#endif
}

// A menu label: "New Tab\t⌘T" becomes "New Tab\tCtrl+Shift+T" outside macOS.
inline QString menuText(const QString &text) {
#ifdef Q_OS_MACOS
    return text;
#else
    const qsizetype tab = text.indexOf(QLatin1Char('\t'));
    if (tab < 0) return text;
    const QString glyph = text.mid(tab + 1);
    for (const Binding &b : kBindings) {
        if (glyph == QString::fromUtf8(b.macGlyph)) {
            return *b.otherKey ? text.left(tab + 1) + QString::fromLatin1(b.otherKey) : text.left(tab);
        }
    }
    return text.left(tab);
#endif
}

// A tooltip or message naming a shortcut: "Ask AI (Cmd+G)" becomes "Ask AI (Ctrl+Shift+I)" outside macOS.
inline QString hint(QString text) {
#ifndef Q_OS_MACOS
    for (const Binding &b : kBindings) {
        const QString mac = QString::fromLatin1(b.macKey);
        if (*b.otherKey && mac.startsWith(QLatin1String("Ctrl+"))) {
            text.replace(QLatin1String("Cmd+") + mac.mid(5), QString::fromLatin1(b.otherKey));
        }
    }
#endif
    return text;
}

inline QString openInFileManagerLabel() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("Open in Finder");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Open in File Explorer");
#else
    return QStringLiteral("Open in File Manager");
#endif
}

}  // namespace Platform
