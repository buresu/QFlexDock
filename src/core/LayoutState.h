// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/LayoutModel.h>

#include <QtCore/QHash>
#include <QtCore/QRect>

#include <array>
#include <optional>
#include <vector>

namespace QFlexDock {

enum class ContainerKind { Workspace, Floating };

/// Fixed order of the four borders, used to index per-edge arrays.
inline constexpr std::array<DockArea, 4> DockEdges{DockArea::Left, DockArea::Right, DockArea::Top,
                                                   DockArea::Bottom};

/// Index of `edge` in DockEdges, or -1.
constexpr int edgeIndex(DockArea edge)
{
    for (int i = 0; i < 4; ++i) {
        if (DockEdges[size_t(i)] == edge)
            return i;
    }
    return -1;
}

/// Where a panel that is not docked right now should go back to. Recorded
/// when a panel is closed, floated, auto-hidden or becomes unavailable.
struct PanelMemory
{
    QString container;
    bool floating = false;
    /// For a floating container: its owner workspace and window geometry.
    QString owner;
    QRect geometry;
    /// The other panels of its tab group, and its index among them.
    QStringList tabSiblings;
    int tabIndex = -1;
    /// It was the tab in front.
    bool front = false;
    /// Panels of the node it was split off from, and its side of that node.
    QStringList neighbors;
    DockArea neighborArea = DockArea::None;
    /// Its share of the split it was a child of.
    double fraction = 0.5;
    /// Set when the panel was closed while sitting in an auto-hide bar.
    DockArea autoHideEdge = DockArea::None;
    /// The panel was open when it became unavailable (unregistered, or missing
    /// while restoring); put it back as soon as it is registered.
    bool reopen = false;
};

/// The layout of one container: a workspace or a floating window.
struct ContainerState
{
    QString id;
    ContainerKind kind = ContainerKind::Workspace;
    LayoutTree tree;
    /// Display state only: this panel's tab group fills the container.
    PanelId maximized;
    /// Workspace only: panels collapsed into the bar of each border.
    std::array<QStringList, 4> autoHide;
    /// Floating only: owner workspace (may be empty) and window geometry.
    QString owner;
    QRect geometry;
};

struct PanelLocation
{
    QString container;
    ContainerKind kind = ContainerKind::Workspace;
    /// Tab group the panel is in; null when it sits in an auto-hide bar.
    NodeId node;
    DockArea autoHideEdge = DockArea::None;

    [[nodiscard]] bool isAutoHidden() const { return autoHideEdge != DockArea::None; }
    [[nodiscard]] bool isFloating() const { return kind == ContainerKind::Floating; }
    [[nodiscard]] bool isDocked() const { return !isFloating() && !isAutoHidden(); }

    friend bool operator==(const PanelLocation &, const PanelLocation &) = default;
};

/// The node an absent panel would be put beside, and on which side of it.
struct ReturnPlace
{
    QString container;
    NodeId anchor;
    DockArea side = DockArea::None;
};

/// The complete docking state of an application as one value: every
/// container's tree plus the memory of where absent panels belong.
///
/// DockManager holds exactly one of these. A change is made on a copy, checked
/// with validate() and swapped in, so views never observe a half-done change
/// and a failed change needs no undoing. Undo steps, presets and saved layouts
/// are all just copies of this value.
class QFLEXDOCK_EXPORT LayoutState
{
public:
    std::vector<ContainerState> containers;
    QHash<PanelId, PanelMemory> memory;

    [[nodiscard]] ContainerState *find(const QString &containerId);
    [[nodiscard]] const ContainerState *find(const QString &containerId) const;
    [[nodiscard]] std::optional<PanelLocation> locate(const PanelId &panel) const;
    [[nodiscard]] bool isPlaced(const PanelId &panel) const { return locate(panel).has_value(); }
    /// Every placed panel: docked, floating and auto-hidden.
    [[nodiscard]] QStringList placedPanels() const;

    /// Adds an empty floating container. `preferredId` is used if it is free.
    /// Invalidates pointers obtained from find().
    ContainerState &addFloating(const QString &owner, const QRect &geometry,
                                const QString &preferredId = {});

    /// Where `panel` is now, expressed as a memory to return to later.
    [[nodiscard]] PanelMemory capture(const PanelId &panel) const;
    /// Removes the panel from wherever it is; with `remember` its position is
    /// stored in `memory`.
    DockResult detach(const PanelId &panel, bool remember);
    DockResult attach(const PanelId &panel, const QString &containerId, NodeId target,
                      DockArea area, int tabIndex = -1, double fraction = 0.5);
    /// Places an absent panel according to its memory: next to its former tab
    /// siblings, else beside its former neighbours, else in a new floating
    /// window if that is where it was, else as a tab in `fallbackWorkspace`.
    DockResult reattach(const PanelId &panel, const QString &fallbackWorkspace);
    /// The same for several panels at once, in the order that gives tab
    /// groups their former order, with the tab in front that was in front.
    DockResult reattachAll(const QStringList &panels, const QString &fallbackWorkspace);
    /// Where reattach() would put the panel, if that is beside a node of a
    /// container that exists: not into a tab group that is still there, an
    /// auto-hide bar or a floating window that is gone.
    [[nodiscard]] std::optional<ReturnPlace> returnPlace(const PanelId &panel) const;

    /// Normalizes all trees, drops emptied floating containers and stale
    /// display state.
    void normalize();
    [[nodiscard]] DockResult validate() const;
};

} // namespace QFlexDock
