// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QRect>
#include <QtGui/QColor>
#include <QtGui/QIcon>
#include <QtGui/QPalette>
#include <QtGui/QPolygonF>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace QFlexDock {

/// Look of the drop overlay. Invalid colours mean "derive from the host
/// palette" (its Highlight colour), so the default follows light/dark themes.
struct QFLEXDOCK_EXPORT DockOverlayStyle
{
    QColor zoneColor;
    QColor zoneBorderColor;
    QColor hoverColor;
    QColor hoverBorderColor;
    QColor previewColor;
    QColor previewBorderColor;
    QColor glyphColor;
    qreal borderWidth = 1.5;
    qreal cornerRadius = 6.0;
    /// Gap between neighbouring areas, in pixels.
    int zoneGap = 6;
    /// Distance the areas keep from the border of the target they lie over.
    int zoneMargin = 6;
    /// What marks the hovered area. False (the default): that area is
    /// highlighted. True: in its place, the rectangle the dropped content
    /// would occupy is shown, in the preview colours.
    bool showPreview = false;
    /// Depth of the four edge areas as a share of the target (0.1 - 0.4).
    double edgeFraction = 0.28;
    /// Width of the band along the workspace border that docks onto the
    /// workspace as a whole. 0 disables outer docking by drag.
    int outerBandWidth = 28;

    /// Copy with every invalid colour filled in from `palette`.
    [[nodiscard]] DockOverlayStyle resolved(const QPalette &palette) const;

    friend bool operator==(const DockOverlayStyle &, const DockOverlayStyle &) = default;
};

/// Buttons whose icon can be replaced.
enum class DockIcon { Close, Maximize, Restore, Float, Dock, Pin, Unpin, Menu };

/// Buttons in the header of a tab group. Float and Close act on the current
/// panel and are only shown for a panel that may be floated or closed.
enum class DockTitleButton {
    Menu = 0x1,
    Maximize = 0x2,
    Float = 0x4,
    Close = 0x8,
};
Q_DECLARE_FLAGS(DockTitleButtons, DockTitleButton)
Q_DECLARE_OPERATORS_FOR_FLAGS(DockTitleButtons)

/// Dock-specific tokens layered on top of the host's QStyle, QPalette, QFont
/// and style sheet. Everything left at its default follows the host.
struct QFLEXDOCK_EXPORT DockTheme
{
    /// Thickness of split handles; -1 uses the host style's PM_SplitterWidth.
    int splitHandleWidth = -1;
    /// Size of tab and button icons; -1 uses the host style's metrics.
    int iconSize = -1;
    /// Which buttons the header of a tab group shows.
    DockTitleButtons titleButtons = DockTitleButton::Menu | DockTitleButton::Maximize;
    DockOverlayStyle overlay;
    /// Replacement icons; anything missing comes from the host style.
    QHash<DockIcon, QIcon> icons;
};

/// What the overlay shows at one moment. Coordinates are local to the overlay.
struct QFLEXDOCK_EXPORT DockOverlayScene
{
    struct Zone
    {
        DockArea area = DockArea::None;
        QPolygonF shape;
        bool hovered = false;
        /// Docks onto the whole workspace rather than onto one tab group.
        bool outer = false;
    };

    QRect bounds;
    QList<Zone> zones;
    /// Where the dropped content would end up; null when nothing is hovered.
    /// Always filled in; whether to show it is the painter's decision (the
    /// built-in one follows DockOverlayStyle::showPreview).
    QRect preview;
    /// Insertion marker inside a tab bar; null unless dropping between tabs.
    QRect tabIndicator;
};

/// Paints the drop overlay. Hit testing is done elsewhere, so a custom painter
/// changes the look only, never the behaviour.
class QFLEXDOCK_EXPORT DockOverlayPainter
{
public:
    virtual ~DockOverlayPainter();
    /// `style` is already resolved against the palette.
    virtual void paint(QPainter *painter, const DockOverlayScene &scene,
                       const DockOverlayStyle &style) = 0;
};

/// The built-in look: translucent areas with a direction glyph, the hovered
/// one highlighted (or replaced by the drop preview, see
/// DockOverlayStyle::showPreview).
class QFLEXDOCK_EXPORT DockDefaultOverlayPainter : public DockOverlayPainter
{
public:
    void paint(QPainter *painter, const DockOverlayScene &scene,
               const DockOverlayStyle &style) override;
};

} // namespace QFlexDock
