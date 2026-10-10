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
    /// Background of a button of DockGuide::Buttons; invalid: the palette's
    /// Window colour, nearly opaque.
    QColor buttonColor;
    qreal borderWidth = 1.5;
    qreal cornerRadius = 6.0;
    /// Gap between neighbouring areas, in pixels.
    int zoneGap = 6;
    /// Distance the areas keep from the border of the target they lie over.
    int zoneMargin = 6;
    /// What the guide consists of: large areas (the default), the preview
    /// of the drop alone, or small buttons.
    DockGuide guide = DockGuide::Zones;
    /// Side of a button of DockGuide::Buttons. Neighbouring buttons are
    /// `zoneGap` apart, and those at the border of a workspace `zoneMargin`
    /// from it.
    int buttonSize = 36;
    /// What marks the hovered area. False (the default): that area is
    /// highlighted. True: in its place, the rectangle the dropped content
    /// would occupy is shown, in the preview colours. Only for
    /// DockGuide::Zones: Preview shows nothing but that rectangle, and
    /// Buttons shows it along with the hovered button.
    bool showPreview = false;
    /// Depth of the four edge areas as a share of the target (0.1 - 0.4).
    double edgeFraction = 0.28;
    /// Their depth in pixels where that is to be the same on every target,
    /// however large (never more than 0.4 of it); -1: `edgeFraction` of it.
    /// For DockGuide::Zones and Preview.
    int edgeExtent = -1;
    /// Width of the band along the workspace border that docks onto the
    /// workspace as a whole. 0 disables outer docking by drag (also with
    /// DockGuide::Buttons, whose buttons at the border are otherwise as
    /// large as the others).
    int outerBandWidth = 28;

    /// Copy with every invalid colour filled in from `palette`.
    [[nodiscard]] DockOverlayStyle resolved(const QPalette &palette) const;

    friend bool operator==(const DockOverlayStyle &, const DockOverlayStyle &) = default;
};

/// Buttons whose icon can be replaced.
enum class DockIcon {
    Close, Maximize, Restore, Float, Dock, Pin, Unpin, Menu,
    /// The button of a column bar and of what comes out of an iconified
    /// column: a mark pointing left, and one pointing right.
    IconifyLeft, IconifyRight,
};

/// Buttons in the header of a tab group. Float and Close act on the current
/// panel and are only shown for a panel that may be floated or closed.
/// AutoHide puts every panel of the group that allows it into the auto-hide
/// bar of the nearest border, and is only shown in a workspace.
enum class DockTitleButton {
    Menu = 0x1,
    Maximize = 0x2,
    Float = 0x4,
    Close = 0x8,
    AutoHide = 0x10,
};
Q_DECLARE_FLAGS(DockTitleButtons, DockTitleButton)
Q_DECLARE_OPERATORS_FOR_FLAGS(DockTitleButtons)

/// What the tabs of a group do when there is not room for all of them.
enum class DockTabOverflow {
    /// They keep their width, and arrows scroll the bar.
    Scroll,
    /// They get narrower together, their titles cut short. Tabs too narrow
    /// for it lose their close button, except the current one.
    Shrink,
};

/// Dock-specific tokens layered on top of the host's QStyle, QPalette, QFont
/// and style sheet. Everything left at its default follows the host.
struct QFLEXDOCK_EXPORT DockTheme
{
    /// Thickness of split handles; -1 uses the host style's PM_SplitterWidth.
    int splitHandleWidth = -1;
    /// Thickness a split handle is drawn with while the pointer is on it or
    /// it is dragged; -1 leaves it as thick as always. A larger value lets a
    /// thin boundary light up wider than it is, over the edges of the tab
    /// groups next to it.
    int splitHandleHoverWidth = -1;
    /// Size of tab and button icons; -1 uses the host style's metrics.
    int iconSize = -1;
    /// Which buttons the header of a tab group shows.
    DockTitleButtons titleButtons = DockTitleButton::Menu | DockTitleButton::Maximize;
    /// Whether tabs show the icon of their panel beside its title. Without
    /// it, the icon is for where a panel has no title to show: the button of
    /// an iconified column, the title of a floating window.
    bool tabIcons = true;
    /// Width of a tab while there is room for it; -1 makes each as wide as
    /// its icon and title need.
    int tabWidth = -1;
    DockTabOverflow tabOverflow = DockTabOverflow::Scroll;
    /// Floating windows whose frame is drawn by QFlexDock (FloatingFrame
    /// Custom and Minimal). The width of the border around their content;
    /// -1 is 4 pixels. The border is what such a window is resized by: a
    /// thinner one is grabbed a few pixels into the content as well.
    int floatingBorderWidth = -1;
    /// The radius of their corners while they are not maximized; 0 leaves
    /// them square. Used by the windows created from then on.
    int floatingCornerRadius = 0;
    /// Height of the bar above a column, where a workspace has such bars
    /// (DockManager::setColumnDocking()); -1 is 14 pixels.
    int columnBarHeight = -1;
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
        /// The area as drawn; with DockGuide::Buttons, the square of its
        /// button.
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
    /// What the drop is aimed at, all of it whatever part `preview` is: a
    /// tab group, the column beside which a workspace that docks in columns
    /// puts what is dropped on the side of a group, or the whole dock area.
    /// Null when nothing is hovered.
    QRect target;
    /// Insertion marker inside a tab bar; null unless dropping between tabs.
    QRect tabIndicator;
    /// For a drop that makes what is dragged a tab of a group: the header of
    /// that group. Null for any other drop.
    QRect header;
    /// Where tab bars show a drag as it would turn out
    /// (DockManager::setTabDragPreviewEnabled()): the place kept open among
    /// the tabs for what is dragged.
    QRect tabGap;
    /// The drop leaves the dragged panel in the group it comes from: at
    /// another place among its tabs, or where it was.
    bool ownGroup = false;
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

/// The built-in look, for each DockGuide. Zones: translucent areas with a
/// direction glyph, the hovered one highlighted (or replaced by the drop
/// preview, see DockOverlayStyle::showPreview). Preview: the rectangle the
/// drop would take. Buttons: a square for each area, picturing a window with
/// the part the drop would take filled in, and the preview with the hovered
/// one.
class QFLEXDOCK_EXPORT DockDefaultOverlayPainter : public DockOverlayPainter
{
public:
    void paint(QPainter *painter, const DockOverlayScene &scene,
               const DockOverlayStyle &style) override;
};

} // namespace QFlexDock
