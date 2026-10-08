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
    /// `margin` is clamped for small targets.
    [[nodiscard]] static DropZoneLayout compute(const QRect &target, double edgeFraction,
                                                int margin = 0);

    /// Outline of one area as drawn. Empty for DockArea::None.
    [[nodiscard]] QPolygonF polygon(DockArea area) const;

    /// Area under `pos`, or DockArea::None outside the target or when the
    /// area under the pointer is not part of `enabled`.
    [[nodiscard]] DockArea hitTest(const QPoint &pos, DockAreas enabled = AllDockAreas) const;
};

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
