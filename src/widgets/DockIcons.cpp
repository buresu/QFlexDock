// SPDX-License-Identifier: MIT
#include "widgets/DockIcons.h"

#include <QtGui/QIconEngine>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>

namespace QFlexDock {

namespace {

class GlyphIconEngine : public QIconEngine
{
public:
    GlyphIconEngine(DockIcon which, const QColor &color)
        : m_which(which)
        , m_color(color)
    {
    }

    QIconEngine *clone() const override { return new GlyphIconEngine(m_which, m_color); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        QColor color = m_color;
        if (mode == QIcon::Disabled)
            color.setAlphaF(color.alphaF() * 0.4f);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        // Draw in a 16x16 design grid, centred in `rect`.
        const qreal side = qMin(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2.0,
                           rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 16.0, side / 16.0);
        painter->setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);

        switch (m_which) {
        case DockIcon::Close:
            painter->drawLine(QPointF(4, 4), QPointF(12, 12));
            painter->drawLine(QPointF(12, 4), QPointF(4, 12));
            break;
        case DockIcon::Maximize:
            painter->drawRect(QRectF(3.5, 3.5, 9, 9));
            break;
        case DockIcon::Restore:
            painter->drawRect(QRectF(3.5, 5.5, 7, 7));
            painter->drawPolyline(QPolygonF({QPointF(5.5, 5.5), QPointF(5.5, 3.5),
                                             QPointF(12.5, 3.5), QPointF(12.5, 10.5),
                                             QPointF(10.5, 10.5)}));
            break;
        case DockIcon::Float:
            // A window lifted off: frame with an arrow leaving its corner.
            painter->drawRect(QRectF(3.5, 6.5, 6, 6));
            painter->drawLine(QPointF(8, 8), QPointF(12.5, 3.5));
            painter->drawPolyline(QPolygonF({QPointF(9, 3.5), QPointF(12.5, 3.5),
                                             QPointF(12.5, 7)}));
            break;
        case DockIcon::Dock:
            // The reverse: an arrow going back into the frame.
            painter->drawRect(QRectF(3.5, 6.5, 6, 6));
            painter->drawLine(QPointF(12.5, 3.5), QPointF(8, 8));
            painter->drawPolyline(QPolygonF({QPointF(8, 4.5), QPointF(8, 8), QPointF(11.5, 8)}));
            break;
        case DockIcon::Pin:
        case DockIcon::Unpin: {
            // A pushpin; lying on its side when unpinned.
            if (m_which == DockIcon::Unpin) {
                painter->translate(8, 8);
                painter->rotate(-90);
                painter->translate(-8, -8);
            }
            painter->drawLine(QPointF(5, 3.5), QPointF(11, 3.5));
            painter->drawRect(QRectF(6, 3.5, 4, 5));
            painter->drawLine(QPointF(4, 8.5), QPointF(12, 8.5));
            painter->drawLine(QPointF(8, 8.5), QPointF(8, 13));
            break;
        }
        case DockIcon::IconifyLeft:
        case DockIcon::IconifyRight: {
            // Two chevrons, one behind the other.
            const qreal tip = m_which == DockIcon::IconifyLeft ? -3.0 : 3.0;
            for (const qreal x : {5.0, 9.0}) {
                const qreal back = m_which == DockIcon::IconifyLeft ? x + 3 : x - 1;
                painter->drawPolyline(QPolygonF({QPointF(back, 4.5), QPointF(back + tip, 8),
                                                 QPointF(back, 11.5)}));
            }
            break;
        }
        case DockIcon::Menu: {
            QPainterPath chevron;
            chevron.moveTo(4.5, 6.5);
            chevron.lineTo(8, 10);
            chevron.lineTo(11.5, 6.5);
            painter->drawPath(chevron);
            break;
        }
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

private:
    DockIcon m_which;
    QColor m_color;
};

} // namespace

QIcon makeGlyphIcon(DockIcon which, const QColor &color)
{
    return QIcon(new GlyphIconEngine(which, color));
}

} // namespace QFlexDock
