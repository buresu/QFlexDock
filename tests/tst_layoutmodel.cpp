// SPDX-License-Identifier: MIT
#include <QFlexDock/LayoutModel.h>

#include <QtCore/QRandomGenerator>
#include <QtTest/QtTest>

using namespace QFlexDock;

namespace {

NodeId groupOf(const LayoutTree &tree, const char *panel)
{
    const LayoutNode *node = tree.findPanel(QString::fromLatin1(panel));
    return node ? node->id : NodeId{};
}

QString p(const char *s)
{
    return QString::fromLatin1(s);
}

// Compact structural description, e.g. "H(a|b, V(c, d))", for readable asserts.
QString describe(const LayoutNode &node)
{
    if (node.isTabs())
        return node.panels.join(QLatin1Char('|'));
    QStringList parts;
    for (const auto &child : node.children)
        parts << describe(child);
    return QLatin1String(node.orientation == Qt::Horizontal ? "H(" : "V(")
        + parts.join(QLatin1String(", ")) + QLatin1Char(')');
}

QString describe(const LayoutTree &tree)
{
    return tree.root() ? describe(*tree.root()) : QStringLiteral("<empty>");
}

} // namespace

class tst_LayoutModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyTree()
    {
        LayoutTree tree;
        QVERIFY(tree.isEmpty());
        QVERIFY(tree.validate());
        QCOMPARE(tree.nodeCount(), 0);
        QVERIFY(!tree.removePanel(p("x")));
    }

    void firstPanelBecomesRoot()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QCOMPARE(describe(tree), p("a"));
        QVERIFY(tree.validate());

        // Any area works on an empty tree.
        LayoutTree other;
        QVERIFY(other.insertPanel(p("a"), {}, DockArea::Left));
        QCOMPARE(describe(other), p("a"));
    }

    void tabJoinAndOrder()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Center));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "a"), DockArea::Center, 0));
        QCOMPARE(describe(tree), p("c|a|b"));
        QCOMPARE(tree.findPanel(p("a"))->active, p("c"));

        QVERIFY(tree.moveTab(p("c"), 2));
        QCOMPARE(describe(tree), p("a|b|c"));
        QVERIFY(!tree.moveTab(p("c"), 3));
        QVERIFY(tree.setActivePanel(p("b")));
        QCOMPARE(tree.findPanel(p("a"))->active, p("b"));
        QVERIFY(tree.validate());
    }

    void splitAllDirections()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("r"), groupOf(tree, "a"), DockArea::Right));
        QCOMPARE(describe(tree), p("H(a, r)"));
        QVERIFY(tree.insertPanel(p("l"), groupOf(tree, "a"), DockArea::Left));
        QCOMPARE(describe(tree), p("H(l, a, r)"));
        QVERIFY(tree.insertPanel(p("t"), groupOf(tree, "a"), DockArea::Top));
        QCOMPARE(describe(tree), p("H(l, V(t, a), r)"));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Bottom));
        QCOMPARE(describe(tree), p("H(l, V(t, a, b), r)"));
        QVERIFY(tree.validate());
        QCOMPARE(tree.depth(), 3);
    }

    void rootEdgeDocking()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        // Along the outside of the whole tree, same direction: joins the root split.
        QVERIFY(tree.insertPanel(p("c"), {}, DockArea::Left, -1, 0.25));
        QCOMPARE(describe(tree), p("H(c, a, b)"));
        QCOMPARE(tree.root()->children.front().weight, 0.25);
        // Other direction: wraps the root.
        QVERIFY(tree.insertPanel(p("d"), {}, DockArea::Bottom, -1, 0.2));
        QCOMPARE(describe(tree), p("V(H(c, a, b), d)"));
        QVERIFY(qFuzzyCompare(tree.root()->children.back().weight, 0.2));
        QVERIFY(tree.validate());
    }

    void fractionSplitsTheTargetsShare()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right, -1, 0.5));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Right, -1, 0.5));
        const auto &children = tree.root()->children;
        QCOMPARE(children.size(), size_t(3));
        QVERIFY(qFuzzyCompare(children[0].weight, 0.5));
        QVERIFY(qFuzzyCompare(children[1].weight, 0.25));
        QVERIFY(qFuzzyCompare(children[2].weight, 0.25));
    }

    void removeCollapsesEmptyNodes()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Bottom));
        QCOMPARE(describe(tree), p("H(a, V(b, c))"));

        QVERIFY(tree.removePanel(p("b")));
        QCOMPARE(describe(tree), p("H(a, c)"));
        QVERIFY(tree.validate());
        QVERIFY(tree.removePanel(p("a")));
        QCOMPARE(describe(tree), p("c"));
        QVERIFY(tree.removePanel(p("c")));
        QVERIFY(tree.isEmpty());
    }

    // What a panel leaves behind goes to one neighbour; nobody else changes
    // size, and putting it back beside that neighbour restores everything.
    void removedPanelLeavesItsShareToItsNeighbour()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Right));
        QVERIFY(tree.setWeights(tree.root()->id, {0.2, 0.6, 0.2}));
        const auto weights = [](const LayoutTree &t) {
            QList<int> percent;
            for (const auto &child : t.root()->children)
                percent << qRound(child.weight * 100);
            return percent;
        };

        LayoutTree first = tree;
        QVERIFY(first.removePanel(p("a"))); // the first one: to the one after it
        QCOMPARE(weights(first), (QList<int>{80, 20}));
        QVERIFY(first.insertPanel(p("a"), groupOf(first, "b"), DockArea::Left, -1, 0.25));
        QCOMPARE(weights(first), (QList<int>{20, 60, 20}));

        LayoutTree middle = tree;
        QVERIFY(middle.removePanel(p("b"))); // otherwise: to the one before it
        QCOMPARE(weights(middle), (QList<int>{80, 20}));

        LayoutTree last = tree;
        QVERIFY(last.removePanel(p("c")));
        QCOMPARE(weights(last), (QList<int>{20, 80}));

        // The same when a whole node is taken out.
        LayoutTree taken = tree;
        QVERIFY(taken.takeNode(groupOf(taken, "a")).has_value());
        QCOMPARE(weights(taken), (QList<int>{80, 20}));
        QVERIFY(taken.validate());

        // Unless a sibling is named to have it.
        LayoutTree named = tree;
        QVERIFY(named.takeNode(groupOf(named, "b"), groupOf(named, "c")).has_value());
        QCOMPARE(weights(named), (QList<int>{20, 80}));
        // Someone who is no sibling gets nothing; the usual neighbour does.
        LayoutTree stranger = tree;
        QVERIFY(stranger.takeNode(groupOf(stranger, "b"), NodeId::create()).has_value());
        QCOMPARE(weights(stranger), (QList<int>{80, 20}));
    }

    void removingTheMiddleFlattensSameOrientation()
    {
        // V(H(a, b), c) with c removed and d added below a: stays in normal form.
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Bottom));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("d"), groupOf(tree, "c"), DockArea::Bottom));
        QCOMPARE(describe(tree), p("V(a, H(b, V(c, d)))"));
        QVERIFY(tree.removePanel(p("b")));
        // H collapses, the inner V merges into the outer V.
        QCOMPARE(describe(tree), p("V(a, c, d)"));
        QVERIFY(tree.validate());
    }

    void removingActiveTabPicksNeighbour()
    {
        LayoutTree tree(LayoutNode::makeTabs({p("a"), p("b"), p("c")}, p("b")));
        QVERIFY(tree.removePanel(p("b")));
        QCOMPARE(tree.root()->active, p("c"));
        QVERIFY(tree.removePanel(p("c")));
        QCOMPARE(tree.root()->active, p("a"));
    }

    void takeAndReinsertWholeGroup()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Center));
        const NodeId group = groupOf(tree, "b");

        std::optional<LayoutNode> taken = tree.takeNode(group);
        QVERIFY(taken.has_value());
        QCOMPARE(taken->panels, QStringList({p("b"), p("c")}));
        QCOMPARE(describe(tree), p("a"));

        QVERIFY(tree.insertNode(*taken, groupOf(tree, "a"), DockArea::Top));
        QCOMPARE(describe(tree), p("V(b|c, a)"));
        QCOMPARE(groupOf(tree, "b"), group); // the group keeps its identity
        QVERIFY(!tree.takeNode(NodeId{12345678}).has_value());
    }

    void insertingASplitSubtreeFlattens()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        LayoutNode sub = LayoutNode::makeSplit(
            Qt::Horizontal, {LayoutNode::makeTabs({p("x")}), LayoutNode::makeTabs({p("y")})});
        QVERIFY(tree.insertNode(sub, groupOf(tree, "a"), DockArea::Right));
        QCOMPARE(describe(tree), p("H(a, x, y)"));
        QVERIFY(tree.validate());
    }

    void invalidOperationsLeaveTheTreeUntouched()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        const QString before = describe(tree);

        QCOMPARE(tree.insertPanel(p("a"), {}, DockArea::Left).error(), DockError::DuplicatePanel);
        QCOMPARE(tree.insertPanel(p("z"), NodeId{999999}, DockArea::Left).error(),
                 DockError::UnknownNode);
        QCOMPARE(tree.insertPanel(p("z"), {}, DockArea::None).error(), DockError::InvalidArgument);
        QCOMPARE(tree.insertPanel(QString(), {}, DockArea::Left).error(),
                 DockError::InvalidArgument);
        // Center onto a split is meaningless.
        QCOMPARE(tree.insertPanel(p("z"), tree.root()->id, DockArea::Center).error(),
                 DockError::InvalidArgument);
        // A split cannot be merged into a tab group.
        LayoutNode sub = LayoutNode::makeSplit(
            Qt::Vertical, {LayoutNode::makeTabs({p("x")}), LayoutNode::makeTabs({p("y")})});
        QCOMPARE(tree.insertNode(sub, groupOf(tree, "a"), DockArea::Center).error(),
                 DockError::InvalidArgument);
        QVERIFY(!tree.setWeights(tree.root()->id, {1.0}));
        QVERIFY(!tree.setWeights(tree.root()->id, {1.0, -1.0}));
        QVERIFY(!tree.setActivePanel(p("nope")));

        QCOMPARE(describe(tree), before);
        QVERIFY(tree.validate());
    }

    void setWeightsNormalizes()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.setWeights(tree.root()->id, {300.0, 100.0}));
        QCOMPARE(tree.root()->children[0].weight, 0.75);
        QCOMPARE(tree.root()->children[1].weight, 0.25);
        QVERIFY(tree.validate());
    }

    void validateCatchesBrokenTrees()
    {
        // Built by hand, bypassing the mutators.
        LayoutNode a = LayoutNode::makeTabs({p("a")});
        LayoutNode dup = LayoutNode::makeTabs({p("a")});
        a.weight = dup.weight = 0.5;
        LayoutNode split = LayoutNode::makeSplit(Qt::Horizontal, {a, dup});

        LayoutTree tree;
        // The constructor normalizes but cannot repair a duplicated panel.
        tree = LayoutTree(split);
        QCOMPARE(tree.validate().error(), DockError::DuplicatePanel);
    }

    void normalizeIsIdempotent()
    {
        LayoutNode inner = LayoutNode::makeSplit(
            Qt::Horizontal, {LayoutNode::makeTabs({p("b")}), LayoutNode::makeTabs({})});
        LayoutNode root = LayoutNode::makeSplit(
            Qt::Horizontal, {LayoutNode::makeTabs({p("a")}), inner, LayoutNode::makeTabs({p("c")})});
        root.children[0].weight = -3; // repaired
        LayoutTree tree(root);
        QCOMPARE(describe(tree), p("H(a, b, c)"));
        QVERIFY(tree.validate());
        const QString once = describe(tree);
        tree.normalize();
        QCOMPARE(describe(tree), once);
        QVERIFY(tree.validate());
    }

    // Random operation sequences must never break an invariant, and a failed
    // operation must never change the tree.
    void randomOperationsKeepInvariants()
    {
        QRandomGenerator rng(20261009);
        const DockArea areas[] = {DockArea::Left, DockArea::Right, DockArea::Top,
                                  DockArea::Bottom, DockArea::Center};

        for (int round = 0; round < 40; ++round) {
            LayoutTree tree;
            QStringList placed;
            int nextPanel = 0;

            for (int step = 0; step < 300; ++step) {
                const QString before = describe(tree);
                const std::vector<const LayoutNode *> groups = tree.tabNodes();
                const auto randomGroup = [&]() -> NodeId {
                    if (groups.empty() || rng.bounded(5) == 0)
                        return {};
                    return groups[size_t(rng.bounded(int(groups.size())))]->id;
                };
                DockResult result;
                switch (rng.bounded(8)) {
                case 7: { // iconify the column of a random group, or show it again
                    if (groups.empty())
                        continue;
                    const LayoutNode *column =
                        tree.columnOf(groups[size_t(rng.bounded(int(groups.size())))]->id);
                    QVERIFY(column);
                    result = tree.setIconified(column->id, !column->iconified);
                    QVERIFY(result);
                    break;
                }
                case 0:
                case 1: { // add a new panel
                    const QString panel = QStringLiteral("p%1").arg(nextPanel++);
                    result = tree.insertPanel(panel, randomGroup(), areas[rng.bounded(5)],
                                              rng.bounded(4) - 1, rng.bounded(1.0));
                    if (result)
                        placed << panel;
                    break;
                }
                case 2: { // remove
                    if (placed.isEmpty())
                        continue;
                    const QString panel = placed.takeAt(rng.bounded(int(placed.size())));
                    result = tree.removePanel(panel);
                    QVERIFY(result);
                    break;
                }
                case 3: { // move a panel: remove + insert on a copy (a transaction)
                    if (placed.isEmpty())
                        continue;
                    const QString panel = placed.at(rng.bounded(int(placed.size())));
                    LayoutTree work = tree;
                    QVERIFY(work.removePanel(panel));
                    const std::vector<const LayoutNode *> rest = work.tabNodes();
                    const NodeId target = rest.empty()
                        ? NodeId{} : rest[size_t(rng.bounded(int(rest.size())))]->id;
                    result = work.insertPanel(panel, target, areas[rng.bounded(5)]);
                    if (result)
                        tree = work;
                    break;
                }
                case 4: { // move a whole group
                    if (groups.size() < 2)
                        continue;
                    LayoutTree work = tree;
                    const NodeId moving = groups[size_t(rng.bounded(int(groups.size())))]->id;
                    std::optional<LayoutNode> taken = work.takeNode(moving);
                    QVERIFY(taken.has_value());
                    const std::vector<const LayoutNode *> rest = work.tabNodes();
                    result = work.insertNode(*taken, rest[size_t(rng.bounded(int(rest.size())))]->id,
                                             areas[rng.bounded(5)]);
                    if (result)
                        tree = work;
                    break;
                }
                case 5: { // bogus requests
                    result = tree.insertPanel(placed.value(0), NodeId{987654321},
                                              areas[rng.bounded(5)]);
                    QVERIFY(!result);
                    break;
                }
                case 6: { // reweight a random split
                    const LayoutNode *root = tree.root();
                    if (!root || !root->isSplit())
                        continue;
                    std::vector<double> weights;
                    for (size_t i = 0; i < root->children.size(); ++i)
                        weights.push_back(rng.bounded(1.0) + 0.01);
                    result = tree.setWeights(root->id, weights);
                    QVERIFY(result);
                    break;
                }
                }

                if (!result)
                    QCOMPARE(describe(tree), before);
                const DockResult valid = tree.validate();
                QVERIFY2(valid, qPrintable(valid.message() + QLatin1String(" in ") + describe(tree)));
                QStringList actual = tree.panels();
                QStringList expected = placed;
                actual.sort();
                expected.sort();
                QCOMPARE(actual, expected);
            }
        }
    }

    // --- Columns -------------------------------------------------------------

    void theColumnOfAGroupIsWhatStandsAboveAndBelowIt()
    {
        // H(a, V(b, c), V(d, H(e, f)))
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Bottom));
        QVERIFY(tree.insertPanel(p("d"), {}, DockArea::Right));
        QVERIFY(tree.insertPanel(p("e"), groupOf(tree, "d"), DockArea::Bottom));
        QVERIFY(tree.insertPanel(p("f"), groupOf(tree, "e"), DockArea::Right));
        QCOMPARE(describe(tree), p("H(a, V(b, c), V(d, H(e, f)))"));

        // A group on its own is its own column.
        QCOMPARE(tree.columnOf(groupOf(tree, "a"))->id, groupOf(tree, "a"));
        // Groups above one another are one.
        const LayoutNode *stack = tree.parentOf(groupOf(tree, "b"));
        QCOMPARE(tree.columnOf(groupOf(tree, "b")), stack);
        QCOMPARE(tree.columnOf(groupOf(tree, "c")), stack);
        QCOMPARE(tree.columnOf(stack->id), stack);
        // Not where something else than tab groups is stacked.
        QCOMPARE(tree.columnOf(groupOf(tree, "d"))->id, groupOf(tree, "d"));
        QCOMPARE(tree.columnOf(groupOf(tree, "e"))->id, groupOf(tree, "e"));
        QVERIFY(!tree.columnOf(NodeId::create()));
    }

    void anIconifiedColumnStaysOneNode()
    {
        // H(a, V(b, c)) with the column of b and c iconified.
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Bottom));
        const NodeId column = tree.columnOf(groupOf(tree, "b"))->id;
        QVERIFY(tree.setIconified(column, true));
        QVERIFY(tree.findNode(column)->iconified);
        QVERIFY(tree.validate());
        QCOMPARE(tree.columnOf(groupOf(tree, "c"))->id, column);
        QVERIFY(!tree.setIconified(NodeId::create(), true));

        // Something above everything: the column is not taken apart for it.
        QVERIFY(tree.insertPanel(p("x"), {}, DockArea::Top));
        QVERIFY(tree.removePanel(p("a")));
        QCOMPARE(describe(tree), p("V(x, V(b, c))"));
        QVERIFY(tree.validate());
        QVERIFY(tree.findNode(column)->iconified);

        // Shown again, it is tab groups among the others.
        QVERIFY(tree.setIconified(column, false));
        QCOMPARE(describe(tree), p("V(x, b, c)"));
        QVERIFY(tree.validate());
    }

    void whatIsLeftOfAnIconifiedColumnIsIconified()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Bottom));
        QVERIFY(tree.setIconified(tree.columnOf(groupOf(tree, "b"))->id, true));
        QVERIFY(tree.removePanel(p("c")));
        QCOMPARE(describe(tree), p("H(a, b)"));
        QVERIFY(tree.findPanel(p("b"))->iconified);
        QVERIFY(!tree.findPanel(p("a"))->iconified);
        QVERIFY(tree.validate());
    }

    void dockingAboveOrBelowJoinsAnIconifiedColumn()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.setIconified(groupOf(tree, "b"), true));

        // Below the one group it is: the two are the column now.
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Bottom));
        QCOMPARE(describe(tree), p("H(a, V(b, c))"));
        const LayoutNode *column = tree.parentOf(groupOf(tree, "b"));
        QVERIFY(column->iconified);
        QVERIFY(!tree.findPanel(p("b"))->iconified);
        QVERIFY(tree.validate());

        // Above all of it, and between two of its groups.
        QVERIFY(tree.insertPanel(p("d"), column->id, DockArea::Top));
        QVERIFY(tree.insertPanel(p("e"), groupOf(tree, "b"), DockArea::Bottom));
        QCOMPARE(describe(tree), p("H(a, V(d, b, e, c))"));
        QVERIFY(tree.parentOf(groupOf(tree, "e"))->iconified);
        QVERIFY(tree.validate());

        // Beside it is not in it.
        QVERIFY(tree.insertPanel(p("f"), tree.columnOf(groupOf(tree, "b"))->id, DockArea::Left));
        QCOMPARE(describe(tree), p("H(a, f, V(d, b, e, c))"));
        QVERIFY(!tree.findPanel(p("f"))->iconified);
        QCOMPARE(tree.columnOf(groupOf(tree, "f"))->id, groupOf(tree, "f"));
    }

    void anIconifiedNodeKeepsThatBesideAColumnOnly()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        LayoutNode strip = LayoutNode::makeTabs({p("b")});
        strip.iconified = true;
        QVERIFY(tree.insertNode(strip, groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.findPanel(p("b"))->iconified);

        // Docked below a group that is open, it is open as well.
        LayoutNode other = LayoutNode::makeTabs({p("c")});
        other.iconified = true;
        QVERIFY(tree.insertNode(other, groupOf(tree, "a"), DockArea::Bottom));
        QCOMPARE(describe(tree), p("H(V(a, c), b)"));
        QVERIFY(!tree.findPanel(p("c"))->iconified);
        QVERIFY(tree.validate());
    }

    void nothingIsIconifiedInsideAnIconifiedNode()
    {
        LayoutNode inner = LayoutNode::makeTabs({p("b")});
        inner.iconified = true;
        LayoutNode column = LayoutNode::makeSplit(Qt::Vertical, {LayoutNode::makeTabs({p("a")}), inner});
        column.iconified = true;
        const LayoutTree tree(column);
        QVERIFY(tree.validate());
        QVERIFY(tree.root()->iconified);
        QVERIFY(!tree.findPanel(p("b"))->iconified);
    }
};

QTEST_APPLESS_MAIN(tst_LayoutModel)
#include "tst_layoutmodel.moc"
