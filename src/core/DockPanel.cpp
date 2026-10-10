// SPDX-License-Identifier: MIT
#include <QFlexDock/DockPanel.h>

#include "core/DockManager_p.h"

#include <QtCore/QEvent>
#include <QtWidgets/QWidget>

#include <algorithm>

namespace QFlexDock {

DockPanel::DockPanel(DockManager *manager, const PanelId &id)
    : QObject(manager)
    , d(std::make_unique<Private>())
{
    d->manager = manager;
    d->id = id;
    d->title = id;
}

DockPanel::~DockPanel()
{
    delete d->compactWidget.data();
}

PanelId DockPanel::id() const
{
    return d->id;
}

DockManager *DockPanel::manager() const
{
    return d->manager;
}

QString DockPanel::title() const
{
    return d->title;
}

void DockPanel::setTitle(const QString &title)
{
    if (d->title == title)
        return;
    d->title = title;
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT titleChanged(title);
}

QIcon DockPanel::icon() const
{
    return d->icon;
}

void DockPanel::setIcon(const QIcon &icon)
{
    d->icon = icon;
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT iconChanged();
}

QString DockPanel::toolTip() const
{
    return d->toolTip;
}

void DockPanel::setToolTip(const QString &toolTip)
{
    if (d->toolTip == toolTip)
        return;
    d->toolTip = toolTip;
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT toolTipChanged();
}

QWidget *DockPanel::widget() const
{
    return d->widget;
}

DockPolicy DockPanel::policy() const
{
    return d->policy;
}

void DockPanel::setPolicy(const DockPolicy &policy)
{
    if (d->policy == policy)
        return;
    d->policy = policy;
    // Whether the tab has a close button depends on it.
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT policyChanged();
}

void DockPanel::setFeatures(DockFeatures features)
{
    DockPolicy policy = d->policy;
    policy.features = features;
    setPolicy(policy);
}

bool DockPanel::isOpen() const
{
    return d->location.has_value();
}

bool DockPanel::isActive() const
{
    return d->active;
}

bool DockPanel::isFloating() const
{
    return d->location && d->location->isFloating();
}

bool DockPanel::isAutoHidden() const
{
    return d->location && d->location->isAutoHidden();
}

DockWorkspace *DockPanel::workspace() const
{
    if (!d->location)
        return nullptr;
    return DockManagerPrivate::get(d->manager)->workspaceFor(d->location->container);
}

bool DockPanel::isDirty() const
{
    return d->dirty;
}

void DockPanel::setDirty(bool dirty)
{
    if (d->dirty == dirty)
        return;
    d->dirty = dirty;
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT metadataChanged();
}

bool DockPanel::isPinnedTab() const
{
    return d->pinnedTab;
}

void DockPanel::setPinnedTab(bool pinned)
{
    if (d->pinnedTab == pinned)
        return;
    d->pinnedTab = pinned;
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT metadataChanged();
}

bool DockPanel::isPreviewTab() const
{
    return d->previewTab;
}

void DockPanel::setPreviewTab(bool preview)
{
    if (d->previewTab == preview)
        return;
    d->previewTab = preview;
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT metadataChanged();
}

bool DockPanel::hidesContentDuringDrag() const
{
    return d->hideContentDuringDrag;
}

bool DockPanel::hasTabCloseButton() const
{
    return d->tabCloseButton;
}

void DockPanel::setTabCloseButton(bool shown)
{
    if (d->tabCloseButton == shown)
        return;
    d->tabCloseButton = shown;
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT metadataChanged();
}

bool DockPanel::isHeaderVisible() const
{
    return d->headerVisible;
}

void DockPanel::setHeaderVisible(bool visible)
{
    if (d->headerVisible == visible)
        return;
    d->headerVisible = visible;
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT metadataChanged();
}

bool DockPanel::isCollapsible() const
{
    return d->collapsible;
}

void DockPanel::setCollapsible(bool collapsible)
{
    d->collapsible = collapsible;
}

QWidget *DockPanel::compactWidget() const
{
    return d->compactWidget;
}

void DockPanel::setCompactWidget(QWidget *widget)
{
    if (d->compactWidget == widget)
        return;
    DockManagerPrivate *manager = DockManagerPrivate::get(d->manager);
    if (QWidget *old = d->compactWidget.data()) {
        old->hide();
        old->setParent(manager->parkingWidget());
        old->deleteLater();
    }
    d->compactWidget = widget;
    if (widget) {
        // Until a strip of buttons shows it.
        widget->hide();
        widget->setParent(manager->parkingWidget());
    }
    manager->panelAppearanceChanged(this);
    Q_EMIT metadataChanged();
}

QList<QAction *> DockPanel::titleActions(DockTitlePlace place) const
{
    QList<QAction *> actions;
    for (const QPointer<QAction> &action : std::as_const(d->titleActions[size_t(place)])) {
        if (action)
            actions.append(action);
    }
    return actions;
}

void DockPanel::setTitleActions(const QList<QAction *> &actions, DockTitlePlace place)
{
    if (titleActions(place) == actions)
        return;
    using Lists = std::array<QList<QPointer<QAction>>, 3>;
    const auto uses = [](const Lists &lists, const QAction *action) {
        return std::any_of(lists.begin(), lists.end(),
                           [action](const auto &list) { return list.contains(action); });
    };
    const Lists before = d->titleActions;
    QList<QPointer<QAction>> &list = d->titleActions[size_t(place)];
    list.clear();
    for (QAction *action : actions) {
        if (action && !list.contains(action))
            list.append(action);
    }
    // An action's button goes with the action. (The same action may be in
    // more than one place; it is watched once, while it is in any.)
    for (const QPointer<QAction> &action : before[size_t(place)]) {
        if (action && !uses(d->titleActions, action))
            disconnect(action, &QObject::destroyed, this, nullptr);
    }
    for (const QPointer<QAction> &action : std::as_const(list)) {
        if (uses(before, action))
            continue;
        connect(action, &QObject::destroyed, this, [this] {
            DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
        });
    }
    DockManagerPrivate::get(d->manager)->panelAppearanceChanged(this);
    Q_EMIT metadataChanged();
}

void DockPanel::setHidesContentDuringDrag(bool hide)
{
    d->hideContentDuringDrag = hide;
}

void DockPanel::open()
{
    (void)d->manager->showPanel(d->id);
}

void DockPanel::close()
{
    (void)d->manager->hidePanel(d->id);
}

void DockPanel::toggle()
{
    (void)d->manager->togglePanel(d->id);
}

void DockPanel::activate()
{
    (void)d->manager->activatePanel(d->id);
}

// Watches the content widget, to report when it really appears on screen.
bool DockPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == d->widget && (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        const bool visible = event->type() == QEvent::Show;
        if (d->contentVisible != visible) {
            d->contentVisible = visible;
            Q_EMIT visibilityChanged(visible);
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace QFlexDock
