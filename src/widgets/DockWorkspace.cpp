// SPDX-License-Identifier: MIT
#include <QFlexDock/DockWorkspace.h>

#include "core/DockManager_p.h"
#include "widgets/DockAreaWidget.h"
#include "widgets/DockAutoHide.h"

#include <QtWidgets/QGridLayout>

namespace QFlexDock {

// What the headers of the tab groups show depends on it.
void DockWorkspace::Private::settingChanged() const
{
    if (manager)
        DockManagerPrivate::get(manager)->refreshAllAppearance();
}

DockWorkspace::DockWorkspace(DockManager *manager, const QString &id, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->manager = manager;
    d->id = id;
    DockManagerPrivate *m = DockManagerPrivate::get(manager);
    d->area = new DockAreaWidget(m, id, this);
    d->autoHide = new DockAutoHideContainer(m, this, d->area);

    // Auto-hide bars around the dock area; they are hidden while empty. The
    // rows and columns between a bar and the area are empty: room for a
    // panel that is out beside the area (DockAutoHideContainer).
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(d->autoHide->bar(DockArea::Top), 0, 2);
    grid->addWidget(d->autoHide->bar(DockArea::Left), 2, 0);
    grid->addWidget(d->area, 2, 2);
    grid->addWidget(d->autoHide->bar(DockArea::Right), 2, 4);
    grid->addWidget(d->autoHide->bar(DockArea::Bottom), 4, 2);
    grid->setRowStretch(2, 1);
    grid->setColumnStretch(2, 1);
}

DockWorkspace::~DockWorkspace()
{
    // Our child widgets are about to be destroyed; the manager moves the
    // panels' content out first.
    if (d->manager)
        DockManagerPrivate::get(d->manager)->workspaceDestroyed(this);
}

QString DockWorkspace::workspaceId() const
{
    return d->id;
}

DockManager *DockWorkspace::manager() const
{
    return d->manager;
}

DockResult DockWorkspace::addPanel(const PanelId &id, DockArea area, double fraction)
{
    if (!d->manager) {
        return DockResult::failure(DockError::UnknownWorkspace,
                                   QStringLiteral("the workspace has no manager any more"));
    }
    return d->manager->movePanel(id, this, area, fraction);
}

QStringList DockWorkspace::panels() const
{
    return d->area->tree().panels();
}

PanelId DockWorkspace::maximizedPanel() const
{
    if (!d->manager)
        return {};
    const ContainerState *container = DockManagerPrivate::get(d->manager)->state.find(d->id);
    return container ? container->maximized : PanelId();
}

bool DockWorkspace::isColumnDocking() const
{
    return d->columnDocking;
}

void DockWorkspace::setColumnDocking(bool enabled)
{
    if (d->columnDocking == enabled)
        return;
    d->columnDocking = enabled;
    if (d->manager)
        DockManagerPrivate::get(d->manager)->columnDockingChanged(this);
}

DockGroupHeader DockWorkspace::groupHeader() const
{
    if (d->groupHeader || !d->manager)
        return d->groupHeader.value_or(DockGroupHeader::Tabs);
    return d->manager->groupHeader();
}

void DockWorkspace::setGroupHeader(DockGroupHeader header)
{
    if (d->groupHeader == header)
        return;
    d->groupHeader = header;
    d->settingChanged();
}

void DockWorkspace::unsetGroupHeader()
{
    if (!d->groupHeader)
        return;
    d->groupHeader.reset();
    d->settingChanged();
}

DockTitleButtons DockWorkspace::titleButtons() const
{
    if (d->titleButtons || !d->manager)
        return d->titleButtons.value_or(DockTheme().titleButtons);
    return d->manager->theme().titleButtons;
}

void DockWorkspace::setTitleButtons(DockTitleButtons buttons)
{
    if (d->titleButtons == buttons)
        return;
    d->titleButtons = buttons;
    d->settingChanged();
}

void DockWorkspace::unsetTitleButtons()
{
    if (!d->titleButtons)
        return;
    d->titleButtons.reset();
    d->settingChanged();
}

bool DockWorkspace::isCenterDropEnabled() const
{
    if (d->centerDrop || !d->manager)
        return d->centerDrop.value_or(true);
    return d->manager->isCenterDropEnabled();
}

void DockWorkspace::setCenterDropEnabled(bool enabled)
{
    d->centerDrop = enabled;
}

void DockWorkspace::unsetCenterDropEnabled()
{
    d->centerDrop.reset();
}

} // namespace QFlexDock
