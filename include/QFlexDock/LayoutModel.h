// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#include <optional>
#include <vector>

namespace QFlexDock {

/// One node of a layout tree: either a tab group (leaf) or an n-ary split.
///
/// This is a plain value. It never refers to widgets, so a tree can be copied,
/// edited, validated and thrown away without touching the UI. That is what
/// makes every layout change a transaction.
struct QFLEXDOCK_EXPORT LayoutNode
{
    enum class Type { Tabs, Split };

    NodeId id;
    Type type = Type::Tabs;
    /// Share of the parent split this node takes, relative to its siblings.
    double weight = 1.0;

    // Type::Tabs
    QStringList panels;
    PanelId active;

    // Type::Split. Horizontal lays children out left to right.
    Qt::Orientation orientation = Qt::Horizontal;
    std::vector<LayoutNode> children;

    /// The node is a column that is shrunk to a strip of buttons, one for
    /// each panel in it, instead of showing the panels themselves. Set on the
    /// column as a whole (see LayoutTree::columnOf()), never on a node inside
    /// one that is iconified already.
    bool iconified = false;

    [[nodiscard]] static LayoutNode makeTabs(const QStringList &panels, const PanelId &active = {});
    [[nodiscard]] static LayoutNode makeSplit(Qt::Orientation orientation,
                                              std::vector<LayoutNode> children);

    [[nodiscard]] bool isTabs() const { return type == Type::Tabs; }
    [[nodiscard]] bool isSplit() const { return type == Type::Split; }
};

/// The docking layout of one container (a workspace or a floating window).
///
/// Normal form, which every mutating function re-establishes:
///  - no empty tab group and no split with fewer than two children,
///  - no split directly inside a split of the same orientation, unless it is
///    an iconified column,
///  - no iconified node inside an iconified node,
///  - sibling weights are positive and sum to 1,
///  - a tab group's active panel is one of its panels.
/// Mutating functions either succeed and leave the tree in normal form, or fail
/// and leave it untouched.
class QFLEXDOCK_EXPORT LayoutTree
{
public:
    /// Nesting deeper than this is rejected by validate().
    static constexpr int MaxDepth = 128;

    LayoutTree() = default;
    explicit LayoutTree(LayoutNode root);

    [[nodiscard]] bool isEmpty() const { return !m_root.has_value(); }
    [[nodiscard]] const LayoutNode *root() const { return m_root ? &*m_root : nullptr; }
    void clear() { m_root.reset(); }

    // --- Queries -----------------------------------------------------------
    [[nodiscard]] const LayoutNode *findNode(NodeId id) const;
    [[nodiscard]] const LayoutNode *parentOf(NodeId id) const;
    /// Tab group holding `panel`, or nullptr.
    [[nodiscard]] const LayoutNode *findPanel(const PanelId &panel) const;
    [[nodiscard]] bool containsPanel(const PanelId &panel) const { return findPanel(panel); }
    /// All panels, depth first, in tab order.
    [[nodiscard]] QStringList panels() const;
    /// All tab groups, depth first.
    [[nodiscard]] std::vector<const LayoutNode *> tabNodes() const;
    [[nodiscard]] int nodeCount() const;
    [[nodiscard]] int depth() const;
    /// The column node `id` belongs to: the iconified node it is or lies in;
    /// else, for a tab group among tab groups that are stacked above one
    /// another and nothing else, the vertical split holding them; else the
    /// node itself. Null if there is no such node.
    [[nodiscard]] const LayoutNode *columnOf(NodeId id) const;

    // --- Mutations ---------------------------------------------------------
    /// Docks `node` (a tab group or a whole subtree) relative to `target`.
    /// A null `target` means the tree as a whole: an edge area docks along the
    /// outside of everything, Center joins the first tab group (or becomes the
    /// root of an empty tree). Center requires `node` to be a tab group and
    /// inserts its panels at `tabIndex` (-1 appends). For edge areas `fraction`
    /// is the share of the target's extent the new node takes (0 < f < 1).
    /// What is docked above or below a node joins the column of that node,
    /// iconified if that is: only beside a column does a node keep being
    /// iconified itself.
    DockResult insertNode(LayoutNode node, NodeId target, DockArea area, int tabIndex = -1,
                          double fraction = 0.5);
    /// Convenience for a single panel.
    DockResult insertPanel(const PanelId &panel, NodeId target, DockArea area, int tabIndex = -1,
                           double fraction = 0.5);
    DockResult removePanel(const PanelId &panel);
    /// Detaches and returns the subtree rooted at `id`. The share it had of
    /// its split goes to `heir` if that is one of its siblings, else to the
    /// sibling before it (after it, for the first one).
    std::optional<LayoutNode> takeNode(NodeId id, NodeId heir = {});

    DockResult setActivePanel(const PanelId &panel);
    /// Moves `panel` to `index` within its own tab group.
    DockResult moveTab(const PanelId &panel, int index);
    /// Sets the weights of all children of split `id` (one per child, all > 0).
    DockResult setWeights(NodeId id, const std::vector<double> &weights);
    /// Shrinks node `id` to a strip of buttons, or shows its panels again.
    DockResult setIconified(NodeId id, bool iconified);

    /// Re-establishes normal form. Idempotent.
    void normalize();
    /// Checks every invariant of the normal form plus id and panel uniqueness.
    [[nodiscard]] DockResult validate() const;

private:
    LayoutNode *findNodeMutable(NodeId id);
    LayoutNode *findPanelMutable(const PanelId &panel);

    std::optional<LayoutNode> m_root;
};

} // namespace QFlexDock
