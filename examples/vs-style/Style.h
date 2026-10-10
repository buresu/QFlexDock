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
namespace VisualStudio {

using namespace Qt::StringLiterals;

inline constexpr QRgb Shell = 0xff222222;      // the window, between the panes
inline constexpr QRgb Document = 0xff232323;   // where the documents are
inline constexpr QRgb Tool = 0xff2c2c2c;       // tool windows
inline constexpr QRgb Raised = 0xff333333;     // inputs, rows on a pane
inline constexpr QRgb Border = 0xff464646;
inline constexpr QRgb Status = 0xff1b1b1b;
inline constexpr QRgb Text = 0xffe6e6e6;
inline constexpr QRgb TextMuted = 0xffa6a6a6;
inline constexpr QRgb IconMuted = 0xff6e6e6e;
inline constexpr QRgb Accent = 0xff938abf;
inline constexpr QRgb Green = 0xff73c991;
inline constexpr QRgb Blue = 0xff75beff;

inline QPalette palette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(Shell));
    p.setColor(QPalette::WindowText, QColor(Text));
    p.setColor(QPalette::Base, QColor(Document));
    p.setColor(QPalette::AlternateBase, QColor(Tool));
    p.setColor(QPalette::Text, QColor(Text));
    p.setColor(QPalette::Button, QColor(Tool));
    p.setColor(QPalette::ButtonText, QColor(Text));
    p.setColor(QPalette::Highlight, QColor(0xff4b4670));
    p.setColor(QPalette::HighlightedText, QColor(0xffffffff));
    p.setColor(QPalette::ToolTipBase, QColor(Tool));
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
/* --- Panes ---------------------------------------------------------------
   Every tab group is a pane with round corners and a gap to the next one,
   drawn by the group itself as one shape with its current tab: the tab
   sticks out of the pane, and one outline goes around the two. The pane the
   user works in has that outline in the accent colour. */
QFlexDock--DockTabGroup {
    qproperty-paneColor: #2c2c2c;
    qproperty-paneBorderColor: #3d3d3d;
    qproperty-paneActiveBorderColor: #938abf;
    qproperty-paneRadius: 6;
}
#documents QFlexDock--DockTabGroup {
    qproperty-paneColor: #232323;
    qproperty-paneBorderColor: #464646;
}
/* The panel that holds the documents is no pane itself. */
QFlexDock--DockTabGroup[headerVisible="false"] {
    qproperty-paneColor: transparent;
    qproperty-paneBorderColor: transparent;
    qproperty-paneActiveBorderColor: transparent;
}
QFlexDock--DockTabGroup #dockTitleBar { background: transparent; }
#dockTitle { color: #e6e6e6; padding: 6px 0 6px 4px; }

/* The gaps between the panes are what they are resized by. */
QFlexDock--DockSplitHandle { background: transparent; }

/* Tabs are names: the current one is part of the pane (see above). Those of
   tool windows are below the content, those of documents above it. */
QFlexDock--DockTabBar {
    qproperty-activeIndicatorColor: transparent;
    background: transparent;
}
QFlexDock--DockTabBar::tab {
    background: transparent;
    color: #a6a6a6;
    border: none;
    margin: 0;
}
QFlexDock--DockTabBar::tab:bottom { padding: 6px 14px 7px 14px; }
QFlexDock--DockTabBar::tab:top { padding: 7px 8px 7px 14px; }
QFlexDock--DockTabBar::tab:hover { color: #e6e6e6; }
QFlexDock--DockTabBar::tab:selected { color: #ffffff; }
QTabBar QToolButton { background: #2c2c2c; border: none; }

/* Buttons in the headers. */
#dockMenuButton, #dockAutoHideButton, #dockCloseButton, #dockPinButton,
#dockMaximizeButton, #dockFloatButton, #dockActionButton,
#dockFloatingMaximizeButton, #dockFloatingCloseButton {
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 4px;
    margin: 0 1px;
}
#dockMenuButton:hover, #dockAutoHideButton:hover, #dockCloseButton:hover, #dockPinButton:hover,
#dockMaximizeButton:hover, #dockFloatButton:hover, #dockActionButton:hover,
#dockFloatingMaximizeButton:hover, #dockFloatingCloseButton:hover { background: #444444; }
#dockActionButton::menu-indicator { image: none; width: 0; }
#dockTitleActions { margin-right: 4px; }

/* Put away at a border: a row of names, and the panel that slides out. */
QFlexDock--DockAutoHideBar { background: #222222; }
QFlexDock--DockAutoHideBar[edge="bottom"] { padding-left: 8px; }
/* The grip it is resized by is the gap to the panes beside it. */
QFlexDock--DockAutoHidePopup { background: #222222; border: none; }
#dockAutoHideBody {
    background: #2c2c2c;
    border: 1px solid #938abf;
    border-radius: 6px;
}
#dockAutoHideTitle { color: #e6e6e6; padding: 4px; }

/* Floating windows. */
QFlexDock--DockFloatingWindow { background: #222222; border: 1px solid #938abf; }
#dockFloatingTitleBar { background: #222222; }
#dockFloatingTitle { color: #e6e6e6; }

/* --- Window chrome ----------------------------------------------------- */
#titleBar QToolButton, #toolBar QToolButton, #statusBar QToolButton {
    background: transparent;
    color: #e6e6e6;
    border: none;
    border-radius: 4px;
    padding: 4px;
}
#titleBar QToolButton:hover, #toolBar QToolButton:hover { background: #3a3a3a; }
#titleBar QToolButton#windowButton { border-radius: 0; padding: 10px 16px; }
#titleBar QToolButton#closeButton { border-radius: 0; padding: 10px 16px; }
#titleBar QToolButton#closeButton:hover { background: #c42b1c; }
#titleBar QToolButton#search { color: #e6e6e6; padding: 4px 8px; }
#titleBar QLabel { color: #e6e6e6; }
QMenuBar { background: transparent; color: #e6e6e6; }
QMenuBar::item { background: transparent; padding: 5px 8px; border-radius: 4px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: #3a3a3a; }
QMenu {
    background: #2c2c2c;
    color: #e6e6e6;
    border: 1px solid #464646;
    border-radius: 6px;
    padding: 4px;
}
QMenu::item { padding: 5px 28px; border-radius: 4px; }
QMenu::item:selected { background: #4b4670; color: #ffffff; }
QMenu::item:disabled { color: #6e6e6e; }
QMenu::separator { height: 1px; background: #464646; margin: 4px 0; }
QMenu::indicator { left: 8px; width: 12px; height: 12px; }
QToolTip { background: #2c2c2c; color: #e6e6e6; border: 1px solid #464646; padding: 3px 6px; }

#toolGroup { background: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 6px; }
#toolBar QToolButton { padding: 3px 4px; }
#toolSeparator { background: #464646; min-width: 1px; max-width: 1px; margin: 5px 4px; }

#statusBar { background: #1b1b1b; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px; }
#statusBar QToolButton { padding: 2px 6px; }
#statusBar QToolButton:hover { background: #333333; }

/* --- Contents ---------------------------------------------------------- */
QTreeView, QListView {
    background: transparent;
    border: none;
    outline: 0;
    color: #e6e6e6;
}
QTreeView::item { padding: 2px 0; }
QTreeView::item:selected { background: #4b4670; color: #ffffff; }
QHeaderView::section {
    background: #2c2c2c;
    color: #a6a6a6;
    border: none;
    border-bottom: 1px solid #464646;
    padding: 3px 6px;
}
QPlainTextEdit {
    background: transparent;
    color: #dcdcdc;
    border: none;
    selection-background-color: #264f78;
    selection-color: #ffffff;
}
#paneBar QToolButton, #editorStatus QToolButton, #chatInput QToolButton {
    background: transparent;
    color: #e6e6e6;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 3px;
}
#paneBar QToolButton:hover, #editorStatus QToolButton:hover,
#chatInput QToolButton:hover { background: #444444; }
#paneBar QToolButton:checked { border-color: #938abf; }
#paneBar QLabel { color: #e6e6e6; }
#editorStatus { background: #333333; border-bottom-left-radius: 5px; border-bottom-right-radius: 5px; }
#editorStatus QLabel { color: #e6e6e6; padding: 0 6px; }
QLineEdit#paneSearch, QComboBox#paneCombo {
    background: #383838;
    color: #e6e6e6;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 4px 6px;
}
QLineEdit#paneSearch:focus { border-color: #938abf; }
QComboBox#paneCombo::drop-down { border: none; width: 18px; }
QPushButton#chip, QPushButton#paneButton {
    background: #2c2c2c;
    color: #e6e6e6;
    border: 1px solid #464646;
    border-radius: 4px;
    padding: 4px 10px;
}
QPushButton#chip:hover, QPushButton#paneButton:hover { background: #3a3a3a; }
QLabel#heading { color: #ffffff; font-size: 22px; font-weight: 600; }
QLabel#hint { color: #a6a6a6; }
QLabel#link { color: #75beff; }
#chatInput { background: #383838; border-radius: 6px; }
#chatInput QPlainTextEdit { background: transparent; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle { background: #5a5a5a; border-radius: 3px; margin: 3px; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: #7a7a7a; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QAbstractScrollArea::corner { background: transparent; border: none; }
)"_s;
}

// --- Icons ---------------------------------------------------------------------

enum class Glyph {
    Logo, Search, ChevronDown, ChevronUp, Minimize, Maximize, Restore, Close,
    Pin, PinSide, Ellipsis, Gear, Play, Save, SaveAll, Undo, Redo, Back, Forward,
    NewFile, Open, Folder, File, Solution, Refresh, Collapse, Wrench, Sync,
    Bell, Repository, Chat, NewChat, History, Send, Plus, Check, Bookmark, Comment,
    Indent, Feedback, Account, Split, Error, Warning, Info,
};

/// Draws a glyph into a 16 by 16 box.
inline void drawGlyph(QPainter &p, Glyph glyph, const QColor &color)
{
    p.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2) {
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const auto poly = [&p](std::initializer_list<QPointF> points) {
        p.drawPolyline(QPolygonF(points));
    };

    switch (glyph) {
    case Glyph::Logo:
        // Panes around a document: this example's own mark.
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(1.5, 2.5, 8, 7.5), 1.5, 1.5);
        p.setOpacity(0.6);
        p.drawRoundedRect(QRectF(10.5, 2.5, 4, 11), 1.5, 1.5);
        p.setOpacity(0.8);
        p.drawRoundedRect(QRectF(1.5, 11, 8, 2.5), 1.2, 1.2);
        break;
    case Glyph::Search:
        p.drawEllipse(QPointF(7, 7), 4.2, 4.2);
        line(10.2, 10.2, 13.5, 13.5);
        break;
    case Glyph::ChevronDown:
        poly({{4.5, 6.5}, {8, 10}, {11.5, 6.5}});
        break;
    case Glyph::ChevronUp:
        poly({{4.5, 9.5}, {8, 6}, {11.5, 9.5}});
        break;
    case Glyph::Minimize:
        line(3.5, 8.5, 12.5, 8.5);
        break;
    case Glyph::Maximize:
        p.drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 1, 1);
        break;
    case Glyph::Restore:
        p.drawRoundedRect(QRectF(3.5, 5.5, 7, 7), 1, 1);
        poly({{5.5, 5.5}, {5.5, 3.5}, {12.5, 3.5}, {12.5, 10.5}, {10.5, 10.5}});
        break;
    case Glyph::Close:
        line(4, 4, 12, 12);
        line(12, 4, 4, 12);
        break;
    case Glyph::Pin:
    case Glyph::PinSide:
        if (glyph == Glyph::PinSide) {
            p.translate(8, 8);
            p.rotate(90);
            p.translate(-8, -8);
        }
        p.setBrush(glyph == Glyph::Pin ? QBrush(color) : QBrush(Qt::NoBrush));
        p.drawPolygon(QPolygonF({{6, 2.5}, {10, 2.5}, {10, 7}, {11.5, 9}, {4.5, 9}, {6, 7}}));
        line(8, 9, 8, 13.5);
        break;
    case Glyph::Ellipsis:
        p.setBrush(color);
        for (qreal x : {3.5, 8.0, 12.5})
            p.drawEllipse(QPointF(x, 8), 0.7, 0.7);
        break;
    case Glyph::Gear:
        p.save();
        p.translate(8, 8);
        for (int i = 0; i < 8; ++i) {
            p.drawLine(QPointF(0, -4.6), QPointF(0, -6.2));
            p.rotate(45);
        }
        p.restore();
        p.drawEllipse(QPointF(8, 8), 4.2, 4.2);
        p.drawEllipse(QPointF(8, 8), 1.6, 1.6);
        break;
    case Glyph::Play:
        p.setBrush(color);
        p.drawPolygon(QPolygonF({{4.5, 3}, {12.5, 8}, {4.5, 13}}));
        break;
    case Glyph::Save:
        p.drawPolygon(QPolygonF({{3, 3}, {11, 3}, {13, 5}, {13, 13}, {3, 13}}));
        p.drawRect(QRectF(5.5, 3, 4.5, 3));
        p.drawRect(QRectF(5.5, 9, 5, 4));
        break;
    case Glyph::SaveAll:
        p.drawPolygon(QPolygonF({{5, 5}, {11.5, 5}, {13.5, 7}, {13.5, 13.5}, {5, 13.5}}));
        p.drawRect(QRectF(7, 10, 4.5, 3.5));
        poly({{3, 11}, {3, 2.5}, {9.5, 2.5}, {11, 4}});
        break;
    case Glyph::Undo:
        p.drawArc(QRectF(4, 5, 8.5, 8), -60 * 16, 240 * 16);
        poly({{3, 4}, {3.6, 7.6}, {7.2, 7}});
        break;
    case Glyph::Redo:
        p.drawArc(QRectF(3.5, 5, 8.5, 8), 0, 240 * 16);
        poly({{13, 4}, {12.4, 7.6}, {8.8, 7}});
        break;
    case Glyph::Back:
        p.drawEllipse(QPointF(8, 8), 5.5, 5.5);
        line(5.5, 8, 10.5, 8);
        poly({{7.5, 6}, {5.5, 8}, {7.5, 10}});
        break;
    case Glyph::Forward:
        p.drawEllipse(QPointF(8, 8), 5.5, 5.5);
        line(5.5, 8, 10.5, 8);
        poly({{8.5, 6}, {10.5, 8}, {8.5, 10}});
        break;
    case Glyph::NewFile:
        poly({{8, 13.5}, {4.5, 13.5}, {4.5, 3.5}, {9.5, 3.5}, {12.5, 6.5}, {12.5, 8}});
        line(12, 10, 12, 14);
        line(10, 12, 14, 12);
        break;
    case Glyph::Open:
    case Glyph::Folder:
        p.drawPolygon(QPolygonF({{2.5, 4}, {6.5, 4}, {8, 5.5}, {13.5, 5.5}, {13.5, 12.5},
                                 {2.5, 12.5}}));
        if (glyph == Glyph::Open)
            line(2.5, 7.5, 13.5, 7.5);
        break;
    case Glyph::File:
        p.drawPolygon(QPolygonF({{4.5, 2.5}, {9.5, 2.5}, {12, 5}, {12, 13.5}, {4.5, 13.5}}));
        poly({{9.3, 2.8}, {9.3, 5.3}, {11.8, 5.3}});
        break;
    case Glyph::Solution:
        p.drawRoundedRect(QRectF(2.5, 3.5, 11, 9), 1.5, 1.5);
        line(2.5, 6.5, 13.5, 6.5);
        line(6.5, 6.5, 6.5, 12.5);
        break;
    case Glyph::Refresh:
        p.drawArc(QRectF(3, 3, 10, 10), 60 * 16, 290 * 16);
        poly({{10.2, 2.2}, {10.8, 4.9}, {13.4, 4.2}});
        break;
    case Glyph::Collapse:
        poly({{4.5, 7}, {8, 3.5}, {11.5, 7}});
        poly({{4.5, 11.5}, {8, 8}, {11.5, 11.5}});
        break;
    case Glyph::Wrench:
        p.drawArc(QRectF(8, 2, 6, 6), 100 * 16, 250 * 16);
        line(9.2, 7.2, 3.5, 13);
        line(3.5, 13, 2.8, 12.3);
        break;
    case Glyph::Sync:
        line(3, 8, 13, 8);
        poly({{5.5, 5.5}, {3, 8}, {5.5, 10.5}});
        poly({{10.5, 5.5}, {13, 8}, {10.5, 10.5}});
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
    case Glyph::Repository:
        p.drawRoundedRect(QRectF(2.5, 3, 11, 3.5), 1, 1);
        poly({{3.5, 6.5}, {3.5, 13}, {12.5, 13}, {12.5, 6.5}});
        line(6.5, 9, 9.5, 9);
        break;
    case Glyph::Chat: {
        // A speech bubble with two eyes: this example's own drawing.
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(2, 3, 12, 8.5), 3, 3);
        p.drawPath(bubble);
        poly({{5.5, 11.5}, {5, 14}, {8, 11.5}});
        p.setBrush(color);
        p.drawEllipse(QPointF(6, 7.2), 0.8, 1.1);
        p.drawEllipse(QPointF(10, 7.2), 0.8, 1.1);
        break;
    }
    case Glyph::NewChat:
        p.drawRoundedRect(QRectF(3.5, 6, 9, 6.5), 2, 2);
        poly({{6, 12.5}, {5.5, 14.5}, {8, 12.5}});
        line(4.5, 1.5, 4.5, 5.5);
        line(2.5, 3.5, 6.5, 3.5);
        break;
    case Glyph::History:
        p.drawArc(QRectF(3, 3, 10, 10), 120 * 16, 300 * 16);
        poly({{2.2, 3.4}, {3.2, 6}, {5.8, 5}});
        poly({{8, 5.5}, {8, 8.3}, {10, 9.5}});
        break;
    case Glyph::Send:
        p.drawPolygon(QPolygonF({{2.5, 3}, {13.5, 8}, {2.5, 13}, {4.5, 8}}));
        line(4.5, 8, 9, 8);
        break;
    case Glyph::Plus:
        line(8, 3.5, 8, 12.5);
        line(3.5, 8, 12.5, 8);
        break;
    case Glyph::Check:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(8, 8), 6, 6);
        p.setPen(QPen(QColor(Raised), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        poly({{5.2, 8.2}, {7.2, 10.2}, {10.8, 6}});
        break;
    case Glyph::Bookmark:
        p.drawPolygon(QPolygonF({{4.5, 2.5}, {11.5, 2.5}, {11.5, 13.5}, {8, 10.5}, {4.5, 13.5}}));
        break;
    case Glyph::Comment:
        p.drawRect(QRectF(2.5, 3.5, 11, 6.5));
        poly({{8, 10}, {8, 13}, {11, 13}});
        break;
    case Glyph::Indent:
        line(6.5, 4, 13.5, 4);
        line(6.5, 8, 13.5, 8);
        line(6.5, 12, 13.5, 12);
        poly({{2.5, 6}, {4.5, 8}, {2.5, 10}});
        break;
    case Glyph::Feedback:
        p.drawRoundedRect(QRectF(2.5, 3, 11, 8), 1.5, 1.5);
        poly({{5, 11}, {5, 13.5}, {8, 11}});
        line(11, 1.5, 11, 5.5);
        line(9, 3.5, 13, 3.5);
        break;
    case Glyph::Account:
        p.drawEllipse(QPointF(6.5, 5.5), 2.4, 2.4);
        p.drawArc(QRectF(2, 9, 9, 8), 20 * 16, 140 * 16);
        line(12, 4, 12, 8);
        line(10, 6, 14, 6);
        break;
    case Glyph::Split:
        p.drawRoundedRect(QRectF(2.5, 3.5, 11, 9), 1.5, 1.5);
        line(2.5, 8, 13.5, 8);
        break;
    case Glyph::Error:
        p.drawEllipse(QPointF(8, 8), 5.5, 5.5);
        line(6, 6, 10, 10);
        line(10, 6, 6, 10);
        break;
    case Glyph::Warning:
        p.drawPolygon(QPolygonF({{8, 2.5}, {14, 13}, {2, 13}}));
        line(8, 6.5, 8, 9.5);
        p.drawPoint(QPointF(8, 11.3));
        break;
    case Glyph::Info:
        p.drawEllipse(QPointF(8, 8), 5.5, 5.5);
        line(8, 7.5, 8, 11);
        p.drawPoint(QPointF(8, 5.2));
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

/// The base style under the style sheet: Fusion, with what a style sheet
/// could only replace by image files drawn here instead.
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
                painter->setBrush(QColor(0xff505050));
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
            painter->setPen(QPen(QColor(TextMuted), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (option->state.testFlag(State_Open))
                painter->drawPolyline(QPolygonF({QPointF(4, 6), QPointF(8, 10), QPointF(12, 6)}));
            else
                painter->drawPolyline(QPolygonF({QPointF(6, 4), QPointF(10, 8), QPointF(6, 12)}));
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    // What is put away at a border is a name there, with a bar towards the
    // window's edge while it is out or pointed at.
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override
    {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (element != CE_TabBarTab || !tab || !widget
            || !widget->inherits("QFlexDock::DockAutoHideTab")) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        const bool selected = tab->state.testFlag(State_Selected);
        const bool hovered = tab->state.testFlag(State_MouseOver);
        const bool vertical = tab->shape == QTabBar::RoundedWest || tab->shape == QTabBar::RoundedEast;
        painter->save();
        QRect box = tab->rect;
        if (vertical) {
            // Read from the top down, like the spine of a book.
            painter->translate(box.right() + 1, box.top());
            painter->rotate(90);
            box = QRect(0, 0, box.height(), box.width());
        }
        painter->setPen(QColor(selected || hovered ? Text : TextMuted));
        painter->drawText(box.adjusted(8, 0, -8, -3), Qt::AlignCenter, tab->text);
        if (selected || hovered) {
            const bool outerIsFar = tab->shape == QTabBar::RoundedSouth
                || tab->shape == QTabBar::RoundedWest;
            const QRect bar(box.left() + 8, outerIsFar ? box.bottom() - 3 : box.top() + 1,
                            box.width() - 16, 3);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? Accent : Border));
            painter->drawRoundedRect(bar, 1.5, 1.5);
        }
        painter->restore();
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override
    {
        if (metric == PM_TabCloseIndicatorWidth || metric == PM_TabCloseIndicatorHeight)
            return 18;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == SH_ItemView_ShowDecorationSelected)
            return 1;
        // No lines under the letters that open the menus.
        if (hint == SH_UnderlineShortcut)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

} // namespace VisualStudio
