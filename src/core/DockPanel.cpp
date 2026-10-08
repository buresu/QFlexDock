// SPDX-License-Identifier: MIT
#include <QFlexDock/DockPanel.h>

#include "core/DockManager_p.h"

#include <QtCore/QEvent>
#include <QtWidgets/QWidget>

namespace QFlexDock {

DockPanel::DockPanel(DockManager *manager, const PanelId &id)
    : QObject(manager)
    , d(std::make_unique<Private>())
{
    d->manager = manager;
    d->id = id;
    d->title = id;
}

DockPanel::~DockPanel() = default;

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
