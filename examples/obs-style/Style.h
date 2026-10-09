// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockTheme.h>

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPalette>
#include <QtGui/QPixmap>

// Colours, the style sheet and the small line icons of this example. Everything
// is drawn here; nothing is taken from the application whose layout it follows.
namespace ObsStyle {

inline constexpr QRgb Window = 0xff1d1f26;   // behind and between the docks
inline constexpr QRgb Dock = 0xff272a33;     // body of a dock
inline constexpr QRgb Header = 0xff3c404d;   // title bars, buttons, borders
inline constexpr QRgb HeaderHover = 0xff4f5465;
inline constexpr QRgb Text = 0xfffefefe;
inline constexpr QRgb TextMuted = 0xff9a9ca3;
inline constexpr QRgb Accent = 0xff284cb8;   // selection
inline constexpr QRgb AccentLight = 0xff718cdc;
inline constexpr QRgb Program = 0xff000000; // the canvas of the preview
inline constexpr QRgb PreviewBack = 0xff16181e;

inline QPalette palette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(Window));
    p.setColor(QPalette::WindowText, QColor(Text));
    p.setColor(QPalette::Base, QColor(Dock));
    p.setColor(QPalette::AlternateBase, QColor(Header));
    p.setColor(QPalette::Text, QColor(Text));
    p.setColor(QPalette::Button, QColor(Header));
    p.setColor(QPalette::ButtonText, QColor(Text));
    p.setColor(QPalette::Highlight, QColor(Accent));
    p.setColor(QPalette::HighlightedText, QColor(Text));
    p.setColor(QPalette::ToolTipBase, QColor(Header));
    p.setColor(QPalette::ToolTipText, QColor(Text));
    p.setColor(QPalette::PlaceholderText, QColor(TextMuted));
    p.setColor(QPalette::Mid, QColor(Header));
    p.setColor(QPalette::Dark, QColor(Window));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(TextMuted));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(TextMuted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(TextMuted));
    return p;
}

inline QString styleSheet()
{
    return QStringLiteral(R"(
/* --- Docks ------------------------------------------------------------- */
QFlexDock--DockTabGroup {
    background: #272a33;
    border: 1px solid #3c404d;
    border-radius: 4px;
}
/* The preview is not a dock: no frame, nothing behind it. */
QFlexDock--DockTabGroup[headerVisible="false"] {
    background: transparent;
    border: none;
}
QFlexDock--DockTabGroup #dockTitleBar {
    background: #3c404d;
    border-top-left-radius: 3px;
    border-top-right-radius: 3px;
    min-height: 28px;
}
#dockTitle {
    color: #fefefe;
    font-weight: bold;
    padding-left: 2px;
}
#dockFloatButton, #dockCloseButton {
    border: none;
    border-radius: 3px;
    padding: 3px;
    margin-right: 3px;
}
#dockFloatButton:hover, #dockCloseButton:hover { background: #4f5465; }

/* Tabs appear below the content once docks are stacked. */
QFlexDock--DockTabBar {
    qproperty-activeIndicatorColor: transparent;
}
QFlexDock--DockTabBar::tab {
    background: #3c404d;
    color: #fefefe;
    padding: 4px 12px;
    margin: 3px 0 3px 3px;
    border-radius: 4px;
}
QFlexDock--DockTabBar::tab:selected { background: #284cb8; }
QFlexDock--DockTabBar::tab:hover:!selected { background: #4f5465; }

/* The gap between docks is the handle; it only shows while it is held. */
QFlexDock--DockSplitHandle { background: transparent; }
QFlexDock--DockSplitHandle[pressed="true"] { background: #d2d5db; }

QFlexDock--DockFloatingWindow {
    background: #1d1f26;
    border: 1px solid #3c404d;
}

/* --- Application chrome -------------------------------------------------- */
QMainWindow, QMenuBar, QStatusBar { background: #1d1f26; color: #fefefe; }
QMenuBar::item { padding: 5px 9px; background: transparent; }
QMenuBar::item:selected { background: #3c404d; border-radius: 4px; }
QMenu { background: #272a33; color: #fefefe; border: 1px solid #3c404d; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 24px; border-radius: 3px; }
QMenu::item:selected { background: #284cb8; }
QMenu::separator { height: 1px; background: #3c404d; margin: 4px 6px; }
QStatusBar { border-top: 1px solid #272a33; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #c8cad0; padding: 0 9px; border-left: 1px solid #3c404d; }

/* --- Dock contents --------------------------------------------------------- */
QListWidget {
    background: transparent;
    border: none;
    outline: none;
    padding: 3px;
}
QListWidget::item { padding: 7px 8px; border-radius: 4px; border: 1px solid transparent; }
QListWidget::item:selected { background: #284cb8; border: 1px solid #718cdc; color: #fefefe; }
QListWidget::item:hover:!selected { background: #32353f; }

#dockToolBar { border-top: 1px solid #3c404d; }
#dockToolBar QToolButton {
    background: #1d1f26;
    border: none;
    border-radius: 4px;
    padding: 5px;
}
#dockToolBar QToolButton:hover { background: #3c404d; }
#dockToolBar QToolButton#plain { background: transparent; }
#dockToolBar QLabel { color: #c8cad0; font-weight: bold; padding: 0 8px; }
#toolSeparator { background: #3c404d; max-width: 1px; min-width: 1px; }

QPushButton {
    background: #3c404d;
    color: #fefefe;
    border: none;
    border-radius: 4px;
    padding: 9px 14px;
    font-weight: bold;
}
QPushButton:hover { background: #4f5465; }
QPushButton:pressed { background: #284cb8; }
QPushButton#flat {
    background: transparent;
    border: 1px solid #3c404d;
    color: #9a9ca3;
    font-weight: normal;
    padding: 7px 14px;
}
QComboBox, QSpinBox {
    background: #3c404d;
    color: #fefefe;
    border: none;
    border-radius: 4px;
    padding: 6px 10px;
    min-height: 18px;
}
QComboBox::drop-down, QSpinBox::up-button, QSpinBox::down-button {
    background: transparent;
    border: none;
    width: 24px;
}
QComboBox::down-arrow, QSpinBox::up-arrow, QSpinBox::down-arrow {
    width: 0; height: 0;
    border-left: 4px solid #3c404d;
    border-right: 4px solid #3c404d;
}
QComboBox::down-arrow, QSpinBox::down-arrow { border-top: 5px solid #fefefe; }
QSpinBox::up-arrow { border-bottom: 5px solid #fefefe; }
#contextBar { background: #272a33; border-radius: 4px; }
#contextBar QLabel { color: #fefefe; font-weight: bold; }
#zoomBar QLabel, #zoomBar QToolButton { color: #9a9ca3; font-size: 11px; border: none; }
#zoomBar QToolButton { border: 1px solid #3c404d; border-radius: 2px; padding: 0 5px; }
)");
}

// --- Icons ---------------------------------------------------------------------

enum class Glyph {
    Plus, Minus, Trash, Gear, Up, Down, Filters, Eye, EyeOff, Lock, Play, Camera,
    Speaker, Monitor, Dots, Float, Bars, Layout, Close,
};

/// A 16px line icon, drawn at twice that for sharpness on dense screens.
inline QIcon icon(Glyph glyph, QRgb rgb = Text)
{
    constexpr int Size = 16;
    constexpr qreal Scale = 2.0;
    QPixmap pixmap(int(Size * Scale), int(Size * Scale));
    pixmap.setDevicePixelRatio(Scale);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor color(rgb);
    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (glyph) {
    case Glyph::Plus:
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        break;
    case Glyph::Minus:
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        break;
    case Glyph::Close:
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
        break;
    case Glyph::Trash:
        p.drawLine(QPointF(3, 4.5), QPointF(13, 4.5));
        p.drawLine(QPointF(6.5, 2.5), QPointF(9.5, 2.5));
        p.drawRoundedRect(QRectF(4.5, 4.5, 7, 9), 1.2, 1.2);
        p.drawLine(QPointF(7, 7), QPointF(7, 11));
        p.drawLine(QPointF(9, 7), QPointF(9, 11));
        break;
    case Glyph::Gear: {
        p.save();
        p.translate(8, 8);
        for (int i = 0; i < 8; ++i) {
            p.drawLine(QPointF(0, -4.6), QPointF(0, -6.2));
            p.rotate(45);
        }
        p.restore();
        p.drawEllipse(QPointF(8, 8), 4.2, 4.2);
        p.setBrush(color);
        p.drawEllipse(QPointF(8, 8), 1.3, 1.3);
        break;
    }
    case Glyph::Up:
        p.drawPolyline(QPolygonF({QPointF(3.5, 10), QPointF(8, 5.5), QPointF(12.5, 10)}));
        break;
    case Glyph::Down:
        p.drawPolyline(QPolygonF({QPointF(3.5, 6), QPointF(8, 10.5), QPointF(12.5, 6)}));
        break;
    case Glyph::Filters:
        p.drawRoundedRect(QRectF(3, 3, 10, 10), 1.5, 1.5);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.setOpacity(0.55);
        p.drawRect(QRectF(4.2, 4.2, 3.6, 7.6));
        break;
    case Glyph::Eye:
    case Glyph::EyeOff: {
        QPainterPath lid;
        lid.moveTo(1.8, 8);
        lid.quadTo(8, 1.8, 14.2, 8);
        lid.quadTo(8, 14.2, 1.8, 8);
        p.drawPath(lid);
        p.setBrush(color);
        p.drawEllipse(QPointF(8, 8), 1.9, 1.9);
        if (glyph == Glyph::EyeOff)
            p.drawLine(QPointF(3, 13), QPointF(13, 3));
        break;
    }
    case Glyph::Lock:
        p.drawArc(QRectF(5, 2.5, 6, 7), 0, 180 * 16);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(3.8, 7, 8.4, 6.5), 1.2, 1.2);
        break;
    case Glyph::Play:
        p.setBrush(color);
        p.drawPolygon(QPolygonF({QPointF(4.5, 3), QPointF(12.5, 8), QPointF(4.5, 13)}));
        break;
    case Glyph::Camera:
        p.setBrush(color);
        p.drawRoundedRect(QRectF(2, 4.5, 12, 8.5), 1.5, 1.5);
        p.drawRect(QRectF(5.5, 3, 5, 2));
        p.setBrush(QColor(Dock));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(8, 8.8), 2.4, 2.4);
        break;
    case Glyph::Speaker:
        p.setBrush(color);
        p.drawPolygon(QPolygonF({QPointF(2.5, 6.3), QPointF(5, 6.3), QPointF(8.2, 3.5),
                                 QPointF(8.2, 12.5), QPointF(5, 9.7), QPointF(2.5, 9.7)}));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(6.5, 4.5, 6, 7), -50 * 16, 100 * 16);
        break;
    case Glyph::Monitor:
        p.drawArc(QRectF(3, 3, 10, 11), 0, 180 * 16);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(2.5, 8.5, 2.6, 4.5), 1, 1);
        p.drawRoundedRect(QRectF(10.9, 8.5, 2.6, 4.5), 1, 1);
        break;
    case Glyph::Dots:
        p.setBrush(color);
        for (qreal y : {3.5, 8.0, 12.5})
            p.drawEllipse(QPointF(8, y), 0.9, 0.9);
        break;
    case Glyph::Float:
        p.drawRect(QRectF(5.5, 2.5, 8, 6.5));
        p.setBrush(QColor(Header));
        p.drawRect(QRectF(2.5, 6.5, 8, 6.5));
        break;
    case Glyph::Bars:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int i = 0; i < 4; ++i)
            p.drawRect(QRectF(2.5 + i * 3, 11 - i * 2.6, 2, 2.5 + i * 2.6));
        break;
    case Glyph::Layout:
        p.drawRoundedRect(QRectF(2.5, 3.5, 11, 9), 1.5, 1.5);
        p.drawLine(QPointF(2.5, 8), QPointF(13.5, 8));
        break;
    }
    return QIcon(pixmap);
}

/// What a dock drag shows: the place the dock would take, and nothing else.
class OverlayPainter : public QFlexDock::DockOverlayPainter
{
public:
    void paint(QPainter *painter, const QFlexDock::DockOverlayScene &scene,
               const QFlexDock::DockOverlayStyle &) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        if (scene.preview.isValid()) {
            painter->setPen(QPen(QColor(AccentLight), 1));
            painter->setBrush(QColor(110, 128, 190, 150));
            painter->drawRoundedRect(QRectF(scene.preview).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        }
        if (scene.tabIndicator.isValid())
            painter->fillRect(scene.tabIndicator, QColor(AccentLight));
    }
};

} // namespace ObsStyle
