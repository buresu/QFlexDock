// SPDX-License-Identifier: MIT
#include "widgets/DockTabBar.h"

#include <QtGui/QContextMenuEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleOptionTab>
#include <QtWidgets/QStylePainter>
#include <QtWidgets/QToolButton>

namespace QFlexDock {

DockTabBar::DockTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setDocumentMode(true);
    setExpanding(false);
    setUsesScrollButtons(true); // overflow: scroll, plus the group's tab list menu
    setElideMode(Qt::ElideRight);
    setMovable(false);
    setDrawBase(false);
    setTabsClosable(true);
}

int DockTabBar::indexOfPanel(const PanelId &panel) const
{
    for (int i = 0; i < count(); ++i) {
        if (panelAt(i) == panel)
            return i;
    }
    return -1;
}

void DockTabBar::setItalicPanels(const QSet<PanelId> &panels)
{
    if (m_italic == panels)
        return;
    m_italic = panels;
    update();
}

void DockTabBar::setActiveGroup(bool active)
{
    if (m_activeGroup == active)
        return;
    m_activeGroup = active;
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void DockTabBar::setActiveIndicatorColor(const QColor &color)
{
    m_indicatorColor = color;
    update();
}

QRect DockTabBar::tabDropRegion() const
{
    if (count() == 0)
        return {};
    const QRect last = tabRect(count() - 1);
    QRect region = tabRect(0).united(last);
    region.setRight(region.right() + qMin(last.width() / 2, 48));
    region.setTop(0);
    region.setBottom(height() - 1);
    return region.intersected(rect());
}

int DockTabBar::insertIndexAt(const QPoint &pos) const
{
    for (int i = 0; i < count(); ++i) {
        if (pos.x() < tabRect(i).center().x())
            return i;
    }
    return count();
}

QRect DockTabBar::insertIndicatorRect(int index) const
{
    const int thickness = 3;
    int x = 0;
    if (count() > 0) {
        x = index < count() ? tabRect(index).left() : tabRect(count() - 1).right() + 1;
    }
    x = qBound(0, x - thickness / 2, qMax(0, width() - thickness));
    return QRect(x, 0, thickness, height());
}

// Same result as QTabBar's own painting, done here so that single tabs can be
// drawn with a different font (preview tabs) and the active group marked.
void DockTabBar::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);

    // Keep tabs from showing through the scroll buttons.
    QRegion clip(rect());
    const auto buttons = findChildren<QToolButton *>(Qt::FindDirectChildrenOnly);
    for (const QToolButton *button : buttons) {
        if (button->isVisible() && button->autoRepeat()) // the two scroll arrows
            clip -= button->geometry();
    }
    painter.setClipRegion(clip);

    const auto drawTab = [&](int index) {
        QStyleOptionTab option;
        initStyleOption(&option, index);
        if (!option.rect.intersects(rect()))
            return;
        QFont tabFont = font();
        tabFont.setItalic(tabFont.italic() || m_italic.contains(panelAt(index)));
        painter.setFont(tabFont);
        painter.drawControl(QStyle::CE_TabBarTab, option);
    };

    const int current = currentIndex();
    for (int i = 0; i < count(); ++i) {
        if (i != current)
            drawTab(i);
    }
    if (current >= 0) {
        drawTab(current);
        const QColor mark = m_indicatorColor.isValid()
            ? m_indicatorColor : palette().color(QPalette::Active, QPalette::Highlight);
        if (m_activeGroup && mark.alpha() > 0) {
            const QRect r = tabRect(current);
            painter.fillRect(QRect(r.left(), r.top(), r.width(), 2), mark);
        }
    }
}

void DockTabBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_pressPos = event->position().toPoint();
        m_pressIndex = tabAt(m_pressPos);
    } else if (event->button() == Qt::MiddleButton) {
        const int index = tabAt(event->position().toPoint());
        if (index >= 0) {
            Q_EMIT panelCloseRequested(panelAt(index));
            return;
        }
    }
    QTabBar::mousePressEvent(event);
}

void DockTabBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && event->buttons().testFlag(Qt::LeftButton)) {
        const QPoint offset = event->position().toPoint() - m_pressPos;
        if (offset.manhattanLength() >= QApplication::startDragDistance()) {
            m_pressed = false; // one drag per press
            if (m_pressIndex >= 0 && m_pressIndex < count())
                Q_EMIT panelDragStarted(panelAt(m_pressIndex));
            else
                Q_EMIT groupDragStarted();
            return;
        }
    }
    QTabBar::mouseMoveEvent(event);
}

void DockTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_pressed = false;
    QTabBar::mouseReleaseEvent(event);
}

void DockTabBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        Q_EMIT barDoubleClicked();
    else
        QTabBar::mouseDoubleClickEvent(event);
}

void DockTabBar::contextMenuEvent(QContextMenuEvent *event)
{
    int index = tabAt(event->pos());
    if (index < 0)
        index = currentIndex();
    if (index >= 0)
        Q_EMIT panelMenuRequested(panelAt(index), event->globalPos());
}

} // namespace QFlexDock
