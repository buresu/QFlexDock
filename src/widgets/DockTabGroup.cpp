// SPDX-License-Identifier: MIT
#include "widgets/DockTabGroup.h"

#include "core/DockDragController.h"
#include "core/DockManager_p.h"
#include "widgets/DockAreaWidget.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockTabBar.h"

#include <QtCore/QEvent>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QShortcut>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>

namespace QFlexDock {

namespace {

// The minimum Qt's own layouts would give a widget: an explicit minimum wins
// over the hint, and an Ignored size policy means "no minimum".
QSize effectiveMinimum(const QWidget *widget)
{
    const QSize hint = widget->minimumSizeHint();
    const QSize explicitMin = widget->minimumSize();
    const QSizePolicy policy = widget->sizePolicy();
    int w = 0;
    int h = 0;
    if (explicitMin.width() > 0)
        w = explicitMin.width();
    else if (policy.horizontalPolicy() != QSizePolicy::Ignored)
        w = qMax(0, hint.width());
    if (explicitMin.height() > 0)
        h = explicitMin.height();
    else if (policy.verticalPolicy() != QSizePolicy::Ignored)
        h = qMax(0, hint.height());
    return QSize(w, h).boundedTo(widget->maximumSize());
}

// Shows or hides only on a change. (An explicit show of a widget that was
// never hidden is not a no-op to Qt: it activates the parent's layout there
// and then, before the group has its tabs.)
void setShown(QWidget *widget, bool shown)
{
    if (widget->isHidden() == shown)
        widget->setVisible(shown);
}

QToolButton *makeTitleButton(QWidget *parent, const char *objectName, const QString &toolTip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QLatin1String(objectName));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    return button;
}

} // namespace

DockTabGroup::DockTabGroup(DockManagerPrivate *manager, DockAreaWidget *area)
    : QFrame(area)
    , m_manager(manager)
    , m_area(area)
{
    // The host style's panel frame keeps neighbouring groups apart; a style
    // sheet can replace it with its own border, or none.
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);

    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("dockTitleBar"));
    m_titleBar->setAttribute(Qt::WA_StyledBackground);
    m_titleBar->installEventFilter(this);

    m_tabBar = new DockTabBar(m_titleBar);
    m_titleLabel = new QLabel(m_titleBar);
    m_titleLabel->setObjectName(QStringLiteral("dockTitle"));
    // The title is part of the title bar as far as the mouse is concerned,
    // and gives way (is cut off) when the group gets narrow.
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // Some room before the text (a style sheet's padding comes on top of it).
    m_titleLabel->setContentsMargins(6, 0, 0, 0);
    m_titleLabel->hide();
    m_menuButton = makeTitleButton(m_titleBar, "dockMenuButton", tr("Panel menu"));
    m_maximizeButton = makeTitleButton(m_titleBar, "dockMaximizeButton", tr("Maximize"));
    m_floatButton = makeTitleButton(m_titleBar, "dockFloatButton", tr("Float"));
    m_closeButton = makeTitleButton(m_titleBar, "dockCloseButton", tr("Close"));
    m_floatButton->hide();
    m_closeButton->hide();

    m_titleLayout = new QHBoxLayout(m_titleBar);
    m_titleLayout->setContentsMargins(0, 0, 2, 0);
    m_titleLayout->setSpacing(0);
    m_titleLayout->addWidget(m_tabBar, 1);
    m_titleLayout->addWidget(m_titleLabel, 1);
    for (QToolButton *button : {m_menuButton, m_maximizeButton, m_floatButton, m_closeButton})
        m_titleLayout->addWidget(button, 0, Qt::AlignVCenter);

    // The content area has no layout of its own: only the current panel's
    // widget is shown, sized to fill it.
    m_host = new QWidget(this);
    m_host->setObjectName(QStringLiteral("dockContentHost"));
    m_host->installEventFilter(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_titleBar);
    layout->addWidget(m_host, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_manager || index < 0)
            return;
        (void)m_manager->activate(m_tabBar->panelAt(index), true);
        // Whatever the manager decided is what the bar shows.
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->setCurrentIndex(m_tabBar->indexOfPanel(m_current));
    });
    connect(m_tabBar, &QTabBar::tabCloseRequested, this,
            [this](int index) { closeByUser(m_tabBar->panelAt(index)); });
    connect(m_tabBar, &DockTabBar::panelCloseRequested, this, &DockTabGroup::closeByUser);
    connect(m_tabBar, &DockTabBar::panelDragStarted, this, [this](const PanelId &panel) {
        startPanelDrag(panel, m_tabBar->grab(m_tabBar->tabRect(m_tabBar->indexOfPanel(panel))));
    });
    connect(m_tabBar, &DockTabBar::groupDragStarted, this, &DockTabGroup::startGroupDrag);
    connect(m_tabBar, &DockTabBar::panelMenuRequested, this, &DockTabGroup::showPanelMenu);
    connect(m_tabBar, &DockTabBar::barDoubleClicked, this, &DockTabGroup::toggleMaximized);
    connect(m_maximizeButton, &QToolButton::clicked, this, &DockTabGroup::toggleMaximized);
    connect(m_menuButton, &QToolButton::clicked, this, &DockTabGroup::showGroupMenu);
    connect(m_floatButton, &QToolButton::clicked, this, &DockTabGroup::toggleFloating);
    connect(m_closeButton, &QToolButton::clicked, this, [this] { closeByUser(m_current); });

    // Standard "next/previous tab" keys while focus is inside the group.
    const auto cycle = [this](int step) {
        const int count = m_tabBar->count();
        if (count > 1)
            m_tabBar->setCurrentIndex((m_tabBar->currentIndex() + step + count) % count);
    };
    auto *next = new QShortcut(QKeySequence::NextChild, this);
    next->setContext(Qt::WidgetWithChildrenShortcut);
    connect(next, &QShortcut::activated, this, [cycle] { cycle(1); });
    auto *previous = new QShortcut(QKeySequence::PreviousChild, this);
    previous->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previous, &QShortcut::activated, this, [cycle] { cycle(-1); });

    refreshAppearance();
}

DockTabGroup::~DockTabGroup()
{
    // Children of the host die with it. Content widgets belong to the manager,
    // so whatever is still parked here has to leave first.
    if (!m_manager)
        return;
    const QObjectList children = m_host->children();
    for (QObject *child : children) {
        if (auto *widget = qobject_cast<QWidget *>(child)) {
            if (DockPanel *panel = m_manager->panelByWidget.value(widget))
                m_manager->parkIfHostedBy(panel, m_host);
        }
    }
}

void DockTabGroup::detachFromManager()
{
    m_manager = nullptr;
}

// --- User actions --------------------------------------------------------------

void DockTabGroup::closeByUser(const PanelId &panel)
{
    if (m_manager && m_manager->userMay(panel, DockFeature::Closable))
        (void)m_manager->closePanels({panel});
}

void DockTabGroup::toggleMaximized()
{
    if (m_manager && m_manager->userMay(m_current, DockFeature::Maximizable))
        (void)m_manager->setMaximized(m_current, !m_maximized);
}

void DockTabGroup::toggleFloating()
{
    if (!m_manager || !m_manager->userMay(m_current, DockFeature::Floatable))
        return;
    const DockPanel *panel = m_manager->panels.value(m_current);
    if (panel && panel->isFloating())
        (void)m_manager->dockBack(m_current);
    else
        (void)m_manager->floatPanels(m_current, false, QRect());
}

// A floating window without a title row is moved by the headers of its
// groups. Where a dock drag moves the window anyway (the platform carries it
// along, or leaves it where it is dropped), that is all there is to it.
// Elsewhere a drag of everything in the window just moves the window.
bool DockTabGroup::moveWindowInstead(qsizetype draggedPanels)
{
    const auto *floating = qobject_cast<const DockFloatingWindow *>(window());
    if (!floating || !floating->hasMinimalFrame() || !floating->windowHandle()
        || m_area->tree().panels().size() != draggedPanels
        || m_manager->drag->carriesWindows() || m_manager->floatOnOutsideDrop) {
        return false;
    }
    return floating->windowHandle()->startSystemMove();
}

void DockTabGroup::startPanelDrag(const PanelId &panel, const QPixmap &pixmap)
{
    if (!m_manager || !m_manager->userMay(panel, DockFeature::Movable) || moveWindowInstead(1))
        return;
    m_manager->drag->requestPanelDrag(panel, pixmap);
}

void DockTabGroup::startGroupDrag()
{
    if (!m_manager)
        return;
    for (const PanelId &panel : std::as_const(m_panels)) {
        if (!m_manager->userMay(panel, DockFeature::Movable))
            return;
    }
    if (!moveWindowInstead(m_panels.size()))
        m_manager->drag->requestGroupDrag(m_current, m_titleBar->grab());
}

void DockTabGroup::showPanelMenu(const PanelId &panel, const QPoint &globalPos)
{
    if (!m_manager)
        return;
    if (DockPanel *p = m_manager->panels.value(panel)) {
        QMenu *menu = m_manager->createPanelMenu(p, window());
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(globalPos);
    }
}

// The title bar of GroupHeader::TitleBar stands for the current panel: drag
// it to move that panel, double click to float it or dock it again.
bool DockTabGroup::titleBarEvent(QEvent *event)
{
    if (!m_titleMode || !m_manager)
        return false;
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_titlePressed = true;
            m_titlePress = mouse->position().toPoint();
            (void)m_manager->activate(m_current, true);
        }
        break;
    }
    case QEvent::MouseMove: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_titlePressed && mouse->buttons().testFlag(Qt::LeftButton)
            && (mouse->position().toPoint() - m_titlePress).manhattanLength()
                   >= QApplication::startDragDistance()) {
            m_titlePressed = false; // one drag per press
            startPanelDrag(m_current, m_titleBar->grab());
        }
        break;
    }
    case QEvent::MouseButtonRelease:
        m_titlePressed = false;
        break;
    case QEvent::MouseButtonDblClick:
        if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            m_titlePressed = false;
            toggleFloating();
            return true;
        }
        break;
    case QEvent::ContextMenu:
        showPanelMenu(m_current, static_cast<QContextMenuEvent *>(event)->globalPos());
        return true;
    default:
        break;
    }
    return false;
}

// --- State ---------------------------------------------------------------------

void DockTabGroup::setNode(const LayoutNode &node, bool maximized)
{
    m_nodeId = node.id;
    if (m_panels != node.panels) {
        m_panels = node.panels;
        m_current = node.active;
        rebuildTabs();
    } else if (m_current != node.active) {
        m_current = node.active;
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->setCurrentIndex(m_tabBar->indexOfPanel(m_current));
    }
    if (m_maximized != maximized) {
        m_maximized = maximized;
        refreshAppearance();
        restyle();
    }
    updateHeader();
    syncContents();
}

// Which parts of the header there are, and what they say. Depends on the
// manager's header setting, on the panels in the group and on the theme.
void DockTabGroup::updateHeader()
{
    if (!m_manager)
        return;
    const bool titleMode = m_manager->groupHeader == DockManager::GroupHeader::TitleBar;
    if (titleMode != m_titleMode) {
        m_titleMode = titleMode;
        auto *groupLayout = static_cast<QVBoxLayout *>(layout());
        if (titleMode) {
            // The tabs move below the content, as plain tabs: closing and
            // the like is done from the title bar.
            m_titleLayout->removeWidget(m_tabBar);
            groupLayout->addWidget(m_tabBar);
            m_tabBar->setShape(QTabBar::RoundedSouth);
            m_tabBar->setTabsClosable(false);
        } else {
            groupLayout->removeWidget(m_tabBar);
            m_titleLayout->insertWidget(0, m_tabBar, 1);
            m_tabBar->setShape(QTabBar::RoundedNorth);
            m_tabBar->setTabsClosable(true);
        }
        setShown(m_titleLabel, titleMode);
        for (int i = 0; i < m_tabBar->count(); ++i)
            updateTab(i);
    }
    setShown(m_tabBar, !titleMode || m_panels.size() > 1);

    const DockPanel *current = m_manager->panels.value(m_current);
    QString title = current ? current->title() : QString();
    if (current && current->isDirty())
        title += QStringLiteral(" ●");
    m_titleLabel->setText(title);
    m_titleBar->setToolTip(titleMode && current ? current->toolTip() : QString());

    const DockTitleButtons buttons = m_manager->theme.titleButtons;
    const DockFeatures features = current ? current->features() : DockFeatures();
    setShown(m_menuButton, buttons.testFlag(DockTitleButton::Menu));
    setShown(m_maximizeButton, buttons.testFlag(DockTitleButton::Maximize));
    setShown(m_floatButton, buttons.testFlag(DockTitleButton::Float)
                                && features.testFlag(DockFeature::Floatable));
    setShown(m_closeButton, buttons.testFlag(DockTitleButton::Close)
                                && features.testFlag(DockFeature::Closable));
    const bool floating = current && current->isFloating();
    m_floatButton->setIcon(m_manager->icon(floating ? DockIcon::Dock : DockIcon::Float, this));
    const QString floatTip = floating ? tr("Dock") : tr("Float");
    m_floatButton->setToolTip(floatTip);
    m_floatButton->setAccessibleName(floatTip);

    // A panel may ask for no header at all while it has the group to itself.
    const bool headerVisible = !(m_panels.size() == 1 && current && !current->isHeaderVisible());
    setShown(m_titleBar, headerVisible);
    if (headerVisible != m_headerVisible) {
        m_headerVisible = headerVisible;
        restyle();
    }
}

void DockTabGroup::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    m_tabBar->setActiveGroup(active);
    restyle();
}

void DockTabGroup::restyle()
{
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void DockTabGroup::rebuildTabs()
{
    const QSignalBlocker blocker(m_tabBar);
    while (m_tabBar->count() > 0)
        m_tabBar->removeTab(0);
    for (const PanelId &panel : std::as_const(m_panels)) {
        const int index = m_tabBar->addTab(QString());
        m_tabBar->setTabData(index, panel);
        updateTab(index);
    }
    m_tabBar->setCurrentIndex(m_tabBar->indexOfPanel(m_current));
}

void DockTabGroup::updateTab(int index)
{
    if (!m_manager || index < 0)
        return;
    DockPanel *panel = m_manager->panels.value(m_tabBar->panelAt(index));
    if (!panel)
        return;

    QString text = panel->title();
    text.replace(QLatin1Char('&'), QLatin1String("&&")); // no mnemonics in titles
    if (panel->isDirty())
        text += QStringLiteral(" ●");
    m_tabBar->setTabText(index, text);
    m_tabBar->setTabIcon(index, panel->icon());
    m_tabBar->setTabToolTip(index, panel->toolTip());

    // The close-button slot shows: a pin for pinned tabs, nothing for panels
    // that may not be closed, the style's close button otherwise.
    const auto side = QTabBar::ButtonPosition(
        style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, m_tabBar));
    QWidget *button = m_tabBar->tabButton(index, side);
    // (Tabs below the content are plain: see updateHeader().)
    const bool wantsPin = !m_titleMode && panel->isPinnedTab();
    const bool wantsClose = !m_titleMode && !wantsPin
        && panel->features().testFlag(DockFeature::Closable);
    const bool hasPin = button && button->objectName() == QLatin1String("dockTabPin");
    if (wantsPin) {
        if (!hasPin) {
            auto *pin = new QLabel(m_tabBar);
            pin->setObjectName(QStringLiteral("dockTabPin"));
            m_tabBar->setTabButton(index, side, pin); // deletes the old button
            button = pin;
        }
        const int size = style()->pixelMetric(QStyle::PM_TabCloseIndicatorWidth, nullptr, m_tabBar);
        static_cast<QLabel *>(button)->setPixmap(
            m_manager->icon(DockIcon::Pin, m_tabBar).pixmap(size, size));
    } else if (!wantsClose && button) {
        m_tabBar->setTabButton(index, side, nullptr);
    } else if (wantsClose && (!button || hasPin)) {
        // Only a rebuild gets the style's own close button back.
        QMetaObject::invokeMethod(this, &DockTabGroup::rebuildTabs, Qt::QueuedConnection);
    }

    QSet<PanelId> italic;
    for (const PanelId &id : std::as_const(m_panels)) {
        const DockPanel *p = m_manager->panels.value(id);
        if (p && p->isPreviewTab())
            italic.insert(id);
    }
    m_tabBar->setItalicPanels(italic);
}

void DockTabGroup::refreshPanel(const PanelId &panel)
{
    updateTab(m_tabBar->indexOfPanel(panel));
    updateHeader();
    m_area->contentLimitsChanged(); // the header may have come or gone
}

void DockTabGroup::refreshAppearance()
{
    if (!m_manager)
        return;
    const int themed = m_manager->theme.iconSize;
    const int small = themed > 0 ? themed
                                 : style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    for (QToolButton *button : {m_menuButton, m_maximizeButton, m_floatButton, m_closeButton})
        button->setIconSize(QSize(small, small));
    m_closeButton->setIcon(m_manager->icon(DockIcon::Close, this));
    if (themed > 0)
        m_tabBar->setIconSize(QSize(themed, themed));
    m_menuButton->setIcon(m_manager->icon(DockIcon::Menu, this));
    m_maximizeButton->setIcon(
        m_manager->icon(m_maximized ? DockIcon::Restore : DockIcon::Maximize, this));
    const QString tip = m_maximized ? tr("Restore") : tr("Maximize");
    m_maximizeButton->setToolTip(tip);
    m_maximizeButton->setAccessibleName(tip);
    for (int i = 0; i < m_tabBar->count(); ++i)
        updateTab(i);
    updateHeader();
}

void DockTabGroup::syncContents()
{
    if (!m_manager)
        return;
    for (const PanelId &id : std::as_const(m_panels)) {
        DockPanel *panel = m_manager->panels.value(id);
        if (!panel)
            continue;
        // Only the visible panel forces a lazily created content into being.
        QWidget *widget = id == m_current ? m_manager->ensureWidget(panel)
                                          : DockManagerPrivate::get(panel)->widget.data();
        if (!widget)
            continue;
        if (widget->parentWidget() != m_host)
            m_manager->reparentContent(panel, m_host);
        if (id != m_current)
            widget->hide();
    }
    updateContentGeometry();
}

void DockTabGroup::updateContentGeometry()
{
    if (!m_manager)
        return;
    DockPanel *panel = m_manager->panels.value(m_current);
    QWidget *widget = panel ? DockManagerPrivate::get(panel)->widget.data() : nullptr;
    if (!widget || widget->parentWidget() != m_host)
        return;
    widget->setGeometry(m_host->rect());
    if (m_manager->contentSuspended(panel)) {
        widget->hide();
        return;
    }
    widget->show();
    widget->raise();
}

SizeLimits DockTabGroup::sizeLimits() const
{
    QSize contentMin(0, 0);
    QSize contentMax(UnboundedSize, UnboundedSize);
    if (m_manager) {
        for (const PanelId &id : m_panels) {
            DockPanel *panel = m_manager->panels.value(id);
            const QWidget *widget = panel ? DockManagerPrivate::get(panel)->widget.data() : nullptr;
            if (!widget)
                continue;
            contentMin = contentMin.expandedTo(effectiveMinimum(widget));
            if (id == m_current)
                contentMax = widget->maximumSize();
        }
    }
    contentMax = contentMax.expandedTo(contentMin);

    // The header: the title row, and tabs that are shown below the content.
    const bool tabsBelow = m_titleMode && !m_tabBar->isHidden();
    const int headerH = (m_titleBar->isHidden() ? 0 : m_titleBar->sizeHint().height())
        + (tabsBelow ? m_tabBar->sizeHint().height() : 0);
    const QMargins frame = contentsMargins();
    const int extraW = frame.left() + frame.right();
    const int extraH = frame.top() + frame.bottom() + headerH;
    const int titleMinW = m_titleBar->isHidden() ? 0 : m_titleBar->minimumSizeHint().width();

    SizeLimits limits;
    limits.min = QSize(qMax(contentMin.width(), titleMinW) + extraW, contentMin.height() + extraH);
    limits.max = QSize(contentMax.width() >= UnboundedSize ? UnboundedSize
                                                           : qMax(contentMax.width(), titleMinW) + extraW,
                       contentMax.height() >= UnboundedSize ? UnboundedSize
                                                            : contentMax.height() + extraH);
    return limits;
}

void DockTabGroup::showGroupMenu()
{
    if (!m_manager)
        return;
    DockPanel *panel = m_manager->panels.value(m_current);
    if (!panel)
        return;
    QMenu *menu = m_manager->createPanelMenu(panel, window());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (m_panels.size() > 1) {
        // Every tab of the group, reachable even when the bar overflows.
        menu->addSeparator();
        for (const PanelId &id : std::as_const(m_panels)) {
            DockPanel *p = m_manager->panels.value(id);
            if (!p)
                continue;
            QAction *action = menu->addAction(p->icon(), p->title());
            action->setCheckable(true);
            action->setChecked(id == m_current);
            DockManagerPrivate *manager = m_manager;
            connect(action, &QAction::triggered, menu, [manager, id] {
                (void)manager->activate(id, true);
            });
        }
    }
    menu->popup(m_menuButton->mapToGlobal(QPoint(0, m_menuButton->height())));
}

void DockTabGroup::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    switch (event->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::FontChange:
        // Icons and metrics come from the style and palette.
        refreshAppearance();
        m_area->contentLimitsChanged();
        break;
    default:
        break;
    }
}

bool DockTabGroup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_titleBar && titleBarEvent(event))
        return true;
    if (watched == m_host) {
        if (event->type() == QEvent::Resize) {
            updateContentGeometry();
        } else if (event->type() == QEvent::LayoutRequest) {
            // A content widget changed its size constraints.
            m_area->contentLimitsChanged();
        }
    }
    return QFrame::eventFilter(watched, event);
}

} // namespace QFlexDock
