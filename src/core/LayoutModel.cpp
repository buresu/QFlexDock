// SPDX-License-Identifier: MIT
#include <QFlexDock/LayoutModel.h>

#include <QtCore/QSet>

#include <algorithm>
#include <cmath>

namespace QFlexDock {

namespace {

DockResult fail(DockError error, const QString &message)
{
    return DockResult::failure(error, message);
}

bool validWeight(double w)
{
    return std::isfinite(w) && w > 0.0;
}

template <typename Node, typename Fn>
void visit(Node &node, Fn &&fn)
{
    fn(node);
    for (auto &child : node.children)
        visit(child, fn);
}

template <typename Node>
Node *findIn(Node &node, NodeId id)
{
    if (node.id == id)
        return &node;
    for (auto &child : node.children) {
        if (Node *found = findIn(child, id))
            return found;
    }
    return nullptr;
}

template <typename Node>
Node *findParentIn(Node &node, NodeId id)
{
    for (auto &child : node.children) {
        if (child.id == id)
            return &node;
        if (Node *found = findParentIn(child, id))
            return found;
    }
    return nullptr;
}

template <typename Node>
Node *findPanelIn(Node &node, const PanelId &panel)
{
    if (node.isTabs())
        return node.panels.contains(panel) ? &node : nullptr;
    for (auto &child : node.children) {
        if (Node *found = findPanelIn(child, panel))
            return found;
    }
    return nullptr;
}

LayoutNode *firstTabNode(LayoutNode &node)
{
    if (node.isTabs())
        return &node;
    for (auto &child : node.children) {
        if (LayoutNode *found = firstTabNode(child))
            return found;
    }
    return nullptr;
}

int depthOf(const LayoutNode &node)
{
    int deepest = 0;
    for (const auto &child : node.children)
        deepest = std::max(deepest, depthOf(child));
    return deepest + 1;
}

// Brings `node` into normal form. Returns false when nothing is left of it.
bool normalizeNode(LayoutNode &node)
{
    if (node.isTabs()) {
        node.children.clear();
        node.panels.removeDuplicates();
        if (node.panels.isEmpty())
            return false;
        if (!node.panels.contains(node.active))
            node.active = node.panels.first();
        return true;
    }

    node.panels.clear();
    node.active.clear();

    // A child with nothing left in it leaves its share to the neighbour before
    // it (after it, for the first one). The other children keep theirs, so
    // putting the panel back beside that neighbour restores every size.
    std::vector<LayoutNode> alive;
    alive.reserve(node.children.size());
    double orphaned = 0.0;
    for (auto &child : node.children) {
        const double weight = validWeight(child.weight) ? child.weight : 1.0;
        if (!normalizeNode(child)) {
            if (alive.empty())
                orphaned += weight;
            else
                alive.back().weight += weight;
            continue;
        }
        child.weight = weight + orphaned;
        orphaned = 0.0;
        alive.push_back(std::move(child));
    }

    std::vector<LayoutNode> kept;
    kept.reserve(alive.size());
    for (auto &child : alive) {
        if (child.isSplit() && child.orientation == node.orientation) {
            // Same direction as us: adopt the grandchildren, each keeping its
            // share of the space the child had.
            double sum = 0.0;
            for (const auto &grandchild : child.children)
                sum += grandchild.weight;
            for (auto &grandchild : child.children) {
                grandchild.weight = child.weight * grandchild.weight / sum;
                kept.push_back(std::move(grandchild));
            }
        } else {
            kept.push_back(std::move(child));
        }
    }
    node.children = std::move(kept);

    if (node.children.empty())
        return false;
    if (node.children.size() == 1) {
        LayoutNode only = std::move(node.children.front());
        only.weight = node.weight;
        node = std::move(only);
        return true;
    }

    double sum = 0.0;
    for (const auto &child : node.children)
        sum += child.weight;
    for (auto &child : node.children)
        child.weight /= sum;
    return true;
}

struct ValidationState
{
    QSet<NodeId> ids;
    QSet<PanelId> panels;
};

DockResult validateNode(const LayoutNode &node, int depth, ValidationState &state)
{
    if (depth > LayoutTree::MaxDepth)
        return fail(DockError::InvalidLayout, QStringLiteral("layout tree is too deep"));
    if (node.id.isNull())
        return fail(DockError::InvalidLayout, QStringLiteral("node without id"));
    if (state.ids.contains(node.id))
        return fail(DockError::InvalidLayout, QStringLiteral("duplicate node id"));
    state.ids.insert(node.id);
    if (!validWeight(node.weight))
        return fail(DockError::InvalidLayout, QStringLiteral("node weight must be positive"));

    if (node.isTabs()) {
        if (!node.children.empty())
            return fail(DockError::InvalidLayout, QStringLiteral("tab group with children"));
        if (node.panels.isEmpty())
            return fail(DockError::InvalidLayout, QStringLiteral("empty tab group"));
        for (const PanelId &panel : node.panels) {
            if (panel.isEmpty())
                return fail(DockError::InvalidLayout, QStringLiteral("empty panel id"));
            if (state.panels.contains(panel)) {
                return fail(DockError::DuplicatePanel,
                            QStringLiteral("panel '%1' is placed more than once").arg(panel));
            }
            state.panels.insert(panel);
        }
        if (!node.panels.contains(node.active))
            return fail(DockError::InvalidLayout,
                        QStringLiteral("active panel is not part of its tab group"));
        return DockResult::success();
    }

    if (!node.panels.isEmpty())
        return fail(DockError::InvalidLayout, QStringLiteral("split with panels"));
    if (node.children.size() < 2)
        return fail(DockError::InvalidLayout, QStringLiteral("split with fewer than two children"));
    double sum = 0.0;
    for (const auto &child : node.children) {
        if (child.isSplit() && child.orientation == node.orientation)
            return fail(DockError::InvalidLayout,
                        QStringLiteral("nested split of the same orientation"));
        if (DockResult r = validateNode(child, depth + 1, state); !r)
            return r;
        sum += child.weight;
    }
    if (std::abs(sum - 1.0) > 1e-6)
        return fail(DockError::InvalidLayout, QStringLiteral("split weights do not sum to 1"));
    return DockResult::success();
}

} // namespace

LayoutNode LayoutNode::makeTabs(const QStringList &panels, const PanelId &active)
{
    LayoutNode node;
    node.id = NodeId::create();
    node.type = Type::Tabs;
    node.panels = panels;
    node.active = panels.contains(active) ? active : panels.value(0);
    return node;
}

LayoutNode LayoutNode::makeSplit(Qt::Orientation orientation, std::vector<LayoutNode> children)
{
    LayoutNode node;
    node.id = NodeId::create();
    node.type = Type::Split;
    node.orientation = orientation;
    node.children = std::move(children);
    return node;
}

LayoutTree::LayoutTree(LayoutNode root)
    : m_root(std::move(root))
{
    normalize();
}

const LayoutNode *LayoutTree::findNode(NodeId id) const
{
    return m_root ? findIn(*m_root, id) : nullptr;
}

LayoutNode *LayoutTree::findNodeMutable(NodeId id)
{
    return m_root ? findIn(*m_root, id) : nullptr;
}

const LayoutNode *LayoutTree::parentOf(NodeId id) const
{
    return m_root ? findParentIn(*m_root, id) : nullptr;
}

const LayoutNode *LayoutTree::findPanel(const PanelId &panel) const
{
    return m_root ? findPanelIn(*m_root, panel) : nullptr;
}

LayoutNode *LayoutTree::findPanelMutable(const PanelId &panel)
{
    return m_root ? findPanelIn(*m_root, panel) : nullptr;
}

QStringList LayoutTree::panels() const
{
    QStringList result;
    if (m_root) {
        visit(*m_root, [&result](const LayoutNode &node) {
            if (node.isTabs())
                result += node.panels;
        });
    }
    return result;
}

std::vector<const LayoutNode *> LayoutTree::tabNodes() const
{
    std::vector<const LayoutNode *> result;
    if (m_root) {
        visit(*m_root, [&result](const LayoutNode &node) {
            if (node.isTabs())
                result.push_back(&node);
        });
    }
    return result;
}

int LayoutTree::nodeCount() const
{
    int count = 0;
    if (m_root)
        visit(*m_root, [&count](const LayoutNode &) { ++count; });
    return count;
}

int LayoutTree::depth() const
{
    return m_root ? depthOf(*m_root) : 0;
}

DockResult LayoutTree::insertNode(LayoutNode node, NodeId target, DockArea area, int tabIndex,
                                  double fraction)
{
    if (area == DockArea::None)
        return fail(DockError::InvalidArgument, QStringLiteral("no dock area given"));
    if (!normalizeNode(node))
        return fail(DockError::InvalidArgument, QStringLiteral("nothing to insert"));
    if (!std::isfinite(fraction))
        fraction = 0.5;
    fraction = std::clamp(fraction, 0.02, 0.98);

    // The inserted subtree must not clash with what is already here.
    DockResult clash = DockResult::success();
    visit(std::as_const(node), [&](const LayoutNode &n) {
        if (!clash)
            return;
        if (findNode(n.id)) {
            clash = fail(DockError::InvalidArgument, QStringLiteral("node is already in the tree"));
            return;
        }
        for (const PanelId &panel : n.panels) {
            if (containsPanel(panel)) {
                clash = fail(DockError::DuplicatePanel,
                             QStringLiteral("panel '%1' is already placed").arg(panel));
                return;
            }
        }
    });
    if (!clash)
        return clash;

    LayoutTree work = *this;

    if (work.isEmpty()) {
        if (!target.isNull())
            return fail(DockError::UnknownNode, QStringLiteral("target node does not exist"));
        work.m_root = std::move(node);
    } else if (area == DockArea::Center) {
        if (!node.isTabs())
            return fail(DockError::InvalidArgument,
                        QStringLiteral("only a tab group can be merged into another one"));
        LayoutNode *group = target.isNull() ? firstTabNode(*work.m_root)
                                            : work.findNodeMutable(target);
        if (!group)
            return fail(DockError::UnknownNode, QStringLiteral("target node does not exist"));
        if (!group->isTabs())
            return fail(DockError::InvalidArgument, QStringLiteral("target is not a tab group"));
        qsizetype index = tabIndex;
        if (index < 0 || index > group->panels.size())
            index = group->panels.size();
        for (const PanelId &panel : std::as_const(node.panels))
            group->panels.insert(index++, panel);
        group->active = node.active;
    } else {
        LayoutNode *anchor = target.isNull() ? &*work.m_root : work.findNodeMutable(target);
        if (!anchor)
            return fail(DockError::UnknownNode, QStringLiteral("target node does not exist"));
        const Qt::Orientation orientation = splitOrientation(area);
        const bool before = area == DockArea::Left || area == DockArea::Top;

        if (anchor->isSplit() && anchor->orientation == orientation) {
            // Along the outside of a split running the same way: become its
            // first/last child, taking `fraction` of the whole.
            double sum = 0.0;
            for (const auto &child : anchor->children)
                sum += child.weight;
            node.weight = sum * fraction / (1.0 - fraction);
            anchor->children.insert(before ? anchor->children.begin() : anchor->children.end(),
                                    std::move(node));
        } else if (LayoutNode *parent = findParentIn(*work.m_root, anchor->id);
                   parent && parent->orientation == orientation) {
            // The anchor already sits in a split running the same way: become
            // its neighbour and take `fraction` of the anchor's share.
            const auto it = std::find_if(parent->children.begin(), parent->children.end(),
                                         [&](const LayoutNode &c) { return c.id == anchor->id; });
            node.weight = it->weight * fraction;
            it->weight *= 1.0 - fraction;
            parent->children.insert(before ? it : it + 1, std::move(node));
        } else {
            // Otherwise wrap the anchor in a new split.
            LayoutNode old = std::move(*anchor);
            LayoutNode split = LayoutNode::makeSplit(orientation, {});
            split.weight = old.weight;
            old.weight = 1.0 - fraction;
            node.weight = fraction;
            if (before) {
                split.children.push_back(std::move(node));
                split.children.push_back(std::move(old));
            } else {
                split.children.push_back(std::move(old));
                split.children.push_back(std::move(node));
            }
            *anchor = std::move(split);
        }
    }

    work.normalize();
    if (DockResult r = work.validate(); !r)
        return r;
    *this = std::move(work);
    return DockResult::success();
}

DockResult LayoutTree::insertPanel(const PanelId &panel, NodeId target, DockArea area, int tabIndex,
                                   double fraction)
{
    if (panel.isEmpty())
        return fail(DockError::InvalidArgument, QStringLiteral("empty panel id"));
    return insertNode(LayoutNode::makeTabs({panel}), target, area, tabIndex, fraction);
}

DockResult LayoutTree::removePanel(const PanelId &panel)
{
    LayoutNode *group = findPanelMutable(panel);
    if (!group)
        return fail(DockError::NotPlaced, QStringLiteral("panel '%1' is not placed").arg(panel));
    const qsizetype index = group->panels.indexOf(panel);
    group->panels.removeAt(index);
    if (group->active == panel && !group->panels.isEmpty())
        group->active = group->panels.at(std::min(index, group->panels.size() - 1));
    normalize();
    return DockResult::success();
}

std::optional<LayoutNode> LayoutTree::takeNode(NodeId id, NodeId heir)
{
    if (!m_root)
        return std::nullopt;
    std::optional<LayoutNode> taken;
    if (m_root->id == id) {
        taken = std::move(*m_root);
        m_root.reset();
    } else if (LayoutNode *parent = findParentIn(*m_root, id)) {
        const auto it = std::find_if(parent->children.begin(), parent->children.end(),
                                     [id](const LayoutNode &c) { return c.id == id; });
        // As when a panel is closed: its share goes to a neighbour.
        const double weight = it->weight;
        taken = std::move(*it);
        const auto next = parent->children.erase(it);
        const auto named = std::find_if(parent->children.begin(), parent->children.end(),
                                        [heir](const LayoutNode &c) { return c.id == heir; });
        if (named != parent->children.end())
            named->weight += weight;
        else if (!parent->children.empty())
            (next == parent->children.begin() ? next : next - 1)->weight += weight;
        normalize();
    }
    if (taken)
        taken->weight = 1.0;
    return taken;
}

DockResult LayoutTree::setActivePanel(const PanelId &panel)
{
    LayoutNode *group = findPanelMutable(panel);
    if (!group)
        return fail(DockError::NotPlaced, QStringLiteral("panel '%1' is not placed").arg(panel));
    group->active = panel;
    return DockResult::success();
}

DockResult LayoutTree::moveTab(const PanelId &panel, int index)
{
    LayoutNode *group = findPanelMutable(panel);
    if (!group)
        return fail(DockError::NotPlaced, QStringLiteral("panel '%1' is not placed").arg(panel));
    if (index < 0 || index >= group->panels.size())
        return fail(DockError::InvalidArgument, QStringLiteral("tab index out of range"));
    group->panels.move(group->panels.indexOf(panel), index);
    return DockResult::success();
}

DockResult LayoutTree::setWeights(NodeId id, const std::vector<double> &weights)
{
    LayoutNode *split = findNodeMutable(id);
    if (!split || !split->isSplit())
        return fail(DockError::UnknownNode, QStringLiteral("no such split node"));
    if (weights.size() != split->children.size())
        return fail(DockError::InvalidArgument, QStringLiteral("one weight per child is required"));
    double sum = 0.0;
    for (double w : weights) {
        if (!validWeight(w))
            return fail(DockError::InvalidArgument, QStringLiteral("weights must be positive"));
        sum += w;
    }
    for (size_t i = 0; i < weights.size(); ++i)
        split->children[i].weight = weights[i] / sum;
    return DockResult::success();
}

void LayoutTree::normalize()
{
    if (!m_root)
        return;
    if (!normalizeNode(*m_root)) {
        m_root.reset();
        return;
    }
    m_root->weight = 1.0;
}

DockResult LayoutTree::validate() const
{
    if (!m_root)
        return DockResult::success();
    ValidationState state;
    return validateNode(*m_root, 1, state);
}

} // namespace QFlexDock
