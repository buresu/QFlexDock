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

// Colours, the style sheet, the icons and the drop guide of this example.
// Everything is drawn here; nothing is taken from the application whose
// layout it follows.
namespace Photoshop {

using namespace Qt::StringLiterals;

inline constexpr QRgb Frame = 0xff535353;     // panels, bars, the tool palette
inline constexpr QRgb Well = 0xff282828;      // around a document
inline constexpr QRgb TabRow = 0xff424242;    // behind the tabs
inline constexpr QRgb Bar = 0xff3d3d3d;       // the bar above a column
inline constexpr QRgb Line = 0xff383838;      // between panels
inline constexpr QRgb Field = 0xff454545;     // inputs
inline constexpr QRgb Selected = 0xff6a6a6a;
inline constexpr QRgb Text = 0xffdddddd;
inline constexpr QRgb TextMuted = 0xff9d9d9d;
inline constexpr QRgb Blue = 0xff2680eb;      // where something would be docked

inline QPalette palette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(Frame));
    p.setColor(QPalette::WindowText, QColor(Text));
    p.setColor(QPalette::Base, QColor(Field));
    p.setColor(QPalette::AlternateBase, QColor(Frame));
    p.setColor(QPalette::Text, QColor(Text));
    p.setColor(QPalette::Button, QColor(Frame));
    p.setColor(QPalette::ButtonText, QColor(Text));
    p.setColor(QPalette::Highlight, QColor(Blue));
    p.setColor(QPalette::HighlightedText, QColor(0xffffffff));
    p.setColor(QPalette::ToolTipBase, QColor(0xfff0f0f0));
    p.setColor(QPalette::ToolTipText, QColor(0xff202020));
    p.setColor(QPalette::PlaceholderText, QColor(TextMuted));
    p.setColor(QPalette::Mid, QColor(Line));
    p.setColor(QPalette::Dark, QColor(Line));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0xff7d7d7d));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0xff7d7d7d));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xff7d7d7d));
    return p;
}

inline QString styleSheet()
{
    return uR"(
/* --- Panels --------------------------------------------------------------
   A tab group is a flat pane; its tabs stand in a darker row, the current
   one in the colour of the pane. The pane itself paints nothing: it is the
   colour of the window it is in, whose round corners it therefore keeps. */
QFlexDock--DockTabGroup { background: transparent; border: none; }
QFlexDock--DockTabGroup #dockTitleBar { background: #424242; }
#dockTitle { color: #dddddd; padding: 6px 4px; }
QFlexDock--DockTabBar {
    qproperty-activeIndicatorColor: transparent;
    background: #424242;
}
QFlexDock--DockTabBar::tab {
    background: #424242;
    color: #a8a8a8;
    border: none;
    border-right: 1px solid #383838;
    margin: 0;
    padding: 6px 10px;
}
QFlexDock--DockTabBar::tab:hover { color: #f0f0f0; }
QFlexDock--DockTabBar::tab:selected { background: #535353; color: #f0f0f0; }
#documents QFlexDock--DockTabBar::tab { padding: 6px 4px 6px 12px; }
QTabBar QToolButton { background: #424242; border: none; }
QFlexDock--DockSplitHandle { background: #383838; }

/* The bar above a column, and what a column is while it is put away: a
   strip of buttons, each tab group of it under a grip. */
QFlexDock--DockColumnBar { background: #3d3d3d; }
QFlexDock--DockIconStrip { background: transparent; }
QFlexDock--DockIconGrip { background: transparent; border-top: 1px solid #383838; }
QFlexDock--DockIconButton {
    background: transparent;
    color: #dddddd;
    border: none;
    padding: 5px 7px;
    text-align: left;
}
QFlexDock--DockIconButton:hover { background: #626262; }
QFlexDock--DockIconButton:checked { background: #3a3a3a; }
/* What a button brings out lies over the document; beside the strip of a
   floating window it is part of that window. */
QFlexDock--DockTabGroup[flyout="true"] { background: #535353; border: 1px solid #2b2b2b; }
QFlexDock--DockFloatingWindow QFlexDock--DockTabGroup[flyout="true"] {
    background: transparent;
    border: none;
    border-left: 1px solid #383838;
}
QFlexDock--DockFloatingWindow QFlexDock--DockTabGroup[flyout="true"] #dockTitleBar {
    border-top-right-radius: 7px;
}

#dockMenuButton, #dockMaximizeButton, #dockCloseButton, #dockFlyoutButton,
#dockIconifyButton, #dockColumnCloseButton, #dockActionButton {
    background: transparent;
    border: none;
    border-radius: 2px;
    padding: 2px;
}
#dockMenuButton:hover, #dockMaximizeButton:hover, #dockCloseButton:hover,
#dockFlyoutButton:hover, #dockActionButton:hover { background: #666666; }
#dockIconifyButton, #dockColumnCloseButton { padding: 0; }
#dockIconifyButton:hover, #dockColumnCloseButton:hover { background: #5a5a5a; }

/* Floating windows have round corners. What is at the top of one is round
   with it: the bar of a column, or the title of a document and its tabs. */
QFlexDock--DockFloatingWindow {
    background: #535353;
    border: 1px solid #2b2b2b;
    border-radius: 8px;
}
QFlexDock--DockFloatingWindow[maximized="true"] { border: none; border-radius: 0; }
QFlexDock--DockFloatingWindow QFlexDock--DockColumnBar {
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QFlexDock--DockFloatingWindow[owner="documents"] #dockTitleBar { background: transparent; }
QFlexDock--DockFloatingWindow[owner="documents"] QFlexDock--DockTabBar { background: transparent; }
QFlexDock--DockFloatingWindow[owner="documents"] QFlexDock--DockTabBar::tab:first {
    border-top-left-radius: 7px;
}
#dockFloatingMaximizeButton, #dockFloatingCloseButton { background: transparent; border: none; }

/* --- Window chrome ----------------------------------------------------- */
#optionsBar {
    background: #535353;
    border-top: 1px solid #484848;
    border-bottom: 1px solid #383838;
}
#titleBar QToolButton, #optionsBar QToolButton {
    background: transparent;
    color: #dddddd;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px;
}
#titleBar QToolButton:hover, #optionsBar QToolButton:hover { background: #626262; }
#optionsBar QToolButton:checked { background: #3a3a3a; border-color: #2e2e2e; }
#optionsBar QToolButton#pill { border: 1px solid #8a8a8a; border-radius: 11px; padding: 3px 10px; }
#titleBar QToolButton#windowButton, #titleBar QToolButton#closeButton {
    border-radius: 0;
    padding: 8px 14px;
}
#titleBar QToolButton#closeButton:hover { background: #c42b1c; }
#titleBar[rounded="true"] QToolButton#closeButton { border-top-right-radius: 7px; }
#optionsBar QLabel { color: #dddddd; }
#optionsBar QLabel:disabled, #optionsBar QCheckBox:disabled { color: #7d7d7d; }
#optionsSeparator { background: #424242; min-width: 1px; max-width: 1px; margin: 4px 6px; }
QMenuBar { background: transparent; color: #dddddd; }
QMenuBar::item { background: transparent; padding: 5px 7px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: #2680eb; color: #ffffff; }
QMenu { background: #f0f0f0; color: #202020; border: 1px solid #9a9a9a; padding: 3px 0; }
QMenu::item { padding: 4px 26px; }
QMenu::item:selected { background: #2680eb; color: #ffffff; }
QMenu::item:disabled { color: #9a9a9a; }
QMenu::separator { height: 1px; background: #c8c8c8; margin: 3px 0; }
QMenu::indicator { left: 7px; width: 12px; height: 12px; }
QToolTip { background: #f0f0f0; color: #202020; border: 1px solid #767676; padding: 2px 5px; }

/* --- Contents ---------------------------------------------------------- */
QLineEdit, QComboBox {
    background: #454545;
    color: #dddddd;
    border: 1px solid #666666;
    border-radius: 3px;
    padding: 2px 5px;
    selection-background-color: #2680eb;
}
QLineEdit:disabled, QComboBox:disabled { color: #7d7d7d; border-color: #5a5a5a; background: #505050; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView { background: #f0f0f0; color: #202020; selection-background-color: #2680eb; }
QCheckBox { color: #dddddd; spacing: 5px; }
QPushButton {
    background: #454545;
    color: #dddddd;
    border: 1px solid #666666;
    border-radius: 3px;
    padding: 3px 12px;
}
QPushButton:hover { background: #505050; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
QListWidget, QTreeWidget {
    background: transparent;
    color: #dddddd;
    border: none;
    outline: 0;
}
QListWidget::item { padding: 5px 6px; border-bottom: 1px solid #484848; }
QListWidget::item:selected, QTreeWidget::item:selected { background: #6a6a6a; color: #ffffff; }
QTreeWidget::item { padding: 5px 2px; }
#panelBar QToolButton, #statusRow QToolButton, #taskBar QToolButton {
    background: transparent;
    color: #dddddd;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 2px;
}
#panelBar QToolButton:hover, #taskBar QToolButton:hover { background: #666666; }
#panelBar QToolButton:checked { background: #3a3a3a; border-color: #2e2e2e; }
#panelFooter { border-top: 1px solid #484848; }
#sectionTitle { color: #f0f0f0; font-weight: 600; }
#muted { color: #9d9d9d; }
#statusRow QLabel { color: #dddddd; padding: 0 8px; }
#taskBar { background: #404040; border: 1px solid #2b2b2b; border-radius: 7px; }
#taskBar QToolButton { border: 1px solid #6a6a6a; padding: 3px 9px; }

QScrollBar:vertical { background: #4a4a4a; width: 14px; margin: 0; }
QScrollBar:horizontal { background: #4a4a4a; height: 14px; margin: 0; }
QScrollBar::handle { background: #6e6e6e; border-radius: 4px; margin: 3px; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: #8a8a8a; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)"_s;
}

// --- Icons ---------------------------------------------------------------------

enum class Glyph {
    // Chrome.
    Logo, Home, Search, Share, Minimize, Maximize, Restore, Close, Menu, ChevronDown,
    ChevronRight, Sparkle, Sliders, Workspace,
    // Tools.
    Move, Marquee, Lasso, ObjectSelect, Crop, Frame, Eyedropper, Heal, Brush, Stamp,
    HistoryBrush, Eraser, Gradient, Drop, Dodge, Pen, Type, Arrow, Rectangle, Hand, Zoom,
    More, QuickMask, ScreenMode, EditToolbar,
    // Selection options.
    SelectNew, SelectAdd, SelectSubtract, SelectIntersect,
    // Panels.
    Color, Swatches, Gradients, Patterns, Properties, Adjustments, Libraries, Layers,
    Channels, Paths, History, Actions,
    // Inside panels.
    Eye, Lock, Link, Fx, Mask, Folder, NewItem, Trash, Camera, Document, Portrait,
    Landscape, Image, Pixels, Position, Artboard, Play, Stop, Record, Snapshot,
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
    const auto solid = [&p, &color](std::initializer_list<QPointF> points) {
        p.save();
        p.setBrush(color);
        p.drawPolygon(QPolygonF(points));
        p.restore();
    };
    const auto dashed = [&p, &color] {
        QPen pen(color, 1.2);
        pen.setDashPattern({2, 1.6});
        p.setPen(pen);
    };

    switch (glyph) {
    case Glyph::Logo:
        // A sheet with a corner of it docked: this example's own mark.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xff0b1f33));
        p.drawRoundedRect(QRectF(0.5, 0.5, 15, 15), 3, 3);
        p.setBrush(QColor(0xff31a8ff));
        p.drawRect(QRectF(3.5, 4, 5.5, 8));
        p.setOpacity(0.6);
        p.drawRect(QRectF(10, 4, 2.5, 8));
        break;
    case Glyph::Home:
        solid({{8, 2.5}, {14, 8}, {12.2, 8}, {12.2, 13.5}, {9.4, 13.5}, {9.4, 10}, {6.6, 10},
               {6.6, 13.5}, {3.8, 13.5}, {3.8, 8}, {2, 8}});
        break;
    case Glyph::Search:
    case Glyph::Zoom:
        p.drawEllipse(QPointF(7, 7), 4.2, 4.2);
        line(10.2, 10.2, 13.5, 13.5);
        break;
    case Glyph::Share:
        poly({{5, 7}, {3.5, 7}, {3.5, 13.5}, {12.5, 13.5}, {12.5, 7}, {11, 7}});
        line(8, 2.5, 8, 10);
        poly({{5.5, 5}, {8, 2.5}, {10.5, 5}});
        break;
    case Glyph::Minimize:
        line(3.5, 8.5, 12.5, 8.5);
        break;
    case Glyph::Maximize:
        p.drawRect(QRectF(3.5, 3.5, 9, 9));
        break;
    case Glyph::Restore:
        p.drawRect(QRectF(3.5, 5.5, 7, 7));
        poly({{5.5, 5.5}, {5.5, 3.5}, {12.5, 3.5}, {12.5, 10.5}, {10.5, 10.5}});
        break;
    case Glyph::Close:
        line(4, 4, 12, 12);
        line(12, 4, 4, 12);
        break;
    case Glyph::Menu:
        for (qreal y : {4.5, 7.0, 9.5, 12.0})
            line(3, y, 13, y);
        break;
    case Glyph::ChevronDown:
        poly({{4.5, 6.5}, {8, 10}, {11.5, 6.5}});
        break;
    case Glyph::ChevronRight:
        poly({{6.5, 4.5}, {10, 8}, {6.5, 11.5}});
        break;
    case Glyph::Sparkle:
        solid({{8, 2}, {9.4, 6.6}, {14, 8}, {9.4, 9.4}, {8, 14}, {6.6, 9.4}, {2, 8}, {6.6, 6.6}});
        break;
    case Glyph::Sliders:
    case Glyph::Properties:
        // (Each a line and the knob on it.)
        for (const QPointF &knob : {QPointF(10, 4), QPointF(5.5, 8), QPointF(9, 12)}) {
            line(2.5, knob.y(), 13.5, knob.y());
            p.save();
            p.setBrush(color);
            p.drawEllipse(knob, 1.5, 1.5);
            p.restore();
        }
        break;
    case Glyph::Workspace:
        p.drawRect(QRectF(2.5, 3.5, 11, 9));
        p.fillRect(QRectF(9.5, 3.5, 4, 9), color);
        break;

    case Glyph::Move:
        line(8, 2, 8, 14);
        line(2, 8, 14, 8);
        poly({{6.2, 3.8}, {8, 2}, {9.8, 3.8}});
        poly({{6.2, 12.2}, {8, 14}, {9.8, 12.2}});
        poly({{3.8, 6.2}, {2, 8}, {3.8, 9.8}});
        poly({{12.2, 6.2}, {14, 8}, {12.2, 9.8}});
        break;
    case Glyph::Marquee:
        dashed();
        p.drawRect(QRectF(2.5, 3.5, 11, 9));
        break;
    case Glyph::Lasso:
        p.drawEllipse(QRectF(2.5, 3, 11, 7));
        poly({{5, 9.4}, {4.2, 11.5}, {5.6, 13.5}});
        break;
    case Glyph::ObjectSelect:
        dashed();
        p.drawRect(QRectF(2.5, 2.5, 9, 9));
        p.setPen(QPen(color, 1));
        solid({{8, 7}, {8, 14.5}, {10, 12.8}, {11.4, 15}, {12.6, 14.4}, {11.3, 12.2}, {13.8, 12}});
        break;
    case Glyph::Crop:
        poly({{4.5, 1.5}, {4.5, 11.5}, {14.5, 11.5}});
        poly({{1.5, 4.5}, {11.5, 4.5}, {11.5, 14.5}});
        break;
    case Glyph::Frame:
        p.drawRect(QRectF(2.5, 3.5, 11, 9));
        line(2.5, 3.5, 13.5, 12.5);
        line(13.5, 3.5, 2.5, 12.5);
        break;
    case Glyph::Eyedropper:
        line(3, 13, 9, 7);
        poly({{3, 13}, {2.6, 13.4}});
        solid({{8.5, 5}, {11, 7.5}, {13.6, 4.9}, {13.8, 2.8}, {11.4, 2.2}});
        break;
    case Glyph::Heal:
        p.save();
        p.translate(8, 8);
        p.rotate(-45);
        p.drawRoundedRect(QRectF(-6.5, -2.8, 13, 5.6), 2.6, 2.6);
        p.drawRect(QRectF(-2, -2.8, 4, 5.6));
        p.restore();
        break;
    case Glyph::Brush:
        solid({{13.5, 2.2}, {14, 3}, {8.6, 9.6}, {7, 8.2}});
        solid({{6.2, 9.2}, {7.8, 10.6}, {6.4, 13}, {2.4, 13.8}, {4.2, 12}});
        break;
    case Glyph::Stamp:
        solid({{6.5, 2.5}, {9.5, 2.5}, {9, 7.5}, {12.5, 9}, {12.5, 11}, {3.5, 11}, {3.5, 9},
               {7, 7.5}});
        line(3, 13.5, 13, 13.5);
        break;
    case Glyph::HistoryBrush:
        solid({{13.5, 2.2}, {14, 3}, {9.6, 8.6}, {8, 7.2}});
        solid({{7.2, 8.2}, {8.8, 9.6}, {7.4, 12}, {4.4, 12.8}, {5.6, 11}});
        p.drawArc(QRectF(1.5, 7.5, 7, 7), 150 * 16, 200 * 16);
        break;
    case Glyph::Eraser:
        p.save();
        p.translate(8, 8);
        p.rotate(-40);
        p.drawRect(QRectF(-6, -3, 12, 6));
        p.fillRect(QRectF(-6, -3, 5, 6), color);
        p.restore();
        break;
    case Glyph::Gradient:
    case Glyph::Gradients: {
        QLinearGradient fade(2.5, 0, 13.5, 0);
        fade.setColorAt(0, color);
        fade.setColorAt(1, Qt::transparent);
        p.fillRect(QRectF(2.5, 3.5, 11, 9), fade);
        p.drawRect(QRectF(2.5, 3.5, 11, 9));
        break;
    }
    case Glyph::Drop: {
        QPainterPath drop;
        drop.moveTo(8, 2);
        drop.cubicTo(11, 6, 12.5, 8, 12.5, 10);
        drop.cubicTo(12.5, 15.3, 3.5, 15.3, 3.5, 10);
        drop.cubicTo(3.5, 8, 5, 6, 8, 2);
        p.fillPath(drop, color);
        break;
    }
    case Glyph::Dodge:
        p.save();
        p.setBrush(color);
        p.drawEllipse(QPointF(10, 6), 3.8, 3.8);
        p.restore();
        line(7, 9, 2.5, 13.5);
        break;
    case Glyph::Pen:
        solid({{8, 1.5}, {11, 8}, {9.5, 12}, {6.5, 12}, {5, 8}});
        line(5.5, 14, 10.5, 14);
        break;
    case Glyph::Type:
        solid({{3, 3}, {13, 3}, {13, 5.6}, {12, 5.6}, {11.4, 4.4}, {9, 4.4}, {9, 12.4}, {10.6, 13},
               {10.6, 13.8}, {5.4, 13.8}, {5.4, 13}, {7, 12.4}, {7, 4.4}, {4.6, 4.4}, {4, 5.6},
               {3, 5.6}});
        break;
    case Glyph::Arrow:
        solid({{4, 2}, {4, 13}, {6.8, 10.4}, {8.8, 14.4}, {10.4, 13.6}, {8.5, 9.8}, {12, 9.6}});
        break;
    case Glyph::Rectangle:
        p.fillRect(QRectF(2.5, 4, 11, 8), QColor(color.red(), color.green(), color.blue(), 120));
        p.drawRect(QRectF(2.5, 4, 11, 8));
        break;
    case Glyph::Hand:
        solid({{4.5, 8}, {4.5, 4.4}, {6, 4.4}, {6, 3}, {7.5, 3}, {7.5, 2.4}, {9, 2.4}, {9, 3},
               {10.5, 3}, {10.5, 4.4}, {12, 4.4}, {12, 10.5}, {10.5, 14}, {6, 14}, {2.5, 9.5},
               {3.4, 8.4}});
        break;
    case Glyph::More:
        p.setBrush(color);
        for (qreal x : {3.5, 8.0, 12.5})
            p.drawEllipse(QPointF(x, 8), 0.9, 0.9);
        break;
    case Glyph::QuickMask:
        p.drawRect(QRectF(2.5, 3.5, 11, 9));
        dashed();
        p.drawEllipse(QPointF(8, 8), 2.8, 2.8);
        break;
    case Glyph::ScreenMode:
        p.drawRect(QRectF(5.5, 2.5, 8, 7));
        p.drawRect(QRectF(2.5, 6.5, 8, 7));
        break;
    case Glyph::EditToolbar:
        p.drawRect(QRectF(2.5, 4.5, 8, 9));
        line(4.5, 7.5, 8.5, 7.5);
        line(4.5, 10.5, 8.5, 10.5);
        line(12.5, 1.5, 12.5, 6.5);
        line(10, 4, 15, 4);
        break;

    case Glyph::SelectNew:
        p.fillRect(QRectF(3.5, 3.5, 9, 9), color);
        break;
    case Glyph::SelectAdd:
        p.fillRect(QRectF(2.5, 2.5, 8, 8), color);
        p.fillRect(QRectF(5.5, 5.5, 8, 8), color);
        break;
    case Glyph::SelectSubtract:
        p.fillRect(QRectF(2.5, 2.5, 8, 8), color);
        p.fillRect(QRectF(5.5, 5.5, 8, 8), QColor(Frame));
        p.drawRect(QRectF(5.5, 5.5, 8, 8));
        break;
    case Glyph::SelectIntersect:
        p.drawRect(QRectF(2.5, 2.5, 8, 8));
        p.drawRect(QRectF(5.5, 5.5, 8, 8));
        p.fillRect(QRectF(5.5, 5.5, 5, 5), color);
        break;

    case Glyph::Color: {
        // A painter's palette.
        QPainterPath shape;
        shape.addEllipse(QRectF(1.5, 3, 13, 10));
        QPainterPath holes;
        for (const QPointF &at : {QPointF(5, 6.5), QPointF(8, 5.3), QPointF(11, 6.5),
                                  QPointF(11.4, 9.8)}) {
            holes.addEllipse(at, 1.1, 1.1);
        }
        p.fillPath(shape.subtracted(holes), color);
        break;
    }
    case Glyph::Swatches:
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 4; ++column)
                p.drawRect(QRectF(2 + column * 3.2, 3.2 + row * 3.4, 2.4, 2.4));
        }
        break;
    case Glyph::Patterns:
        p.drawRect(QRectF(2.5, 3, 11, 10));
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((row + column) % 2 == 0)
                    p.fillRect(QRectF(3.4 + column * 3.2, 4 + row * 4.2, 2.8, 3.6), color);
            }
        }
        break;
    case Glyph::Adjustments:
        p.drawEllipse(QPointF(8, 8), 5.5, 5.5);
        p.save();
        p.setBrush(color);
        p.drawPie(QRectF(2.5, 2.5, 11, 11), 90 * 16, 180 * 16);
        p.restore();
        break;
    case Glyph::Libraries:
        p.drawRoundedRect(QRectF(2.5, 2.5, 11, 11), 1.5, 1.5);
        solid({{6, 5}, {10, 5}, {10, 11.5}, {8, 9.8}, {6, 11.5}});
        break;
    case Glyph::Layers:
        solid({{8, 2.5}, {14, 6}, {8, 9.5}, {2, 6}});
        poly({{2, 9}, {8, 12.5}, {14, 9}});
        break;
    case Glyph::Channels:
        p.save();
        p.setBrush(QColor(color.red(), color.green(), color.blue(), 150));
        p.drawEllipse(QPointF(8, 5.6), 3.4, 3.4);
        p.drawEllipse(QPointF(5.6, 10), 3.4, 3.4);
        p.drawEllipse(QPointF(10.4, 10), 3.4, 3.4);
        p.restore();
        break;
    case Glyph::Paths: {
        QPainterPath curve;
        curve.moveTo(3, 12);
        curve.cubicTo(4, 3, 12, 3, 13, 12);
        p.drawPath(curve);
        p.fillRect(QRectF(1.6, 10.6, 2.8, 2.8), color);
        p.fillRect(QRectF(11.6, 10.6, 2.8, 2.8), color);
        p.fillRect(QRectF(6.6, 4, 2.8, 2.8), color);
        break;
    }
    case Glyph::History:
        for (qreal y : {3.5, 7.0, 10.5})
            p.fillRect(QRectF(2.5, y, 3, 2.4), color);
        line(7, 4.7, 9, 4.7);
        line(7, 8.2, 9, 8.2);
        p.drawArc(QRectF(8.5, 5, 5.5, 7), -90 * 16, 220 * 16);
        poly({{9.4, 13.4}, {11.2, 12}, {9.6, 10.4}});
        break;
    case Glyph::Actions:
        solid({{6, 5}, {6, 14}, {8.2, 11.8}, {9.8, 15}, {11.2, 14.2}, {9.6, 11.2}, {12.6, 11}});
        for (int i = 0; i < 6; ++i) {
            p.save();
            p.translate(6, 5);
            p.rotate(150 + i * 36);
            p.drawLine(QPointF(0, 2.2), QPointF(0, 4));
            p.restore();
        }
        break;

    case Glyph::Eye: {
        QPainterPath eye;
        eye.moveTo(1.5, 8);
        eye.cubicTo(4.5, 3, 11.5, 3, 14.5, 8);
        eye.cubicTo(11.5, 13, 4.5, 13, 1.5, 8);
        p.drawPath(eye);
        p.setBrush(color);
        p.drawEllipse(QPointF(8, 8), 2, 2);
        break;
    }
    case Glyph::Lock:
        p.fillRect(QRectF(4, 7.5, 8, 6), color);
        p.drawArc(QRectF(5.5, 3, 5, 8), 0, 180 * 16);
        break;
    case Glyph::Link:
        p.drawRoundedRect(QRectF(2, 6, 7, 4), 2, 2);
        p.drawRoundedRect(QRectF(7, 6, 7, 4), 2, 2);
        break;
    case Glyph::Fx: {
        QFont font = p.font();
        font.setPixelSize(11);
        font.setItalic(true);
        p.setFont(font);
        p.drawText(QRectF(0, 0, 16, 15), Qt::AlignCenter, u"fx"_s);
        break;
    }
    case Glyph::Mask:
        p.fillRect(QRectF(2.5, 3.5, 11, 9), color);
        p.setBrush(QColor(Frame));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(8, 8), 2.8, 2.8);
        break;
    case Glyph::Folder:
        solid({{2.5, 4}, {6.5, 4}, {8, 5.5}, {13.5, 5.5}, {13.5, 12.5}, {2.5, 12.5}});
        break;
    case Glyph::NewItem:
        p.drawRect(QRectF(3, 3, 10, 10));
        line(8, 5.5, 8, 10.5);
        line(5.5, 8, 10.5, 8);
        break;
    case Glyph::Trash:
        solid({{4.5, 5.5}, {11.5, 5.5}, {10.8, 13.5}, {5.2, 13.5}});
        line(3.5, 4, 12.5, 4);
        line(6.5, 2.5, 9.5, 2.5);
        break;
    case Glyph::Camera:
    case Glyph::Snapshot:
        solid({{2, 5}, {5, 5}, {6, 3.5}, {10, 3.5}, {11, 5}, {14, 5}, {14, 12.5}, {2, 12.5}});
        p.setBrush(QColor(Frame));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(8, 8.6), 2.4, 2.4);
        break;
    case Glyph::Document:
        p.drawPolygon(QPolygonF({{4.5, 2.5}, {9.5, 2.5}, {12, 5}, {12, 13.5}, {4.5, 13.5}}));
        poly({{9.3, 2.8}, {9.3, 5.3}, {11.8, 5.3}});
        break;
    case Glyph::Portrait:
        p.drawRect(QRectF(4.5, 2.5, 7, 11));
        p.setBrush(color);
        p.drawEllipse(QPointF(8, 6.5), 1.4, 1.4);
        p.drawChord(QRectF(5.6, 8.6, 4.8, 5), 0, 180 * 16);
        break;
    case Glyph::Landscape:
        p.drawRect(QRectF(2.5, 4.5, 11, 7));
        p.setBrush(color);
        p.drawEllipse(QPointF(8, 7.4), 1.2, 1.2);
        p.drawChord(QRectF(5.8, 9, 4.4, 4), 0, 180 * 16);
        break;
    case Glyph::Image:
        p.drawRect(QRectF(2.5, 3.5, 11, 9));
        poly({{3.5, 11}, {6.5, 7.5}, {9, 10}, {10.5, 8.5}, {12.5, 11}});
        break;
    case Glyph::Pixels:
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                if ((row + column) % 2 == 0)
                    p.fillRect(QRectF(3 + column * 2.5, 3 + row * 2.5, 2.5, 2.5), color);
            }
        }
        p.drawRect(QRectF(3, 3, 10, 10));
        break;
    case Glyph::Position:
        line(8, 2, 8, 14);
        line(2, 8, 14, 8);
        poly({{6.4, 3.6}, {8, 2}, {9.6, 3.6}});
        poly({{12.4, 6.4}, {14, 8}, {12.4, 9.6}});
        break;
    case Glyph::Artboard:
        p.drawRect(QRectF(4.5, 4.5, 7, 7));
        line(2, 4.5, 3.5, 4.5);
        line(4.5, 2, 4.5, 3.5);
        line(12.5, 11.5, 14, 11.5);
        line(11.5, 12.5, 11.5, 14);
        break;
    case Glyph::Play:
        solid({{5, 3.5}, {12, 8}, {5, 12.5}});
        break;
    case Glyph::Stop:
        p.fillRect(QRectF(4, 4, 8, 8), color);
        break;
    case Glyph::Record:
        p.setBrush(color);
        p.drawEllipse(QPointF(8, 8), 4, 4);
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

/// A pair of marks, one behind the other: what the bar above a column has
/// for a button.
inline QIcon chevrons(bool right, int size = 10)
{
    constexpr qreal Scale = 2.0;
    QPixmap pixmap(int(size * Scale), int(size * Scale));
    pixmap.setDevicePixelRatio(Scale);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 10.0, size / 10.0);
    p.setPen(QPen(QColor(Text), 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (qreal x : {2.0, 5.5}) {
        const qreal from = right ? x : x + 2.5;
        const qreal tip = right ? x + 2.5 : x;
        p.drawPolyline(QPolygonF({QPointF(from, 2.5), QPointF(tip, 5), QPointF(from, 7.5)}));
    }
    return QIcon(pixmap);
}

// --- The drop guide ----------------------------------------------------------------

/// Nothing but where the drop would go, in blue:
///  - a tab group that takes one more tab is outlined, its tab row tinted;
///  - a tab that stays in its group has the outline around the pane and
///    around the place it would have among the tabs, as one shape;
///  - where a tab group or a column would be put in between, a bar along
///    that edge of what it is put beside.
class OverlayPainter : public QFlexDock::DockOverlayPainter
{
public:
    void paint(QPainter *painter, const QFlexDock::DockOverlayScene &scene,
               const QFlexDock::DockOverlayStyle &) override
    {
        using QFlexDock::DockArea;
        if (!scene.target.isValid())
            return;
        const QColor blue(Blue);
        DockArea side = DockArea::Center;
        for (const auto &zone : scene.zones) {
            if (zone.hovered)
                side = zone.area;
        }
        const QRect whole = scene.target;
        if (side != DockArea::Center) {
            constexpr int Thick = 4;
            QRect bar = whole;
            switch (side) {
            case DockArea::Left:
                bar.setWidth(Thick);
                break;
            case DockArea::Right:
                bar.setLeft(whole.right() - Thick + 1);
                break;
            case DockArea::Top:
                bar.setHeight(Thick);
                break;
            default:
                bar.setTop(whole.bottom() - Thick + 1);
                break;
            }
            painter->fillRect(bar, blue);
            return;
        }

        painter->setRenderHint(QPainter::Antialiasing, false);
        const QPen pen(blue, 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        const QRectF outline = QRectF(whole).adjusted(1, 1, -1, -1);
        const QRect tab = scene.tabGap.intersected(whole);
        const bool underHeader = scene.header.isValid() && scene.header.bottom() < whole.bottom();
        if (scene.ownGroup && tab.isValid() && underHeader) {
            // Around the pane and the tab, the rest of the tab row left out.
            const qreal top = scene.header.bottom() + 1;
            const qreal left = qMax<qreal>(tab.left() + 1, outline.left());
            const qreal right = qMin<qreal>(tab.right(), outline.right());
            QPolygonF shape;
            shape << QPointF(outline.left(), outline.bottom()) << QPointF(outline.left(), top);
            if (left > outline.left())
                shape << QPointF(left, top);
            shape << QPointF(left, tab.top() + 1) << QPointF(right, tab.top() + 1);
            if (right < outline.right())
                shape << QPointF(right, top);
            shape << QPointF(outline.right(), top) << QPointF(outline.right(), outline.bottom());
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPolygon(shape);
            return;
        }
        if (scene.header.isValid())
            painter->fillRect(scene.header.intersected(whole), QColor(38, 128, 235, 90));
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(outline);
    }
};

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
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->translate(QRectF(option->rect).center() - QPointF(5, 5));
            painter->scale(10.0 / 16, 10.0 / 16);
            const bool hovered = option->state.testFlag(State_Raised);
            drawGlyph(*painter, Glyph::Close, QColor(hovered ? 0xffffffff : TextMuted));
            painter->restore();
            return;
        }
        if (element == PE_IndicatorBranch) {
            if (!option->state.testFlag(State_Children))
                return;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->translate(QRectF(option->rect).center() - QPointF(8, 8));
            drawGlyph(*painter, option->state.testFlag(State_Open) ? Glyph::ChevronDown
                                                                   : Glyph::ChevronRight,
                      QColor(Text));
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override
    {
        if (metric == PM_TabCloseIndicatorWidth || metric == PM_TabCloseIndicatorHeight)
            return 16;
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

} // namespace Photoshop
