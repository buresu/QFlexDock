// SPDX-License-Identifier: MIT
#include "widgets/DockSplitHandle.h"

#include "widgets/DockAreaWidget.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleOption>

#include <utility>

namespace QFlexDock {

DockSplitHandle::DockSplitHandle(DockAreaWidget *area)
    : QWidget(area)
    , m_area(area)
{
    // Lets style sheets give the handle a background and border.
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::SplitHCursor);
}

void DockSplitHandle::configure(int index, Qt::Orientation orientation, const QRect &bar)
{
    m_index = index;
    if (m_orientation != orientation) {
        m_orientation = orientation;
        restyle();
    }
    setCursor(orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
    place(bar);
}

void DockSplitHandle::place(const QRect &bar)
{
    m_bar = bar;
    const bool horizontal = m_orientation == Qt::Horizontal;
    const int thickness = horizontal ? bar.width() : bar.height();
    const auto widened = [&](int before, int after) {
        return horizontal ? bar.adjusted(-before, 0, after, 0) : bar.adjusted(0, -before, 0, after);
    };
    const int lit = qMax(0, m_area->handleHoverWidth() - thickness);
    m_litBar = widened(lit / 2, lit - lit / 2);
    const int margin = qMax(0, (MinimumGrabExtent - thickness + 1) / 2);
    setGeometry(widened(margin, margin).united(m_litBar));
    updateMask();
}

QRect DockSplitHandle::drawnGeometry() const
{
    return m_hovered || m_pressed ? m_litBar : m_bar;
}

// The widget is larger than what it shows: the margin is there for the mouse
// only. Painting (a styled background included) is confined to the bar, hit
// testing is not.
void DockSplitHandle::updateMask()
{
    const QRect drawn = drawnGeometry();
    setAttribute(Qt::WA_MouseNoMask, m_bar != geometry());
    const QRegion visible = drawn != geometry() ? QRegion(drawn.translated(-pos())) : QRegion();
    if (mask() != visible) {
        if (visible.isEmpty())
            clearMask();
        else
            setMask(visible);
    }
}

void DockSplitHandle::setHovered(bool hovered)
{
    if (m_hovered == hovered)
        return;
    m_hovered = hovered;
    updateMask();
    restyle();
}

// Dynamic-property selectors are only re-evaluated when the widget is
// re-polished.
void DockSplitHandle::restyle()
{
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void DockSplitHandle::paintEvent(QPaintEvent *)
{
    // A style sheet that styles the handle has painted it already (as its
    // styled background); the style's own grip would only be drawn over that.
    if (testAttribute(Qt::WA_StyleSheetTarget))
        return;
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    option.rect = drawnGeometry().translated(-pos());
    option.state.setFlag(QStyle::State_Horizontal, m_orientation == Qt::Horizontal);
    option.state.setFlag(QStyle::State_MouseOver, m_hovered || m_pressed);
    option.state.setFlag(QStyle::State_Sunken, m_pressed);
    style()->drawControl(QStyle::CE_Splitter, &option, &painter, this);
}

void DockSplitHandle::enterEvent(QEnterEvent *)
{
    if (!m_pressed)
        m_area->setHandleHover(m_index, true);
}

void DockSplitHandle::leaveEvent(QEvent *)
{
    if (!m_pressed)
        m_area->setHandleHover(m_index, false);
}

void DockSplitHandle::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    m_pressPos = event->globalPosition().toPoint();
    // Alt drags this handle alone, whatever the linking setting.
    m_area->beginHandleDrag(m_index, !event->modifiers().testFlag(Qt::AltModifier));
    grabKeyboard(); // for Escape
    updateMask();
    restyle();
}

void DockSplitHandle::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed)
        return;
    const QPoint offset = event->globalPosition().toPoint() - m_pressPos;
    m_area->moveHandleDrag(m_orientation == Qt::Horizontal ? offset.x() : offset.y());
}

void DockSplitHandle::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressed)
        finish(false);
}

void DockSplitHandle::keyPressEvent(QKeyEvent *event)
{
    if (m_pressed && event->key() == Qt::Key_Escape)
        finish(true);
    else
        QWidget::keyPressEvent(event);
}

void DockSplitHandle::finish(bool cancel)
{
    m_pressed = false;
    releaseKeyboard();
    m_area->endHandleDrag(cancel);
    m_area->setHandleHover(m_index, underMouse());
    updateMask();
    restyle();
}

// --- DockEdgeHandle ----------------------------------------------------------

DockEdgeHandle::DockEdgeHandle(DockAreaWidget *area)
    : QWidget(area)
    , m_area(area)
{
    setFocusPolicy(Qt::NoFocus);
}

// `bar` made `thickness` thick, growing away from the edge it lies along.
QRect DockEdgeHandle::inwards(const QRect &bar, int thickness) const
{
    switch (m_side) {
    case DockArea::Left:
        return QRect(bar.left(), bar.top(), thickness, bar.height());
    case DockArea::Right:
        return QRect(bar.right() - thickness + 1, bar.top(), thickness, bar.height());
    case DockArea::Top:
        return QRect(bar.left(), bar.top(), bar.width(), thickness);
    default:
        return QRect(bar.left(), bar.bottom() - thickness + 1, bar.width(), thickness);
    }
}

void DockEdgeHandle::configure(const QStringList &panels, DockArea side, const QRect &bar)
{
    m_panels = panels;
    if (m_side != side) {
        m_side = side;
        restyle();
    }
    m_bar = bar;
    setCursor(splitOrientation(side) == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
    setGeometry(inwards(bar, GrabExtent));
}

QString DockEdgeHandle::edgeName() const
{
    switch (m_side) {
    case DockArea::Left:
        return QStringLiteral("left");
    case DockArea::Right:
        return QStringLiteral("right");
    case DockArea::Top:
        return QStringLiteral("top");
    default:
        return QStringLiteral("bottom");
    }
}

void DockEdgeHandle::restyle()
{
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void DockEdgeHandle::paintEvent(QPaintEvent *)
{
    if (!m_hovered && !m_pressed)
        return;
    const bool upright = splitOrientation(m_side) == Qt::Horizontal;
    const int thickness = qMax(upright ? m_bar.width() : m_bar.height(), m_area->handleHoverWidth());
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    const QRect lit = inwards(m_bar, thickness).translated(-pos());
    if (testAttribute(Qt::WA_StyleSheetTarget)) {
        // The style sheet's background, where the bar is.
        painter.setClipRect(lit);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        return;
    }
    option.rect = lit;
    option.state.setFlag(QStyle::State_Horizontal, upright);
    option.state.setFlag(QStyle::State_MouseOver, true);
    option.state.setFlag(QStyle::State_Sunken, m_pressed);
    style()->drawControl(QStyle::CE_Splitter, &option, &painter, this);
}

void DockEdgeHandle::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    restyle();
}

void DockEdgeHandle::leaveEvent(QEvent *)
{
    m_hovered = false;
    restyle();
}

void DockEdgeHandle::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    m_pulled = false;
    m_pressPos = event->globalPosition().toPoint();
    grabKeyboard(); // for Escape
    restyle();
}

void DockEdgeHandle::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed)
        return;
    const QPoint offset = event->globalPosition().toPoint() - m_pressPos;
    int inward = 0;
    switch (m_side) {
    case DockArea::Left:
        inward = offset.x();
        break;
    case DockArea::Right:
        inward = -offset.x();
        break;
    case DockArea::Top:
        inward = offset.y();
        break;
    default:
        inward = -offset.y();
        break;
    }
    // Out they come with the first move inwards. How much of them shows is
    // then a matter of the handle beside them, like any other drag of it.
    if (!m_pulled && inward >= QApplication::startDragDistance())
        m_pulled = m_area->beginEdgeReopen(m_panels, m_side);
    if (m_pulled)
        m_area->moveEdgeReopen(inward);
}

void DockEdgeHandle::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressed)
        finish(false);
}

void DockEdgeHandle::keyPressEvent(QKeyEvent *event)
{
    if (m_pressed && event->key() == Qt::Key_Escape)
        finish(true);
    else
        QWidget::keyPressEvent(event);
}

void DockEdgeHandle::finish(bool cancel)
{
    m_pressed = false;
    releaseKeyboard();
    if (std::exchange(m_pulled, false))
        m_area->endHandleDrag(cancel);
    m_hovered = underMouse();
    restyle();
    // Whether there is still an edge to pull at is the area's to say.
    m_area->relayout();
}

// --- DockSplitCorner ---------------------------------------------------------

DockSplitCorner::DockSplitCorner(DockAreaWidget *area)
    : QWidget(area)
    , m_area(area)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::SizeAllCursor);
}

void DockSplitCorner::configure(int index, const QRect &patch)
{
    m_index = index;
    m_patch = patch;
    const int horizontal = qMax(2, (MinimumGrabExtent - patch.width() + 1) / 2);
    const int vertical = qMax(2, (MinimumGrabExtent - patch.height() + 1) / 2);
    setGeometry(patch.adjusted(-horizontal, -vertical, horizontal, vertical));
}

void DockSplitCorner::enterEvent(QEnterEvent *)
{
    if (!m_pressed)
        m_area->setCornerHover(m_index, true);
}

void DockSplitCorner::leaveEvent(QEvent *)
{
    if (!m_pressed)
        m_area->setCornerHover(m_index, false);
}

void DockSplitCorner::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    m_pressPos = event->globalPosition().toPoint();
    // Alt moves just the bars that meet here, without the ones in line with them.
    m_area->beginCornerDrag(m_index, !event->modifiers().testFlag(Qt::AltModifier));
    grabKeyboard(); // for Escape
}

void DockSplitCorner::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed)
        m_area->moveCornerDrag(event->globalPosition().toPoint() - m_pressPos);
}

void DockSplitCorner::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressed)
        finish(false);
}

void DockSplitCorner::keyPressEvent(QKeyEvent *event)
{
    if (m_pressed && event->key() == Qt::Key_Escape)
        finish(true);
    else
        QWidget::keyPressEvent(event);
}

void DockSplitCorner::finish(bool cancel)
{
    m_pressed = false;
    releaseKeyboard();
    m_area->endHandleDrag(cancel);
    m_area->setCornerHover(m_index, underMouse());
}

} // namespace QFlexDock
