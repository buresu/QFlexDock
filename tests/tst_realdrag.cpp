// SPDX-License-Identifier: MIT
//
// End-to-end drags through the real QDrag::exec(): mouse events go in at the
// window-system level and the platform's own drag machinery delivers them.
// Only platforms whose drag loop is driven by Qt's event queue can be
// automated like this (X11); elsewhere these tests skip and the manual
// procedure in docs/platform-notes.md applies.
#include "TestUtils.h"

#include "core/DockDragController.h"
#include "widgets/DockColumn.h"
#include "widgets/DockDropOverlay.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockTabBar.h"

#include <QtCore/QTimer>
#include <QtGui/QWindow>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

bool platformDragIsScriptable()
{
    return QGuiApplication::platformName() == QLatin1String("xcb");
}

/// Runs `steps` one after the other from the event loop, which is what keeps
/// running inside QDrag::exec(), and sets `done` after the last one. The
/// whole drag typically happens within a single processEvents() call of the
/// test, so tests observe it through what the steps record.
void script(QObject *context, std::vector<std::function<void()>> steps, bool *done,
            int interval = 60)
{
    *done = false;
    int delay = interval;
    for (auto &step : steps) {
        QTimer::singleShot(delay, context, std::move(step));
        delay += interval;
    }
    QTimer::singleShot(delay, context, [done] { *done = true; });
}

void moveTo(QWidget *window, const QPoint &global)
{
    QTest::mouseMove(window->windowHandle(), window->mapFromGlobal(global));
}

void releaseAt(QWidget *window, const QPoint &global)
{
    QTest::mouseRelease(window->windowHandle(), Qt::LeftButton, {}, window->mapFromGlobal(global));
}

} // namespace

class tst_RealDrag : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        if (!platformDragIsScriptable())
            QSKIP("A real drag can only be scripted on X11; see docs/platform-notes.md");
    }

    void dragATabIntoAnotherMainWindow()
    {
        TwoWindows f;
        f.windowA.move(20, 20);
        f.windowB.move(940, 40);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("c")));
        DockAreaWidget *target = areaOf(f.b);
        DockTabBar *bar = areaOf(f.a)->groupOfPanel(p("a"))->tabBar();
        QWidget *widget = f.widgets[p("a")];
        DockDragController *controller = priv(f.manager)->drag;

        const QPoint press = bar->tabRect(0).center();
        const QPoint start = bar->mapToGlobal(press);
        const QRect group = target->groupOfPanel(p("c"))->geometry();
        const QPoint over = target->mapToGlobal(QPoint(group.right() - 60, group.center().y()));
        bool guideSeen = false;
        bool layoutUntouchedWhileDragging = false;
        bool sessionSeen = false;
        bool done = false;

        // Pressing and pulling the tab starts the drag; everything after that
        // happens inside QDrag::exec().
        QTest::mousePress(bar, Qt::LeftButton, {}, press);
        QTest::mouseMove(bar, press + QPoint(40, 40));
        script(this, {
            [&] { moveTo(&f.windowA, start + QPoint(80, 80)); },
            [&] { moveTo(&f.windowB, over - QPoint(30, 0)); },
            [&] { moveTo(&f.windowB, over); },
            [&] {
                sessionSeen = controller->isActive();
                guideSeen = target->overlay()->isVisible()
                    && target->overlay()->scene().preview.isValid();
                layoutUntouchedWhileDragging = describe(f.a) == p("a|b") && describe(f.b) == p("c");
            },
            [&] { releaseAt(&f.windowB, over); },
        }, &done);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);

        QVERIFY(sessionSeen);
        QVERIFY(guideSeen);
        QVERIFY(layoutUntouchedWhileDragging);
        QCOMPARE(describe(f.a), p("b"));
        QCOMPARE(describe(f.b), p("H(c, a)"));
        QCOMPARE(f.manager.panel(p("a"))->widget(), widget);
        QCOMPARE(widget->window(), &f.windowB);
        QVERIFY(widget->isVisible());
        QVERIFY(!target->overlay()->isVisible());
    }

    // The title bar of a group of stacked panels: dragged, it takes the
    // current panel along, or all of them (setTitleBarMovesGroup()).
    void dragATitleBar_data()
    {
        QTest::addColumn<bool>("wholeGroup");
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("arrived");
        QTest::newRow("the current panel") << false << p("b") << p("H(c, a)");
        QTest::newRow("the whole group") << true << p("<empty>") << p("H(c, a|b)");
    }

    void dragATitleBar()
    {
        QFETCH(bool, wholeGroup);
        QFETCH(QString, left);
        QFETCH(QString, arrived);
        TwoWindows f;
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        f.manager.setTitleBarMovesGroup(wholeGroup);
        f.windowA.move(20, 20);
        f.windowB.move(940, 40);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("a")));
        QVERIFY(f.b->addPanel(p("c")));
        DockAreaWidget *target = areaOf(f.b);
        QWidget *title = areaOf(f.a)->groupOfPanel(p("a"))->titleBar();
        DockDragController *controller = priv(f.manager)->drag;

        const QPoint press(40, title->height() / 2);
        const QPoint start = title->mapToGlobal(press);
        const QRect group = target->groupOfPanel(p("c"))->geometry();
        const QPoint over = target->mapToGlobal(QPoint(group.right() - 60, group.center().y()));
        QStringList dragged;
        bool done = false;

        QTest::mousePress(title, Qt::LeftButton, {}, press);
        QTest::mouseMove(title, press + QPoint(40, 40));
        script(this, {
            [&] { moveTo(&f.windowA, start + QPoint(80, 80)); },
            [&] { moveTo(&f.windowB, over - QPoint(30, 0)); },
            [&] { moveTo(&f.windowB, over); },
            [&] {
                if (controller->session())
                    dragged = controller->session()->panels;
            },
            [&] { releaseAt(&f.windowB, over); },
        }, &done);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);

        QCOMPARE(dragged, wholeGroup ? QStringList({p("a"), p("b")}) : QStringList{p("a")});
        QCOMPARE(describe(f.a), left);
        QCOMPARE(describe(f.b), arrived);
    }

    // Content that takes drops of its own (a text editor) must not swallow a
    // dock drag: it refuses the dock mime type and Qt offers the drag to its
    // parents, up to the dock area.
    void dragOntoContentThatAcceptsDrops()
    {
        TwoWindows f;
        f.windowA.move(20, 20);
        f.windowB.move(940, 40);
        auto *editor = new QPlainTextEdit(p("some text"));
        QVERIFY(editor->acceptDrops());
        f.manager.registerPanel(p("editor"), editor);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("editor")));
        DockTabBar *bar = areaOf(f.a)->groupOfPanel(p("a"))->tabBar();
        DockAreaWidget *target = areaOf(f.b);
        DockDragController *controller = priv(f.manager)->drag;

        const QPoint press = bar->tabRect(0).center();
        const QPoint over = editor->mapToGlobal(editor->rect().center());
        bool guideSeen = false;
        bool done = false;
        QTest::mousePress(bar, Qt::LeftButton, {}, press);
        QTest::mouseMove(bar, press + QPoint(40, 40));
        script(this, {
            [&] { moveTo(&f.windowB, over - QPoint(20, 0)); },
            [&] { moveTo(&f.windowB, over); },
            [&] { guideSeen = target->overlay()->isVisible(); },
            [&] { releaseAt(&f.windowB, over); },
        }, &done);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);

        QVERIFY(guideSeen);
        QCOMPARE(describe(f.b), p("editor|a"));
        QCOMPARE(editor->toPlainText(), p("some text")); // nothing was "dropped into" the text
    }

    void escapeCancelsARealDrag()
    {
        TwoWindows f;
        f.windowA.move(20, 20);
        f.windowB.move(940, 40);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("c")));
        DockAreaWidget *target = areaOf(f.b);
        DockTabBar *bar = areaOf(f.a)->groupOfPanel(p("a"))->tabBar();
        DockDragController *controller = priv(f.manager)->drag;

        const QPoint press = bar->tabRect(0).center();
        const QPoint over = target->mapToGlobal(target->rect().center());
        bool guideSeen = false;
        bool done = false;
        QTest::mousePress(bar, Qt::LeftButton, {}, press); // also makes "a" the current tab
        QTest::mouseMove(bar, press + QPoint(40, 40));
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);
        script(this, {
            [&] { moveTo(&f.windowB, over - QPoint(20, 0)); },
            [&] { moveTo(&f.windowB, over); },
            [&] { guideSeen = controller->isActive() && target->overlay()->isVisible(); },
            [&] { QTest::keyClick(f.windowB.windowHandle(), Qt::Key_Escape); },
        }, &done);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        // The button is still down as far as the platform knows.
        QTest::mouseRelease(f.windowB.windowHandle(), Qt::LeftButton, {},
                            f.windowB.mapFromGlobal(over));

        QVERIFY(guideSeen);
        QCOMPARE(describe(f.a), p("a|b"));
        QCOMPARE(describe(f.b), p("c"));
        QCOMPARE(changed.size(), 0);
        QVERIFY(!target->overlay()->isVisible());
        // Escape must not be mistaken for "dropped outside": nothing floats.
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
    }

    // Windows that are rows of tabs and nothing else: a tab goes from one
    // window into the row of another, and a tab let go of anywhere else
    // becomes a window. No workspace is involved.
    void tabsBetweenWindowsOfTabs()
    {
        DockManager manager;
        manager.setFloatingWindowFrame(DockManager::FloatingFrame::Minimal);
        manager.setCenterDropEnabled(false);
        QVERIFY(manager.floatsOnOutsideDrop());
        QHash<QString, QLabel *> labels;
        DockPolicy policy;
        policy.allowedAreas = DockArea::Center;
        for (const char *id : {"a", "b", "c"}) {
            labels.insert(p(id), new QLabel(p(id)));
            QVERIFY(manager.registerPanel(p(id), labels.value(p(id))));
            QVERIFY(manager.setDockPolicy(p(id), policy));
        }
        DockManagerPrivate *d = priv(manager);
        DockDragController *controller = d->drag;
        QVERIFY(manager.floatPanel(p("a"), QRect(40, 60, 700, 420)));
        QVERIFY(manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(manager.floatPanel(p("c"), QRect(820, 80, 700, 420)));
        const auto windowOf = [&](const char *id) {
            return qobject_cast<DockFloatingWindow *>(labels[p(id)]->window());
        };
        DockFloatingWindow *first = windowOf("a");
        DockFloatingWindow *second = windowOf("c");
        QVERIFY(first && second && first != second);
        QVERIFY(QTest::qWaitForWindowExposed(first));
        QVERIFY(QTest::qWaitForWindowExposed(second));
        const auto barOf = [&](const char *id) {
            return windowOf(id)->area()->groupOfPanel(p(id))->tabBar();
        };
        bool done = false;
        const auto dragTab = [&](const char *id, const QPoint &globalTarget) {
            DockTabBar *bar = barOf(id);
            QWidget *window = bar->window();
            const QPoint press = bar->tabRect(bar->indexOfPanel(p(id))).center();
            QCursor::setPos(bar->mapToGlobal(press));
            QTest::mousePress(bar, Qt::LeftButton, {}, press);
            QTest::mouseMove(bar, press + QPoint(30, 30));
            // (The pointer itself is taken along, from the press to the
            // release: where a window lands is worked out from where it is.)
            script(this, {
                [window, globalTarget] { moveTo(window, globalTarget - QPoint(30, 10)); },
                [window, globalTarget] {
                    QCursor::setPos(globalTarget);
                    moveTo(window, globalTarget);
                },
                [window, globalTarget] { releaseAt(window, globalTarget); },
            }, &done);
        };

        // 1. From the first window onto the title row of the second, beside
        // its one tab: a tab there.
        const DockTabBar *target = barOf("c");
        const QWidget *title = second->area()->groupOfPanel(p("c"))->titleBar();
        dragTab("b", title->mapToGlobal(QPoint(target->tabRect(0).right() + 160, title->height() / 2)));
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        QCOMPARE(describe(second->area()->tree()), p("c|b"));
        QCOMPARE(describe(first->area()->tree()), p("a"));
        QCOMPARE(d->floatingWindows.size(), 2);

        // 2. Let go of over the content of its own window: torn off, into a
        // window of the same size.
        const QPoint onContent = labels[p("c")]->mapToGlobal(QPoint(200, 150));
        dragTab("c", onContent);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        QCOMPARE(d->floatingWindows.size(), 3);
        DockFloatingWindow *third = windowOf("c");
        QVERIFY(third != second);
        QCOMPARE(describe(second->area()->tree()), p("b"));
        QCOMPARE(describe(third->area()->tree()), p("c"));
        QTRY_COMPARE(third->size(), second->size());

        // 3. The only tab of a window, let go of where nothing takes it: the
        // window goes as far as the pointer went, and stays the only one.
        QVERIFY(QTest::qWaitForWindowExposed(third));
        const QPoint before = third->pos();
        const DockTabBar *own = barOf("c");
        const QPoint grip = own->mapToGlobal(own->tabRect(0).center());
        const QPoint nowhere = grip + QPoint(-150, 260);
        QVERIFY(!third->geometry().contains(nowhere));
        dragTab("c", nowhere);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        QCOMPARE(d->floatingWindows.size(), 3);
        QCOMPARE(windowOf("c"), third);
        QTRY_COMPARE(third->pos(), before + QPoint(-150, 260));

        // 4. Back into the first window, before its tab; its own window goes.
        const QPointer<DockFloatingWindow> gone(third);
        const DockTabBar *firstBar = barOf("a");
        dragTab("c", firstBar->mapToGlobal(firstBar->tabRect(0).topLeft() + QPoint(6, 12)));
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        QCOMPARE(describe(first->area()->tree()), p("c|a"));
        QCOMPARE(d->floatingWindows.size(), 2);
        QTRY_VERIFY(!gone);
    }

    void droppingOutsideEveryWindowFloatsThePanel()
    {
        TwoWindows f;
        f.windowA.move(20, 20);
        f.windowB.move(940, 40);
        f.show();
        QVERIFY(f.manager.floatsOnOutsideDrop()); // the default on X11
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        DockTabBar *bar = areaOf(f.a)->groupOfPanel(p("a"))->tabBar();
        DockDragController *controller = priv(f.manager)->drag;

        const QPoint press = bar->tabRect(0).center();
        // Below both windows: nobody's dock area.
        const QPoint nowhere(400, f.windowA.frameGeometry().bottom() + 200);
        bool sessionSeen = false;
        bool done = false;
        QTest::mousePress(bar, Qt::LeftButton, {}, press);
        QTest::mouseMove(bar, press + QPoint(40, 40));
        script(this, {
            [&] { moveTo(&f.windowA, nowhere - QPoint(0, 60)); },
            [&] { moveTo(&f.windowA, nowhere); },
            [&] { sessionSeen = controller->isActive(); },
            [&] { releaseAt(&f.windowA, nowhere); },
        }, &done);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        QVERIFY(sessionSeen);

        QVERIFY(f.manager.panel(p("a"))->isFloating());
        QCOMPARE(describe(f.a), p("b"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 1);

        // With the option off, the same gesture is just a cancelled drag.
        f.manager.setFloatsOnOutsideDrop(false);
        QVERIFY(f.manager.dockPanel(p("a")));
        bar = areaOf(f.a)->groupOfPanel(p("a"))->tabBar();
        const QPoint again = bar->tabRect(bar->indexOfPanel(p("a"))).center();
        QTest::mousePress(bar, Qt::LeftButton, {}, again);
        QTest::mouseMove(bar, again + QPoint(40, 40));
        script(this, {
            [&] { moveTo(&f.windowA, nowhere); },
            [&] { releaseAt(&f.windowA, nowhere); },
        }, &done);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        QVERIFY(!f.manager.panel(p("a"))->isFloating());
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
    }

    void aColumnIsDraggedByItsBarAndAPanelByItsButton()
    {
        TwoWindows f;
        f.windowA.move(20, 20);
        f.windowB.move(940, 40);
        f.manager.setColumnDocking(f.a, true);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        DockAreaWidget *area = areaOf(f.a);
        DockDragController *controller = priv(f.manager)->drag;
        const NodeId column = area->tree().columnOf(area->tree().findPanel(p("b"))->id)->id;

        // The bar above the column of b and c, to the left side of a.
        DockColumnBar *bar = area->columnBar(column);
        QVERIFY(bar);
        const QPoint press(bar->width() / 2, bar->height() / 2);
        const QRect group = area->groupOfPanel(p("a"))->geometry();
        const QPoint left = area->mapToGlobal(QPoint(group.left() + 8, group.center().y()));
        bool wholeColumn = false;
        bool guideSeen = false;
        bool done = false;
        QTest::mousePress(bar, Qt::LeftButton, {}, press);
        QTest::mouseMove(bar, press + QPoint(-40, 30));
        script(this, {
            [&] { moveTo(&f.windowA, left + QPoint(60, 0)); },
            [&] { moveTo(&f.windowA, left); },
            [&] {
                const DragSession *session = controller->session();
                wholeColumn = session && session->sourceNode == column
                    && session->panels == QStringList({p("b"), p("c")});
                guideSeen = area->overlay()->isVisible()
                    && area->overlay()->scene().preview.isValid();
            },
            [&] { releaseAt(&f.windowA, left); },
        }, &done);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        QVERIFY(wholeColumn);
        QVERIFY(guideSeen);
        QCOMPARE(describe(f.a), p("H(V(b, c), a)"));

        // Iconified, one of its buttons is dragged onto a: a tab there, and
        // its button none the worse for a press that ended elsewhere.
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockIconStrip *strip = area->iconStrips().value(0);
        QVERIFY(strip);
        DockIconButton *button = strip->button(p("c"));
        const QPoint middle = area->mapToGlobal(area->groupOfPanel(p("a"))->geometry().center());
        bool onePanel = false;
        QTest::mousePress(button, Qt::LeftButton, {}, button->rect().center());
        QTest::mouseMove(button, button->rect().center() + QPoint(40, 10));
        script(this, {
            [&] { moveTo(&f.windowA, middle - QPoint(40, 0)); },
            [&] { moveTo(&f.windowA, middle); },
            [&] {
                const DragSession *session = controller->session();
                onePanel = session && !session->wholeGroup && session->primary == p("c");
            },
            [&] { releaseAt(&f.windowA, middle); },
        }, &done);
        QTRY_VERIFY_WITH_TIMEOUT(done && !controller->isActive(), 5000);
        QVERIFY(onePanel);
        QCOMPARE(describe(f.a), p("H(b, a|c)"));
        QVERIFY(f.manager.isColumnIconified(p("b")));
        QVERIFY(!f.manager.isColumnIconified(p("c")));
        QVERIFY(f.widgets[p("c")]->isVisible());
        QVERIFY(area->flyout().isNull());

        // A click on the button that is left still brings its group out.
        strip = area->iconStrips().value(0);
        QTest::mouseClick(strip->button(p("b")), Qt::LeftButton);
        QCOMPARE(area->flyout(), area->tree().findPanel(p("b"))->id);
    }
};

QTEST_MAIN(tst_RealDrag)
#include "tst_realdrag.moc"
