#include "MenuStyle.h"

#include <QApplication>
#include <QChildEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHash>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyleOptionMenuItem>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QToolButton>

namespace {

void styleDialog(QDialog *dlg) {
    if (dlg->property("tpDialogStyled").toBool()) return;
    dlg->setProperty("tpDialogStyled", true);
    const bool dark = MenuStyle::isDark(dlg);
    const auto &c = MenuStyle::colors(dark);
    const QString btnBg = dark ? "#393b40" : "#ebecf0";
    const QString btnHover = dark ? "#43454a" : "#dfe1e5";
    const QString muted = dark ? "#868a91" : "#6f737a";
    const QString inputBg = dark ? "#1e1f22" : "#ffffff";
    dlg->setStyleSheet(QString(
        "QDialog, QMessageBox { background:%1; color:%2; }"
        "QLabel { color:%2; font-size:13px; font-weight:400; background:transparent; }"
        "QLabel#qt_msgbox_label { font-size:14px; font-weight:600; min-width:340px; }"
        "QLabel#qt_msgbox_informativelabel { color:%3; font-weight:400; }"
        "QPushButton { background:%4; color:%2; border:1px solid %5; border-radius:6px; padding:5px 14px; min-width:90px; font-size:12px; }"
        "QPushButton:hover { background:%6; }"
        "QPushButton:pressed { background:%5; }"
        "QPushButton:default { background:#3574f0; border:1px solid #3574f0; color:#ffffff; }"
        "QPushButton:default:hover { background:#4a84f2; }"
        "QPushButton:focus { outline:none; }"
        "QLineEdit, QComboBox, QSpinBox, QPlainTextEdit { background:%7; color:%2; border:1px solid %5; border-radius:6px; padding:4px 8px;"
        " selection-background-color:#3574f0; selection-color:#ffffff; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border:1px solid #3574f0; }")
        .arg(c.panel.name(), c.text.name(), muted, btnBg, c.border.name(), btnHover, inputBg));

    if (auto *mb = qobject_cast<QMessageBox *>(dlg)) {
        mb->setIcon(QMessageBox::NoIcon);
        for (QLabel *l : mb->findChildren<QLabel *>()) {
            l->setTextInteractionFlags(Qt::NoTextInteraction);
            l->setWordWrap(true);
        }
        mb->setContentsMargins(14, 12, 14, 12);
    }
}

class MenuChildFilter : public QObject {
public:
    std::optional<bool> dark;
    explicit MenuChildFilter(QObject *parent, std::optional<bool> d = std::nullopt)
        : QObject(parent), dark(d) {}

protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::ChildAdded) {
            auto *ce = static_cast<QChildEvent *>(e);
            if (auto *childMenu = qobject_cast<QMenu *>(ce->child())) {
                MenuStyle::apply(childMenu, dark);
            }
        }
        return false;
    }
};

class GlobalMenuEventFilter : public QObject {
public:
    explicit GlobalMenuEventFilter(QObject *parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::Show) {
            if (auto *m = qobject_cast<QMenu *>(o)) {
                MenuStyle::apply(m);
            } else if (qobject_cast<QMessageBox *>(o) || qobject_cast<QInputDialog *>(o)) {
                styleDialog(static_cast<QDialog *>(o));
            }
        } else if (e->type() == QEvent::ChildAdded) {
            auto *ce = static_cast<QChildEvent *>(e);
            if (auto *childMenu = qobject_cast<QMenu *>(ce->child())) {
                MenuStyle::apply(childMenu);
            }
        } else if (e->type() == QEvent::MouseButtonPress) {
            if (auto *tb = qobject_cast<QToolButton *>(o)) {
                if (tb->popupMode() == QToolButton::InstantPopup && tb->menu() && tb->isEnabled()) {
                    auto *me = static_cast<QMouseEvent *>(e);
                    if (me->button() == Qt::LeftButton) {
                        QMenu *m = tb->menu();
                        if (m->isVisible()) {
                            m->close();
                            return true;
                        }
                        MenuStyle::apply(m, MenuStyle::isDark(tb));
                        tb->setDown(true);
                        QObject::connect(m, &QMenu::aboutToHide, tb, [tb] {
                            tb->setDown(false);
                        }, Qt::SingleShotConnection);
                        const QPoint pos = tb->mapToGlobal(QPoint(0, tb->height() + 2));
                        m->popup(pos);
                        return true;
                    }
                }
            }
        }
        return false;
    }
};

}  // namespace

MenuStyle::MenuStyle(QStyle *base) : QProxyStyle(base) {}

MenuStyle::~MenuStyle() = default;

MenuStyle *MenuStyle::instance() {
    static MenuStyle *s_inst = [] {
#ifdef Q_OS_WIN
        // Without a base, QProxyStyle wraps the desktop style even after QApplication::setStyle(),
        // and QMenu::exec() freezes under the Windows 11 one.
        auto *style = new MenuStyle(QStyleFactory::create(QStringLiteral("Fusion")));
#else
        auto *style = new MenuStyle();
#endif
        if (qApp) style->setParent(qApp);
        return style;
    }();
    return s_inst;
}

void MenuStyle::installGlobalHook() {
    if (!qApp) return;
    static bool installed = false;
    if (installed) return;
    installed = true;
    qApp->installEventFilter(new GlobalMenuEventFilter(qApp));
}

bool MenuStyle::isDark(const QWidget *widget) {
    if (widget) {
        const QVariant v = widget->property("dark");
        if (v.isValid()) return v.toBool();
    }
    const QVariant v = instance()->property("dark");
    if (v.isValid()) return v.toBool();
    return true;
}

const MenuStyle::Colors &MenuStyle::colors(bool dark) {
    static const Colors darkColors{
        QColor("#2b2d30"),  // panel
        QColor("#393b40"),  // border
        QColor("#393b40"),  // separator
        QColor("#2e436e"),  // selFill
        QColor("#3574f0"),  // selBorder
        QColor("#dfe1e5"),  // text
        QColor("#ffffff"),  // textSel
        QColor("#6f737a"),  // textOff
        QColor("#868a91"),  // shortcut
        QColor("#c5c7cb")   // shortcutSel
    };
    static const Colors lightColors{
        QColor("#ffffff"),  // panel
        QColor("#d1d1d1"),  // border
        QColor("#ebecf0"),  // separator
        QColor("#d4e2ff"),  // selFill
        QColor("#3574f0"),  // selBorder
        QColor("#1e1f22"),  // text
        QColor("#1e1f22"),  // textSel
        QColor("#a8abb0"),  // textOff
        QColor("#6f737a"),  // shortcut
        QColor("#4e5157")   // shortcutSel
    };
    return dark ? darkColors : lightColors;
}

bool MenuStyle::menuHasIcons(const QMenu *menu) {
    if (!menu) return true;
    for (const QAction *a : menu->actions()) {
        if (!a->isSeparator() && (!a->icon().isNull() || a->isCheckable())) {
            return true;
        }
    }
    return false;
}

void MenuStyle::apply(QMenu *menu, std::optional<bool> dark) {
    if (!menu) return;
    menu->setStyle(instance());
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    if (dark.has_value()) {
        menu->setProperty("dark", dark.value());
    }

    if (!menu->property("_menuStyleInstalled").toBool()) {
        menu->setProperty("_menuStyleInstalled", true);
        menu->installEventFilter(new MenuChildFilter(menu, dark));

        QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, dark] {
            for (QAction *a : menu->actions()) {
                if (QMenu *sub = a->menu()) {
                    apply(sub, dark);
                }
            }
        });
    }

    for (QAction *a : menu->actions()) {
        if (QMenu *sub = a->menu()) {
            apply(sub, dark);
        }
    }
}

void MenuStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *opt, QPainter *p, const QWidget *widget) const {
    if (element == PE_PanelMenu || element == PE_FrameMenu) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(opt->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const auto &c = colors(isDark(widget));
        p->setPen(QPen(c.border, 1.0));
        p->setBrush(c.panel);
        p->drawRoundedRect(r, 8.0, 8.0);
        p->restore();
        return;
    }
    QProxyStyle::drawPrimitive(element, opt, p, widget);
}

int MenuStyle::pixelMetric(PixelMetric metric, const QStyleOption *opt, const QWidget *widget) const {
    if (metric == PM_MenuPanelWidth) {
        return 1;
    }
    if (metric == PM_MenuHMargin) {
        return 0;
    }
    if (metric == PM_MenuVMargin) {
        return 5;
    }
    return QProxyStyle::pixelMetric(metric, opt, widget);
}

void MenuStyle::drawControl(ControlElement element, const QStyleOption *opt, QPainter *p, const QWidget *widget) const {
    if (element == CE_MenuItem) {
        const auto *mopt = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
        if (!mopt) {
            QProxyStyle::drawControl(element, opt, p, widget);
            return;
        }

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        const QRect r = mopt->rect;
        const bool selected = (mopt->state & State_Selected) && (mopt->state & State_Enabled);
        const bool enabled = mopt->state & State_Enabled;
        const auto &c = colors(isDark(widget));

        if (mopt->menuItemType == QStyleOptionMenuItem::Separator) {
            p->setPen(QPen(c.separator, 1.0));
            p->drawLine(QPointF(r.left() + 8.0, r.center().y()), QPointF(r.right() - 8.0, r.center().y()));
            p->restore();
            return;
        }

        if (selected) {
            const QRectF itemRect(r.left() + 4.5, r.top() + 0.5, r.width() - 9.0, r.height() - 1.0);
            p->setPen(QPen(c.selBorder, 1.0));
            p->setBrush(c.selFill);
            p->drawRoundedRect(itemRect, 4.0, 4.0);
        }

        const auto *menu = qobject_cast<const QMenu *>(widget);
        const bool hasIcons = menuHasIcons(menu);

        if (mopt->checked) {
            p->save();
            p->setPen(QPen(selected ? c.textSel : c.selBorder, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath check;
            const qreal cx = r.left() + 14.0;
            const qreal cy = r.top() + (r.height() - 10.0) / 2.0;
            check.moveTo(cx, cy + 5.0);
            check.lineTo(cx + 3.5, cy + 8.5);
            check.lineTo(cx + 9.0, cy + 2.0);
            p->drawPath(check);
            p->restore();
        } else if (!mopt->icon.isNull()) {
            const int iconX = r.left() + 12;
            const int iconY = r.top() + (r.height() - 16) / 2;
            const QIcon::Mode mode = !enabled ? QIcon::Disabled : selected ? QIcon::Selected : QIcon::Normal;
            mopt->icon.paint(p, iconX, iconY, 16, 16, Qt::AlignCenter, mode);
        }

        QString title = mopt->text;
        QString shortcut;
        const int tabIdx = title.indexOf('\t');
        if (tabIdx >= 0) {
            shortcut = title.mid(tabIdx + 1);
            title = title.left(tabIdx);
        }

        QString clean;
        clean.reserve(title.size());
        for (int i = 0; i < title.size(); ++i) {
            if (title[i] == QLatin1Char('&')) {
                if (i + 1 < title.size() && title[i + 1] == QLatin1Char('&')) {
                    clean += QLatin1Char('&');
                    ++i;
                }
            } else {
                clean += title[i];
            }
        }
        title = clean;

        const int textX = r.left() + (hasIcons ? 36 : 14);
        const int textRight = r.right() - (mopt->menuItemType == QStyleOptionMenuItem::SubMenu ? 26 : (shortcut.isEmpty() ? 12 : 65));
        const QRect textRect(textX, r.top(), std::max(0, textRight - textX), r.height());

#if defined(Q_OS_MACOS)
        QFont font(QStringLiteral(".AppleSystemUIFont"));
#else
        QFont font;
#endif
        font.setPixelSize(12);
        font.setWeight(QFont::Normal);
        font.setStyleStrategy(QFont::PreferAntialias);
        p->setFont(font);
        p->setPen(selected ? c.textSel : enabled ? c.text : c.textOff);
        p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, title);

        if (!shortcut.isEmpty()) {
            QFont scFont = font;
            scFont.setPixelSize(11);
            p->setFont(scFont);
            p->setPen(selected ? c.shortcutSel : c.shortcut);
            p->drawText(QRect(r.left(), r.top(), r.width() - 12, r.height()), Qt::AlignRight | Qt::AlignVCenter, shortcut);
        }

        if (mopt->menuItemType == QStyleOptionMenuItem::SubMenu) {
            p->setPen(QPen(selected ? c.textSel : c.shortcut, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const qreal arrowRight = r.right() - 12.0;
            const qreal arrowY = r.center().y();
            QPainterPath path;
            path.moveTo(arrowRight - 4.5, arrowY - 4.5);
            path.lineTo(arrowRight, arrowY);
            path.lineTo(arrowRight - 4.5, arrowY + 4.5);
            p->drawPath(path);
        }

        p->restore();
        return;
    }

    QProxyStyle::drawControl(element, opt, p, widget);
}

QSize MenuStyle::sizeFromContents(ContentsType type, const QStyleOption *opt, const QSize &size, const QWidget *widget) const {
    if (type == CT_MenuItem) {
        const auto *mopt = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
        if (mopt && mopt->menuItemType == QStyleOptionMenuItem::Separator) {
            return QSize(190, 9);
        }
        if (mopt) {
            const auto *menu = qobject_cast<const QMenu *>(widget);
            const bool hasIcons = menuHasIcons(menu);
            const int leftPad = hasIcons ? 36 : 14;

            QString title = mopt->text;
            QString shortcut;
            const int tabIdx = title.indexOf('\t');
            if (tabIdx >= 0) {
                shortcut = title.mid(tabIdx + 1);
                title = title.left(tabIdx);
            }
            title.remove('&');

#if defined(Q_OS_MACOS)
            QFont font(QStringLiteral(".AppleSystemUIFont"));
#else
            QFont font;
#endif
            font.setPixelSize(12);
            font.setWeight(QFont::Normal);
            QFontMetrics fm(font);

            int w = leftPad + fm.horizontalAdvance(title);
            if (!shortcut.isEmpty()) {
                QFont scFont = font;
                scFont.setPixelSize(11);
                QFontMetrics scFm(scFont);
                w += 24 + scFm.horizontalAdvance(shortcut);
            }
            if (mopt->menuItemType == QStyleOptionMenuItem::SubMenu) {
                w += 24;
            } else {
                w += 16;
            }
            const int minW = hasIcons ? 190 : 150;
            return QSize(std::max(minW, w), 28);
        }
        return QSize(190, 28);
    }
    return QProxyStyle::sizeFromContents(type, opt, size, widget);
}

QIcon MenuStyle::makeIcon(const char *svgPattern, bool dark, const char *customNormal) {
    static QHash<QString, QIcon> cache;
    const QString key = QString("%1|%2|%3").arg(QString::fromUtf8(svgPattern), dark ? "1" : "0", customNormal ? QString::fromUtf8(customNormal) : QString());
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return *it;
    }

    const char *normal = customNormal ? customNormal : (dark ? "#868a91" : "#6f737a");
    const char *selected = dark ? "#ffffff" : "#1e1f22";
    const char *disabled = dark ? "#4e5157" : "#a8abb0";

    QIcon icon;
    for (int dpr : {1, 2, 3}) {
        auto makePm = [&](const char *color) {
            QByteArray bytes(svgPattern);
            bytes.replace("@COLOR@", color);
            QSvgRenderer r(bytes);
            QPixmap pm(16 * dpr, 16 * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing, true);
            r.render(&p, QRectF(0, 0, 16, 16));
            return pm;
        };
        icon.addPixmap(makePm(normal), QIcon::Normal, QIcon::Off);
        icon.addPixmap(makePm(selected), QIcon::Selected, QIcon::Off);
        icon.addPixmap(makePm(disabled), QIcon::Disabled, QIcon::Off);
    }
    cache.insert(key, icon);
    return icon;
}

namespace MenuIcons {

const char *kCopy = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><rect x='5.5' y='5.5' width='8' height='8' rx='1.5' stroke='@COLOR@' stroke-width='1.2'/><path d='M10.5 3.5H3.5C2.7 3.5 2 4.2 2 5V12' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round'/></svg>";

const char *kPaste = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><rect x='3' y='4' width='10' height='10' rx='1.5' stroke='@COLOR@' stroke-width='1.2'/><path d='M6 2H10V4.5H6V2Z' stroke='@COLOR@' stroke-width='1.1'/></svg>";

const char *kCut = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><circle cx='4' cy='4.5' r='2' stroke='@COLOR@' stroke-width='1.2'/><circle cx='4' cy='11.5' r='2' stroke='@COLOR@' stroke-width='1.2'/><path d='M5.5 6L13.5 12M5.5 10L13.5 4' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round'/></svg>";

const char *kSelectAll = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><rect x='2.5' y='2.5' width='11' height='11' rx='1.5' stroke='@COLOR@' stroke-width='1.2' stroke-dasharray='2 1.5'/></svg>";

const char *kClear = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><path d='M3 4H13M5.5 4V2.5C5.5 2 6 1.5 6.5 1.5H9.5C10 1.5 10.5 2 10.5 2.5V4M12 4V13.5C12 14 11.5 14.5 11 14.5H5C4.5 14.5 4 14 4 13.5V4' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round'/><path d='M6.5 7V11.5M9.5 7V11.5' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round'/></svg>";

const char *kSplitRight = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><rect x='2.5' y='2.5' width='11' height='11' rx='1.5' stroke='@COLOR@' stroke-width='1.2'/><line x1='8' y1='2.5' x2='8' y2='13.5' stroke='@COLOR@' stroke-width='1.2'/></svg>";

const char *kSplitDown = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><rect x='2.5' y='2.5' width='11' height='11' rx='1.5' stroke='@COLOR@' stroke-width='1.2'/><line x1='2.5' y1='8' x2='13.5' y2='8' stroke='@COLOR@' stroke-width='1.2'/></svg>";

const char *kPlay = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><path d='M5 3.5L12 8L5 12.5V3.5Z' stroke='@COLOR@' stroke-width='1.2' stroke-linejoin='round'/></svg>";

const char *kFlash = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><path d='M8.5 2L3.5 8.5H8L7.5 14L12.5 7.5H8L8.5 2Z' stroke='@COLOR@' stroke-width='1.2' stroke-linejoin='round'/></svg>";

const char *kSettings = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><circle cx='8' cy='8' r='2.5' stroke='@COLOR@' stroke-width='1.2'/><path d='M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M3.4 12.6l1.4-1.4M11.2 4.8l1.4-1.4' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round'/></svg>";

const char *kFinder = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><path d='M1.5 4v8.5a1 1 0 0 0 1 1h11a1 1 0 0 0 1-1V5.5a1 1 0 0 0-1-1H7.5L6 3H2.5a1 1 0 0 0-1 1Z' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round' stroke-linejoin='round'/></svg>";

const char *kGit = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><circle cx='4' cy='4' r='1.5' stroke='@COLOR@' stroke-width='1.2'/><circle cx='4' cy='12' r='1.5' stroke='@COLOR@' stroke-width='1.2'/><circle cx='12' cy='7' r='1.5' stroke='@COLOR@' stroke-width='1.2'/><path d='M4 5.5v5M4 8a3 3 0 0 1 3-3h3.5' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round'/></svg>";

const char *kDiff = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><rect x='2' y='2.5' width='5' height='11' rx='1' stroke='@COLOR@' stroke-width='1.2'/><rect x='9' y='2.5' width='5' height='11' rx='1' stroke='@COLOR@' stroke-width='1.2'/><path d='M3.5 6h2M3.5 8.5h2M10.5 6h2M10.5 8.5h2' stroke='@COLOR@' stroke-width='1.1' stroke-linecap='round'/></svg>";

const char *kClose = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><path d='M4 4l8 8M12 4l-8 8' stroke='@COLOR@' stroke-width='1.3' stroke-linecap='round'/></svg>";

const char *kCode = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><path d='M5.5 4.5L2 8L5.5 11.5M10.5 4.5L14 8L10.5 11.5M9 3L7 13' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round' stroke-linejoin='round'/></svg>";

const char *kWindow = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><rect x='2' y='3' width='12' height='10' rx='1.5' stroke='@COLOR@' stroke-width='1.2'/><path d='M2 6.5h12M4.5 4.8h.01M6.5 4.8h.01' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round'/></svg>";

const char *kAdd = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><path d='M8 3v10M3 8h10' stroke='@COLOR@' stroke-width='1.2' stroke-linecap='round' stroke-linejoin='round'/></svg>";

const char *kRename = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' fill='none'><path d='M11 2L14 5L5 14H2V11L11 2Z' stroke='@COLOR@' stroke-width='1.2' stroke-linejoin='round'/><path d='M9.5 3.5L12.5 6.5' stroke='@COLOR@' stroke-width='1.2'/></svg>";

}  // namespace MenuIcons
