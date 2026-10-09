// SPDX-License-Identifier: MIT
#include "core/LayoutState.h"

#include <QtCore/QSet>

#include <algorithm>

namespace QFlexDock {

namespace {

DockResult fail(DockError error, const QString &message)
{
    return DockResult::failure(error, message);
}

void collectPanels(const LayoutNode &node, QStringList &out)
{
    if (node.isTabs())
        out += node.panels;
    for (const auto &child : node.children)
        collectPanels(child, out);
}

// Counts the panels of `wanted` below `node`; `found` receives the deepest
// node that holds all of them.
int findCommonAncestor(const LayoutNode &node, const QSet<PanelId> &wanted, NodeId &found)
{
    int count = 0;
    if (node.isTabs()) {
        for (const PanelId &panel : node.panels)
            count += wanted.contains(panel) ? 1 : 0;
    } else {
        for (const auto &child : node.children) {
            count += findCommonAncestor(child, wanted, found);
            if (!found.isNull())
                return count;
        }
    }
    if (count == wanted.size())
        found = node.id;
    return count;
}

// The node of `tree` that holds what is left of the neighbours a panel
// remembers; null if none of them is there.
NodeId anchorOf(const LayoutTree &tree, const PanelMemory &m)
{
    QSet<PanelId> present;
    for (const PanelId &neighbor : m.neighbors) {
        if (tree.containsPanel(neighbor))
            present.insert(neighbor);
    }
    NodeId anchor;
    if (!present.isEmpty() && tree.root())
        findCommonAncestor(*tree.root(), present, anchor);
    return anchor;
}

} // namespace

ContainerState *LayoutState::find(const QString &containerId)
{
    for (auto &c : containers) {
        if (c.id == containerId)
            return &c;
    }
    return nullptr;
}

const ContainerState *LayoutState::find(const QString &containerId) const
{
    return const_cast<LayoutState *>(this)->find(containerId);
}

std::optional<PanelLocation> LayoutState::locate(const PanelId &panel) const
{
    for (const auto &c : containers) {
        if (const LayoutNode *group = c.tree.findPanel(panel))
            return PanelLocation{c.id, c.kind, group->id, DockArea::None};
        for (int i = 0; i < 4; ++i) {
            if (c.autoHide[size_t(i)].contains(panel))
                return PanelLocation{c.id, c.kind, NodeId{}, DockEdges[size_t(i)]};
        }
    }
    return std::nullopt;
}

QStringList LayoutState::placedPanels() const
{
    QStringList result;
    for (const auto &c : containers) {
        result += c.tree.panels();
        for (const QStringList &bar : c.autoHide)
            result += bar;
    }
    return result;
}

ContainerState &LayoutState::addFloating(const QString &owner, const QRect &geometry,
                                         const QString &preferredId)
{
    QString id = preferredId;
    for (int n = 1; id.isEmpty() || find(id); ++n)
        id = QStringLiteral("floating-%1").arg(n);

    ContainerState container;
    container.id = id;
    container.kind = ContainerKind::Floating;
    container.owner = owner;
    container.geometry = geometry;
    containers.push_back(std::move(container));
    return containers.back();
}

PanelMemory LayoutState::capture(const PanelId &panel) const
{
    const std::optional<PanelLocation> location = locate(panel);
    if (!location)
        return memory.value(panel);

    const ContainerState *container = find(location->container);
    PanelMemory m;
    if (location->isAutoHidden()) {
        // Keep what we know about where it was docked before it was collapsed.
        m = memory.value(panel);
        m.autoHideEdge = location->autoHideEdge;
    }
    m.container = container->id;
    m.floating = container->kind == ContainerKind::Floating;
    m.owner = container->owner;
    m.geometry = container->geometry;
    m.reopen = false;
    if (location->isAutoHidden())
        return m;

    const LayoutNode *group = container->tree.findNode(location->node);
    m.front = group->active == panel;
    m.tabIndex = int(group->panels.indexOf(panel));
    m.tabSiblings = group->panels;
    m.tabSiblings.removeAll(panel);

    if (const LayoutNode *parent = container->tree.parentOf(group->id)) {
        const auto it = std::find_if(parent->children.begin(), parent->children.end(),
                                     [&](const LayoutNode &c) { return c.id == group->id; });
        const bool first = it == parent->children.begin();
        const LayoutNode &sibling = first ? *(it + 1) : *(it - 1);
        collectPanels(sibling, m.neighbors);
        if (parent->orientation == Qt::Horizontal)
            m.neighborArea = first ? DockArea::Left : DockArea::Right;
        else
            m.neighborArea = first ? DockArea::Top : DockArea::Bottom;
        m.fraction = group->weight;
    }
    return m;
}

DockResult LayoutState::detach(const PanelId &panel, bool remember)
{
    const std::optional<PanelLocation> location = locate(panel);
    if (!location)
        return fail(DockError::NotPlaced, QStringLiteral("panel '%1' is not placed").arg(panel));
    if (remember)
        memory.insert(panel, capture(panel));

    ContainerState *container = find(location->container);
    if (location->isAutoHidden()) {
        container->autoHide[size_t(edgeIndex(location->autoHideEdge))].removeAll(panel);
    } else if (DockResult r = container->tree.removePanel(panel); !r) {
        return r;
    }
    normalize();
    return DockResult::success();
}

DockResult LayoutState::attach(const PanelId &panel, const QString &containerId, NodeId target,
                               DockArea area, int tabIndex, double fraction)
{
    if (isPlaced(panel))
        return fail(DockError::DuplicatePanel,
                    QStringLiteral("panel '%1' is already placed").arg(panel));
    ContainerState *container = find(containerId);
    if (!container)
        return fail(DockError::UnknownWorkspace,
                    QStringLiteral("no container '%1'").arg(containerId));
    return container->tree.insertPanel(panel, target, area, tabIndex, fraction);
}

DockResult LayoutState::reattach(const PanelId &panel, const QString &fallbackWorkspace)
{
    if (isPlaced(panel))
        return fail(DockError::DuplicatePanel,
                    QStringLiteral("panel '%1' is already placed").arg(panel));
    const PanelMemory m = memory.value(panel);

    // Back into the auto-hide bar it was closed from.
    if (m.autoHideEdge != DockArea::None) {
        ContainerState *container = find(m.container);
        if (container && container->kind == ContainerKind::Workspace) {
            container->autoHide[size_t(edgeIndex(m.autoHideEdge))].append(panel);
            memory[panel].autoHideEdge = DockArea::None;
            memory[panel].reopen = false;
            return DockResult::success();
        }
    }

    const auto finish = [&](DockResult result) {
        if (result) {
            memory.remove(panel);
            normalize();
        }
        return result;
    };

    // Next to a former tab sibling, wherever that is now.
    for (const PanelId &sibling : m.tabSiblings) {
        for (auto &container : containers) {
            if (const LayoutNode *group = container.tree.findPanel(sibling)) {
                const int index = std::clamp(m.tabIndex, 0, int(group->panels.size()));
                return finish(container.tree.insertPanel(panel, group->id, DockArea::Center, index));
            }
        }
    }

    ContainerState *container = find(m.container);
    if (!container && m.floating) {
        // It was in a floating window that is gone: give it a new one.
        const QString owner = find(m.owner) ? m.owner : fallbackWorkspace;
        ContainerState &floating = addFloating(owner, m.geometry, m.container);
        return finish(floating.tree.insertPanel(panel, {}, DockArea::Center));
    }
    if (!container)
        container = find(fallbackWorkspace);
    if (!container)
        return fail(DockError::UnknownWorkspace,
                    QStringLiteral("there is no workspace to show panel '%1' in").arg(panel));

    // Beside the node it used to be split off from.
    const NodeId anchor = m.neighborArea != DockArea::None ? anchorOf(container->tree, m) : NodeId();
    if (!anchor.isNull()) {
        // The panel gets the share it had of the split it was in. Where that
        // split is still there, its room was left to the neighbour, and is
        // now taken out of the neighbour's share: everything else in the
        // split keeps its size, whatever came and went in the meantime.
        double fraction = m.fraction;
        const LayoutNode *node = container->tree.findNode(anchor);
        const LayoutNode *parent = container->tree.parentOf(anchor);
        const Qt::Orientation along = splitOrientation(m.neighborArea);
        const bool joinsAnchor = node->isSplit() && node->orientation == along;
        if (!joinsAnchor && parent && parent->orientation == along)
            fraction = std::min(m.fraction / node->weight, 0.9);
        return finish(container->tree.insertPanel(panel, anchor, m.neighborArea, -1, fraction));
    }

    return finish(container->tree.insertPanel(panel, {}, DockArea::Center));
}

DockResult LayoutState::reattachAll(const QStringList &panels, const QString &fallbackWorkspace)
{
    QStringList absent;
    QStringList fronts;
    for (const PanelId &panel : panels) {
        if (isPlaced(panel) || absent.contains(panel))
            continue;
        absent.append(panel);
        if (memory.value(panel).front)
            fronts.append(panel);
    }
    // One that remembers fewer tabs beside it left its group later and has to
    // be back earlier, for the others to find it; among those that left
    // together, the order of the tabs.
    std::stable_sort(absent.begin(), absent.end(), [this](const PanelId &a, const PanelId &b) {
        const PanelMemory ma = memory.value(a);
        const PanelMemory mb = memory.value(b);
        if (ma.tabSiblings.size() != mb.tabSiblings.size())
            return ma.tabSiblings.size() < mb.tabSiblings.size();
        return ma.tabIndex < mb.tabIndex;
    });
    for (const PanelId &panel : std::as_const(absent)) {
        if (DockResult r = reattach(panel, fallbackWorkspace); !r)
            return r;
    }
    for (const PanelId &panel : std::as_const(fronts)) {
        const std::optional<PanelLocation> location = locate(panel);
        if (location && !location->isAutoHidden())
            (void)find(location->container)->tree.setActivePanel(panel);
    }
    return DockResult::success();
}

std::optional<ReturnPlace> LayoutState::returnPlace(const PanelId &panel) const
{
    const auto it = memory.constFind(panel);
    if (it == memory.constEnd() || isPlaced(panel) || it->autoHideEdge != DockArea::None
        || it->neighborArea == DockArea::None) {
        return std::nullopt;
    }
    for (const PanelId &sibling : it->tabSiblings) {
        if (isPlaced(sibling))
            return std::nullopt;
    }
    const ContainerState *container = find(it->container);
    if (!container || !container->tree.root())
        return std::nullopt;
    const NodeId anchor = anchorOf(container->tree, *it);
    if (anchor.isNull())
        return std::nullopt;
    return ReturnPlace{container->id, anchor, it->neighborArea};
}

void LayoutState::normalize()
{
    for (auto &c : containers) {
        c.tree.normalize();
        if (!c.maximized.isEmpty() && !c.tree.containsPanel(c.maximized))
            c.maximized.clear();
        for (QStringList &bar : c.autoHide) {
            if (c.kind == ContainerKind::Floating)
                bar.clear();
            bar.removeDuplicates();
        }
    }
    std::erase_if(containers, [](const ContainerState &c) {
        return c.kind == ContainerKind::Floating && c.tree.isEmpty();
    });
    // A docked panel has nothing to return to.
    for (auto it = memory.begin(); it != memory.end();) {
        const std::optional<PanelLocation> location = locate(it.key());
        if (location && location->isDocked())
            it = memory.erase(it);
        else
            ++it;
    }
}

DockResult LayoutState::validate() const
{
    QSet<QString> ids;
    QSet<PanelId> panels;
    for (const auto &c : containers) {
        if (c.id.isEmpty())
            return fail(DockError::InvalidLayout, QStringLiteral("container without id"));
        if (ids.contains(c.id))
            return fail(DockError::InvalidLayout,
                        QStringLiteral("duplicate container id '%1'").arg(c.id));
        ids.insert(c.id);

        if (DockResult r = c.tree.validate(); !r)
            return r;
        QStringList placed = c.tree.panels();
        for (const QStringList &bar : c.autoHide) {
            if (c.kind == ContainerKind::Floating && !bar.isEmpty())
                return fail(DockError::InvalidLayout,
                            QStringLiteral("floating window with auto-hide panels"));
            placed += bar;
        }
        for (const PanelId &panel : std::as_const(placed)) {
            if (panel.isEmpty())
                return fail(DockError::InvalidLayout, QStringLiteral("empty panel id"));
            if (panels.contains(panel)) {
                return fail(DockError::DuplicatePanel,
                            QStringLiteral("panel '%1' is placed more than once").arg(panel));
            }
            panels.insert(panel);
        }
        if (c.kind == ContainerKind::Floating && c.tree.isEmpty())
            return fail(DockError::InvalidLayout, QStringLiteral("empty floating window"));
        if (!c.maximized.isEmpty() && !c.tree.containsPanel(c.maximized))
            return fail(DockError::InvalidLayout,
                        QStringLiteral("maximized panel is not in its container"));
    }
    return DockResult::success();
}

} // namespace QFlexDock
