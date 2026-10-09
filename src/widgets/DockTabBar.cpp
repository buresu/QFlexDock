// SPDX-License-Identifier: MIT
#include "widgets/DockTabBar.h"

#include <QtGui/QContextMenuEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleOptionTab>
#include <QtWidgets/QStylePainter>
#include <QtWidgets/QToolButton>

#include <utility>

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
    connect(this, &QTabBar::currentChanged, this, &DockTabBar::updateTabButtons);
}

// --- Sizes ---------------------------------------------------------------------

void DockTabBar::setTabSizing(int tabWidth, DockTabOverflow overflow)
{
    const bool shrink = overflow == DockTabOverflow::Shrink;
    if (m_tabWidth == tabWidth && m_shrink == shrink)
        return;
    m_tabWidth = tabWidth;
    m_shrink = shrink;
    // Shrinking is what QTabBar does with its tabs when it may not scroll.
    setUsesScrollButtons(!shrink);
    setElideMode(elideMode()); // has the tabs measured again
    updateGeometry();
    updateTabButtons();
}

QSize DockTabBar::tabSizeHint(int index) const
{
    QSize size = QTabBar::tabSizeHint(index);
    if (m_measuringMinimum)
        return size;
    if (m_tabWidth > 0)
        size.setWidth(m_tabWidth);
    else if (isGap(index) && m_gapWidth > 0)
        size.setWidth(m_gapWidth);
    return size;
}

int DockTabBar::gapIndex() const
{
    for (int i = 0; i < count(); ++i) {
        if (isGap(i))
            return i;
    }
    return -1;
}

void DockTabBar::setGapWidth(int width)
{
    m_gapWidth = width;
}

// What a tab needs to show its icon, the start of its title and its button.
QSize DockTabBar::unsqueezedMinimum(int index) const
{
    m_measuringMinimum = true;
    QSize size = QTabBar::minimumTabSizeHint(index);
    m_measuringMinimum = false;
    if (m_tabWidth > 0)
        size.setWidth(qMin(size.width(), m_tabWidth));
    return size;
}

QSize DockTabBar::minimumTabSizeHint(int index) const
{
    if (!m_shrink)
        return QTabBar::minimumTabSizeHint(index);
    // Whatever it takes for all of them to fit.
    QSize size = unsqueezedMinimum(index);
    size.setWidth(qMin(size.width(), qMax(1, width() / qMax(1, count()))));
    return size;
}

QSize DockTabBar::minimumSizeHint() const
{
    // QTabBar asks for room to scroll in even when its tabs take less.
    const QSize size = QTabBar::minimumSizeHint();
    return QSize(qMin(size.width(), m_shrink ? 24 : sizeHint().width()), size.height());
}

void DockTabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();
    updateTabButtons();
}

// A tab squeezed below what it needs gives up its button, so that what is
// left of it shows the icon. The current tab keeps its own.
void DockTabBar::updateTabButtons()
{
    if (!m_shrink && !m_buttonsHidden)
        return;
    m_buttonsHidden = false;
    for (int i = 0; i < count(); ++i) {
        const bool fits = !m_shrink || i == currentIndex()
            || tabRect(i).width() >= unsqueezedMinimum(i).width();
        m_buttonsHidden = m_buttonsHidden || !fits;
        for (const ButtonPosition side : {QTabBar::LeftSide, QTabBar::RightSide}) {
            QWidget *button = tabButton(i, side);
            if (button && button->isHidden() == fits)
                button->setVisible(fits);
        }
    }
}

int DockTabBar::indexOfPanel(const PanelId &panel) const
{
    if (panel.isEmpty())
        return -1; // not the gap
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
    if (count() == 0 || isHidden())
        return {};
    const QRect last = tabRect(count() - 1);
    QRect region = tabRect(0).united(last);
    region.setTop(0);
    region.setBottom(height() - 1);
    region = region.intersected(rect());
    // Room for appending, unless the last tab is scrolled out of sight.
    if (last.right() <= rect().right())
        region.setRight(last.right() + qMin(last.width() / 2, 48));
    return region;
}

int DockTabBar::insertIndexAt(const QPoint &pos) const
{
    int tabs = 0;
    for (int i = 0; i < count(); ++i) {
        if (isGap(i))
            continue;
        if (pos.x() < tabRect(i).center().x())
            return tabs;
        ++tabs;
    }
    return tabs;
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
        if (isGap(index))
            return;
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
            // On the edge of the tab that faces away from the content.
            const QRect r = tabRect(current);
            const bool below = shape() == QTabBar::RoundedSouth || shape() == QTabBar::TriangularSouth;
            painter.fillRect(QRect(r.left(), below ? r.bottom() - 1 : r.top(), r.width(), 2), mark);
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
        // Closes the tab once the button is let go of on it. QTabBar never
        // gets to see the middle button: some versions close a tab on it
        // themselves, which would make two of one click.
        m_middlePanel = panelAt(tabAt(event->position().toPoint()));
        return;
    }
    QTabBar::mousePressEvent(event);
}

void DockTabBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && event->buttons().testFlag(Qt::LeftButton)) {
        const QPoint offset = event->position().toPoint() - m_pressPos;
        if (offset.manhattanLength() >= QApplication::startDragDistance()) {
            m_pressed = false; // one drag per press
            // The drag takes the pointer away without a word, and the tabs
            // may be others by the time it is back.
            QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1),
                              event->position());
            QCoreApplication::sendEvent(this, &leave);
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
    if (event->button() == Qt::MiddleButton) {
        const PanelId pressed = std::exchange(m_middlePanel, PanelId());
        const int index = tabAt(event->position().toPoint());
        if (index >= 0 && !pressed.isEmpty() && panelAt(index) == pressed)
            Q_EMIT panelCloseRequested(pressed);
        return;
    }
    if (event->button() == Qt::LeftButton)
        m_pressed = false;
    QTabBar::mouseReleaseEvent(event);
}

void DockTabBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        Q_EMIT barDoubleClicked(tabAt(event->position().toPoint()) >= 0);
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
