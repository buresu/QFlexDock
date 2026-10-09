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

// Colours, the style sheet and the icons of this example. Everything is drawn
// here; nothing is taken from the application whose layout it follows.
namespace VsStyle {

using namespace Qt::StringLiterals;

inline constexpr QRgb Chrome = 0xff181818;    // title bar, side bars, panel, status bar
inline constexpr QRgb Editor = 0xff1f1f1f;    // where the documents are
inline constexpr QRgb Border = 0xff2b2b2b;
inline constexpr QRgb Text = 0xffcccccc;
inline constexpr QRgb TextBright = 0xffffffff;
inline constexpr QRgb TextMuted = 0xff9d9d9d;
inline constexpr QRgb IconMuted = 0xff868686;
inline constexpr QRgb Accent = 0xff0078d4;
inline constexpr QRgb Selection = 0xff04395e;
inline constexpr QRgb SelectionInactive = 0xff37373d;
inline constexpr QRgb Hover = 0xff2a2d2e;
inline constexpr QRgb Input = 0xff313131;

inline QPalette palette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(Chrome));
    p.setColor(QPalette::WindowText, QColor(Text));
    p.setColor(QPalette::Base, QColor(Editor));
    p.setColor(QPalette::AlternateBase, QColor(Chrome));
    p.setColor(QPalette::Text, QColor(Text));
    p.setColor(QPalette::Button, QColor(Chrome));
    p.setColor(QPalette::ButtonText, QColor(Text));
    p.setColor(QPalette::Highlight, QColor(Selection));
    p.setColor(QPalette::Inactive, QPalette::Highlight, QColor(SelectionInactive));
    p.setColor(QPalette::HighlightedText, QColor(TextBright));
    p.setColor(QPalette::ToolTipBase, QColor(Editor));
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
/* --- Dock areas ---------------------------------------------------------- */
QFlexDock--DockTabGroup { background: #181818; border: none; }
QFlexDock--DockTabGroup #dockTitleBar { background: #181818; min-height: 35px; }

/* A boundary is a one pixel line that lights up, wider, when pointed at. */
QFlexDock--DockSplitHandle { background: #2b2b2b; }
QFlexDock--DockSplitHandle[hovered="true"],
QFlexDock--DockSplitHandle[pressed="true"] { background: #0078d4; }
/* The edge an area was put away to: lights up the same way when pointed at. */
QFlexDock--DockEdgeHandle { background: #0078d4; }

/* Side bars and the panel: flat tabs, the current one underlined. A single
   one is just a title. */
QFlexDock--DockTabBar {
    qproperty-activeIndicatorColor: transparent;
    background: transparent;
    font-size: 11px;
}
QFlexDock--DockTabBar::tab {
    background: transparent;
    color: #9d9d9d;
    height: 33px;
    padding: 0 1px;
    margin: 0 9px;
    border-top: 1px solid transparent;
    border-bottom: 1px solid transparent;
}
QFlexDock--DockTabBar::tab:first { margin-left: 19px; }
QFlexDock--DockTabBar::tab:hover { color: #e7e7e7; }
QFlexDock--DockTabBar::tab:selected { color: #e7e7e7; border-bottom-color: #0078d4; }
QFlexDock--DockTabBar::tab:only-one { color: #cccccc; border-bottom-color: transparent; }

/* The documents in the middle: tabs as boxes, the current one marked along
   its top. */
#editorArea { background: #1f1f1f; }
#editors QFlexDock--DockTabGroup { background: #1f1f1f; }
#editors QFlexDock--DockTabBar { font-size: 13px; }
#editors QFlexDock--DockTabBar::tab {
    background: #181818;
    color: #9d9d9d;
    height: 34px;
    padding: 0 6px 0 10px;
    margin: 0;
    border: none;
    border-top: 1px solid transparent;
    border-right: 1px solid #2b2b2b;
}
#editors QFlexDock--DockTabBar::tab:hover { color: #cccccc; }
#editors QFlexDock--DockTabBar::tab:selected {
    background: #1f1f1f;
    color: #ffffff;
    border-top-color: #3c3c3c;
}
/* The group the user is working in. */
#editors QFlexDock--DockTabBar[activeGroup="true"]::tab:selected {
    border-top-color: #0078d4;
}
QTabBar QToolButton { background: #181818; border: none; }

/* Buttons in the headers. */
#dockTitleActions { margin-right: 6px; }
#dockActionButton {
    background: transparent;
    border: none;
    border-radius: 5px;
    padding: 3px;
    margin: 0 1px;
}
#dockActionButton:hover, #dockActionButton:checked, #dockActionButton:pressed {
    background: #333435;
}
#dockActionButton::menu-indicator { image: none; width: 0; }
#dockActionSeparator {
    background: #3c3c3c;
    border: none;
    margin: 5px 6px;
    min-width: 1px;
    max-width: 1px;
}
#panelFilter {
    background: #313131;
    color: #cccccc;
    border: 1px solid #3c3c3c;
    border-radius: 3px;
    padding: 2px 6px;
    margin-right: 8px;
    max-width: 260px;
}
#panelFilter:focus { border-color: #0078d4; }

/* --- Window chrome --------------------------------------------------------- */
#titleBar { border-bottom: 1px solid #2b2b2b; }
#titleBar QToolButton {
    background: transparent;
    border: none;
    border-radius: 5px;
    padding: 4px;
}
#titleBar QToolButton:hover { background: #2d2e2e; }
#titleBar QToolButton#windowButton { border-radius: 0; padding: 9px 15px; }
#titleBar QToolButton#closeButton { border-radius: 0; padding: 9px 15px; }
#titleBar QToolButton#closeButton:hover { background: #e81123; }
#commandCenter {
    background: #252526;
    color: #cccccc;
    border: 1px solid #3a3a3a;
    border-radius: 6px;
    padding: 3px 12px;
}
#commandCenter:hover { background: #2d2e2e; border-color: #4a4a4a; }
QMenuBar { background: transparent; color: #cccccc; }
QMenuBar::item { background: transparent; padding: 5px 8px; border-radius: 5px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: #2d2e2e; }
QMenu {
    background: #1f1f1f;
    color: #cccccc;
    border: 1px solid #454545;
    border-radius: 6px;
    padding: 4px;
}
QMenu::item { padding: 5px 28px; border-radius: 4px; }
QMenu::item:selected { background: #0078d4; color: #ffffff; }
QMenu::item:disabled { color: #6b6b6b; }
QMenu::separator { height: 1px; background: #454545; margin: 4px 0; }
QMenu::indicator { left: 8px; width: 12px; height: 12px; }
QToolTip { background: #202020; color: #cccccc; border: 1px solid #454545; padding: 3px 6px; }

#activityBar { background: #181818; border-right: 1px solid #2b2b2b; }
#activityBar QToolButton {
    background: transparent;
    border: none;
    border-left: 2px solid transparent;
    border-right: 2px solid transparent;
    padding: 11px 9px;
}
#activityBar QToolButton:checked { border-left-color: #0078d4; }

#statusBar { border-top: 1px solid #2b2b2b; }
#statusBar QToolButton {
    background: transparent;
    color: #cccccc;
    border: none;
    font-size: 12px;
    padding: 0 5px;
    min-height: 22px;
}
#statusBar QToolButton:hover { background: #2d2e2e; }
#statusBar QToolButton#remote { background: #0078d4; padding: 0 10px; }
#statusBar QToolButton#remote:hover { background: #1a86d9; }

#commandPalette {
    background: #222222;
    border: 1px solid #454545;
    border-radius: 8px;
}
#commandPalette QLineEdit {
    background: #313131;
    color: #cccccc;
    border: 1px solid #0078d4;
    border-radius: 4px;
    padding: 4px 8px;
}
#commandPalette QListWidget { background: transparent; border: none; outline: 0; }
#commandPalette QListWidget::item { padding: 4px 6px; border-radius: 4px; color: #cccccc; }
#commandPalette QListWidget::item:selected { background: #04395e; color: #ffffff; }
#commandPalette QListWidget::item:hover:!selected { background: #2a2d2e; }

/* --- Contents -------------------------------------------------------------- */
QTreeView, QListView {
    background: transparent;
    border: none;
    outline: 0;
    color: #cccccc;
}
QPlainTextEdit {
    background: transparent;
    color: #cccccc;
    border: none;
    selection-background-color: #264f78;
    selection-color: #ffffff;
}
QLineEdit#viewInput {
    background: #313131;
    color: #cccccc;
    border: 1px solid #3c3c3c;
    border-radius: 3px;
    padding: 4px 6px;
}
QLineEdit#viewInput:focus { border-color: #0078d4; }
QPushButton#primary {
    background: #0078d4;
    color: #ffffff;
    border: none;
    border-radius: 3px;
    padding: 6px 14px;
}
QPushButton#primary:hover { background: #1a86d9; }
QLabel#sectionHeader { color: #cccccc; font-size: 11px; font-weight: bold; padding: 4px 8px; }
QLabel#hint { color: #9d9d9d; }
QLabel#breadcrumbs { color: #9d9d9d; padding: 0 16px; min-height: 22px; background: #1f1f1f; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle { background: rgba(121, 121, 121, 90); }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: rgba(121, 121, 121, 150); }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QAbstractScrollArea::corner { background: transparent; border: none; }
)"_s;
}

// --- Icons ---------------------------------------------------------------------

enum class Glyph {
    Files, Search, SourceControl, Debug, Extensions, Account, Gear,
    Ellipsis, Close, Plus, Trash, Refresh, CollapseAll, NewFile, NewFolder, Filter, Split,
    ChevronUp, ChevronDown, ArrowLeft, ArrowRight,
    SideBarLeft, SideBarLeftOff, PanelBottom, PanelBottomOff, SideBarRight, SideBarRightOff,
    Minimize, Maximize, Restore,
    Branch, Sync, Error, Warning, Bell, Remote, Sparkle, Logo,
};

/// Draws a glyph into a 16 by 16 box.
inline void drawGlyph(QPainter &p, Glyph glyph, const QColor &color)
{
    QPen pen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const auto frame = [&p] { p.drawRoundedRect(QRectF(2, 3, 12, 10), 1.5, 1.5); };
    const auto filled = [&p, &color](const QRectF &rect) {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRect(rect);
        p.restore();
    };

    switch (glyph) {
    case Glyph::Files: {
        QPainterPath front;
        front.moveTo(5.5, 4.5);
        front.lineTo(10.5, 4.5);
        front.lineTo(13.5, 7.5);
        front.lineTo(13.5, 14);
        front.lineTo(5.5, 14);
        front.closeSubpath();
        p.drawPath(front);
        p.drawPolyline(QPolygonF({QPointF(10.2, 4.8), QPointF(10.2, 7.8), QPointF(13.2, 7.8)}));
        p.drawPolyline(QPolygonF({QPointF(5.5, 11.5), QPointF(2.5, 11.5), QPointF(2.5, 2),
                                  QPointF(8, 2), QPointF(10, 4)}));
        break;
    }
    case Glyph::Search:
        p.drawEllipse(QPointF(9.3, 6.7), 4.2, 4.2);
        p.drawLine(QPointF(6.2, 9.8), QPointF(2.5, 13.5));
        break;
    case Glyph::SourceControl:
        p.drawEllipse(QPointF(4.5, 3.5), 1.6, 1.6);
        p.drawEllipse(QPointF(4.5, 12.5), 1.6, 1.6);
        p.drawEllipse(QPointF(11.5, 5.5), 1.6, 1.6);
        p.drawLine(QPointF(4.5, 5.1), QPointF(4.5, 10.9));
        p.drawArc(QRectF(4.5, 2.6, 7, 7), 180 * 16, 90 * 16);
        p.drawLine(QPointF(8, 9.6), QPointF(11.5, 7.1));
        break;
    case Glyph::Debug:
        p.drawPolygon(QPolygonF({QPointF(6.5, 2.5), QPointF(13.5, 7), QPointF(6.5, 11.5)}));
        p.drawEllipse(QPointF(4.5, 11.5), 2.4, 2.4);
        p.drawLine(QPointF(2.2, 9.2), QPointF(1.2, 8.2));
        p.drawLine(QPointF(6.8, 13.8), QPointF(7.8, 14.8));
        break;
    case Glyph::Extensions:
        p.drawRect(QRectF(2, 8.5, 5.5, 5.5));
        p.drawRect(QRectF(7.5, 8.5, 5.5, 5.5));
        p.drawRect(QRectF(2, 3, 5.5, 5.5));
        p.save();
        p.translate(11.4, 4.4);
        p.rotate(45);
        p.drawRect(QRectF(-2.4, -2.4, 4.8, 4.8));
        p.restore();
        break;
    case Glyph::Account:
        p.drawEllipse(QPointF(8, 8), 6.2, 6.2);
        p.drawEllipse(QPointF(8, 6.3), 2.2, 2.2);
        p.drawArc(QRectF(4, 9.3, 8, 7), 20 * 16, 140 * 16);
        break;
    case Glyph::Gear:
        p.save();
        p.translate(8, 8);
        for (int i = 0; i < 8; ++i) {
            p.drawLine(QPointF(0, -4.8), QPointF(0, -6.4));
            p.rotate(45);
        }
        p.restore();
        p.drawEllipse(QPointF(8, 8), 4.4, 4.4);
        p.drawEllipse(QPointF(8, 8), 1.6, 1.6);
        break;
    case Glyph::Ellipsis:
        p.setBrush(color);
        for (qreal x : {3.5, 8.0, 12.5})
            p.drawEllipse(QPointF(x, 8), 0.7, 0.7);
        break;
    case Glyph::Close:
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
        break;
    case Glyph::Plus:
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        break;
    case Glyph::Trash:
        p.drawLine(QPointF(3, 4.5), QPointF(13, 4.5));
        p.drawLine(QPointF(6.5, 2.5), QPointF(9.5, 2.5));
        p.drawRoundedRect(QRectF(4.5, 4.5, 7, 9), 1.2, 1.2);
        break;
    case Glyph::Refresh:
        p.drawArc(QRectF(3, 3, 10, 10), 60 * 16, 290 * 16);
        p.drawPolyline(QPolygonF({QPointF(10.2, 2.2), QPointF(10.8, 4.9), QPointF(13.4, 4.2)}));
        break;
    case Glyph::CollapseAll:
        p.drawRect(QRectF(5.5, 5.5, 8, 8));
        p.drawPolyline(QPolygonF({QPointF(3, 10.5), QPointF(3, 3), QPointF(10.5, 3)}));
        p.drawLine(QPointF(7.5, 9.5), QPointF(11.5, 9.5));
        break;
    case Glyph::NewFile:
        p.drawPolyline(QPolygonF({QPointF(8, 13.5), QPointF(3.5, 13.5), QPointF(3.5, 2.5),
                                  QPointF(9, 2.5), QPointF(12.5, 6), QPointF(12.5, 8)}));
        p.drawLine(QPointF(12, 10.5), QPointF(12, 14.5));
        p.drawLine(QPointF(10, 12.5), QPointF(14, 12.5));
        break;
    case Glyph::NewFolder:
        p.drawPolyline(QPolygonF({QPointF(8.5, 12.5), QPointF(2.5, 12.5), QPointF(2.5, 3.5),
                                  QPointF(6.5, 3.5), QPointF(8, 5.5), QPointF(13.5, 5.5),
                                  QPointF(13.5, 8.5)}));
        p.drawLine(QPointF(12, 10.5), QPointF(12, 14.5));
        p.drawLine(QPointF(10, 12.5), QPointF(14, 12.5));
        break;
    case Glyph::Filter:
        p.drawPolygon(QPolygonF({QPointF(2.5, 3.5), QPointF(13.5, 3.5), QPointF(9.5, 8.5),
                                 QPointF(9.5, 12.5), QPointF(6.5, 13.5), QPointF(6.5, 8.5)}));
        break;
    case Glyph::Split:
        frame();
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        break;
    case Glyph::ChevronUp:
        p.drawPolyline(QPolygonF({QPointF(3.5, 10), QPointF(8, 5.5), QPointF(12.5, 10)}));
        break;
    case Glyph::ChevronDown:
        p.drawPolyline(QPolygonF({QPointF(3.5, 6), QPointF(8, 10.5), QPointF(12.5, 6)}));
        break;
    case Glyph::ArrowLeft:
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        p.drawPolyline(QPolygonF({QPointF(7, 4), QPointF(3, 8), QPointF(7, 12)}));
        break;
    case Glyph::ArrowRight:
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        p.drawPolyline(QPolygonF({QPointF(9, 4), QPointF(13, 8), QPointF(9, 12)}));
        break;
    case Glyph::SideBarLeft:
        frame();
        filled(QRectF(2.6, 3.6, 4, 8.8));
        break;
    case Glyph::SideBarLeftOff:
        frame();
        p.drawLine(QPointF(6.6, 3), QPointF(6.6, 13));
        break;
    case Glyph::PanelBottom:
        frame();
        filled(QRectF(2.6, 9, 10.8, 3.4));
        break;
    case Glyph::PanelBottomOff:
        frame();
        p.drawLine(QPointF(2, 9), QPointF(14, 9));
        break;
    case Glyph::SideBarRight:
        frame();
        filled(QRectF(9.4, 3.6, 4, 8.8));
        break;
    case Glyph::SideBarRightOff:
        frame();
        p.drawLine(QPointF(9.4, 3), QPointF(9.4, 13));
        break;
    case Glyph::Minimize:
        p.drawLine(QPointF(3.5, 8.5), QPointF(12.5, 8.5));
        break;
    case Glyph::Maximize:
        p.drawRect(QRectF(3.5, 3.5, 9, 9));
        break;
    case Glyph::Restore:
        p.drawRect(QRectF(3.5, 5.5, 7, 7));
        p.drawPolyline(QPolygonF({QPointF(5.5, 5.5), QPointF(5.5, 3.5), QPointF(12.5, 3.5),
                                  QPointF(12.5, 10.5), QPointF(10.5, 10.5)}));
        break;
    case Glyph::Branch:
        p.drawEllipse(QPointF(5, 3.5), 1.5, 1.5);
        p.drawEllipse(QPointF(5, 12.5), 1.5, 1.5);
        p.drawEllipse(QPointF(11, 5.5), 1.5, 1.5);
        p.drawLine(QPointF(5, 5), QPointF(5, 11));
        p.drawLine(QPointF(5, 9.5), QPointF(11, 7));
        break;
    case Glyph::Sync:
        p.drawArc(QRectF(3, 3, 10, 10), 30 * 16, 150 * 16);
        p.drawArc(QRectF(3, 3, 10, 10), 210 * 16, 150 * 16);
        p.drawPolyline(QPolygonF({QPointF(13.6, 3.2), QPointF(12.6, 5.6), QPointF(10.2, 4.8)}));
        p.drawPolyline(QPolygonF({QPointF(2.4, 12.8), QPointF(3.4, 10.4), QPointF(5.8, 11.2)}));
        break;
    case Glyph::Error:
        p.drawEllipse(QPointF(8, 8), 5.5, 5.5);
        p.drawLine(QPointF(6, 6), QPointF(10, 10));
        p.drawLine(QPointF(10, 6), QPointF(6, 10));
        break;
    case Glyph::Warning:
        p.drawPolygon(QPolygonF({QPointF(8, 2.5), QPointF(14, 13), QPointF(2, 13)}));
        p.drawLine(QPointF(8, 6.5), QPointF(8, 9.5));
        p.drawPoint(QPointF(8, 11.3));
        break;
    case Glyph::Bell: {
        QPainterPath bell;
        bell.moveTo(3, 11.5);
        bell.lineTo(4.5, 9.5);
        bell.lineTo(4.5, 6.5);
        bell.cubicTo(4.5, 1.5, 11.5, 1.5, 11.5, 6.5);
        bell.lineTo(11.5, 9.5);
        bell.lineTo(13, 11.5);
        bell.closeSubpath();
        p.drawPath(bell);
        p.drawArc(QRectF(6.5, 10.5, 3, 3), 180 * 16, 180 * 16);
        break;
    }
    case Glyph::Remote:
        p.drawPolyline(QPolygonF({QPointF(3, 4), QPointF(7, 7), QPointF(3, 10)}));
        p.drawPolyline(QPolygonF({QPointF(13, 6), QPointF(9, 9), QPointF(13, 12)}));
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
    case Glyph::Logo:
        // Two panes side by side: this example's own mark.
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(2, 2.5, 4.5, 11), 1, 1);
        p.setOpacity(0.55);
        p.drawRoundedRect(QRectF(7.5, 2.5, 6.5, 6.5), 1, 1);
        p.setOpacity(0.8);
        p.drawRoundedRect(QRectF(7.5, 10, 6.5, 3.5), 1, 1);
        break;
    }
}

inline QPixmap pixmap(Glyph glyph, QRgb rgb = Text, int size = 16)
{
    constexpr qreal Scale = 2.0;
    QPixmap pixmap(int(size * Scale), int(size * Scale));
    pixmap.setDevicePixelRatio(Scale);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 16.0, size / 16.0);
    drawGlyph(p, glyph, QColor(rgb));
    return pixmap;
}

inline QIcon icon(Glyph glyph, QRgb rgb = Text, int size = 16)
{
    return QIcon(pixmap(glyph, rgb, size));
}

/// An icon that changes with the checked state of its button.
inline QIcon icon(Glyph off, QRgb offRgb, Glyph on, QRgb onRgb, int size = 16)
{
    QIcon result;
    result.addPixmap(pixmap(off, offRgb, size), QIcon::Normal, QIcon::Off);
    result.addPixmap(pixmap(on, onRgb, size), QIcon::Normal, QIcon::On);
    return result;
}

/// What stands for a kind of file in tabs and in the explorer: a letter.
inline QIcon fileIcon(const QString &fileName)
{
    QString letter(QChar(0x2261));
    QRgb rgb = TextMuted;
    if (fileName.endsWith(".h"_L1)) {
        letter = u"H"_s;
        rgb = 0xffa074c4;
    } else if (fileName.endsWith(".cpp"_L1)) {
        letter = u"C"_s;
        rgb = 0xff519aba;
    } else if (fileName.endsWith(".txt"_L1) || fileName.endsWith(".cmake"_L1)) {
        letter = u"M"_s;
        rgb = 0xff6d8086;
    } else if (fileName.endsWith(".md"_L1)) {
        letter = u"i"_s;
        rgb = 0xff519aba;
    }
    constexpr qreal Scale = 2.0;
    QPixmap pixmap(int(16 * Scale), int(16 * Scale));
    pixmap.setDevicePixelRatio(Scale);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font = p.font();
    font.setPixelSize(13);
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor(rgb));
    p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, letter);
    return QIcon(pixmap);
}

/// What a drag shows: the place the dragged tab would take, and nothing else.
class OverlayPainter : public QFlexDock::DockOverlayPainter
{
public:
    void paint(QPainter *painter, const QFlexDock::DockOverlayScene &scene,
               const QFlexDock::DockOverlayStyle &) override
    {
        if (scene.preview.isValid())
            painter->fillRect(scene.preview, QColor(83, 89, 93, 128));
        if (scene.tabIndicator.isValid())
            painter->fillRect(scene.tabIndicator, QColor(Accent));
    }
};

/// The base style under the style sheet: Fusion, with the two things a style
/// sheet could only replace by image files drawn here instead.
class Style : public QProxyStyle
{
public:
    Style()
        : QProxyStyle(QStyleFactory::create(u"Fusion"_s))
    {
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override
    {
        if (element == PE_IndicatorTabClose) {
            // On the current tab, or under the pointer.
            const bool hovered = option->state.testFlag(State_Raised);
            if (!hovered && !option->state.testFlag(State_Selected))
                return;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const QRectF box = QRectF(option->rect).adjusted(1, 1, -1, -1);
            if (hovered) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(0xff333435));
                painter->drawRoundedRect(box, 4, 4);
            }
            painter->translate(box.center() - QPointF(6, 6));
            painter->scale(0.75, 0.75);
            drawGlyph(*painter, Glyph::Close, QColor(Text));
            painter->restore();
            return;
        }
        if (element == PE_IndicatorBranch) {
            if (!option->state.testFlag(State_Children))
                return;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->translate(QRectF(option->rect).center() - QPointF(8, 8));
            painter->setPen(QPen(QColor(Text), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (option->state.testFlag(State_Open))
                painter->drawPolyline(QPolygonF({QPointF(4, 6), QPointF(8, 10), QPointF(12, 6)}));
            else
                painter->drawPolyline(QPolygonF({QPointF(6, 4), QPointF(10, 8), QPointF(6, 12)}));
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override
    {
        if (metric == PM_TabCloseIndicatorWidth || metric == PM_TabCloseIndicatorHeight)
            return 20;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        // A selected row is marked from the very left, arrows included.
        if (hint == SH_ItemView_ShowDecorationSelected)
            return 1;
        // No lines under the letters that open the menus.
        if (hint == SH_UnderlineShortcut)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

} // namespace VsStyle
