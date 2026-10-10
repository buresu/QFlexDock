// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtGui/QPolygonF>

namespace QFlexDock {

/// Geometry of the large five-area drop guide laid over a drop target.
///
/// The target rectangle is cut into a centre rectangle and four trapezoids
/// that reach from the centre to the edges, so the five areas together cover
/// the whole target: there is no dead space and no small icon to aim for.
/// Pure geometry; painting lives in DockOverlayPainter.
struct QFLEXDOCK_EXPORT DropZoneLayout
{
    /// The whole target: every point of it belongs to one of the five areas.
    QRect bounds;
    /// What is drawn: `bounds` shrunk by the margin. The areas keep their
    /// distance from the target's border, but a pointer in that margin still
    /// hits the edge area next to it, so there is no dead space.
    QRect visible;
    QRect center;

    /// `edgeFraction` is the depth of the edge areas as a share of the
    /// (visible) width/height, clamped so the centre keeps a usable size.
    /// `margin` is clamped for small targets. `edgeExtent`, if not negative,
    /// is that depth in pixels instead, at most 0.4 of the width/height.
    [[nodiscard]] static DropZoneLayout compute(const QRect &target, double edgeFraction,
                                                int margin = 0, int edgeExtent = -1);

    /// Outline of one area as drawn. Empty for DockArea::None.
    [[nodiscard]] QPolygonF polygon(DockArea area) const;

    /// Area under `pos`, or DockArea::None outside the target or when the
    /// area under the pointer is not part of `enabled`.
    [[nodiscard]] DockArea hitTest(const QPoint &pos, DockAreas enabled = AllDockAreas) const;
};

/// Geometry of the button guide (DockGuide::Buttons): one square per area, the
/// four edges around the centre like a cross. Only a point on a button is a
/// target. Pure geometry; painting lives in DockOverlayPainter.
///
/// A second ring of edge buttons, one step further out, belongs to whatever
/// lies around the target the cross is for: a workspace inside a panel has
/// the cross for its tab groups, and the workspace around it the ring, for
/// docking beside that panel.
struct QFLEXDOCK_EXPORT DropButtonLayout
{
    /// Middle of the centre button.
    QPoint center;
    /// Side of a button; 0 for no buttons at all.
    int size = 0;
    int gap = 0;

    /// The cross in the middle of `target`, moved as little as it takes to
    /// lie inside `within` with all its `rings` (1: the cross alone).
    [[nodiscard]] static DropButtonLayout compute(const QRect &target, const QRect &within,
                                                  int size, int gap, int rings = 1);

    [[nodiscard]] bool isValid() const { return size > 0; }
    /// The square of one button. `ring` 0 is the cross, 1 the edge buttons
    /// around it; the centre is in the cross only. Null for DockArea::None.
    [[nodiscard]] QRect rect(DockArea area, int ring = 0) const;
    /// The button of ring `ring` at `pos`, if it is one of `enabled`. The
    /// gap around a button counts as the button.
    [[nodiscard]] DockArea hitTest(const QPoint &pos, DockAreas enabled, int ring = 0) const;
    /// Whether `pos` is on any button of the first `rings` rings.
    [[nodiscard]] bool contains(const QPoint &pos, int rings = 1) const;

    friend bool operator==(const DropButtonLayout &, const DropButtonLayout &) = default;
};

/// The button for docking against a whole workspace: a square of `size` in the
/// middle of border `edge` of `bounds`, `margin` away from it.
QFLEXDOCK_EXPORT QRect outerButtonRect(const QRect &bounds, DockArea edge, int size, int margin);

/// Rectangle a drop on `area` of `target` would occupy: the whole target for
/// Center, the `fraction` nearest that edge otherwise.
QFLEXDOCK_EXPORT QRect dropPreviewRect(const QRect &target, DockArea area, double fraction);

/// Edge of `bounds` whose outer band (of `bandWidth` pixels) contains `pos`,
/// or DockArea::None. Used for docking along the outside of a whole workspace
/// as opposed to onto one tab group. In corners the nearer edge wins.
QFLEXDOCK_EXPORT DockArea outerBandAt(const QRect &bounds, const QPoint &pos, int bandWidth);

/// The band itself, for painting.
QFLEXDOCK_EXPORT QRect outerBandRect(const QRect &bounds, DockArea edge, int bandWidth);

} // namespace QFlexDock
