// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>
#include <QFlexDock/LayoutModel.h>

#include <QtWidgets/QWidget>

#include <memory>

namespace QFlexDock {

class DockManager;
class DockManagerPrivate;

/// The dock area of one window: shows one layout tree of tab groups and split
/// handles, plus the auto-hide bars along its borders.
///
/// Created by DockManager::createWorkspace() and then owned like any widget
/// (typically by the QMainWindow it is the central widget of). Destroying a
/// workspace closes its panels; the panels and their content stay registered
/// with the manager and can be shown elsewhere.
class QFLEXDOCK_EXPORT DockWorkspace : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString workspaceId READ workspaceId CONSTANT)

public:
    ~DockWorkspace() override;

    [[nodiscard]] QString workspaceId() const;
    /// Null once the manager has been destroyed.
    [[nodiscard]] DockManager *manager() const;

    /// Same as DockManager::addPanel(id, this, area, fraction).
    DockResult addPanel(const PanelId &id, DockArea area = DockArea::Center,
                        double fraction = -1.0);

    /// Panels docked in this workspace, in layout order. Floating and
    /// auto-hidden panels are not included.
    [[nodiscard]] QStringList panels() const;
    /// A copy of the current layout tree.
    [[nodiscard]] LayoutTree layoutTree() const;

private:
    friend class DockManager;
    friend class DockManagerPrivate;
    struct Private;
    DockWorkspace(DockManager *manager, const QString &id, QWidget *parent);
    std::unique_ptr<Private> d;
};

} // namespace QFlexDock
