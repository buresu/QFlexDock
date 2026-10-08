// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#include <functional>

namespace QFlexDock {

/// Per-panel rules for user interaction.
struct QFLEXDOCK_EXPORT DockPolicy
{
    DockFeatures features = AllDockFeatures;
    /// Areas of a drop target this panel may be dropped on.
    DockAreas allowedAreas = AllDockAreas;
    /// Workspaces (by id) the panel may be dropped into; empty means any.
    /// Floating windows count as their owner workspace.
    QStringList allowedWorkspaces;

    friend bool operator==(const DockPolicy &, const DockPolicy &) = default;
};

/// A drop the user is about to make, handed to the drop filter.
struct QFLEXDOCK_EXPORT DockDropRequest
{
    /// The dragged panels (one, or all panels of a dragged tab group).
    QStringList panels;
    /// Target workspace; for a floating window, its owner workspace.
    QString workspaceId;
    bool intoFloatingWindow = false;
    /// A panel of the target tab group; empty when docking onto the workspace
    /// as a whole (outer edge or empty workspace).
    PanelId targetPanel;
    DockArea area = DockArea::None;
};

/// Application-wide veto on top of the per-panel policies. Return false to
/// refuse the drop. Must not modify the layout.
using DockDropFilter = std::function<bool(const DockDropRequest &)>;

} // namespace QFlexDock
