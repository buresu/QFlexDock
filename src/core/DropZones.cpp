// SPDX-License-Identifier: MIT
#include "core/DropZones.h"

#include <algorithm>
#include <cmath>

namespace QFlexDock {

DropZoneLayout DropZoneLayout::compute(const QRect &target, double edgeFraction, int margin,
                                       int edgeExtent)
{
    DropZoneLayout layout;
    layout.bounds = target;
    layout.visible = target;
    if (!target.isValid())
        return layout;
    margin = std::clamp(margin, 0, std::min(target.width(), target.height()) / 6);
    layout.visible = target.adjusted(margin, margin, -margin, -margin);
    if (!std::isfinite(edgeFraction))
        edgeFraction = 0.28;
    edgeFraction = std::clamp(edgeFraction, 0.1, 0.4);
    int dx = int(std::lround(layout.visible.width() * edgeFraction));
    int dy = int(std::lround(layout.visible.height() * edgeFraction));
    if (edgeExtent >= 0) {
        dx = std::min(edgeExtent, int(layout.visible.width() * 0.4));
        dy = std::min(edgeExtent, int(layout.visible.height() * 0.4));
    }
    layout.center = layout.visible.adjusted(dx, dy, -dx, -dy);
    return layout;
}

QPolygonF DropZoneLayout::polygon(DockArea area) const
{
    // Work on outline coordinates: right/bottom are one past the last pixel.
    const QRectF o(visible);
    const QRectF c(center);
    switch (area) {
    case DockArea::Left:
        return QPolygonF({o.topLeft(), c.topLeft(), c.bottomLeft(), o.bottomLeft()});
    case DockArea::Right:
        return QPolygonF({o.topRight(), o.bottomRight(), c.bottomRight(), c.topRight()});
    case DockArea::Top:
        return QPolygonF({o.topLeft(), o.topRight(), c.topRight(), c.topLeft()});
    case DockArea::Bottom:
        return QPolygonF({o.bottomLeft(), c.bottomLeft(), c.bottomRight(), o.bottomRight()});
    case DockArea::Center:
        return QPolygonF(c);
    case DockArea::None:
        break;
    }
    return {};
}

DockArea DropZoneLayout::hitTest(const QPoint &pos, DockAreas enabled) const
{
    if (!bounds.isValid() || !bounds.contains(pos))
        return DockArea::None;

    DockArea area = DockArea::Center;
    if (!center.contains(pos)) {
        // The trapezoid borders are the lines from the outer to the inner
        // corners. Measured in units of the edge depth, the nearest edge is
        // the trapezoid we are in. In the margin the distances are negative,
        // which extends each trapezoid outwards to the border of the target.
        const double depthX = std::max(1, center.left() - visible.left());
        const double depthY = std::max(1, center.top() - visible.top());
        const double left = (pos.x() - visible.left() + 0.5) / depthX;
        const double right = (visible.right() - pos.x() + 0.5) / depthX;
        const double top = (pos.y() - visible.top() + 0.5) / depthY;
        const double bottom = (visible.bottom() - pos.y() + 0.5) / depthY;
        const double nearest = std::min({left, right, top, bottom});
        if (nearest == left)
            area = DockArea::Left;
        else if (nearest == right)
            area = DockArea::Right;
        else if (nearest == top)
            area = DockArea::Top;
        else
            area = DockArea::Bottom;
    }
    return enabled.testFlag(area) ? area : DockArea::None;
}

DropButtonLayout DropButtonLayout::compute(const QRect &target, const QRect &within, int size,
                                           int gap, int rings)
{
    DropButtonLayout layout;
    if (!target.isValid() || size <= 0)
        return layout;
    layout.size = size;
    layout.gap = std::max(0, gap);
    layout.center = target.center();
    // How far the outermost buttons reach from the middle.
    const int reach = size / 2 + std::max(1, rings) * (size + layout.gap);
    const auto fit = [reach](int value, int low, int high) {
        // Too little room for all of it: the middle is the best there is.
        return high - low < 2 * reach ? (low + high) / 2
                                      : std::clamp(value, low + reach, high - reach);
    };
    if (within.isValid()) {
        layout.center.setX(fit(layout.center.x(), within.left(), within.right()));
        layout.center.setY(fit(layout.center.y(), within.top(), within.bottom()));
    }
    return layout;
}

QRect DropButtonLayout::rect(DockArea area, int ring) const
{
    if (!isValid() || area == DockArea::None || (area == DockArea::Center && ring != 0))
        return {};
    const int step = (std::max(0, ring) + 1) * (size + gap);
    QPoint offset;
    switch (area) {
    case DockArea::Left:
        offset = QPoint(-step, 0);
        break;
    case DockArea::Right:
        offset = QPoint(step, 0);
        break;
    case DockArea::Top:
        offset = QPoint(0, -step);
        break;
    case DockArea::Bottom:
        offset = QPoint(0, step);
        break;
    default:
        break;
    }
    return QRect(center.x() - size / 2, center.y() - size / 2, size, size).translated(offset);
}

DockArea DropButtonLayout::hitTest(const QPoint &pos, DockAreas enabled, int ring) const
{
    const int around = (gap + 1) / 2;
    for (DockArea area : {DockArea::Center, DockArea::Left, DockArea::Right, DockArea::Top,
                          DockArea::Bottom}) {
        if (!enabled.testFlag(area))
            continue;
        const QRect button = rect(area, ring);
        if (button.isValid() && button.adjusted(-around, -around, around, around).contains(pos))
            return area;
    }
    return DockArea::None;
}

bool DropButtonLayout::contains(const QPoint &pos, int rings) const
{
    for (int ring = 0; ring < rings; ++ring) {
        if (hitTest(pos, AllDockAreas, ring) != DockArea::None)
            return true;
    }
    return false;
}

QRect outerButtonRect(const QRect &bounds, DockArea edge, int size, int margin)
{
    if (!bounds.isValid() || size <= 0)
        return {};
    const QPoint middle = bounds.center();
    switch (edge) {
    case DockArea::Left:
        return QRect(bounds.left() + margin, middle.y() - size / 2, size, size);
    case DockArea::Right:
        return QRect(bounds.right() - margin - size + 1, middle.y() - size / 2, size, size);
    case DockArea::Top:
        return QRect(middle.x() - size / 2, bounds.top() + margin, size, size);
    case DockArea::Bottom:
        return QRect(middle.x() - size / 2, bounds.bottom() - margin - size + 1, size, size);
    default:
        break;
    }
    return {};
}

QRect dropPreviewRect(const QRect &target, DockArea area, double fraction)
{
    if (!std::isfinite(fraction))
        fraction = 0.5;
    fraction = std::clamp(fraction, 0.02, 0.98);
    const int w = int(std::lround(target.width() * fraction));
    const int h = int(std::lround(target.height() * fraction));
    switch (area) {
    case DockArea::Left:
        return QRect(target.left(), target.top(), w, target.height());
    case DockArea::Right:
        return QRect(target.right() - w + 1, target.top(), w, target.height());
    case DockArea::Top:
        return QRect(target.left(), target.top(), target.width(), h);
    case DockArea::Bottom:
        return QRect(target.left(), target.bottom() - h + 1, target.width(), h);
    case DockArea::Center:
        return target;
    case DockArea::None:
        break;
    }
    return {};
}

DockArea outerBandAt(const QRect &bounds, const QPoint &pos, int bandWidth)
{
    if (bandWidth <= 0 || !bounds.contains(pos))
        return DockArea::None;
    const int left = pos.x() - bounds.left();
    const int right = bounds.right() - pos.x();
    const int top = pos.y() - bounds.top();
    const int bottom = bounds.bottom() - pos.y();
    const int nearest = std::min({left, right, top, bottom});
    if (nearest >= bandWidth)
        return DockArea::None;
    if (nearest == left)
        return DockArea::Left;
    if (nearest == right)
        return DockArea::Right;
    if (nearest == top)
        return DockArea::Top;
    return DockArea::Bottom;
}

QRect outerBandRect(const QRect &bounds, DockArea edge, int bandWidth)
{
    switch (edge) {
    case DockArea::Left:
        return QRect(bounds.left(), bounds.top(), bandWidth, bounds.height());
    case DockArea::Right:
        return QRect(bounds.right() - bandWidth + 1, bounds.top(), bandWidth, bounds.height());
    case DockArea::Top:
        return QRect(bounds.left(), bounds.top(), bounds.width(), bandWidth);
    case DockArea::Bottom:
        return QRect(bounds.left(), bounds.bottom() - bandWidth + 1, bounds.width(), bandWidth);
    default:
        break;
    }
    return {};
}

} // namespace QFlexDock
