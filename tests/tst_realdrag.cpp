// SPDX-License-Identifier: MIT
//
// End-to-end drags through the real QDrag::exec(): mouse events go in at the
// window-system level and the platform's own drag machinery delivers them.
// Only platforms whose drag loop is driven by Qt's event queue can be
// automated like this (X11); elsewhere these tests skip and the manual
// procedure in docs/platform-notes.md applies.
#include "TestUtils.h"

#include "core/DockDragController.h"
#include "widgets/DockDropOverlay.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockTabBar.h"

#include <QtCore/QTimer>
#include <QtGui/QWindow>
#include <QtTest/QSignalSpy>
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
};

QTEST_MAIN(tst_RealDrag)
#include "tst_realdrag.moc"
