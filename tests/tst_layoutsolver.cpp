// SPDX-License-Identifier: MIT
#include "core/DropZones.h"
#include "core/LayoutSolver.h"
#include "core/SplitterCoordinator.h"

#include <QtCore/QRandomGenerator>
#include <QtTest/QtTest>

#include <numeric>

using namespace QFlexDock;

namespace {

QString p(const char *s)
{
    return QString::fromLatin1(s);
}

NodeId groupOf(const LayoutTree &tree, const char *panel)
{
    return tree.findPanel(p(panel))->id;
}

QRect rectOf(const LayoutTree &tree, const SolvedLayout &layout, const char *panel)
{
    return layout.rects.value(groupOf(tree, panel));
}

// Limits keyed by the first panel of a tab group.
LimitsProvider limitsBy(const QHash<QString, SizeLimits> &table)
{
    return [table](const LayoutNode &tabs) { return table.value(tabs.panels.value(0)); };
}

// 2x2 grid: V(H(a, b), H(c, d)). The two vertical bars are aligned.
LayoutTree makeGrid()
{
    LayoutTree tree;
    (void)tree.insertPanel(p("a"), {}, DockArea::Center);
    (void)tree.insertPanel(p("c"), groupOf(tree, "a"), DockArea::Bottom);
    (void)tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right);
    (void)tree.insertPanel(p("d"), groupOf(tree, "c"), DockArea::Right);
    return tree;
}

int handleIndex(const LayoutTree &tree, const SolvedLayout &layout, const char *beforePanel,
                Qt::Orientation orientation)
{
    const NodeId before = groupOf(tree, beforePanel);
    for (size_t i = 0; i < layout.handles.size(); ++i) {
        const SolvedHandle &h = layout.handles[i];
        const LayoutNode *split = tree.findNode(h.split);
        if (h.orientation == orientation && split->children[size_t(h.index)].id == before)
            return int(i);
    }
    return -1;
}

void applyUpdates(LayoutTree &tree, const std::vector<SplitterCoordinator::WeightUpdate> &updates)
{
    for (const auto &u : updates)
        QVERIFY(tree.setWeights(u.split, u.weights));
}

} // namespace

class tst_LayoutSolver : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- distribute --------------------------------------------------------
    void distributeByWeight()
    {
        const auto sizes = LayoutSolver::distribute(1000, {1, 1, 2}, {0, 0, 0},
                                                    {UnboundedSize, UnboundedSize, UnboundedSize});
        QCOMPARE(sizes, std::vector<int>({250, 250, 500}));
    }

    void distributeKeepsEveryPixel()
    {
        QRandomGenerator rng(7);
        for (int i = 0; i < 2000; ++i) {
            const int n = 1 + rng.bounded(6);
            std::vector<double> weights;
            std::vector<int> mins, maxs;
            for (int k = 0; k < n; ++k) {
                weights.push_back(rng.bounded(1.0) + 0.001);
                mins.push_back(rng.bounded(80));
                maxs.push_back(rng.bounded(4) == 0 ? mins.back() + rng.bounded(300) : UnboundedSize);
            }
            const int total = rng.bounded(1500);
            const auto sizes = LayoutSolver::distribute(total, weights, mins, maxs);
            const qint64 sum = std::accumulate(sizes.begin(), sizes.end(), qint64(0));
            const qint64 minSum = std::accumulate(mins.begin(), mins.end(), qint64(0));
            const qint64 maxSum = std::accumulate(maxs.begin(), maxs.end(), qint64(0));
            QCOMPARE(sum, std::min<qint64>(total, maxSum));
            for (int k = 0; k < n; ++k) {
                QVERIFY(sizes[size_t(k)] >= 0);
                QVERIFY(sizes[size_t(k)] <= maxs[size_t(k)]);
                if (minSum <= total)
                    QVERIFY(sizes[size_t(k)] >= mins[size_t(k)]);
            }
        }
    }

    void distributeRespectsMinAndMax()
    {
        // The first wants 10% but needs 300; the last is capped at 100.
        const auto sizes = LayoutSolver::distribute(1000, {1, 4.5, 4.5}, {300, 0, 0},
                                                    {UnboundedSize, UnboundedSize, 100});
        QCOMPARE(sizes, std::vector<int>({300, 600, 100}));
    }

    void distributeBelowMinimumShrinksProportionally()
    {
        const auto sizes = LayoutSolver::distribute(100, {1, 1}, {300, 100},
                                                    {UnboundedSize, UnboundedSize});
        QCOMPARE(sizes, std::vector<int>({75, 25}));
    }

    // --- solve -------------------------------------------------------------
    void solveNestedSplit()
    {
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Bottom));

        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1004, 604), 4, {});
        QCOMPARE(rectOf(tree, layout, "a"), QRect(0, 0, 500, 604));
        QCOMPARE(rectOf(tree, layout, "b"), QRect(504, 0, 500, 300));
        QCOMPARE(rectOf(tree, layout, "c"), QRect(504, 304, 500, 300));
        QCOMPARE(layout.handles.size(), size_t(2));
        QCOMPARE(layout.handles[0].rect, QRect(500, 0, 4, 604));
        QCOMPARE(layout.handles[1].rect, QRect(504, 300, 500, 4));
    }

    void solveTilesTheBoundsWithoutOverlap()
    {
        QRandomGenerator rng(99);
        const DockArea areas[] = {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom};
        for (int round = 0; round < 30; ++round) {
            LayoutTree tree;
            for (int i = 0; i < 25; ++i) {
                const auto groups = tree.tabNodes();
                const NodeId target = groups.empty()
                    ? NodeId{} : groups[size_t(rng.bounded(int(groups.size())))]->id;
                QVERIFY(tree.insertPanel(QStringLiteral("p%1").arg(i), target,
                                         areas[rng.bounded(4)], -1, 0.2 + rng.bounded(0.6)));
            }
            const QRect bounds(10, 20, 3000, 2000);
            const SolvedLayout layout = LayoutSolver::solve(tree, bounds, 5, {});
            qint64 area = 0;
            QList<QRect> pieces;
            for (const LayoutNode *group : tree.tabNodes())
                pieces << layout.rects.value(group->id);
            for (const SolvedHandle &h : layout.handles)
                pieces << h.rect;
            for (int i = 0; i < pieces.size(); ++i) {
                QVERIFY(bounds.contains(pieces[i]));
                area += qint64(pieces[i].width()) * pieces[i].height();
                for (int k = i + 1; k < pieces.size(); ++k)
                    QVERIFY(!pieces[i].intersects(pieces[k]));
            }
            QCOMPARE(area, qint64(bounds.width()) * bounds.height());
        }
    }

    void solvePropagatesLimitsThroughSplits()
    {
        LayoutTree tree = makeGrid();
        QHash<QString, SizeLimits> table;
        table[p("a")] = {QSize(200, 100), QSize(UnboundedSize, UnboundedSize)};
        table[p("b")] = {QSize(50, 150), QSize(UnboundedSize, UnboundedSize)};
        table[p("c")] = {QSize(300, 80), QSize(UnboundedSize, 120)};
        table[p("d")] = {QSize(10, 10), QSize(UnboundedSize, UnboundedSize)};

        const SizeLimits limits = LayoutSolver::limits(*tree.root(), 4, limitsBy(table));
        // Width: the wider row (c + d + handle). Height: both rows + handle.
        QCOMPARE(limits.min, QSize(300 + 10 + 4, 150 + 80 + 4));

        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 804, 604), 4,
                                                        limitsBy(table));
        // The bottom row is capped at 120, the top row takes the rest.
        QCOMPARE(rectOf(tree, layout, "c").height(), 120);
        QCOMPARE(rectOf(tree, layout, "a").height(), 480);
    }

    // --- linked splitters --------------------------------------------------
    void alignedContiguousHandlesAreLinked()
    {
        const LayoutTree tree = makeGrid();
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1004, 604), 4, {});
        const int top = handleIndex(tree, layout, "a", Qt::Horizontal);
        const int bottom = handleIndex(tree, layout, "c", Qt::Horizontal);
        QVERIFY(top >= 0 && bottom >= 0);

        QCOMPARE(SplitterCoordinator::linkedHandles(layout, top), std::vector<int>({top, bottom}));
        QCOMPARE(SplitterCoordinator::linkedHandles(layout, bottom), std::vector<int>({bottom, top}));
        // The horizontal bar between the rows has no aligned partner.
        for (size_t i = 0; i < layout.handles.size(); ++i) {
            if (layout.handles[i].orientation == Qt::Vertical)
                QCOMPARE(SplitterCoordinator::linkedHandles(layout, int(i)).size(), size_t(1));
        }
        QVERIFY(SplitterCoordinator::linkedHandles(layout, 99).empty());
    }

    void misalignedHandlesAreNotLinked()
    {
        LayoutTree tree = makeGrid();
        // Move only the top bar: 30% / 70%.
        const NodeId topRow = tree.parentOf(groupOf(tree, "a"))->id;
        QVERIFY(tree.setWeights(topRow, {0.3, 0.7}));
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1004, 604), 4, {});
        const int top = handleIndex(tree, layout, "a", Qt::Horizontal);
        QCOMPARE(SplitterCoordinator::linkedHandles(layout, top), std::vector<int>({top}));
    }

    void alignedButSeparatedHandlesAreNotLinked()
    {
        // Three rows; the middle row has no bar at that position.
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("m"), groupOf(tree, "a"), DockArea::Bottom));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "m"), DockArea::Bottom));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("d"), groupOf(tree, "c"), DockArea::Right));
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1004, 908), 4, {});
        const int top = handleIndex(tree, layout, "a", Qt::Horizontal);
        const int bottom = handleIndex(tree, layout, "c", Qt::Horizontal);
        QCOMPARE(layout.handles[size_t(top)].rect.x(), layout.handles[size_t(bottom)].rect.x());
        QCOMPARE(SplitterCoordinator::linkedHandles(layout, top), std::vector<int>({top}));
    }

    void linkedRunIsTransitive()
    {
        // Three rows, each split at the same x: all three bars move together.
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "a"), DockArea::Bottom));
        QVERIFY(tree.insertPanel(p("e"), groupOf(tree, "c"), DockArea::Bottom));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("d"), groupOf(tree, "c"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("f"), groupOf(tree, "e"), DockArea::Right));
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1004, 908), 4, {});
        const int top = handleIndex(tree, layout, "a", Qt::Horizontal);
        QCOMPARE(SplitterCoordinator::linkedHandles(layout, top).size(), size_t(3));
    }

    // --- corners -----------------------------------------------------------
    void aCrossIsOneCorner()
    {
        const LayoutTree tree = makeGrid();
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1004, 604), 4, {});
        const int top = handleIndex(tree, layout, "a", Qt::Horizontal);
        const int bottom = handleIndex(tree, layout, "c", Qt::Horizontal);
        const int rows = handleIndex(tree, layout, "a", Qt::Vertical);
        QVERIFY(rows < 0); // the row boundary follows the whole top row, not panel a
        const auto corners = SplitterCoordinator::corners(layout);
        QCOMPARE(corners.size(), size_t(1));
        QCOMPARE(corners[0].columns, std::vector<int>({top, bottom}));
        QCOMPARE(corners[0].rows.size(), size_t(1));
        const QRect level = layout.handles[size_t(corners[0].rows[0])].rect;
        QCOMPARE(layout.handles[size_t(corners[0].rows[0])].orientation, Qt::Vertical);
        QCOMPARE(corners[0].rect,
                 QRect(layout.handles[size_t(top)].rect.x(), level.y(), 4, 4));
    }

    void aBarEndingOnAnotherIsACorner()
    {
        // H(a, V(b, c)): the bar between b and c ends on the one beside a.
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "b"), DockArea::Bottom));
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1004, 604), 4, {});
        const int upright = handleIndex(tree, layout, "a", Qt::Horizontal);
        const int level = handleIndex(tree, layout, "b", Qt::Vertical);
        QVERIFY(upright >= 0 && level >= 0);
        const auto corners = SplitterCoordinator::corners(layout);
        QCOMPARE(corners.size(), size_t(1));
        QCOMPARE(corners[0].columns, std::vector<int>({upright}));
        QCOMPARE(corners[0].rows, std::vector<int>({level}));
        QCOMPARE(corners[0].rect, QRect(layout.handles[size_t(upright)].rect.x(),
                                        layout.handles[size_t(level)].rect.y(), 4, 4));

        // One pixel thin, they still meet.
        const SolvedLayout thin = LayoutSolver::solve(tree, QRect(0, 0, 1001, 601), 1, {});
        QCOMPARE(SplitterCoordinator::corners(thin).size(), size_t(1));
        QCOMPARE(SplitterCoordinator::corners(thin)[0].rect.size(), QSize(1, 1));
    }

    void barsThatDoNotMeetHaveNoCorner()
    {
        // A single boundary.
        LayoutTree pair;
        QVERIFY(pair.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(pair.insertPanel(p("b"), groupOf(pair, "a"), DockArea::Right));
        QVERIFY(SplitterCoordinator::corners(LayoutSolver::solve(pair, QRect(0, 0, 800, 600), 4, {}))
                    .empty());

        // H(V(a, c), m, V(b, d)): the two row boundaries are level with each
        // other, but each ends on its own upright bar, with m in between.
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("a"), {}, DockArea::Center));
        QVERIFY(tree.insertPanel(p("m"), groupOf(tree, "a"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("b"), groupOf(tree, "m"), DockArea::Right));
        QVERIFY(tree.insertPanel(p("c"), groupOf(tree, "a"), DockArea::Bottom));
        QVERIFY(tree.insertPanel(p("d"), groupOf(tree, "b"), DockArea::Bottom));
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1208, 604), 4, {});
        const auto corners = SplitterCoordinator::corners(layout);
        QCOMPARE(corners.size(), size_t(2));
        for (const auto &corner : corners) {
            QCOMPARE(corner.columns.size(), size_t(1));
            QCOMPARE(corner.rows.size(), size_t(1));
        }
        QVERIFY(corners[0].columns != corners[1].columns);
        QVERIFY(corners[0].rows != corners[1].rows);
    }

    void aCrossPulledApartIsTwoCorners()
    {
        LayoutTree tree = makeGrid();
        const NodeId topRow = tree.parentOf(groupOf(tree, "a"))->id;
        QVERIFY(tree.setWeights(topRow, {0.3, 0.7}));
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 1004, 604), 4, {});
        const auto corners = SplitterCoordinator::corners(layout);
        QCOMPARE(corners.size(), size_t(2));
        QCOMPARE(corners[0].rows, corners[1].rows);
        QVERIFY(corners[0].columns != corners[1].columns);
        QVERIFY(!corners[0].rect.intersects(corners[1].rect));
    }

    void movingACornerMovesBothAxes()
    {
        LayoutTree tree = makeGrid();
        const QRect bounds(0, 0, 1004, 604);
        const SolvedLayout start = LayoutSolver::solve(tree, bounds, 4, {});
        const auto corners = SplitterCoordinator::corners(start);
        QCOMPARE(corners.size(), size_t(1));
        const QRect a = rectOf(tree, start, "a");
        applyUpdates(tree, SplitterCoordinator::moveHandles(tree, start, corners[0].columns, 70));
        applyUpdates(tree, SplitterCoordinator::moveHandles(tree, start, corners[0].rows, -40));
        QVERIFY(tree.validate());
        const SolvedLayout moved = LayoutSolver::solve(tree, bounds, 4, {});
        QCOMPARE(rectOf(tree, moved, "a").size(), a.size() + QSize(70, -40));
        QCOMPARE(rectOf(tree, moved, "c").width(), a.width() + 70);
        QCOMPARE(rectOf(tree, moved, "d").topLeft(), rectOf(tree, start, "d").topLeft() + QPoint(70, -40));
        // Still one corner, moved along.
        const auto after = SplitterCoordinator::corners(moved);
        QCOMPARE(after.size(), size_t(1));
        QCOMPARE(after[0].rect, corners[0].rect.translated(70, -40));
    }

    void movingLinkedHandlesKeepsThemAligned()
    {
        LayoutTree tree = makeGrid();
        const QRect bounds(0, 0, 1004, 604);
        const SolvedLayout start = LayoutSolver::solve(tree, bounds, 4, {});
        const int top = handleIndex(tree, start, "a", Qt::Horizontal);
        const std::vector<int> group = SplitterCoordinator::linkedHandles(start, top);
        QCOMPARE(group.size(), size_t(2));

        applyUpdates(tree, SplitterCoordinator::moveHandles(tree, start, group, 120));
        QVERIFY(tree.validate());
        const SolvedLayout moved = LayoutSolver::solve(tree, bounds, 4, {});
        QCOMPARE(rectOf(tree, moved, "a").width(), 620);
        QCOMPARE(rectOf(tree, moved, "c").width(), 620);
        QCOMPARE(rectOf(tree, moved, "b").width(), 380);
        QCOMPARE(rectOf(tree, moved, "d").width(), 380);
        // Still linked afterwards, also after the window is resized.
        const SolvedLayout resized = LayoutSolver::solve(tree, QRect(0, 0, 1337, 700), 4, {});
        const int topAgain = handleIndex(tree, resized, "a", Qt::Horizontal);
        QCOMPARE(SplitterCoordinator::linkedHandles(resized, topAgain).size(), size_t(2));
    }

    void movingASingleHandleLeavesTheOtherAlone()
    {
        LayoutTree tree = makeGrid();
        const QRect bounds(0, 0, 1004, 604);
        const SolvedLayout start = LayoutSolver::solve(tree, bounds, 4, {});
        const int top = handleIndex(tree, start, "a", Qt::Horizontal);
        applyUpdates(tree, SplitterCoordinator::moveHandles(tree, start, {top}, -100));
        const SolvedLayout moved = LayoutSolver::solve(tree, bounds, 4, {});
        QCOMPARE(rectOf(tree, moved, "a").width(), 400);
        QCOMPARE(rectOf(tree, moved, "c").width(), 500);
    }

    void deltaRangeIsTheIntersectionOfAllLimits()
    {
        LayoutTree tree = makeGrid();
        QHash<QString, SizeLimits> table;
        table[p("a")] = {QSize(100, 0), QSize(UnboundedSize, UnboundedSize)};
        table[p("b")] = {QSize(200, 0), QSize(UnboundedSize, UnboundedSize)};
        table[p("c")] = {QSize(350, 0), QSize(UnboundedSize, UnboundedSize)};
        table[p("d")] = {QSize(0, 0), QSize(560, UnboundedSize)};
        const QRect bounds(0, 0, 1004, 604);
        const SolvedLayout start = LayoutSolver::solve(tree, bounds, 4, limitsBy(table));
        const int top = handleIndex(tree, start, "a", Qt::Horizontal);
        const std::vector<int> group = SplitterCoordinator::linkedHandles(start, top);
        QCOMPARE(group.size(), size_t(2));

        // Left: a could shrink by 400, but c only by 150 and d may only grow by 60.
        // Right: b allows 300, d allows 500.
        QCOMPARE(SplitterCoordinator::deltaRange(tree, start, group), std::make_pair(-60, 300));
        QCOMPARE(SplitterCoordinator::deltaRange(tree, start, {top}), std::make_pair(-400, 300));

        // A wild drag is clamped and never violates a limit.
        applyUpdates(tree, SplitterCoordinator::moveHandles(tree, start, group, -5000));
        const SolvedLayout moved = LayoutSolver::solve(tree, bounds, 4, limitsBy(table));
        QCOMPARE(rectOf(tree, moved, "a").width(), 440);
        QCOMPARE(rectOf(tree, moved, "c").width(), 440);
        QCOMPARE(rectOf(tree, moved, "d").width(), 560);
    }

    void repeatedDraggingDoesNotDrift()
    {
        LayoutTree tree = makeGrid();
        const QRect bounds(0, 0, 1004, 604);
        QRandomGenerator rng(3);
        int expected = 500;
        for (int i = 0; i < 200; ++i) {
            const SolvedLayout start = LayoutSolver::solve(tree, bounds, 4, {});
            const int top = handleIndex(tree, start, "a", Qt::Horizontal);
            const std::vector<int> group = SplitterCoordinator::linkedHandles(start, top);
            QCOMPARE(group.size(), size_t(2));
            const int delta = rng.bounded(21) - 10;
            applyUpdates(tree, SplitterCoordinator::moveHandles(tree, start, group, delta));
            expected += delta;
            const SolvedLayout moved = LayoutSolver::solve(tree, bounds, 4, {});
            QCOMPARE(rectOf(tree, moved, "a").width(), expected);
            QCOMPARE(rectOf(tree, moved, "c").width(), expected);
            QVERIFY(tree.validate());
        }
    }

    // --- drop zones --------------------------------------------------------
    void zonesCoverTheWholeTarget()
    {
        const QRect target(40, 30, 400, 300);
        const DropZoneLayout zones = DropZoneLayout::compute(target, 0.25);
        QCOMPARE(zones.center, QRect(140, 105, 200, 150));
        for (int y = target.top(); y <= target.bottom(); y += 3) {
            for (int x = target.left(); x <= target.right(); x += 3)
                QVERIFY(zones.hitTest(QPoint(x, y)) != DockArea::None);
        }
        QCOMPARE(zones.hitTest(QPoint(39, 100)), DockArea::None);
        QCOMPARE(zones.hitTest(target.center()), DockArea::Center);
        QCOMPARE(zones.hitTest(QPoint(45, 180)), DockArea::Left);
        QCOMPARE(zones.hitTest(QPoint(435, 180)), DockArea::Right);
        QCOMPARE(zones.hitTest(QPoint(240, 35)), DockArea::Top);
        QCOMPARE(zones.hitTest(QPoint(240, 325)), DockArea::Bottom);
        // Corners belong to the trapezoid on their side of the diagonal.
        QCOMPARE(zones.hitTest(QPoint(60, 40)), DockArea::Top);
        QCOMPARE(zones.hitTest(QPoint(50, 60)), DockArea::Left);
    }

    void zonePolygonsMatchHitTest()
    {
        const QRect target(0, 0, 500, 320);
        const DropZoneLayout zones = DropZoneLayout::compute(target, 0.3);
        const DockArea areas[] = {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                                  DockArea::Center};
        QRandomGenerator rng(11);
        for (int i = 0; i < 3000; ++i) {
            const QPoint pos(rng.bounded(500), rng.bounded(320));
            const DockArea hit = zones.hitTest(pos);
            // Wherever exactly one polygon contains the pixel centre (i.e. away
            // from the shared borders), it is the area the hit test reports.
            const QPointF c = QPointF(pos) + QPointF(0.5, 0.5);
            int containing = 0;
            DockArea owner = DockArea::None;
            for (DockArea area : areas) {
                if (zones.polygon(area).containsPoint(c, Qt::OddEvenFill)) {
                    ++containing;
                    owner = area;
                }
            }
            if (containing == 1)
                QCOMPARE(hit, owner);
        }
        QVERIFY(zones.polygon(DockArea::None).isEmpty());
    }

    void marginKeepsTheAreasOffTheBorderWithoutDeadSpace()
    {
        const QRect target(40, 30, 400, 300);
        const DropZoneLayout zones = DropZoneLayout::compute(target, 0.25, 20);
        QCOMPARE(zones.bounds, target);
        QCOMPARE(zones.visible, QRect(60, 50, 360, 260));
        QCOMPARE(zones.center, QRect(150, 115, 180, 130));
        // What is drawn stays inside the margin...
        for (DockArea area : {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                              DockArea::Center}) {
            QVERIFY(QRectF(zones.visible).contains(zones.polygon(area).boundingRect()));
        }
        // ...but the margin itself still hits the area beside it.
        for (int y = target.top(); y <= target.bottom(); y += 2) {
            for (int x = target.left(); x <= target.right(); x += 2)
                QVERIFY(zones.hitTest(QPoint(x, y)) != DockArea::None);
        }
        QCOMPARE(zones.hitTest(QPoint(42, 180)), DockArea::Left);
        QCOMPARE(zones.hitTest(QPoint(437, 180)), DockArea::Right);
        QCOMPARE(zones.hitTest(QPoint(240, 32)), DockArea::Top);
        QCOMPARE(zones.hitTest(QPoint(240, 327)), DockArea::Bottom);
        QCOMPARE(zones.hitTest(QPoint(240, 180)), DockArea::Center);
        // Where an area is drawn is where it is hit.
        QRandomGenerator rng(21);
        const DockArea areas[] = {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                                  DockArea::Center};
        for (int i = 0; i < 3000; ++i) {
            const QPoint pos(target.left() + rng.bounded(400), target.top() + rng.bounded(300));
            const QPointF c = QPointF(pos) + QPointF(0.5, 0.5);
            int containing = 0;
            DockArea owner = DockArea::None;
            for (DockArea area : areas) {
                if (zones.polygon(area).containsPoint(c, Qt::OddEvenFill)) {
                    ++containing;
                    owner = area;
                }
            }
            if (containing == 1)
                QCOMPARE(zones.hitTest(pos), owner);
        }
        // A margin too large for a small target is cut down, never inverted.
        const DropZoneLayout tiny = DropZoneLayout::compute(QRect(0, 0, 30, 24), 0.25, 50);
        QVERIFY(tiny.visible.isValid() && tiny.center.isValid());
        QVERIFY(QRect(0, 0, 30, 24).contains(tiny.visible));
    }

    void disabledZonesDoNotHit()
    {
        const DropZoneLayout zones = DropZoneLayout::compute(QRect(0, 0, 400, 300), 0.25);
        const DockAreas noCenter = EdgeDockAreas;
        QCOMPARE(zones.hitTest(QPoint(200, 150), noCenter), DockArea::None);
        QCOMPARE(zones.hitTest(QPoint(5, 150), noCenter), DockArea::Left);
        QCOMPARE(zones.hitTest(QPoint(5, 150), DockArea::Center), DockArea::None);
    }

    void previewRects()
    {
        const QRect target(100, 50, 400, 200);
        QCOMPARE(dropPreviewRect(target, DockArea::Center, 0.5), target);
        QCOMPARE(dropPreviewRect(target, DockArea::Left, 0.5), QRect(100, 50, 200, 200));
        QCOMPARE(dropPreviewRect(target, DockArea::Right, 0.25), QRect(400, 50, 100, 200));
        QCOMPARE(dropPreviewRect(target, DockArea::Top, 0.5), QRect(100, 50, 400, 100));
        QCOMPARE(dropPreviewRect(target, DockArea::Bottom, 0.25), QRect(100, 200, 400, 50));
    }

    void outerBand()
    {
        const QRect bounds(0, 0, 800, 600);
        QCOMPARE(outerBandAt(bounds, QPoint(400, 300), 30), DockArea::None);
        QCOMPARE(outerBandAt(bounds, QPoint(10, 300), 30), DockArea::Left);
        QCOMPARE(outerBandAt(bounds, QPoint(790, 300), 30), DockArea::Right);
        QCOMPARE(outerBandAt(bounds, QPoint(400, 5), 30), DockArea::Top);
        QCOMPARE(outerBandAt(bounds, QPoint(400, 595), 30), DockArea::Bottom);
        QCOMPARE(outerBandAt(bounds, QPoint(5, 20), 30), DockArea::Left); // nearer edge wins
        QCOMPARE(outerBandAt(bounds, QPoint(900, 20), 30), DockArea::None);
        QCOMPARE(outerBandRect(bounds, DockArea::Right, 30), QRect(770, 0, 30, 600));
    }
};

QTEST_APPLESS_MAIN(tst_LayoutSolver)
#include "tst_layoutsolver.moc"
