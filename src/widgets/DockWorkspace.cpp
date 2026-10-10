// SPDX-License-Identifier: MIT
#include <QFlexDock/DockWorkspace.h>

#include "core/DockManager_p.h"
#include "widgets/DockAreaWidget.h"
#include "widgets/DockAutoHide.h"

#include <QtWidgets/QGridLayout>

namespace QFlexDock {

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
    return d->manager->addPanel(id, this, area, fraction);
}

QStringList DockWorkspace::panels() const
{
    return d->area->tree().panels();
}

LayoutTree DockWorkspace::layoutTree() const
{
    return d->area->tree();
}

} // namespace QFlexDock
