// SPDX-License-Identifier: MIT
#include "widgets/DockSplitHandle.h"

#include "widgets/DockAreaWidget.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleOption>

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

    m_bar = bar;
    const bool horizontal = orientation == Qt::Horizontal;
    const int thickness = horizontal ? bar.width() : bar.height();
    const int margin = qMax(0, (MinimumGrabExtent - thickness + 1) / 2);
    setGeometry(horizontal ? bar.adjusted(-margin, 0, margin, 0)
                           : bar.adjusted(0, -margin, 0, margin));
    // The margin is there for the mouse only: painting (a styled background
    // included) is confined to the bar, hit testing is not.
    setAttribute(Qt::WA_MouseNoMask, margin > 0);
    const QRegion visible = margin > 0 ? QRegion(bar.translated(-pos())) : QRegion();
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
    option.rect = m_bar.translated(-pos());
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
    restyle();
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
