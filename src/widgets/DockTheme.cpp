// SPDX-License-Identifier: MIT
#include <QFlexDock/DockTheme.h>

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>

namespace QFlexDock {

namespace {

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

// Moves every edge of a convex polygon inwards by `distance`.
QPolygonF insetConvex(const QPolygonF &polygon, qreal distance)
{
    const qsizetype n = polygon.size();
    if (n < 3 || distance <= 0)
        return polygon;

    qreal twiceArea = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF a = polygon[i];
        const QPointF b = polygon[(i + 1) % n];
        twiceArea += a.x() * b.y() - b.x() * a.y();
    }
    const qreal sign = twiceArea >= 0 ? 1.0 : -1.0;

    struct Line
    {
        QPointF point;
        QPointF direction;
    };
    QList<Line> lines;
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF a = polygon[i];
        const QPointF b = polygon[(i + 1) % n];
        const qreal length = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (length < 1e-6)
            continue;
        const QPointF direction = (b - a) / length;
        const QPointF inward(-direction.y() * sign, direction.x() * sign);
        lines.append({a + inward * distance, direction});
    }

    QPolygonF result;
    const qsizetype m = lines.size();
    for (qsizetype i = 0; i < m; ++i) {
        const Line &p = lines[(i + m - 1) % m];
        const Line &c = lines[i];
        const qreal cross = p.direction.x() * c.direction.y() - p.direction.y() * c.direction.x();
        if (std::abs(cross) < 1e-9) {
            result << c.point;
            continue;
        }
        const QPointF delta = c.point - p.point;
        const qreal t = (delta.x() * c.direction.y() - delta.y() * c.direction.x()) / cross;
        result << p.point + p.direction * t;
    }
    // Inset too far for the shape: nothing sensible is left.
    if (!polygon.boundingRect().adjusted(-1, -1, 1, 1).contains(result.boundingRect()))
        return {};
    qreal resultArea = 0;
    for (qsizetype i = 0; i < result.size(); ++i) {
        const QPointF a = result[i];
        const QPointF b = result[(i + 1) % result.size()];
        resultArea += a.x() * b.y() - b.x() * a.y();
    }
    if (resultArea * sign <= 0)
        return {};
    return result;
}

// A polygon with rounded corners. Each corner is rounded over the same
// distance along its two edges (at most `radius`), rather than with the same
// radius: a circle of fixed radius would cut a sharp corner far back and turn
// the pointed end of a trapezoid into a blob.
QPainterPath roundedPolygon(const QPolygonF &polygon, qreal radius)
{
    QPainterPath path;
    const qsizetype n = polygon.size();
    if (n < 3 || radius <= 0.5) {
        path.addPolygon(polygon);
        path.closeSubpath();
        return path;
    }
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF corner = polygon[i];
        const QPointF toPrevious = polygon[(i + n - 1) % n] - corner;
        const QPointF toNext = polygon[(i + 1) % n] - corner;
        const qreal previousLength = std::hypot(toPrevious.x(), toPrevious.y());
        const qreal nextLength = std::hypot(toNext.x(), toNext.y());
        if (previousLength < 1e-6 || nextLength < 1e-6) {
            i == 0 ? path.moveTo(corner) : path.lineTo(corner);
            continue;
        }
        const qreal cosine = std::clamp((toPrevious.x() * toNext.x() + toPrevious.y() * toNext.y())
                                            / (previousLength * nextLength), -1.0, 1.0);
        const qreal tanHalf = std::tan(std::acos(cosine) / 2);
        // Where a circle of `radius` would touch the edges, but never further
        // from the corner than `radius` itself (sharp corners), and never
        // more than a good third of an edge.
        qreal distance = tanHalf > 1e-6 ? std::min(radius, radius / tanHalf) : radius;
        distance = std::min({distance, previousLength * 0.35, nextLength * 0.35});
        const QPointF start = corner + toPrevious / previousLength * distance;
        const QPointF end = corner + toNext / nextLength * distance;
        i == 0 ? path.moveTo(start) : path.lineTo(start);
        path.quadTo(corner, end);
    }
    path.closeSubpath();
    return path;
}

QPointF centroid(const QPolygonF &polygon)
{
    QPointF sum;
    for (const QPointF &point : polygon)
        sum += point;
    return polygon.isEmpty() ? sum : sum / qreal(polygon.size());
}

void drawGlyph(QPainter *painter, DockArea area, const QPointF &center, qreal size)
{
    const qreal h = size / 2;
    if (area == DockArea::Center) {
        // A little window with a tab strip.
        const QRectF frame(center.x() - h, center.y() - h * 0.8, size, size * 0.8);
        QPainterPath path;
        path.addRoundedRect(frame, 2, 2);
        painter->strokePath(path, painter->pen());
        painter->fillRect(QRectF(frame.left(), frame.top(), frame.width(), frame.height() * 0.28),
                          painter->pen().color());
        return;
    }
    QPolygonF arrow;
    switch (area) {
    case DockArea::Left:
        arrow << QPointF(-h * 0.6, 0) << QPointF(h * 0.4, -h * 0.8) << QPointF(h * 0.4, h * 0.8);
        break;
    case DockArea::Right:
        arrow << QPointF(h * 0.6, 0) << QPointF(-h * 0.4, -h * 0.8) << QPointF(-h * 0.4, h * 0.8);
        break;
    case DockArea::Top:
        arrow << QPointF(0, -h * 0.6) << QPointF(-h * 0.8, h * 0.4) << QPointF(h * 0.8, h * 0.4);
        break;
    case DockArea::Bottom:
        arrow << QPointF(0, h * 0.6) << QPointF(-h * 0.8, -h * 0.4) << QPointF(h * 0.8, -h * 0.4);
        break;
    default:
        return;
    }
    arrow.translate(center);
    QPainterPath path;
    path.addPolygon(arrow);
    path.closeSubpath();
    painter->fillPath(path, painter->pen().color());
}

// The rectangle the drop would take.
void drawPreview(QPainter *painter, const DockOverlayScene &scene, const DockOverlayStyle &style)
{
    // A border lies inside the rectangle; without one it is filled to its edge.
    const qreal inset = style.borderWidth > 0 ? style.borderWidth / 2 + 1 : 0;
    QPainterPath path;
    path.addRoundedRect(QRectF(scene.preview).adjusted(inset, inset, -inset, -inset),
                        style.cornerRadius, style.cornerRadius);
    painter->fillPath(path, style.previewColor);
    if (style.borderWidth > 0)
        painter->strokePath(path, QPen(style.previewBorderColor, style.borderWidth));
}

// What a button stands for: a window, with the part the drop would take
// filled in. A button at the border of a workspace has a smaller window and
// an arrow towards that border.
void drawButtonPicture(QPainter *painter, const DockOverlayScene::Zone &zone, const QRectF &button,
                       const DockOverlayStyle &style)
{
    const qreal unit = button.width() / 16.0;
    QRectF window = button.adjusted(3.5 * unit, 3.5 * unit, -3.5 * unit, -3.5 * unit);
    QColor line = style.glyphColor;
    QColor fill = style.hoverBorderColor;
    if (!zone.hovered) {
        line.setAlphaF(line.alphaF() * 0.8f);
        fill.setAlphaF(fill.alphaF() * 0.75f);
    }

    if (zone.outer) {
        // Room for the arrow on the side of the border.
        const qreal shift = 1.6 * unit;
        const qreal half = 2.2 * unit;
        QPolygonF arrow;
        switch (zone.area) {
        case DockArea::Left:
            window.translate(shift, 0);
            arrow << QPointF(window.left() - 3.4 * unit, window.center().y())
                  << QPointF(window.left() - 1.4 * unit, window.center().y() - half)
                  << QPointF(window.left() - 1.4 * unit, window.center().y() + half);
            break;
        case DockArea::Right:
            window.translate(-shift, 0);
            arrow << QPointF(window.right() + 3.4 * unit, window.center().y())
                  << QPointF(window.right() + 1.4 * unit, window.center().y() - half)
                  << QPointF(window.right() + 1.4 * unit, window.center().y() + half);
            break;
        case DockArea::Top:
            window.translate(0, shift);
            arrow << QPointF(window.center().x(), window.top() - 3.4 * unit)
                  << QPointF(window.center().x() - half, window.top() - 1.4 * unit)
                  << QPointF(window.center().x() + half, window.top() - 1.4 * unit);
            break;
        default:
            window.translate(0, -shift);
            arrow << QPointF(window.center().x(), window.bottom() + 3.4 * unit)
                  << QPointF(window.center().x() - half, window.bottom() + 1.4 * unit)
                  << QPointF(window.center().x() + half, window.bottom() + 1.4 * unit);
            break;
        }
        QPainterPath path;
        path.addPolygon(arrow);
        path.closeSubpath();
        painter->fillPath(path, line);
    }

    QRectF part = window;
    switch (zone.area) {
    case DockArea::Left:
        part.setWidth(window.width() / 2);
        break;
    case DockArea::Right:
        part.setLeft(window.center().x());
        break;
    case DockArea::Top:
        part.setHeight(window.height() / 2);
        break;
    case DockArea::Bottom:
        part.setTop(window.center().y());
        break;
    default:
        break;
    }
    painter->fillRect(part, fill);
    const qreal width = std::max<qreal>(1.0, unit);
    painter->setPen(QPen(line, width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(window);
    // The title of the window, or of the part: a thicker line along its top.
    const QRectF titled = zone.area == DockArea::Center ? window : part;
    painter->fillRect(QRectF(titled.left(), titled.top(), titled.width(), 2 * unit), line);
}

void drawButtons(QPainter *painter, const DockOverlayScene &scene, const DockOverlayStyle &style)
{
    if (scene.preview.isValid())
        drawPreview(painter, scene, style);
    for (const DockOverlayScene::Zone &zone : scene.zones) {
        const QRectF button = zone.shape.boundingRect();
        if (button.isEmpty())
            continue;
        const qreal inset = style.borderWidth / 2;
        const qreal radius = std::min(style.cornerRadius, button.width() / 4);
        QPainterPath path;
        path.addRoundedRect(button.adjusted(inset, inset, -inset, -inset), radius, radius);
        painter->fillPath(path, style.buttonColor);
        if (zone.hovered)
            painter->fillPath(path, style.hoverColor);
        if (style.borderWidth > 0) {
            painter->strokePath(path, QPen(zone.hovered ? style.hoverBorderColor
                                                        : style.zoneBorderColor,
                                           style.borderWidth));
        }
        painter->save();
        drawButtonPicture(painter, zone, button, style);
        painter->restore();
    }
}

} // namespace

DockOverlayStyle DockOverlayStyle::resolved(const QPalette &palette) const
{
    DockOverlayStyle s = *this;
    const QColor accent = palette.color(QPalette::Active, QPalette::Highlight);
    if (!s.zoneColor.isValid())
        s.zoneColor = withAlpha(accent, 34);
    if (!s.zoneBorderColor.isValid())
        s.zoneBorderColor = withAlpha(accent, 150);
    if (!s.hoverColor.isValid())
        s.hoverColor = withAlpha(accent, 115);
    if (!s.hoverBorderColor.isValid())
        s.hoverBorderColor = accent;
    if (!s.previewColor.isValid())
        s.previewColor = withAlpha(accent, 64);
    if (!s.previewBorderColor.isValid())
        s.previewBorderColor = withAlpha(accent, 210);
    if (!s.glyphColor.isValid())
        s.glyphColor = withAlpha(palette.color(QPalette::Active, QPalette::WindowText), 190);
    if (!s.buttonColor.isValid())
        s.buttonColor = withAlpha(palette.color(QPalette::Active, QPalette::Window), 235);
    s.buttonSize = std::clamp(s.buttonSize, 12, 96);
    s.borderWidth = std::max<qreal>(0.0, s.borderWidth);
    s.cornerRadius = std::max<qreal>(0.0, s.cornerRadius);
    s.zoneGap = std::max(0, s.zoneGap);
    s.zoneMargin = std::max(0, s.zoneMargin);
    return s;
}

DockOverlayPainter::~DockOverlayPainter() = default;

void DockDefaultOverlayPainter::paint(QPainter *painter, const DockOverlayScene &scene,
                                      const DockOverlayStyle &style)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (style.guide != DockGuide::Zones) {
        if (style.guide == DockGuide::Buttons)
            drawButtons(painter, scene, style);
        else if (scene.preview.isValid())
            drawPreview(painter, scene, style);
        if (scene.tabIndicator.isValid())
            painter->fillRect(scene.tabIndicator, style.hoverBorderColor);
        painter->restore();
        return;
    }

    // The hovered area is marked either by highlighting it or, if the style
    // asks for the preview, by the rectangle the drop would occupy instead.
    // Never both: they cover much the same place.
    const bool previewShown = style.showPreview && scene.preview.isValid();
    if (previewShown) {
        drawPreview(painter, scene, style);
        // The other areas show only where the preview leaves them visible.
        painter->setClipRegion(QRegion(scene.bounds).subtracted(scene.preview));
    }

    for (const DockOverlayScene::Zone &zone : scene.zones) {
        if (previewShown && zone.hovered)
            continue;
        const QPolygonF shape = insetConvex(zone.shape, style.zoneGap / 2.0);
        if (shape.isEmpty())
            continue;
        const QRectF box = shape.boundingRect();
        const qreal radius = std::min({style.cornerRadius, box.width() / 4, box.height() / 4});
        const QPainterPath path = roundedPolygon(shape, radius);
        painter->fillPath(path, zone.hovered ? style.hoverColor : style.zoneColor);
        if (style.borderWidth > 0) {
            painter->strokePath(path, QPen(zone.hovered ? style.hoverBorderColor
                                                        : style.zoneBorderColor,
                                           style.borderWidth));
        }

        const qreal glyph = std::min({box.width(), box.height(), 44.0}) * (zone.outer ? 0.55 : 0.5);
        if (glyph >= 6) {
            QColor color = style.glyphColor;
            if (!zone.hovered)
                color.setAlphaF(color.alphaF() * 0.6f);
            painter->setPen(QPen(color, 1.5));
            drawGlyph(painter, zone.area, centroid(shape), glyph);
        }
    }
    painter->setClipping(false);

    if (scene.tabIndicator.isValid())
        painter->fillRect(scene.tabIndicator, style.hoverBorderColor);

    painter->restore();
}

} // namespace QFlexDock
