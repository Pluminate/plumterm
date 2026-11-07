#pragma once

#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QProxyStyle>
#include <QString>
#include <optional>

class MenuStyle : public QProxyStyle {
    Q_OBJECT
public:
    explicit MenuStyle(QStyle *base = nullptr);
    ~MenuStyle() override;

    static MenuStyle *instance();
    static void apply(QMenu *menu, std::optional<bool> dark = std::nullopt);
    static void installGlobalHook();
    static bool isDark(const QWidget *widget = nullptr);
    static bool menuHasIcons(const QMenu *menu);
    static QIcon makeIcon(const char *svgPattern, bool dark = true, const char *customNormal = nullptr);

    struct Colors {
        QColor panel;
        QColor border;
        QColor separator;
        QColor selFill;
        QColor selBorder;
        QColor text;
        QColor textSel;
        QColor textOff;
        QColor shortcut;
        QColor shortcutSel;
    };
    static const Colors &colors(bool dark = true);

    void drawPrimitive(PrimitiveElement element, const QStyleOption *opt, QPainter *p, const QWidget *widget) const override;
    void drawControl(ControlElement element, const QStyleOption *opt, QPainter *p, const QWidget *widget) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *opt, const QWidget *widget) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *opt, const QSize &size, const QWidget *widget) const override;
};

namespace MenuIcons {
extern const char *kCopy;
extern const char *kPaste;
extern const char *kCut;
extern const char *kSelectAll;
extern const char *kClear;
extern const char *kSplitRight;
extern const char *kSplitDown;
extern const char *kPlay;
extern const char *kFlash;
extern const char *kSettings;
extern const char *kFinder;
extern const char *kGit;
extern const char *kDiff;
extern const char *kClose;
extern const char *kCode;
extern const char *kWindow;
extern const char *kAdd;
extern const char *kRename;
}  // namespace MenuIcons
