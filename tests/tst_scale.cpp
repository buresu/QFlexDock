// SPDX-License-Identifier: MIT
//
// Many panels and deep trees: everything must stay correct, and nothing may
// grow so badly with size that it would be felt. The time limits are loose on
// purpose (debug builds, sanitizers, busy CI machines); the measured times are
// printed for comparison between runs.
#include "TestUtils.h"

#include "core/LayoutSolver.h"
#include "core/SplitterCoordinator.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QRandomGenerator>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

// Sanitizer builds run several times slower; the limits scale with that.
#if defined(__SANITIZE_ADDRESS__)
constexpr int TimeFactor = 10;
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
constexpr int TimeFactor = 10;
#  else
constexpr int TimeFactor = 1;
#  endif
#else
constexpr int TimeFactor = 1;
#endif

struct Stopwatch
{
    explicit Stopwatch(const char *what)
        : m_what(what)
    {
        m_timer.start();
    }
    qint64 stop()
    {
        const qint64 ms = m_timer.elapsed();
        qInfo("%-46s %5lld ms", m_what, static_cast<long long>(ms));
        return ms;
    }

private:
    const char *m_what;
    QElapsedTimer m_timer;
};

} // namespace

class tst_Scale : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void modelWithThousandsOfPanels()
    {
        QRandomGenerator rng(1);
        const DockArea areas[] = {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                                  DockArea::Center};
        LayoutTree tree;
        Stopwatch build("model: insert 3000 panels");
        for (int i = 0; i < 3000; ++i) {
            const auto groups = tree.tabNodes();
            const NodeId target = groups.empty()
                ? NodeId{} : groups[size_t(rng.bounded(int(groups.size())))]->id;
            QVERIFY(tree.insertPanel(QStringLiteral("p%1").arg(i), target, areas[rng.bounded(5)]));
        }
        QVERIFY(build.stop() < 20000 * TimeFactor);
        QVERIFY(tree.validate());
        QCOMPARE(tree.panels().size(), 3000);

        Stopwatch solve("solver: 200 x solve of 3000 panels");
        SolvedLayout layout;
        for (int i = 0; i < 200; ++i)
            layout = LayoutSolver::solve(tree, QRect(0, 0, 4000 + i, 3000), 4, {});
        QVERIFY(solve.stop() < 10000 * TimeFactor);

        Stopwatch link("splitters: linked run of every handle");
        size_t linked = 0;
        for (size_t i = 0; i < layout.handles.size(); i += 7)
            linked += SplitterCoordinator::linkedHandles(layout, int(i)).size();
        QVERIFY(link.stop() < 10000 * TimeFactor);
        QVERIFY(linked >= layout.handles.size() / 7);
    }

    void deepestAllowedTree()
    {
        // Alternating splits nest one level per panel: the worst case for
        // recursion. It must build up to the limit and refuse to go beyond.
        LayoutTree tree;
        QVERIFY(tree.insertPanel(p("p0"), {}, DockArea::Center));
        int placed = 1;
        for (; placed < 400; ++placed) {
            const NodeId last = tree.findPanel(QStringLiteral("p%1").arg(placed - 1))->id;
            const DockResult result = tree.insertPanel(QStringLiteral("p%1").arg(placed), last,
                                                       placed % 2 ? DockArea::Right : DockArea::Bottom);
            if (!result) {
                QCOMPARE(result.error(), DockError::InvalidLayout);
                break;
            }
        }
        QCOMPARE(tree.depth(), LayoutTree::MaxDepth);
        QCOMPARE(placed, LayoutTree::MaxDepth);
        QVERIFY(tree.validate());
        const SolvedLayout layout = LayoutSolver::solve(tree, QRect(0, 0, 100000, 100000), 2, {});
        QCOMPARE(layout.rects.size(), tree.nodeCount());
    }

    void manyPanelsInWidgets()
    {
        const int count = 300;
        DockManager manager;
        QMainWindow window;
        DockWorkspace *workspace = manager.createWorkspace(p("main"));
        window.setCentralWidget(workspace);
        window.resize(1600, 1000);
        QList<QPointer<QWidget>> widgets;
        for (int i = 0; i < count; ++i) {
            auto *label = new QLabel(QString::number(i));
            widgets << label;
            manager.registerPanel(QStringLiteral("p%1").arg(i), label);
        }
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        manager.setUndoLimit(10);

        // A grid of tab groups: 6 columns x 5 rows, 10 tabs each.
        Stopwatch build("widgets: place 300 panels in 30 groups");
        for (int i = 0; i < count; ++i) {
            const QString id = QStringLiteral("p%1").arg(i);
            const int group = i / 10;
            if (i % 10) {
                QVERIFY(manager.movePanel(id, QStringLiteral("p%1").arg(group * 10), DockArea::Center));
            } else if (group == 0) {
                QVERIFY(workspace->addPanel(id));
            } else if (group % 5) {
                QVERIFY(manager.movePanel(id, QStringLiteral("p%1").arg((group - 1) * 10),
                                          DockArea::Bottom));
            } else {
                QVERIFY(workspace->addPanel(id, DockArea::Right, 1.0 / (group / 5 + 1)));
            }
        }
        QVERIFY(build.stop() < 30000 * TimeFactor);
        DockAreaWidget *area = areaOf(workspace);
        QCOMPARE(area->groups().size(), 30);
        QCOMPARE(workspace->panels().size(), count);
        QVERIFY(workspace->layoutTree().validate());

        Stopwatch resize("widgets: 60 window resizes");
        for (int i = 0; i < 60; ++i) {
            window.resize(1600 - i * 8, 1000 - i * 5);
            QCoreApplication::processEvents();
        }
        QVERIFY(resize.stop() < 15000 * TimeFactor);

        Stopwatch moves("widgets: 100 moves between groups");
        QRandomGenerator rng(8);
        for (int i = 0; i < 100; ++i) {
            const QString a = QStringLiteral("p%1").arg(rng.bounded(count));
            const QString b = QStringLiteral("p%1").arg(rng.bounded(count));
            (void)manager.movePanel(a, b, DockArea::Center);
        }
        QVERIFY(moves.stop() < 20000 * TimeFactor);

        Stopwatch persist("persistence: 20 x save + restore");
        for (int i = 0; i < 20; ++i) {
            const QByteArray saved = manager.saveLayout();
            QVERIFY(manager.restoreLayout(saved));
        }
        QVERIFY(persist.stop() < 20000 * TimeFactor);
        QVERIFY(manager.hidePanel(p("p0")));
        QVERIFY(manager.undo());

        // Nothing was lost or duplicated along the way.
        for (const QPointer<QWidget> &widget : std::as_const(widgets))
            QVERIFY(widget);
        QCOMPARE(workspace->panels().size(), count);
        int visible = 0;
        for (const QPointer<QWidget> &widget : std::as_const(widgets))
            visible += widget->isVisible() ? 1 : 0;
        QCOMPARE(visible, int(area->groups().size())); // exactly the current tab of each group

        // Tearing it all down is clean too.
        Stopwatch teardown("widgets: unregister 300 panels");
        for (int i = 0; i < count; ++i)
            QVERIFY(manager.unregisterPanel(QStringLiteral("p%1").arg(i)));
        QVERIFY(teardown.stop() < 20000 * TimeFactor);
        QVERIFY(workspace->layoutTree().isEmpty());
        for (const QPointer<QWidget> &widget : std::as_const(widgets))
            QVERIFY(!widget);
    }
};

QTEST_MAIN(tst_Scale)
#include "tst_scale.moc"
