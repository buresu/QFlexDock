// SPDX-License-Identifier: MIT
#include "widgets/DockTabBar.h"

#include <QtCore/QVariantAnimation>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleOptionTab>
#include <QtWidgets/QStylePainter>
#include <QtWidgets/QToolButton>

#include <utility>

namespace QFlexDock {

namespace {

// How long tabs take to slide to their places (DockTabReorder::Animated), in
// milliseconds.
constexpr int SlideDuration = 150;
// Where the picture of a tab that is moved hangs from the pointer
// (DockTabReorder::Picture).
constexpr QPoint PictureOffset(10, 6);

// The picture of a tab that is moved, over whatever is in its window.
class TabPicture : public QWidget
{
public:
    TabPicture(const QPixmap &picture, QWidget *parent)
        : QWidget(parent)
        , m_picture(picture)
    {
        setObjectName(QStringLiteral("dockTabPicture"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        resize(picture.deviceIndependentSize().toSize());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawPixmap(rect(), m_picture);
    }

private:
    QPixmap m_picture;
};

} // namespace

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

void DockTabBar::setHugsTabs(bool hugs)
{
    if (m_hugsTabs == hugs)
        return;
    m_hugsTabs = hugs;
    updateGeometry();
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
    const QSize size = QTabBar::minimumSizeHint();
    if (m_shrink)
        return QSize(qMin(size.width(), 24), size.height());
    // QTabBar asks for room to scroll in even when its tabs take less, which
    // a bar that ends with its last tab does not have. Any other bar keeps
    // that minimum: it is the same whatever the titles are, so that groups
    // side by side and one above the other can be as narrow as each other.
    if (m_hugsTabs)
        return QSize(qMin(size.width(), sizeHint().width()), size.height());
    return size;
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

void DockTabBar::setMovablePanels(const QSet<PanelId> &panels)
{
    m_movable = panels;
}

QRect DockTabBar::tabMoveRegion() const
{
    const int slack = QApplication::startDragDistance();
    QRect region = rect().adjusted(0, -slack, 0, slack);
    if (const QWidget *row = parentWidget()) {
        region.setLeft(-x());
        region.setWidth(row->width());
    }
    return region;
}

void DockTabBar::setTabReorder(DockTabReorder reorder)
{
    m_reorder = reorder;
}

void DockTabBar::beginTabMove(const PanelId &panel, const QPoint &pos)
{
    m_moving = panel;
    const bool marked = m_reorder == DockTabReorder::Picture || m_reorder == DockTabReorder::Marker;
    if (marked)
        setCursor(Qt::ClosedHandCursor); // the tabs do not show that one is held
    if (m_reorder == DockTabReorder::Picture) {
        // The tab as it looks: with what is behind it, which may be all of
        // its background.
        const QRect tab = tabRect(indexOfPanel(panel)).intersected(rect());
        QWidget *top = window();
        m_movePicture = new TabPicture(top->grab(QRect(mapTo(top, tab.topLeft()), tab.size())), top);
        m_movePicture->show();
        m_movePicture->raise();
    }
    moveTabUnder(pos);
}

// Where a tab dropped at the pointer would be put is where the tab that is
// moved goes: past the middle of a neighbour, on the other side of it. Either
// the tabs show that at once, and where the tab then is the same pointer
// leaves it; or they stay, and a mark stands where it would go.
void DockTabBar::moveTabUnder(const QPoint &pos)
{
    const int from = indexOfPanel(m_moving);
    if (from < 0)
        return;
    // (Beside the bar is at its end, not among tabs that are scrolled away.)
    const int before = insertIndexAt(QPoint(qBound(0, pos.x(), width() - 1), pos.y()));
    if (m_reorder == DockTabReorder::Picture || m_reorder == DockTabReorder::Marker) {
        // No mark for staying where it is.
        setMoveMark(before == from || before == from + 1 ? -1 : before);
        if (m_movePicture)
            m_movePicture->move(mapTo(m_movePicture->parentWidget(), pos) + PictureOffset);
        return;
    }
    const int to = before > from ? before - 1 : before;
    if (to == from)
        return;
    QHash<PanelId, int> shownAt;
    if (m_reorder == DockTabReorder::Animated) {
        for (int i = 0; i < count(); ++i)
            shownAt.insert(panelAt(i), visualTabRect(i).left());
    }
    {
        // (The current tab is the same one at another index.)
        const QSignalBlocker blocker(this);
        moveTab(from, to);
        // QTabBar moves tabs as wide as they are, and a style sheet may have
        // made one wider for being the first: measured again.
        setElideMode(elideMode());
    }
    if (m_reorder == DockTabReorder::Animated)
        slideTabsFrom(shownAt);
}

void DockTabBar::finishTabMove()
{
    const PanelId panel = std::exchange(m_moving, PanelId());
    const int mark = m_moveMark;
    endMoveFeedback();
    int index = indexOfPanel(panel);
    if (index < 0)
        return;
    if (mark >= 0)
        index = mark > index ? mark - 1 : mark;
    else if (const int gap = gapIndex(); gap >= 0 && gap < index)
        --index;
    Q_EMIT panelMoveRequested(panel, index);
}

// The mark, the picture and the cursor of a tab that is moved.
void DockTabBar::endMoveFeedback()
{
    setMoveMark(-1);
    delete m_movePicture.data();
    unsetCursor();
}

void DockTabBar::setMoveMark(int index)
{
    if (m_moveMark == index)
        return;
    m_moveMark = index;
    Q_EMIT moveMarkChanged(index);
}

// --- Tabs that slide ------------------------------------------------------------

QRect DockTabBar::visualTabRect(int index) const
{
    return tabRect(index).translated(slideOffset(index), 0);
}

int DockTabBar::slideOffset(int index) const
{
    if (m_slideFrom.isEmpty() || !m_slide)
        return 0;
    return qRound(m_slideFrom.value(panelAt(index)) * m_slide->currentValue().toReal());
}

// The tabs are in their places already. They are drawn from where they were
// shown (`shownAt`, the left edge of each) to there.
void DockTabBar::slideTabsFrom(const QHash<PanelId, int> &shownAt)
{
    m_slideFrom.clear();
    for (int i = 0; i < count(); ++i) {
        const int left = tabRect(i).left();
        if (const int offset = shownAt.value(panelAt(i), left) - left; offset != 0)
            m_slideFrom.insert(panelAt(i), offset);
    }
    if (!m_slide) {
        m_slide = new QVariantAnimation(this);
        m_slide->setDuration(SlideDuration);
        m_slide->setEasingCurve(QEasingCurve::OutCubic);
        m_slide->setStartValue(1.0);
        m_slide->setEndValue(0.0);
        connect(m_slide, &QVariantAnimation::valueChanged, this, [this] {
            placeSlidingButtons();
            update();
        });
        connect(m_slide, &QVariantAnimation::finished, this, &DockTabBar::stopSliding);
    }
    m_slide->stop();
    m_slide->start();
    placeSlidingButtons();
    update();
}

void DockTabBar::stopSliding()
{
    if (m_slideFrom.isEmpty())
        return;
    m_slideFrom.clear();
    m_slide->stop();
    setElideMode(elideMode()); // QTabBar puts the buttons of the tabs back
    update();
}

// The buttons of a tab are widgets, which QTabBar puts where the tab is.
void DockTabBar::placeSlidingButtons()
{
    for (int i = 0; i < count(); ++i) {
        if (!m_slideFrom.contains(panelAt(i)))
            continue;
        QStyleOptionTab option;
        initStyleOption(&option, i);
        for (const ButtonPosition side : {QTabBar::LeftSide, QTabBar::RightSide}) {
            QWidget *button = tabButton(i, side);
            if (!button)
                continue;
            const QRect place = style()->subElementRect(
                side == QTabBar::LeftSide ? QStyle::SE_TabBarTabLeftButton
                                          : QStyle::SE_TabBarTabRightButton,
                &option, this);
            button->move(place.topLeft() + QPoint(slideOffset(i), 0));
        }
    }
}

void DockTabBar::setActiveGroup(bool active)
{
    if (m_activeGroup == active)
        return;
    m_activeGroup = active;
    style()->unpolish(this);
    style()->polish(this);
    // A style may give the bar another font while it is at that (the macOS
    // style does, and takes it back), and QTabBar measures its tabs as soon
    // as the font changes: with what holds halfway through. Once more, then,
    // with what holds now.
    setElideMode(elideMode());
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
        option.rect.translate(slideOffset(index), 0);
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
            const QRect r = visualTabRect(current);
            const bool below = shape() == QTabBar::RoundedSouth || shape() == QTabBar::TriangularSouth;
            painter.fillRect(QRect(r.left(), below ? r.bottom() - 1 : r.top(), r.width(), 2), mark);
        }
    }
}

void DockTabBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // (A move whose end was never heard of ends as it is shown.)
        if (!m_moving.isEmpty())
            finishTabMove();
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
    const QPoint pos = event->position().toPoint();
    const bool held = event->buttons().testFlag(Qt::LeftButton);
    // The drag takes the pointer away without a word, and the tabs may be
    // others by the time it is back.
    const auto leaveForDrag = [this, event] {
        QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), event->position());
        QCoreApplication::sendEvent(this, &leave);
    };
    if (!m_moving.isEmpty() && !held) {
        finishTabMove(); // let go of unseen
    } else if (!m_moving.isEmpty()) {
        if (tabMoveRegion().contains(pos) && indexOfPanel(m_moving) >= 0) {
            moveTabUnder(pos);
        } else {
            // Off the bar: a dock drag from here on, of a tab that is where
            // it was.
            const PanelId panel = std::exchange(m_moving, PanelId());
            endMoveFeedback();
            stopSliding();
            leaveForDrag();
            Q_EMIT panelDragStarted(panel);
        }
        return;
    }
    if (m_pressed && held) {
        const QPoint offset = pos - m_pressPos;
        if (offset.manhattanLength() >= QApplication::startDragDistance()) {
            m_pressed = false; // one drag per press
            const PanelId panel = panelAt(m_pressIndex);
            if (panel.isEmpty()) {
                leaveForDrag();
                Q_EMIT groupDragStarted();
            } else if (m_movable.contains(panel) && count() > 1 && gapIndex() < 0
                       && tabMoveRegion().contains(pos)) {
                beginTabMove(panel, pos);
            } else {
                leaveForDrag();
                Q_EMIT panelDragStarted(panel);
            }
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
    if (event->button() == Qt::LeftButton && !m_moving.isEmpty())
        finishTabMove();
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
