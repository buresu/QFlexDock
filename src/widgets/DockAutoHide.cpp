// SPDX-License-Identifier: MIT
#include "widgets/DockAutoHide.h"

#include "widgets/DockAreaWidget.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QStyleOptionTab>
#include <QtWidgets/QStylePainter>
#include <QtWidgets/QToolButton>

namespace QFlexDock {

namespace {

bool isVerticalEdge(DockArea edge)
{
    return edge == DockArea::Left || edge == DockArea::Right;
}

constexpr int GripThickness = 5;

} // namespace

// --- DockAutoHideTab ---------------------------------------------------------

DockAutoHideTab::DockAutoHideTab(const PanelId &panel, DockArea edge, QWidget *parent)
    : QAbstractButton(parent)
    , m_panel(panel)
    , m_edge(edge)
{
    setCheckable(true);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover);
}

void DockAutoHideTab::initOption(QStyleOptionTab *option) const
{
    option->initFrom(this);
    switch (m_edge) {
    case DockArea::Left:
        option->shape = QTabBar::RoundedWest;
        break;
    case DockArea::Right:
        option->shape = QTabBar::RoundedEast;
        break;
    case DockArea::Bottom:
        option->shape = QTabBar::RoundedSouth;
        break;
    default:
        option->shape = QTabBar::RoundedNorth;
        break;
    }
    option->text = text();
    option->icon = icon();
    const int iconExtent = style()->pixelMetric(QStyle::PM_TabBarIconSize, nullptr, this);
    option->iconSize = QSize(iconExtent, iconExtent);
    option->position = QStyleOptionTab::OnlyOneTab;
    option->selectedPosition = QStyleOptionTab::NotAdjacent;
    option->documentMode = true;
    option->state.setFlag(QStyle::State_Selected, isChecked());
    option->state.setFlag(QStyle::State_Sunken, isDown());
    option->state.setFlag(QStyle::State_MouseOver, underMouse());
}

QSize DockAutoHideTab::sizeHint() const
{
    QStyleOptionTab option;
    initOption(&option);
    const QFontMetrics metrics = fontMetrics();
    const int hSpace = style()->pixelMetric(QStyle::PM_TabBarTabHSpace, &option, this);
    const int vSpace = style()->pixelMetric(QStyle::PM_TabBarTabVSpace, &option, this);
    const int along = metrics.horizontalAdvance(text()) + hSpace
        + (icon().isNull() ? 0 : option.iconSize.width() + 4);
    const int across = qMax(metrics.height(), icon().isNull() ? 0 : option.iconSize.height())
        + vSpace;
    const QSize content = isVerticalEdge(m_edge) ? QSize(across, along) : QSize(along, across);
    return style()->sizeFromContents(QStyle::CT_TabBarTab, &option, content, this);
}

void DockAutoHideTab::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionTab option;
    initOption(&option);
    painter.drawControl(QStyle::CE_TabBarTab, option);
}

// --- DockAutoHideBar ---------------------------------------------------------

DockAutoHideBar::DockAutoHideBar(DockArea edge, QWidget *parent)
    : QWidget(parent)
    , m_edge(edge)
{
    setAttribute(Qt::WA_StyledBackground);
    m_layout = new QBoxLayout(isVerticalEdge(edge) ? QBoxLayout::TopToBottom
                                                   : QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addStretch(1);
    hide();
}

QString DockAutoHideBar::edgeName() const
{
    switch (m_edge) {
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

QList<DockAutoHideTab *> DockAutoHideBar::tabs() const
{
    return findChildren<DockAutoHideTab *>(Qt::FindDirectChildrenOnly);
}

DockAutoHideTab *DockAutoHideBar::tab(const PanelId &panel) const
{
    for (DockAutoHideTab *t : tabs()) {
        if (t->panelId() == panel)
            return t;
    }
    return nullptr;
}

void DockAutoHideBar::setPanels(DockManagerPrivate *manager, const QStringList &panels)
{
    if (m_panels != panels) {
        m_panels = panels;
        qDeleteAll(tabs());
        int index = 0;
        for (const PanelId &panel : panels) {
            auto *t = new DockAutoHideTab(panel, m_edge, this);
            m_layout->insertWidget(index++, t);
            connect(t, &QAbstractButton::clicked, this, [this, panel] { Q_EMIT tabClicked(panel); });
        }
    }
    for (DockAutoHideTab *t : tabs()) {
        if (const DockPanel *panel = manager->panels.value(t->panelId())) {
            t->setText(panel->title());
            t->setIcon(panel->icon());
            t->setToolTip(panel->toolTip());
            t->updateGeometry();
        }
    }
    setVisible(!panels.isEmpty());
}

void DockAutoHideBar::setExpanded(const PanelId &panel)
{
    for (DockAutoHideTab *t : tabs())
        t->setChecked(!panel.isEmpty() && t->panelId() == panel);
}

// --- DockAutoHidePopup -------------------------------------------------------

DockAutoHidePopup::DockAutoHidePopup(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true); // it covers the dock area underneath
    setFocusPolicy(Qt::StrongFocus);

    // Everything but the grip: a frame a style sheet can draw, so that the
    // grip may be a gap beside it.
    auto *body = new QFrame(this);
    body->setObjectName(QStringLiteral("dockAutoHideBody"));
    m_title = new QLabel(body);
    m_title->setObjectName(QStringLiteral("dockAutoHideTitle"));
    const auto makeButton = [body](const char *name, const QString &tip) {
        auto *button = new QToolButton(body);
        button->setObjectName(QLatin1String(name));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        return button;
    };
    m_pin = makeButton("dockPinButton", tr("Pin"));
    m_close = makeButton("dockCloseButton", tr("Close"));

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(6, 2, 2, 2);
    titleRow->addWidget(m_title, 1);
    titleRow->addWidget(m_pin);
    titleRow->addWidget(m_close);

    m_host = new QWidget(body);
    auto *hostLayout = new QVBoxLayout(m_host);
    hostLayout->setContentsMargins(0, 0, 0, 0);

    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addLayout(titleRow);
    bodyLayout->addWidget(m_host, 1);

    m_grip = new QWidget(this);
    m_grip->setObjectName(QStringLiteral("dockAutoHideGrip"));
    m_grip->installEventFilter(this);

    m_layout = new QGridLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addWidget(body, 1, 1);
    setEdge(DockArea::Left);
    hide();
}

void DockAutoHidePopup::setTitle(const QString &title)
{
    m_title->setText(title);
}

void DockAutoHidePopup::setEdge(DockArea edge)
{
    m_edge = edge;
    // The grip sits on the side facing the dock area.
    m_layout->removeWidget(m_grip);
    m_grip->setMinimumSize(0, 0);
    m_grip->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (isVerticalEdge(edge)) {
        m_grip->setFixedWidth(GripThickness);
        m_grip->setCursor(Qt::SplitHCursor);
        m_layout->addWidget(m_grip, 1, edge == DockArea::Left ? 2 : 0);
    } else {
        m_grip->setFixedHeight(GripThickness);
        m_grip->setCursor(Qt::SplitVCursor);
        m_layout->addWidget(m_grip, edge == DockArea::Top ? 2 : 0, 1);
    }
}

bool DockAutoHidePopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_grip) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            m_gripPress = mouse->globalPosition().toPoint();
            m_gripStartExtent = isVerticalEdge(m_edge) ? width() : height();
            return true;
        }
        if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->buttons().testFlag(Qt::LeftButton)) {
                const QPoint offset = mouse->globalPosition().toPoint() - m_gripPress;
                int delta = isVerticalEdge(m_edge) ? offset.x() : offset.y();
                if (m_edge == DockArea::Right || m_edge == DockArea::Bottom)
                    delta = -delta;
                m_extent = qMax(40, m_gripStartExtent + delta);
                Q_EMIT extentChanged();
            }
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void DockAutoHidePopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        Q_EMIT escapePressed();
    else
        QFrame::keyPressEvent(event);
}

// --- DockAutoHideContainer ---------------------------------------------------

DockAutoHideContainer::DockAutoHideContainer(DockManagerPrivate *manager, DockWorkspace *workspace,
                                             DockAreaWidget *area)
    : QObject(workspace)
    , m_manager(manager)
    , m_workspace(workspace)
    , m_area(area)
{
    for (int i = 0; i < 4; ++i) {
        m_bars[size_t(i)] = new DockAutoHideBar(DockEdges[size_t(i)], workspace);
        connect(m_bars[size_t(i)], &DockAutoHideBar::tabClicked, this, [this](const PanelId &panel) {
            if (m_expanded == panel)
                collapse();
            else
                expand(panel);
        });
    }
    m_popup = new DockAutoHidePopup(workspace);
    connect(m_popup, &DockAutoHidePopup::escapePressed, this, &DockAutoHideContainer::collapse);
    connect(m_popup, &DockAutoHidePopup::extentChanged, this,
            &DockAutoHideContainer::updatePopupGeometry);
    connect(m_popup->pinButton(), &QToolButton::clicked, this, [this] {
        if (m_manager && !m_expanded.isEmpty())
            (void)m_manager->setAutoHide(m_expanded, false, DockArea::None);
    });
    connect(m_popup->closeButton(), &QToolButton::clicked, this, [this] {
        if (m_manager && m_manager->userMay(m_expanded, DockFeature::Closable))
            (void)m_manager->closePanels({m_expanded});
    });
    area->installEventFilter(this);
    workspace->installEventFilter(this);
    refreshAppearance();
}

void DockAutoHideContainer::detachFromManager()
{
    m_popup->hide();
    m_expanded.clear();
    reserve(DockArea::None, 0);
    m_manager = nullptr;
}

DockAutoHideBar *DockAutoHideContainer::bar(DockArea edge) const
{
    const int index = edgeIndex(edge);
    return index >= 0 ? m_bars[size_t(index)] : nullptr;
}

DockArea DockAutoHideContainer::edgeOf(const PanelId &panel) const
{
    for (int i = 0; i < 4; ++i) {
        if (m_panels[size_t(i)].contains(panel))
            return DockEdges[size_t(i)];
    }
    return DockArea::None;
}

void DockAutoHideContainer::setLayoutState(const ContainerState &container)
{
    if (!m_manager)
        return;
    m_panels = container.autoHide;
    for (int i = 0; i < 4; ++i)
        m_bars[size_t(i)]->setPanels(m_manager, m_panels[size_t(i)]);
    if (!m_expanded.isEmpty() && edgeOf(m_expanded) == DockArea::None)
        collapse();
    for (DockAutoHideBar *b : m_bars)
        b->setExpanded(m_expanded);
}

void DockAutoHideContainer::expand(const PanelId &panel)
{
    if (!m_manager || m_expanded == panel)
        return;
    const DockArea edge = edgeOf(panel);
    DockPanel *p = m_manager->panels.value(panel);
    if (edge == DockArea::None || !p)
        return;
    collapse();

    m_expanded = panel;
    if (QWidget *content = m_manager->ensureWidget(p)) {
        m_manager->reparentContent(p, m_popup->contentHost());
        m_popup->contentHost()->layout()->addWidget(content);
        content->setVisible(!m_manager->contentSuspended(p));
    }
    m_popup->setEdge(edge);
    m_popup->setTitle(p->title());
    updatePopupGeometry();
    m_popup->show();
    m_popup->raise();
    for (DockAutoHideBar *b : m_bars)
        b->setExpanded(m_expanded);
    m_manager->updatePanelStates(true);
}

void DockAutoHideContainer::collapse()
{
    if (m_expanded.isEmpty())
        return;
    const PanelId panel = m_expanded;
    m_expanded.clear();
    m_popup->hide();
    reserve(DockArea::None, 0);
    for (DockAutoHideBar *b : m_bars)
        b->setExpanded({});
    if (!m_manager)
        return;
    if (DockPanel *p = m_manager->panels.value(panel))
        m_manager->parkIfHostedBy(p, m_popup->contentHost());
}

void DockAutoHideContainer::refreshPanel(const PanelId &panel)
{
    if (!m_manager)
        return;
    for (int i = 0; i < 4; ++i)
        m_bars[size_t(i)]->setPanels(m_manager, m_panels[size_t(i)]);
    if (panel == m_expanded) {
        if (const DockPanel *p = m_manager->panels.value(panel))
            m_popup->setTitle(p->title());
    }
}

void DockAutoHideContainer::refreshAppearance()
{
    if (!m_manager)
        return;
    m_popup->pinButton()->setIcon(m_manager->icon(DockIcon::Pin, m_popup));
    m_popup->closeButton()->setIcon(m_manager->icon(DockIcon::Close, m_popup));
    updatePopupGeometry(); // over the dock area or beside it
}

void DockAutoHideContainer::pressedElsewhere(QWidget *pressed)
{
    if (m_expanded.isEmpty() || !pressed)
        return;
    // Menus and combo box lists opened from the popup are windows of their own.
    if (pressed->window() != m_workspace->window())
        return;
    if (pressed == m_popup || m_popup->isAncestorOf(pressed))
        return;
    for (const DockAutoHideBar *b : m_bars) {
        if (pressed == b || b->isAncestorOf(pressed))
            return;
    }
    collapse();
}

void DockAutoHideContainer::reserve(DockArea edge, int extent)
{
    if (edge == DockArea::None)
        extent = 0;
    if (m_reservedEdge == edge && m_reserved == extent)
        return;
    auto *grid = qobject_cast<QGridLayout *>(m_workspace->layout());
    if (!grid)
        return;
    m_reservedEdge = edge;
    m_reserved = extent;
    // The empty row or column between each bar and the dock area.
    grid->setColumnMinimumWidth(1, edge == DockArea::Left ? extent : 0);
    grid->setColumnMinimumWidth(3, edge == DockArea::Right ? extent : 0);
    grid->setRowMinimumHeight(1, edge == DockArea::Top ? extent : 0);
    grid->setRowMinimumHeight(3, edge == DockArea::Bottom ? extent : 0);
    // The area has its new size before the popup is put beside it.
    m_reserving = true;
    grid->activate();
    m_reserving = false;
}

void DockAutoHideContainer::updatePopupGeometry()
{
    if (m_reserving)
        return;
    const DockArea edge = edgeOf(m_expanded);
    if (edge == DockArea::None) {
        reserve(DockArea::None, 0);
        return;
    }
    const bool beside = m_manager
        && m_manager->autoHideReveal == DockManager::AutoHideReveal::Beside;
    const bool vertical = isVerticalEdge(edge);
    QRect area = m_area->geometry();
    // What the area has, and what it has given up already.
    int available = vertical ? area.width() : area.height();
    if (m_reservedEdge != DockArea::None && isVerticalEdge(m_reservedEdge) == vertical)
        available += m_reserved;
    const QSize minimum = m_popup->minimumSizeHint();
    int extent = m_popup->extent() > 0 ? m_popup->extent() : available * 3 / 10;
    extent = qMax(extent, vertical ? minimum.width() : minimum.height());
    int most = qMax(40, available * 9 / 10);
    if (beside) {
        // The area keeps what its panels need at the least.
        const QSize least = m_area->layoutMinimumSize();
        most = qMin(most, qMax(40, available - (vertical ? least.width() : least.height())));
    }
    extent = qMin(extent, most);

    QRect geometry = area;
    if (beside) {
        reserve(edge, extent);
        area = m_area->geometry();
        switch (edge) {
        case DockArea::Left:
            geometry = QRect(area.left() - extent, area.top(), extent, area.height());
            break;
        case DockArea::Right:
            geometry = QRect(area.right() + 1, area.top(), extent, area.height());
            break;
        case DockArea::Top:
            geometry = QRect(area.left(), area.top() - extent, area.width(), extent);
            break;
        default:
            geometry = QRect(area.left(), area.bottom() + 1, area.width(), extent);
            break;
        }
    } else {
        reserve(DockArea::None, 0);
        switch (edge) {
        case DockArea::Left:
            geometry.setWidth(extent);
            break;
        case DockArea::Right:
            geometry.setLeft(area.right() - extent + 1);
            break;
        case DockArea::Top:
            geometry.setHeight(extent);
            break;
        default:
            geometry.setTop(area.bottom() - extent + 1);
            break;
        }
    }
    m_popup->setGeometry(geometry);
}

bool DockAutoHideContainer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_area && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        updatePopupGeometry();
    // The panels in the area may need more room than before: a panel that
    // is out beside them gives way first, before the window has to grow.
    if (watched == m_workspace && event->type() == QEvent::LayoutRequest && m_reserved > 0)
        updatePopupGeometry();
    return QObject::eventFilter(watched, event);
}

} // namespace QFlexDock
