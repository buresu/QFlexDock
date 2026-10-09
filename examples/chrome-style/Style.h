// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockTheme.h>

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPalette>
#include <QtGui/QPixmap>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QStyleOption>
#include <QtWidgets/QTabBar>

#include <cmath>

// Colours, sizes, the style sheet and the icons of this example. Everything
// is drawn here; nothing is taken from the application whose layout it
// follows.
namespace ChromeStyle {

using namespace Qt::StringLiterals;

inline constexpr QRgb Frame = 0xff25292c;      // the window, behind the tabs
inline constexpr QRgb Toolbar = 0xff2d3033;    // the current tab and what it is joined to
inline constexpr QRgb Outline = 0xff575a5d;    // around the current tab
inline constexpr QRgb TabHover = 0xff33373b;
inline constexpr QRgb Page = 0xff1f2124;
inline constexpr QRgb Field = 0xff202124;      // the address field
inline constexpr QRgb Card = 0xff37373a;
inline constexpr QRgb Menu = 0xff1f2023;
inline constexpr QRgb MenuHover = 0xff3a3d40;
inline constexpr QRgb Border = 0xff3e4144;
inline constexpr QRgb Text = 0xfff1f3f4;
inline constexpr QRgb TextMuted = 0xffc4c7c5;
inline constexpr QRgb IconMuted = 0xff80868b;
inline constexpr QRgb Accent = 0xffa6c5fa;
inline constexpr QRgb CloseHover = 0xffd93025;

// Where the tabs are: a gap above them to take the window by, then the tabs.
inline constexpr int StripHeight = 40;
inline constexpr int TabTop = 6;
inline constexpr int TabWidth = 240;
/// How far the foot of the current tab curves out to either side. The tabs
/// keep this distance from the edges of the room QTabBar gives them.
inline constexpr int TabFoot = 8;
inline constexpr int TabRadius = 10;
inline constexpr int ToolbarHeight = 46;
inline constexpr int WindowRadius = 10;

inline QPalette palette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(Frame));
    p.setColor(QPalette::WindowText, QColor(Text));
    p.setColor(QPalette::Base, QColor(Field));
    p.setColor(QPalette::AlternateBase, QColor(Toolbar));
    p.setColor(QPalette::Text, QColor(Text));
    p.setColor(QPalette::Button, QColor(Toolbar));
    p.setColor(QPalette::ButtonText, QColor(Text));
    p.setColor(QPalette::Highlight, QColor(0xff004a77));
    p.setColor(QPalette::HighlightedText, QColor(Text));
    p.setColor(QPalette::ToolTipBase, QColor(Menu));
    p.setColor(QPalette::ToolTipText, QColor(Text));
    p.setColor(QPalette::PlaceholderText, QColor(TextMuted));
    p.setColor(QPalette::Mid, QColor(Border));
    p.setColor(QPalette::Dark, QColor(Border));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(IconMuted));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(IconMuted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(IconMuted));
    return p;
}

inline QString styleSheet()
{
    return uR"(
/* --- The window: a frame of the library's own, round at the top ---------- */
QFlexDock--DockFloatingWindow {
    background: #25292c;
    border: 1px solid #17191b;
    border-top-left-radius: 10px;
    border-top-right-radius: 10px;
}
QFlexDock--DockFloatingWindow[maximized="true"] { border: none; border-radius: 0; }
QFlexDock--DockTabGroup { border: none; }

/* --- The tab strip. The tabs themselves are drawn by the style. ---------- */
#dockTitleBar { background: transparent; border-bottom: 1px solid #575a5d; }
QFlexDock--DockTabBar { qproperty-activeIndicatorColor: transparent; font-size: 12px; }

#dockActionButton {
    background: transparent; border: none; border-radius: 14px;
    min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px;
    margin-top: 5px;
}
#dockActionButton:hover { background: #3a3e42; }
#dockActionButton:pressed { background: #474b50; }
#dockActionButton::menu-indicator { image: none; }
#dockTitleStartActions #dockActionButton { border-radius: 8px; margin-left: 8px; margin-right: 0; }
#dockTabActions #dockActionButton { margin-left: 2px; }
#dockTitleActions #dockActionButton { margin-left: 8px; }
#dockTitleActions #dockActionButton[action="windowClose"] { margin-right: 8px; }
#dockTitleActions #dockActionButton[action="windowClose"]:hover { background: #d93025; }

/* --- Toolbar -------------------------------------------------------------- */
#toolbar { background: #2d3033; border-bottom: 1px solid #202326; }
#toolButton {
    background: transparent; border: none; border-radius: 16px; color: #f1f3f4;
    min-width: 32px; max-width: 32px; min-height: 32px; max-height: 32px;
}
#toolButton:hover { background: #3c4043; }
#toolButton:pressed { background: #4a4e52; }
#toolButton::menu-indicator { image: none; }
#barSeparator { background: #4a4d51; border: none; max-width: 1px; min-width: 1px; }
#addressField {
    background: transparent; border: none; color: #f1f3f4; font-size: 14px;
    selection-background-color: #004a77;
}

/* --- Menus --------------------------------------------------------------- */
QMenu {
    background: #1f2023; color: #f1f3f4; border: 1px solid #3e4144; border-radius: 8px;
    padding: 6px 0; font-size: 13px;
}
QMenu::item { padding: 7px 28px 7px 14px; margin: 0; }
QMenu::item:selected { background: #3a3d40; }
QMenu::item:disabled { color: #80868b; }
QMenu::icon { padding-left: 14px; }
QMenu::separator { height: 1px; background: #3e4144; margin: 6px 0; }
QMenu::right-arrow { image: none; }
QToolTip { background: #1f2023; color: #f1f3f4; border: 1px solid #3e4144; padding: 4px 6px; }
)"_s;
}

// --- Icons -----------------------------------------------------------------

enum class Glyph {
    ChevronDown, Plus, Close, Minimize, Maximize, Restore, Back, Forward, Reload, Search, Dots,
    Apps, Extension, Star, Microphone, Camera, Sparkle, Tab, Window, Hat, Key, Clock, Download,
    Trash, Zoom, Print, Help, Gear, Pencil, Page, Grid, Person,
};

/// Draws `glyph` into the 16 x 16 square at the origin.
inline void drawGlyph(QPainter &p, Glyph glyph, const QColor &color)
{
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto dot = [&](qreal x, qreal y, qreal r = 1.3) {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(x, y), r, r);
        p.restore();
    };
    switch (glyph) {
    case Glyph::ChevronDown:
        p.drawPolyline(QPolygonF({QPointF(4, 6.5), QPointF(8, 10.5), QPointF(12, 6.5)}));
        break;
    case Glyph::Plus:
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        break;
    case Glyph::Close:
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
        break;
    case Glyph::Minimize:
        p.drawLine(QPointF(3.5, 12), QPointF(12.5, 12));
        break;
    case Glyph::Maximize:
        p.drawRect(QRectF(3.5, 3.5, 9, 9));
        break;
    case Glyph::Restore:
        p.drawRect(QRectF(3.5, 5.5, 7, 7));
        p.drawPolyline(QPolygonF({QPointF(5.5, 5.5), QPointF(5.5, 3.5), QPointF(12.5, 3.5),
                                  QPointF(12.5, 10.5), QPointF(10.5, 10.5)}));
        break;
    case Glyph::Back:
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        p.drawPolyline(QPolygonF({QPointF(7.5, 3.5), QPointF(3, 8), QPointF(7.5, 12.5)}));
        break;
    case Glyph::Forward:
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        p.drawPolyline(QPolygonF({QPointF(8.5, 3.5), QPointF(13, 8), QPointF(8.5, 12.5)}));
        break;
    case Glyph::Reload:
        p.drawArc(QRectF(3, 3, 10, 10), 45 * 16, 290 * 16);
        p.drawPolyline(QPolygonF({QPointF(12.8, 2.6), QPointF(12.8, 6), QPointF(9.4, 6)}));
        break;
    case Glyph::Search:
        p.drawEllipse(QPointF(7, 7), 4, 4);
        p.drawLine(QPointF(10, 10), QPointF(13.5, 13.5));
        break;
    case Glyph::Dots:
        dot(8, 3.5);
        dot(8, 8);
        dot(8, 12.5);
        break;
    case Glyph::Apps:
        for (const qreal x : {3.0, 9.0}) {
            for (const qreal y : {3.0, 9.0})
                p.drawRect(QRectF(x, y, 4, 4));
        }
        break;
    case Glyph::Extension: {
        QPainterPath piece;
        piece.moveTo(3, 5.5);
        piece.lineTo(6, 5.5);
        piece.cubicTo(5.2, 2.2, 9.8, 2.2, 9, 5.5);
        piece.lineTo(11.5, 5.5);
        piece.lineTo(11.5, 8);
        piece.cubicTo(14.8, 7.2, 14.8, 11.8, 11.5, 11);
        piece.lineTo(11.5, 13.5);
        piece.lineTo(3, 13.5);
        piece.closeSubpath();
        p.drawPath(piece);
        break;
    }
    case Glyph::Star: {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const qreal r = i % 2 ? 2.6 : 6;
            const qreal a = -M_PI / 2 + i * M_PI / 5;
            star << QPointF(8 + r * std::cos(a), 8.4 + r * std::sin(a));
        }
        p.drawPolygon(star);
        break;
    }
    case Glyph::Microphone:
        p.drawRoundedRect(QRectF(6, 2, 4, 7.5), 2, 2);
        p.drawArc(QRectF(3.5, 3.5, 9, 8.5), 180 * 16, 180 * 16);
        p.drawLine(QPointF(8, 12), QPointF(8, 14));
        break;
    case Glyph::Camera:
        p.drawRoundedRect(QRectF(2.5, 4, 11, 9), 2, 2);
        p.drawEllipse(QPointF(8, 8.5), 2.3, 2.3);
        dot(11.5, 6, 0.8);
        break;
    case Glyph::Sparkle: {
        QPainterPath star;
        star.moveTo(8, 2);
        star.quadTo(8.6, 7.4, 14, 8);
        star.quadTo(8.6, 8.6, 8, 14);
        star.quadTo(7.4, 8.6, 2, 8);
        star.quadTo(7.4, 7.4, 8, 2);
        p.drawPath(star);
        break;
    }
    case Glyph::Tab:
        p.drawRoundedRect(QRectF(2.5, 3.5, 11, 9), 1.5, 1.5);
        p.drawLine(QPointF(8, 3.5), QPointF(8, 6.5));
        p.drawLine(QPointF(8, 6.5), QPointF(13.5, 6.5));
        break;
    case Glyph::Window:
        p.drawPolyline(QPolygonF({QPointF(8, 3.5), QPointF(3.5, 3.5), QPointF(3.5, 12.5),
                                  QPointF(12.5, 12.5), QPointF(12.5, 8)}));
        p.drawLine(QPointF(11.5, 2), QPointF(11.5, 6));
        p.drawLine(QPointF(9.5, 4), QPointF(13.5, 4));
        break;
    case Glyph::Hat:
        p.drawLine(QPointF(2, 8), QPointF(14, 8));
        p.drawPolyline(QPolygonF({QPointF(4.5, 8), QPointF(5.5, 3.5), QPointF(10.5, 3.5),
                                  QPointF(11.5, 8)}));
        p.drawEllipse(QPointF(5.3, 11.3), 1.8, 1.8);
        p.drawEllipse(QPointF(10.7, 11.3), 1.8, 1.8);
        break;
    case Glyph::Key:
        p.drawEllipse(QPointF(5, 8), 2.5, 2.5);
        p.drawPolyline(QPolygonF({QPointF(7.5, 8), QPointF(13.5, 8), QPointF(13.5, 10.5)}));
        p.drawLine(QPointF(11, 8), QPointF(11, 10));
        break;
    case Glyph::Clock:
        p.drawEllipse(QPointF(8, 8), 5.5, 5.5);
        p.drawPolyline(QPolygonF({QPointF(8, 4.8), QPointF(8, 8), QPointF(10.3, 9.6)}));
        break;
    case Glyph::Download:
        p.drawLine(QPointF(8, 2.5), QPointF(8, 10));
        p.drawPolyline(QPolygonF({QPointF(4.5, 6.8), QPointF(8, 10.2), QPointF(11.5, 6.8)}));
        p.drawLine(QPointF(3.5, 13), QPointF(12.5, 13));
        break;
    case Glyph::Trash:
        p.drawLine(QPointF(3, 4.5), QPointF(13, 4.5));
        p.drawPolyline(QPolygonF({QPointF(4.5, 4.5), QPointF(5, 13.5), QPointF(11, 13.5),
                                  QPointF(11.5, 4.5)}));
        p.drawLine(QPointF(6.5, 2.5), QPointF(9.5, 2.5));
        break;
    case Glyph::Zoom:
        p.drawEllipse(QPointF(7, 7), 4, 4);
        p.drawLine(QPointF(10, 10), QPointF(13.5, 13.5));
        p.drawLine(QPointF(5.2, 7), QPointF(8.8, 7));
        p.drawLine(QPointF(7, 5.2), QPointF(7, 8.8));
        break;
    case Glyph::Print:
        p.drawRect(QRectF(2.5, 6.5, 11, 5));
        p.drawRect(QRectF(5, 2.5, 6, 4));
        p.drawRect(QRectF(5, 9.5, 6, 4));
        break;
    case Glyph::Help:
        p.drawEllipse(QPointF(8, 8), 5.5, 5.5);
        p.drawArc(QRectF(6.2, 4.6, 3.6, 3.6), -40 * 16, 220 * 16);
        p.drawLine(QPointF(8, 8.3), QPointF(8, 9.2));
        dot(8, 11.2, 0.8);
        break;
    case Glyph::Gear:
        p.drawEllipse(QPointF(8, 8), 2.2, 2.2);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * M_PI / 4;
            p.drawLine(QPointF(8 + 4.3 * std::cos(a), 8 + 4.3 * std::sin(a)),
                       QPointF(8 + 5.8 * std::cos(a), 8 + 5.8 * std::sin(a)));
        }
        p.drawEllipse(QPointF(8, 8), 4.3, 4.3);
        break;
    case Glyph::Pencil:
        p.drawPolygon(QPolygonF({QPointF(3, 13), QPointF(3.6, 10.2), QPointF(10.6, 3.2),
                                 QPointF(12.8, 5.4), QPointF(5.8, 12.4)}));
        break;
    case Glyph::Grid:
        for (const qreal gx : {3.0, 8.0, 13.0}) {
            for (const qreal gy : {3.0, 8.0, 13.0})
                dot(gx, gy, 1.5);
        }
        break;
    case Glyph::Person:
        p.drawEllipse(QPointF(8, 8), 6.3, 6.3);
        p.drawEllipse(QPointF(8, 6.4), 2.1, 2.1);
        p.drawArc(QRectF(4, 9.6, 8, 7), 25 * 16, 130 * 16);
        break;
    case Glyph::Page:
        // A ring within a ring: this example's own mark for an empty tab.
        p.setPen(QPen(color, 1.8));
        p.drawEllipse(QPointF(8, 8), 6, 6);
        dot(8, 8, 2.4);
        break;
    }
}

inline QPixmap pixmap(Glyph glyph, QRgb rgb = Text, int size = 16)
{
    constexpr qreal Scale = 2.0;
    QPixmap result(QSize(size, size) * Scale);
    result.setDevicePixelRatio(Scale);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 16.0, size / 16.0);
    drawGlyph(painter, glyph, QColor(rgb));
    return result;
}

inline QIcon icon(Glyph glyph, QRgb rgb = Text, int size = 16)
{
    QIcon result(pixmap(glyph, rgb, size));
    result.addPixmap(pixmap(glyph, IconMuted, size), QIcon::Disabled);
    return result;
}

/// A disc with a letter on it: what a site without artwork of ours gets.
inline QPixmap letterPixmap(const QString &text, QRgb rgb, int size)
{
    constexpr qreal Scale = 2.0;
    QPixmap result(QSize(size, size) * Scale);
    result.setDevicePixelRatio(Scale);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(rgb));
    painter.drawEllipse(QRectF(0, 0, size, size));
    QFont font = painter.font();
    font.setPixelSize(qMax(8, size * 5 / 9));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, text.left(1).toUpper());
    return result;
}

// --- The tab the style draws ------------------------------------------------

/// The parts of a tab, inside the rectangle QTabBar gives it.
struct TabParts
{
    QRectF body;
    QRect icon;
    QRect text;
    QRect closeButton;
};

inline TabParts tabParts(const QRect &rect, const QSize &closeButton)
{
    TabParts parts;
    parts.body = QRectF(rect.left() + TabFoot, rect.top() + TabTop, rect.width() - 2 * TabFoot,
                        rect.height() - TabTop);
    const int middle = rect.top() + TabTop + (rect.height() - TabTop) / 2;
    const int left = rect.left() + TabFoot;
    const int right = rect.right() + 1 - TabFoot;
    parts.icon = QRect(left + 10, middle - 8, 16, 16);
    int textRight = right - 10;
    if (!closeButton.isEmpty()) {
        parts.closeButton = QRect(right - 8 - closeButton.width(),
                                  middle - closeButton.height() / 2, closeButton.width(),
                                  closeButton.height());
        textRight = parts.closeButton.left() - 4;
    }
    parts.text = QRect(parts.icon.right() + 9, rect.top() + TabTop,
                       qMax(0, textRight - parts.icon.right() - 9), rect.height() - TabTop);
    return parts;
}

/// The base style under the style sheet: Fusion, with the tabs drawn here.
/// A style sheet gives a tab a box; the shape of these, joined to the
/// toolbar below with a foot curving out on either side, takes a painter.
class Style : public QProxyStyle
{
public:
    Style()
        : QProxyStyle(QStyleFactory::create(u"Fusion"_s))
    {
    }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override
    {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (element == CE_TabBarTab && tab) {
            drawTab(*tab, painter);
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override
    {
        if (element == PE_IndicatorTabClose) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const QRectF box(option->rect);
            if (option->state.testFlag(State_Raised) || option->state.testFlag(State_Sunken)) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(0xff4a4e52));
                painter->drawEllipse(box);
            }
            painter->translate(box.center() - QPointF(5, 5));
            painter->scale(10.0 / 16, 10.0 / 16);
            painter->setPen(QPen(QColor(Text), 2.2, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(3.5, 3.5), QPointF(12.5, 12.5));
            painter->drawLine(QPointF(12.5, 3.5), QPointF(3.5, 12.5));
            painter->restore();
            return;
        }
        if (element == PE_FrameTabBarBase)
            return;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    QRect subElementRect(SubElement element, const QStyleOption *option,
                         const QWidget *widget) const override
    {
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
            const TabParts parts = tabParts(tab->rect, tab->rightButtonSize);
            if (element == SE_TabBarTabText)
                return parts.text;
            if (element == SE_TabBarTabRightButton)
                return parts.closeButton;
        }
        return QProxyStyle::subElementRect(element, option, widget);
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget) const override
    {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_TabBarTab)
            result = QSize(result.width() + 2 * TabFoot, StripHeight);
        return result;
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override
    {
        switch (metric) {
        case PM_TabCloseIndicatorWidth:
        case PM_TabCloseIndicatorHeight:
            return 16;
        case PM_TabBarTabOverlap:
        case PM_TabBarBaseOverlap:
        case PM_TabBarTabShiftHorizontal:
        case PM_TabBarTabShiftVertical:
            return 0;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == SH_TabBar_CloseButtonPosition)
            return QTabBar::RightSide;
        if (hint == SH_TabBar_Alignment)
            return Qt::AlignLeft;
        if (hint == SH_UnderlineShortcut)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

private:
    static void drawTab(const QStyleOptionTab &tab, QPainter *painter)
    {
        const TabParts parts = tabParts(tab.rect, tab.rightButtonSize);
        const QRectF body = parts.body;
        const bool selected = tab.state.testFlag(State_Selected);
        const bool hovered = tab.state.testFlag(State_MouseOver);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        if (selected) {
            // Up the left foot, around the top and down the right foot. The
            // line under the strip runs into the feet; the tab covers it.
            const qreal bottom = tab.rect.bottom() + 0.5;
            const qreal left = body.left() + 0.5;
            const qreal right = body.right() - 0.5;
            const qreal top = body.top() + 0.5;
            const qreal r = qMin<qreal>(TabRadius, (right - left) / 2);
            QPainterPath outline;
            outline.moveTo(left - TabFoot, bottom);
            outline.arcTo(QRectF(left - 2 * TabFoot, bottom - 2 * TabFoot, 2 * TabFoot, 2 * TabFoot),
                          270, 90);
            outline.lineTo(left, top + r);
            outline.arcTo(QRectF(left, top, 2 * r, 2 * r), 180, -90);
            outline.lineTo(right - r, top);
            outline.arcTo(QRectF(right - 2 * r, top, 2 * r, 2 * r), 90, -90);
            outline.lineTo(right, bottom - TabFoot);
            outline.arcTo(QRectF(right, bottom - 2 * TabFoot, 2 * TabFoot, 2 * TabFoot), 180, 90);
            QPainterPath shape = outline;
            shape.lineTo(right + TabFoot, bottom + 1);
            shape.lineTo(left - TabFoot, bottom + 1);
            shape.closeSubpath();
            painter->fillPath(shape, QColor(Toolbar));
            painter->setPen(QPen(QColor(Outline), 1));
            painter->drawPath(outline);
        } else {
            if (hovered) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(TabHover));
                painter->drawRoundedRect(body.adjusted(0, 0, 0, -5), TabRadius, TabRadius);
            } else if (tab.selectedPosition != QStyleOptionTab::NextIsSelected) {
                // A short line parts two tabs neither of which is the current one.
                painter->setRenderHint(QPainter::Antialiasing, false);
                const int x = tab.rect.right();
                const int middle = int(body.center().y()) - 2;
                painter->fillRect(QRect(x, middle - 8, 1, 16), QColor(0xff4a4d51));
                painter->setRenderHint(QPainter::Antialiasing);
            }
        }

        // Icon and title, as far as there is room for them.
        painter->setClipRect(body.adjusted(4, 0, -4, 0), Qt::IntersectClip);
        if (!tab.icon.isNull()) {
            tab.icon.paint(painter, parts.icon, Qt::AlignCenter,
                           tab.state.testFlag(State_Enabled) ? QIcon::Normal : QIcon::Disabled);
        }
        painter->setPen(QColor(selected ? Text : TextMuted));
        painter->drawText(parts.text, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                          tab.text);
        painter->restore();
    }
};

/// The drop guide: tabs go between tabs and nowhere else, so a mark between
/// two tabs is all there is to show.
class OverlayPainter : public QFlexDock::DockOverlayPainter
{
public:
    void paint(QPainter *painter, const QFlexDock::DockOverlayScene &scene,
               const QFlexDock::DockOverlayStyle &) override
    {
        if (!scene.tabIndicator.isValid())
            return;
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(Accent));
        const QRectF mark(scene.tabIndicator.center().x() - 1.5, scene.tabIndicator.top() + TabTop + 4,
                          3, scene.tabIndicator.height() - TabTop - 10);
        painter->drawRoundedRect(mark, 1.5, 1.5);
    }
};

} // namespace ChromeStyle
