// SPDX-License-Identifier: MIT
//
// Floating windows: the choice of frame, and windows carried along with a drag
// (the drag ghost, and custom-framed windows dragged by their title row).
// What the compositor does with a carried window cannot be scripted, so the
// drag outcomes are fed to DockDragController::finish() directly; the manual
// procedure for the real thing is in docs/platform-notes.md.
#include "TestUtils.h"

#include "core/DockDragController.h"
#include "widgets/DockDropOverlay.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockTabBar.h"

#include <QtCore/QDataStream>
#include <QtCore/QMimeData>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWindow>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLayout>
#include <QtWidgets/QToolButton>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

bool onWayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

DockFloatingWindow *floatingWindowOf(TwoWindows &f, const char *panel)
{
    return qobject_cast<DockFloatingWindow *>(f.widgets[p(panel)]->window());
}

void buildLayout(TwoWindows &f)
{
    QVERIFY(f.a->addPanel(p("a")));
    QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
    QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
    QVERIFY(f.manager.activatePanel(p("b")));
    f.manager.clearUndoHistory();
}

} // namespace

class tst_Floating : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nativeFrameIsTheDefault()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        QCOMPARE(f.manager.floatingWindowFrame(), DockManager::FloatingFrame::Native);
        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 320, 240)));
        DockFloatingWindow *window = floatingWindowOf(f, "b");
        QVERIFY(window);
        QVERIFY(!window->hasCustomFrame());
        QVERIFY(!window->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!window->titleBar());
        // The dock area takes the whole window; the window system frames it.
        QCOMPARE(window->area()->geometry(), window->rect());
    }

    void customFrameHasItsOwnTitleRow()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        QVERIFY(f.manager.floatPanel(p("a"), QRect(40, 40, 300, 200)));
        f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Custom);
        QCOMPARE(f.manager.floatingWindowFrame(), DockManager::FloatingFrame::Custom);
        // Windows that exist keep the frame they were created with.
        QVERIFY(!floatingWindowOf(f, "a")->hasCustomFrame());

        QVERIFY(f.manager.floatTabGroup(p("b"), QRect(80, 80, 360, 260)));
        DockFloatingWindow *window = floatingWindowOf(f, "b");
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(window->hasCustomFrame());
        QVERIFY(window->windowFlags().testFlag(Qt::FramelessWindowHint));
        QTRY_COMPARE(window->size(), QSize(360, 260));

        // A title row above the dock area, inside a border that can be grabbed.
        QWidget *titleBar = window->titleBar();
        QVERIFY(titleBar && titleBar->isVisible());
        QVERIFY(titleBar->geometry().bottom() < window->area()->geometry().top());
        QVERIFY(window->area()->geometry().left() > 0);
        QVERIFY(window->area()->geometry().right() < window->width() - 1);
        auto *title = titleBar->findChild<QLabel *>(QStringLiteral("dockFloatingTitle"));
        QVERIFY(title);
        QCOMPARE(title->text(), p("b"));
        QCOMPARE(window->windowTitle(), p("b"));
        grab(window, p("floating-custom-frame"));

        // It names the current panel of the window, and follows it.
        f.manager.panel(p("b"))->setTitle(p("Bravo"));
        QCOMPARE(title->text(), p("Bravo"));
        QVERIFY(f.manager.activatePanel(p("c")));
        QCOMPARE(title->text(), p("c"));

        // The border shows resize cursors; the inside does not. (A plain
        // hover: QTest::mouseMove() without a button only moves the cursor.)
        const auto hover = [window](const QPoint &pos) {
            QMouseEvent move(QEvent::MouseMove, pos, window->mapToGlobal(pos), Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &move);
            return window->cursor().shape();
        };
        QCOMPARE(hover(QPoint(1, window->height() / 2)), Qt::SizeHorCursor);
        QCOMPARE(hover(QPoint(window->width() - 2, window->height() / 2)), Qt::SizeHorCursor);
        QCOMPARE(hover(QPoint(window->width() / 2, window->height() - 2)), Qt::SizeVerCursor);
        QCOMPARE(hover(QPoint(1, 1)), Qt::SizeFDiagCursor);
        QCOMPARE(hover(QPoint(window->width() - 2, 1)), Qt::SizeBDiagCursor);
        QCOMPARE(hover(QPoint(window->width() - 2, window->height() - 2)), Qt::SizeFDiagCursor);

        // Maximizing takes the border away; the button turns into "restore".
        // (Wait for the window system to have really done it before asking
        // for the opposite: a request made in between is dropped by Qt.)
        const QString maximizeTip = window->maximizeButton()->toolTip();
        const QSize normalSize = window->size();
        QTest::mouseClick(window->maximizeButton(), Qt::LeftButton);
        if (QTest::qWaitFor([&] { return window->isMaximized() && window->size() != normalSize; }, 3000)) {
            QVERIFY(window->maximizeButton()->toolTip() != maximizeTip);
            QTRY_COMPARE(window->area()->geometry().left(), 0);
            QTest::qWait(100);
            QTest::mouseClick(window->maximizeButton(), Qt::LeftButton);
            QTRY_VERIFY(!window->isMaximized());
            QCOMPARE(window->maximizeButton()->toolTip(), maximizeTip);
            QTRY_VERIFY(window->area()->geometry().left() > 0);
        }

        // The close button closes the panels, subject to their policy.
        f.manager.panel(p("c"))->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Closable));
        QTest::mouseClick(window->closeButton(), Qt::LeftButton);
        QVERIFY(window->isVisible());
        QVERIFY(f.manager.panel(p("b"))->isOpen());
        f.manager.panel(p("c"))->setFeatures(AllDockFeatures);
        const QPointer<DockFloatingWindow> guard(window);
        QTest::mouseClick(window->closeButton(), Qt::LeftButton);
        QVERIFY(!f.manager.panel(p("b"))->isOpen());
        QVERIFY(!f.manager.panel(p("c"))->isOpen());
        QTRY_VERIFY(!guard);

        // Shown again, they come back in a window with the frame now in force.
        f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Native);
        QVERIFY(f.manager.showPanel(p("b")));
        QVERIFY(f.manager.panel(p("b"))->isFloating());
        QVERIFY(!floatingWindowOf(f, "b")->hasCustomFrame());
    }

    void customFrameCanBeStyled()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Custom);
        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 320, 240)));
        DockFloatingWindow *window = floatingWindowOf(f, "b");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto pixel = [](QWidget *widget, const QPoint &pos) {
            return widget->grab().toImage().pixelColor(pos);
        };
        // Without a style sheet: a hairline in the palette's Mid colour.
        QCOMPARE(pixel(window, QPoint(0, window->height() / 2)).rgb(),
                 window->palette().color(QPalette::Mid).rgb());

        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockFloatingWindow { background: #112233; }
            QFlexDock--DockFloatingWindow[customFrame="true"] { border: 2px solid #ff8800; }
            #dockFloatingTitleBar { background: #445566; }
            #dockFloatingTitle { color: #ffffff; }
            #dockFloatingCloseButton { background: #aa0000; border: none; }
            #dockFloatingMaximizeButton { background: #00aa00; border: none; }
        )"));
        QCoreApplication::processEvents();
        QCOMPARE(pixel(window, QPoint(0, window->height() / 2)), QColor(0xff, 0x88, 0x00));
        QCOMPARE(pixel(window->titleBar(), QPoint(3, 2)), QColor(0x44, 0x55, 0x66));
        QCOMPARE(pixel(window->closeButton(), QPoint(1, 1)), QColor(0xaa, 0, 0));
        QCOMPARE(pixel(window->maximizeButton(), QPoint(1, 1)), QColor(0, 0xaa, 0));
        qApp->setStyleSheet(QString());
    }

    // An application made of floating windows alone: no workspace anywhere.
    void windowsNeedNoWorkspace()
    {
        DockManager manager;
        QHash<QString, QLabel *> labels;
        for (const char *id : {"a", "b", "c"}) {
            labels.insert(p(id), new QLabel(p(id)));
            QVERIFY(manager.registerPanel(p(id), labels.value(p(id))));
        }
        DockManagerPrivate *d = priv(manager);
        QVERIFY(manager.workspaces().isEmpty());

        // A closed panel is shown in a window of its own, which belongs to
        // no other window.
        QVERIFY(manager.floatPanel(p("a"), QRect(40, 40, 400, 300)));
        const DockPanel *a = manager.panel(p("a"));
        QVERIFY(a->isOpen());
        QVERIFY(a->isFloating());
        QVERIFY(!a->workspace());
        QCOMPARE(d->floatingWindows.size(), 1);
        auto *first = qobject_cast<DockFloatingWindow *>(labels[p("a")]->window());
        QVERIFY(first);
        QVERIFY(QTest::qWaitForWindowExposed(first));
        QTRY_COMPARE(first->size(), QSize(400, 300));
        QVERIFY(!first->windowHandle()->transientParent());
        QCOMPARE(first->windowTitle(), p("a"));

        // More tabs for it, and a second window.
        QVERIFY(manager.movePanel(p("b"), p("a"), DockArea::Center));
        QCOMPARE(labels[p("b")]->window(), first);
        QVERIFY(manager.floatPanel(p("c")));
        auto *second = qobject_cast<DockFloatingWindow *>(labels[p("c")]->window());
        QVERIFY(second && second != first);
        QCOMPARE(d->floatingWindows.size(), 2);

        // A tab dragged from one window to the other; its window goes with
        // its last panel.
        const QPointer<DockFloatingWindow> guard(second);
        DropTarget target;
        target.container = first->containerId();
        target.node = first->area()->groupOfPanel(p("a"))->nodeId();
        target.area = DockArea::Center;
        target.tabIndex = 1;
        QVERIFY(d->drag->begin(p("c"), false));
        QVERIFY(d->drag->drop(target));
        QCOMPARE(describe(first->area()->tree()), p("a|c|b"));
        QCOMPARE(d->floatingWindows.size(), 1);
        QTRY_VERIFY(!guard);

        // Torn off again, and closed: shown once more, it has its window back.
        QVERIFY(manager.floatPanel(p("b"), QRect(100, 100, 320, 240)));
        QCOMPARE(d->floatingWindows.size(), 2);
        QVERIFY(manager.hidePanel(p("b")));
        QCOMPARE(d->floatingWindows.size(), 1);
        QVERIFY(manager.floatPanel(p("b")));
        QCOMPARE(d->floatingWindows.size(), 2);
        QCOMPARE(d->state.find(d->state.locate(p("b"))->container)->geometry.size(),
                 QSize(320, 240));
        QVERIFY(manager.hidePanel(p("b")));
        QVERIFY(manager.showPanel(p("b")));
        QVERIFY(manager.panel(p("b"))->isFloating());

        // Nothing is left when every panel is closed, and undo brings the
        // windows back.
        QVERIFY(manager.hidePanels({p("a"), p("b"), p("c")}));
        QVERIFY(d->floatingWindows.isEmpty());
        QVERIFY(d->state.containers.empty());
        QVERIFY(manager.undo());
        QCOMPARE(d->floatingWindows.size(), 2);
        QVERIFY(manager.panel(p("a"))->isOpen());
    }

    // How wide the frame QFlexDock draws is, and how round.
    void customFrameFollowsTheTheme()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Minimal);
        QVERIFY(f.manager.floatPanel(p("a"), QRect(40, 40, 300, 200)));
        DockFloatingWindow *plain = floatingWindowOf(f, "a");
        QCOMPARE(plain->borderWidth(), 4);
        QCOMPARE(plain->cornerRadius(), 0);
        QVERIFY(plain->resizeGrips().isEmpty());
        QVERIFY(!plain->testAttribute(Qt::WA_TranslucentBackground));

        DockTheme theme;
        theme.floatingBorderWidth = 1;
        theme.floatingCornerRadius = 10;
        f.manager.setTheme(theme);
        // Windows that exist keep what they were made with.
        QCOMPARE(plain->borderWidth(), 4);
        QVERIFY(f.manager.floatTabGroup(p("b"), QRect(80, 80, 360, 260)));
        DockFloatingWindow *window = floatingWindowOf(f, "b");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(window->size(), QSize(360, 260));
        QCOMPARE(window->borderWidth(), 1);
        QCOMPARE(window->cornerRadius(), 10);
        QCOMPARE(window->area()->geometry(), window->rect().adjusted(1, 1, -1, -1));

        // Round corners: nothing in the corner itself, the hairline where
        // the edge runs straight, the content inside.
        QVERIFY(window->testAttribute(Qt::WA_TranslucentBackground));
        const QImage image = window->grab().toImage();
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1).alpha(), 0);
        QCOMPARE(image.pixelColor(0, image.height() / 2).rgb(),
                 window->palette().color(QPalette::Mid).rgb());
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2).alpha(), 255);
        grab(window, p("floating-round-frame"));

        // A border too thin to be grabbed: the window is taken hold of over
        // the edge of the content as well.
        QCOMPARE(window->resizeGrips().size(), 4);
        const QPoint leftEdge(2, window->height() / 2);
        QVERIFY(window->area()->geometry().contains(leftEdge));
        QVERIFY(window->resizeGrips().contains(window->childAt(leftEdge)));
        QVERIFY(window->resizeGrips().contains(window->childAt(window->width() / 2, 2)));
        QVERIFY(!window->resizeGrips().contains(window->childAt(8, window->height() / 2)));
        QCOMPARE(window->resizeEdgesAt(leftEdge), Qt::Edges(Qt::LeftEdge));
        QCOMPARE(window->resizeEdgesAt(QPoint(1, 1)), Qt::Edges(Qt::LeftEdge | Qt::TopEdge));
        QCOMPARE(window->resizeEdgesAt(QPoint(window->width() - 2, window->height() - 2)),
                 Qt::Edges(Qt::RightEdge | Qt::BottomEdge));
        QCOMPARE(window->resizeEdgesAt(QPoint(40, 40)), Qt::Edges());
        QWidget *grip = window->childAt(leftEdge);
        QMouseEvent move(QEvent::MouseMove, grip->mapFrom(window, leftEdge),
                         window->mapToGlobal(leftEdge), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(grip, &move);
        QCOMPARE(grip->cursor().shape(), Qt::SizeHorCursor);

        // Maximized, it has neither border nor grips nor corners.
        window->showMaximized();
        const QSize normal(360, 260);
        if (QTest::qWaitFor([&] { return window->isMaximized() && window->size() != normal; }, 3000)) {
            QTRY_COMPARE(window->area()->geometry(), window->rect());
            QVERIFY(!window->resizeGrips().constFirst()->isVisible());
            QCOMPARE(window->resizeEdgesAt(QPoint(1, 1)), Qt::Edges());
            QCOMPARE(window->grab().toImage().pixelColor(0, 0).alpha(), 255);
        }
    }

    // A maximized floating window stays maximized when its layout changes.
    void maximizedWindowKeepsItsSizeWhenTheLayoutChanges()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Minimal);
        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 420, 300)));
        DockFloatingWindow *window = floatingWindowOf(f, "b");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(window->size(), QSize(420, 300));
        window->showMaximized();
        const QSize normal(420, 300);
        if (!QTest::qWaitFor([&] { return window->isMaximized() && window->size() != normal; }, 3000))
            QSKIP("Nothing maximizes windows here");
        const QSize maximized = window->size();

        // Another tab, a tab less, another current tab.
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("d"), p("b"), DockArea::Center));
        QVERIFY(f.manager.hidePanel(p("c")));
        QVERIFY(f.manager.activatePanel(p("b")));
        QTest::qWait(200);
        QVERIFY(window->isMaximized());
        QCOMPARE(window->size(), maximized);
        // What the state remembers is still the plain window.
        const ContainerState *container = priv(f.manager)->state.find(window->containerId());
        QCOMPARE(container->geometry.size(), normal);

        window->showNormal();
        QTRY_VERIFY(!window->isMaximized());
        QTRY_COMPARE(window->size(), normal);
    }

    // Some window systems tell the size of a maximized window before they
    // tell that it is maximized. That size is not the plain window's.
    void sizeToldBeforeTheStateIsNotThePlainWindows()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 420, 300)));
        DockFloatingWindow *window = floatingWindowOf(f, "b");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto remembered = [&] {
            return priv(f.manager)->state.find(window->containerId())->geometry.size();
        };
        QTRY_COMPARE(remembered(), QSize(420, 300));
        QTest::qWait(350); // the window has been like this for a while

        window->resize(900, 700);
        QTRY_COMPARE(remembered(), QSize(900, 700));
        window->setWindowState(Qt::WindowMaximized);
        QCOMPARE(remembered(), QSize(420, 300));
        // And it stays so, whatever is told about the maximized window.
        QTest::qWait(200);
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
        QCOMPARE(remembered(), QSize(420, 300));
        window->showNormal();
        QTRY_VERIFY(!window->isMaximized());
    }

    void outsideDropDefaultFollowsThePlatform()
    {
        DockManager manager;
        const QString platform = QGuiApplication::platformName();
        QCOMPARE(manager.floatsOnOutsideDrop(),
                 platform == QLatin1String("xcb") || platform == QLatin1String("windows")
                     || platform == QLatin1String("cocoa") || onWayland());
        QVERIFY(manager.isDragGhostEnabled());
        // Windows are carried along with a drag on Wayland, by the
        // compositor, and on Windows and macOS, where they are moved from here.
        const bool moved = platform == QLatin1String("windows")
            || platform == QLatin1String("cocoa");
        DockDragController *controller = DockManagerPrivate::get(&manager)->drag;
        QCOMPARE(controller->carriesWindows(), onWayland() || moved);
        QCOMPARE(controller->movesCarriedWindows(), moved);
        manager.setDragGhostEnabled(false);
        QVERIFY(!controller->carriesWindows());
        QVERIFY(!controller->movesCarriedWindows());
    }

    // A ghost that QFlexDock itself keeps at the pointer (Windows, macOS) lets the
    // pointer through, and so cannot be the window. Dropped outside, a
    // window proper takes its place; what Qt reports of the drag says no
    // more than it does without a ghost.
    void ghostMovedFromHereMakesWayForTheWindow()
    {
        if (onWayland())
            QSKIP("On Wayland a drop outside is known by a ghost the compositor carries");
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        DockDragController *controller = priv(f.manager)->drag;

        QVERIFY(controller->begin(p("b"), false));
        QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        QVERIFY(QTest::qWaitForWindowExposed(ghost));
        ghost->move(300, 200);
        const QRect put = ghost->geometry();
        // Nobody took the drop and the button is up: dropped outside.
        controller->finish(Qt::IgnoreAction, ghost, false, true);
        QVERIFY(!controller->isActive());
        QVERIFY(f.manager.panel(p("b"))->isFloating());
        QCOMPARE(describe(f.a), p("H(a, c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 1);
        const DockFloatingWindow *window = floatingWindowOf(f, "b");
        QVERIFY(window && window != ghost.data() && !window->isGhost());
        QVERIFY(!window->windowFlags().testFlag(Qt::WindowTransparentForInput));
        QCOMPARE(priv(f.manager)->state.find(window->containerId())->geometry, put);
        QTRY_COMPARE(window->size(), put.size());
        QVERIFY(!ghost || !ghost->isVisible());
        QTRY_VERIFY(!ghost);

        // Cancelled from the keyboard, the button is still down: nothing
        // floats. (Windows and macOS ask the system for the button, which no
        // test holds.)
        if (QGuiApplication::platformName() == QLatin1String("windows")
            || QGuiApplication::platformName() == QLatin1String("cocoa")) {
            return;
        }
        QVERIFY(f.manager.undo());
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QWidget *content = f.widgets[p("a")];
        const QPoint held = content->mapTo(&f.windowA, content->rect().center());
        QTest::mousePress(f.windowA.windowHandle(), Qt::LeftButton, {}, held);
        QVERIFY(controller->begin(p("b"), false));
        ghost = controller->createGhost();
        QVERIFY(ghost);
        controller->finish(Qt::IgnoreAction, ghost, false, true);
        QTest::mouseRelease(f.windowA.windowHandle(), Qt::LeftButton, {}, held);
        QVERIFY(!controller->isActive());
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
        QTRY_VERIFY(!ghost);
    }

    // A floating window that is itself moved along with a drag lets the
    // pointer through only where another window with a dock area is: what
    // tells is where the windows of the manager are.
    void dockWindowsAtAPoint()
    {
        TwoWindows f;
        f.windowA.move(20, 20);
        f.windowB.move(940, 40);
        f.show();
        buildLayout(f);
        QVERIFY(f.manager.floatPanel(p("a"), QRect(60, 60, 320, 240)));
        DockFloatingWindow *window = floatingWindowOf(f, "a");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const DockDragController *controller = priv(f.manager)->drag;

        // The window of a workspace, a floating window; nothing elsewhere.
        const QRect all =
            f.windowA.frameGeometry() | f.windowB.frameGeometry() | window->frameGeometry();
        QVERIFY(controller->dockWindowAt(f.windowA.frameGeometry().center(), window));
        QVERIFY(controller->dockWindowAt(window->frameGeometry().center(), &f.windowA));
        QVERIFY(!controller->dockWindowAt(all.bottomRight() + QPoint(50, 50), window));

        // Not the window that is left out, and not one that is not shown.
        const QPoint corner = f.windowA.frameGeometry().topLeft() + QPoint(5, 5);
        if (!windowPositionsWork() || f.windowB.frameGeometry().contains(corner)
            || window->frameGeometry().contains(corner)) {
            return;
        }
        QVERIFY(controller->dockWindowAt(corner, window));
        QVERIFY(!controller->dockWindowAt(corner, &f.windowA));
        f.windowA.hide();
        QVERIFY(!controller->dockWindowAt(corner, window));
    }

    // Such a ghost may take the drop itself, where there is no dock area
    // (macOS): that is a drop outside whatever else is known of the drag,
    // and the window proper comes where the ghost is.
    void ghostThatTakesTheDropFloatsWhereItIs()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        DockDragController *controller = priv(f.manager)->drag;

        QVERIFY(controller->begin(p("b"), false));
        QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        QVERIFY(QTest::qWaitForWindowExposed(ghost));
        controller->setCarriedWindow(ghost);
        const std::unique_ptr<QMimeData> mime(controller->createMimeData());
        const QPoint at = controller->ghostGrip();
        // Whether the ghost would take a drop at the pointer.
        const auto offered = [&] {
            QT_WARNING_PUSH
            QT_WARNING_DISABLE_DEPRECATED
            QDragEnterEvent enter(at, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(ghost, &enter);
            QDragMoveEvent move(at, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
            move.ignore();
            QCoreApplication::sendEvent(ghost, &move);
            QT_WARNING_POP
            return move.isAccepted();
        };
        // (A drop goes to whoever took the drag when it entered.)
        const auto dropOnGhost = [&](const QMimeData *data) {
            (void)offered();
            QDropEvent drop(at, Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
            drop.ignore();
            QCoreApplication::sendEvent(ghost, &drop);
            return drop.isAccepted();
        };

        // As it is made, it stays out of the drag.
        QVERIFY(!ghost->takesDrops());
        QVERIFY(!offered());
        QVERIFY(!dropOnGhost(mime.get()));

        ghost->setTakesDrops(true);
        QVERIFY(offered());
        // A drop that names no drag of ours is none.
        QMimeData foreign;
        foreign.setData(DockDragController::mimeType(), QByteArrayLiteral("someone else's"));
        QVERIFY(!dropOnGhost(&foreign));
        ghost->move(300, 200);
        const QRect put = ghost->geometry();
        QVERIFY(dropOnGhost(mime.get()));
        controller->setCarriedWindow(nullptr);
        // The drag then comes back as accepted, with no dock area named.
        controller->finish(Qt::MoveAction, ghost, false, true);
        QVERIFY(!controller->isActive());
        QVERIFY(f.manager.panel(p("b"))->isFloating());
        QCOMPARE(describe(f.a), p("H(a, c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 1);
        const DockFloatingWindow *window = floatingWindowOf(f, "b");
        QVERIFY(window && window != ghost.data() && !window->isGhost());
        QCOMPARE(priv(f.manager)->state.find(window->containerId())->geometry, put);
        QTRY_VERIFY(!ghost);

        // That is forgotten with the drag: the next one that comes back
        // accepted without a drop on its ghost floats nothing.
        QVERIFY(f.manager.undo());
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QVERIFY(controller->begin(p("b"), false));
        ghost = controller->createGhost();
        QVERIFY(ghost);
        controller->finish(Qt::MoveAction, ghost, false, true);
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
        QTRY_VERIFY(!ghost);
    }

    void windowMovedAlongLetsThePointerThrough()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        // A floating window whose whole content is dragged: for the time of
        // the drag, and without becoming another window.
        QVERIFY(f.manager.floatPanel(p("a"), QRect(60, 60, 320, 240)));
        DockFloatingWindow *window = floatingWindowOf(f, "a");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const QPointer<QWindow> handle(window->windowHandle());
        window->setCarriedAlong(true);
        QCOMPARE(window->windowHandle(), handle.data());
        QVERIFY(handle->flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(window->windowOpacity() < 1.0);
        QVERIFY(window->isVisible());
        window->setCarriedAlong(false);
        QCOMPARE(window->windowHandle(), handle.data());
        QVERIFY(!handle->flags().testFlag(Qt::WindowTransparentForInput));
        QCOMPARE(window->windowOpacity(), 1.0);
        QVERIFY(window->isVisible());

        // A ghost is made that way before it is shown, and stays on top.
        DockFloatingWindow ghost(priv(f.manager), QString(), DockManager::FloatingFrame::Native);
        ghost.setCarriedAlong(true);
        QVERIFY(ghost.windowFlags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(ghost.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(ghost.testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(ghost.windowOpacity() < 1.0);

        // Shown, it stops letting the pointer through for as long as it
        // takes drops itself, and stays as much of a picture as it was.
        ghost.beginGhost(QPixmap(), QString());
        ghost.present(QRect(80, 80, 200, 150), nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(&ghost));
        const QPointer<QWindow> ghostHandle(ghost.windowHandle());
        QVERIFY(ghostHandle->flags().testFlag(Qt::WindowTransparentForInput));
        ghost.setTakesDrops(true);
        QCOMPARE(ghost.windowHandle(), ghostHandle.data());
        QVERIFY(!ghostHandle->flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(ghost.windowOpacity() < 1.0);
        ghost.setTakesDrops(false);
        QVERIFY(ghostHandle->flags().testFlag(Qt::WindowTransparentForInput));
    }

    void carriedWindowIsNamedInTheMimeData()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        DockDragController *controller = priv(f.manager)->drag;
        QVERIFY(controller->begin(p("b"), false));
        const std::unique_ptr<QMimeData> plain(controller->createMimeData());
        QCOMPARE(plain->formats(), QStringList{DockDragController::mimeType()});

        // The two formats Qt's Wayland platform looks for, in its encoding:
        // the QWindow's address, and where the pointer holds the window.
        QWindow *window = f.windowB.windowHandle();
        const std::unique_ptr<QMimeData> carrying(controller->createMimeData(window, QPoint(18, 12)));
        QVERIFY(carrying->hasFormat(DockDragController::mimeType()));
        QByteArray windowData = carrying->data(p("application/x-qt-mainwindowdrag-window"));
        QByteArray gripData = carrying->data(p("application/x-qt-mainwindowdrag-position"));
        QVERIFY(!windowData.isEmpty() && !gripData.isEmpty());
        qintptr address = 0;
        QDataStream windowStream(&windowData, QIODevice::ReadOnly);
        windowStream >> address;
        QCOMPARE(reinterpret_cast<QWindow *>(address), window);
        QPoint grip;
        QDataStream gripStream(&gripData, QIODevice::ReadOnly);
        gripStream >> grip;
        QCOMPARE(grip, QPoint(18, 12));
        controller->cancel();
    }

    void ghostDroppedOutsideBecomesTheFloatingWindow_data()
    {
        QTest::addColumn<bool>("customFrame");
        QTest::addColumn<bool>("wholeGroup");
        QTest::newRow("native frame, one panel") << false << false;
        QTest::newRow("custom frame, one panel") << true << false;
        QTest::newRow("native frame, whole group") << false << true;
    }

    void ghostDroppedOutsideBecomesTheFloatingWindow()
    {
        QFETCH(bool, customFrame);
        QFETCH(bool, wholeGroup);
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        if (customFrame)
            f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Custom);
        DockDragController *controller = priv(f.manager)->drag;
        const QSize groupSize = areaOf(f.a)->groupOfPanel(p("b"))->size();
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);

        // The drag starts: a ghost appears, and nothing else changes.
        QVERIFY(controller->begin(p("b"), wholeGroup));
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        QVERIFY(QTest::qWaitForWindowExposed(ghost));
        QVERIFY(ghost->isGhost());
        QCOMPARE(ghost->hasCustomFrame(), customFrame);
        QCOMPARE(ghost->windowTitle(), p("b"));
        // It pictures the group at its actual size (a custom frame goes
        // around that), so the tab group seems to come off in one piece.
        const QSize ghostSize = ghost->size();
        if (customFrame) {
            QVERIFY(ghostSize.width() > groupSize.width() && ghostSize.height() > groupSize.height());
            QVERIFY(ghostSize.width() <= groupSize.width() + 16);
        } else {
            QCOMPARE(ghostSize, groupSize);
        }
        // Held somewhere on it, not outside.
        QVERIFY(QRect(QPoint(0, 0), ghostSize).contains(controller->ghostGrip()));
        QVERIFY(!ghost->area()->isVisible());
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(f.widgets[p("b")]->window(), &f.windowA);
        grab(ghost, customFrame ? p("ghost-custom") : p("ghost-native"));

        // Dropped where no dock area takes it (Qt reports that as accepted
        // when a window is carried): the ghost itself is now the floating
        // window. Nothing about its size changes at that moment.
        controller->finish(Qt::MoveAction, ghost, false);
        QVERIFY(ghost);
        QVERIFY(!ghost->isGhost());
        QVERIFY(!controller->isActive());
        // (Asked at once: which window the compositor makes the active one
        // afterwards, and with it which panel, is not the drop's doing.)
        QCOMPARE(f.manager.activePanel(), f.manager.panel(p("b")));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 1);
        QCOMPARE(priv(f.manager)->floatingWindows.begin().value(), ghost.data());
        QCOMPARE(f.widgets[p("b")]->window(), ghost.data());
        QVERIFY(f.manager.panel(p("b"))->isFloating());
        QCOMPARE(f.manager.panel(p("c"))->isFloating(), wholeGroup);
        QCOMPARE(describe(f.a), wholeGroup ? p("a") : p("H(a, c)"));
        QCOMPARE(describe(ghost->area()->tree()), wholeGroup ? p("b|c") : p("b"));
        QVERIFY(ghost->area()->isVisible());
        QVERIFY(f.widgets[p("b")]->isVisible());
        QCoreApplication::processEvents();
        QCOMPARE(ghost->size(), ghostSize);
        QTRY_COMPARE(ghost->area()->size(), groupSize);
        QCOMPARE(changed.size(), 1);

        // From here on it is a floating window like any other.
        QVERIFY(f.manager.undo());
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QTRY_VERIFY(!ghost);
    }

    // The ghost of one tab out of several shows the header as it will be:
    // that tab alone, and right behind it what stands behind the tabs.
    void ghostOfATabKeepsWhatStandsBehindTheTabs()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        QAction add(p("+"));
        for (const char *id : {"b", "c"})
            f.manager.panel(p(id))->setTitleActions({&add}, DockTitlePlace::AfterTabs);
        qApp->setStyleSheet(p("#dockTabActions #dockActionButton { background: #ff0000; border: none; "
                              "min-width: 20px; max-width: 20px; }"));
        QCoreApplication::processEvents();
        const DockTabGroup *group = areaOf(f.a)->groupOfPanel(p("b"));
        const DockTabBar *bar = group->tabBar();
        const QWidget *button = group->widgetForAction(&add);
        QVERIFY(button && button->isVisible());
        // (A corner of the button: its middle has the text on it.)
        const QPoint was = button->mapTo(group, QPoint(2, 2));
        const int tabWidth = bar->tabRect(bar->indexOfPanel(p("b"))).width();
        const QPoint barAt = bar->mapTo(group, QPoint(0, 0));

        DockDragController *controller = priv(f.manager)->drag;
        QVERIFY(controller->begin(p("b"), false));
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        QVERIFY(QTest::qWaitForWindowExposed(ghost));
        QCoreApplication::processEvents();
        grab(ghost, p("floating-ghost-tab-actions"));
        const QImage image = picture(ghost);
        const QColor red(0xff, 0, 0);
        // Behind the one tab, and no longer where it stood behind two.
        QCOMPARE(image.pixelColor(barAt.x() + tabWidth + 2, was.y()), red);
        QVERIFY(image.pixelColor(was) != red);
        controller->finish(Qt::IgnoreAction, ghost, false);
        QTRY_VERIFY(!ghost);

        // A tab from further along stays where it is in the picture, under
        // the pointer that holds it: the place of the tabs before it is empty.
        qApp->setStyleSheet(p("#dockTabActions #dockActionButton { background: #ff0000; border: none; "
                              "min-width: 20px; max-width: 20px; }"
                              "QTabBar::tab { background: #0000ff; padding: 4px 12px; }"
                              "QTabBar::tab:selected { background: #00ff00; }"));
        QVERIFY(f.manager.activatePanel(p("c")));
        QCoreApplication::processEvents();
        const QRect first = bar->tabRect(0).translated(bar->mapTo(group, QPoint(0, 0)));
        const QRect second = bar->tabRect(1).translated(bar->mapTo(group, QPoint(0, 0)));
        const int plusLeft = button->mapTo(group, QPoint(0, 0)).x();
        QCOMPARE(plusLeft, second.right() + 1);
        QVERIFY(controller->begin(p("c"), false));
        const QPointer<DockFloatingWindow> other = controller->createGhost();
        QVERIFY(other);
        QVERIFY(QTest::qWaitForWindowExposed(other));
        QCoreApplication::processEvents();
        grab(other, p("floating-ghost-tab-in-place"));
        const QImage held = picture(other);
        QCOMPARE(held.pixelColor(second.left() + 3, second.center().y()), QColor(0, 0xff, 0));
        QVERIFY(held.pixelColor(first.left() + 3, first.center().y()) != QColor(0, 0, 0xff));
        QCOMPARE(held.pixelColor(plusLeft + 2, was.y()), red);
        // Held where the pointer is on the group: on that tab, if it is.
        controller->finish(Qt::IgnoreAction, other, false);
        QTRY_VERIFY(!other);
        qApp->setStyleSheet(QString());
    }

    // The window a ghost becomes is nobody's child but the owner workspace's:
    // not the window its tab was dragged out of, which it must be able to
    // get behind, and to outlive.
    void ghostDoesNotBelongToTheWindowItCameFrom()
    {
        {
            // Windows of tabs, no workspace: every window stands for itself.
            DockManager manager;
            manager.setFloatsOnOutsideDrop(true);
            QHash<QString, QLabel *> labels;
            for (const char *id : {"a", "b"}) {
                labels.insert(p(id), new QLabel(p(id)));
                QVERIFY(manager.registerPanel(p(id), labels.value(p(id))));
            }
            QVERIFY(manager.floatPanel(p("a"), QRect(40, 40, 400, 300)));
            QVERIFY(manager.movePanel(p("b"), p("a"), DockArea::Center));
            const QPointer<QWidget> source(labels[p("a")]->window());
            QVERIFY(QTest::qWaitForWindowExposed(source));
            DockDragController *controller = priv(manager)->drag;
            QVERIFY(controller->begin(p("b"), false));
            const QPointer<DockFloatingWindow> ghost = controller->createGhost();
            QVERIFY(ghost);
            QVERIFY(QTest::qWaitForWindowExposed(ghost));
            QVERIFY(!ghost->windowHandle()->transientParent());
            controller->finish(Qt::MoveAction, ghost, false);
            QCOMPARE(labels[p("b")]->window(), ghost.data());
            QVERIFY(!ghost->windowHandle()->transientParent());

            // The window the tab came from is closed: the other one stays.
            QVERIFY(source->close());
            QTRY_VERIFY(!source);
            QVERIFY(ghost);
            QVERIFY(ghost->isVisible());
            QVERIFY(manager.panel(p("b"))->isOpen());
            QVERIFY(!manager.panel(p("a"))->isOpen());
        }

        // With a workspace, floating windows stay above its window, also
        // one that came out of another floating window.
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        QVERIFY(f.manager.floatTabGroup(p("b"), QRect(60, 60, 360, 260)));
        DockFloatingWindow *floating = floatingWindowOf(f, "b");
        QVERIFY(QTest::qWaitForWindowExposed(floating));
        QCOMPARE(floating->windowHandle()->transientParent(), f.windowA.windowHandle());
        DockDragController *controller = priv(f.manager)->drag;
        QVERIFY(controller->begin(p("c"), false));
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        QVERIFY(QTest::qWaitForWindowExposed(ghost));
        QCOMPARE(ghost->windowHandle()->transientParent(), f.windowA.windowHandle());
        controller->finish(Qt::MoveAction, ghost, false);
        QCOMPARE(f.widgets[p("c")]->window(), ghost.data());
        QCOMPARE(ghost->windowHandle()->transientParent(), f.windowA.windowHandle());
    }

    void cancelledGhostDragLeavesNothingBehind()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        DockDragController *controller = priv(f.manager)->drag;
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);

        QVERIFY(controller->begin(p("b"), false));
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost && ghost->isVisible());
        // Escape: Qt reports a cancelled drag as ignored.
        controller->finish(Qt::IgnoreAction, ghost, false);
        QVERIFY(!controller->isActive());
        QVERIFY(!ghost || !ghost->isVisible());
        QTRY_VERIFY(!ghost);
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
        QCOMPARE(changed.size(), 0);
        QVERIFY(!f.manager.canUndo());
        QVERIFY(f.widgets[p("b")]->isVisible());
    }

    void ghostIsDiscardedWhenTheDropIsTakenOrFloatingIsOff()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        QVERIFY(f.b->addPanel(p("d")));
        DockDragController *controller = priv(f.manager)->drag;

        // Dropped onto a dock area: docked there, the ghost goes away.
        f.manager.setFloatsOnOutsideDrop(true);
        QVERIFY(controller->begin(p("b"), false));
        QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        const DropTarget target{p("B"), f.b->layoutTree().findPanel(p("d"))->id, DockArea::Right, -1, 0.5};
        QVERIFY(controller->drop(target));
        controller->finish(Qt::MoveAction, ghost, false);
        QCOMPARE(describe(f.b), p("H(d, b)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
        QTRY_VERIFY(!ghost);

        // Floating by dropping outside turned off: the unclaimed drop is void.
        f.manager.setFloatsOnOutsideDrop(false);
        QVERIFY(controller->begin(p("c"), false));
        ghost = controller->createGhost();
        QVERIFY(ghost);
        controller->finish(Qt::MoveAction, ghost, false);
        QVERIFY(!f.manager.panel(p("c"))->isFloating());
        QCOMPARE(describe(f.a), p("H(a, c)"));
        QTRY_VERIFY(!ghost);

        // A panel that may not float gets no ghost in the first place.
        f.manager.setFloatsOnOutsideDrop(true);
        f.manager.panel(p("c"))->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Floatable));
        QVERIFY(controller->begin(p("c"), false));
        QVERIFY(!controller->createGhost());
        controller->finish(Qt::MoveAction, nullptr, false);
        QVERIFY(!f.manager.panel(p("c"))->isFloating());
        QVERIFY(!controller->isActive());
    }

    // Dragging the only tab of a floating window out of it again must not
    // spawn a window every time: there is nothing left to float.
    void draggingAFloatingWindowsOnlyContentCreatesNoMoreWindows()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        DockDragController *controller = priv(f.manager)->drag;
        const auto dockWindows = [] {
            int count = 0;
            for (const QWidget *widget : QApplication::topLevelWidgets()) {
                if (qobject_cast<const DockFloatingWindow *>(widget) && widget->isVisible())
                    ++count;
            }
            return count;
        };
        QVERIFY(f.manager.floatPanel(p("a"), QRect(60, 60, 320, 240)));
        const QPointer<DockFloatingWindow> window = floatingWindowOf(f, "a");
        QCOMPARE(dockWindows(), 1);
        f.manager.clearUndoHistory();
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);

        for (int i = 0; i < 3; ++i) {
            QVERIFY(controller->begin(p("a"), false));
            // Its whole content: the window itself is what a drag carries.
            QCOMPARE(controller->windowDraggedWhole(), window.data());
            // Even if a ghost were made and dropped outside, it is discarded.
            const QPointer<DockFloatingWindow> ghost = controller->createGhost();
            QVERIFY(ghost);
            controller->finish(Qt::MoveAction, ghost, false);
            QVERIFY(!controller->isActive());
            QVERIFY(!ghost || !ghost->isVisible());
            QTRY_VERIFY(!ghost);
            QCOMPARE(dockWindows(), 1);
            QCOMPARE(priv(f.manager)->floatingWindows.size(), 1);
            QCOMPARE(f.widgets[p("a")]->window(), window.data());
        }
        // The same when the window itself was carried and put down somewhere.
        QVERIFY(controller->begin(p("a"), false));
        controller->finish(Qt::MoveAction, nullptr, true);
        QCOMPARE(dockWindows(), 1);
        QCOMPARE(changed.size(), 0);
        QVERIFY(!f.manager.canUndo());

        // A tab group that is all there is in the window counts as well; one
        // tab out of several does not, and floats into a window of its own.
        QVERIFY(f.manager.movePanel(p("d"), p("a"), DockArea::Center));
        QVERIFY(controller->begin(p("a"), true));
        QCOMPARE(controller->windowDraggedWhole(), window.data());
        controller->cancel();
        QVERIFY(controller->begin(p("d"), false));
        QVERIFY(!controller->windowDraggedWhole());
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        controller->finish(Qt::MoveAction, ghost, false);
        QCOMPARE(dockWindows(), 2);
        QCOMPARE(f.widgets[p("d")]->window(), ghost.data());
        QCOMPARE(f.widgets[p("a")]->window(), window.data());
        // Panels docked in a workspace never count.
        QVERIFY(controller->begin(p("b"), true));
        QVERIFY(!controller->windowDraggedWhole());
        controller->cancel();
        // And the API refuses to give a lone floating panel a second window.
        QVERIFY(!priv(f.manager)->floatPanels(p("a"), false, QRect(0, 0, 100, 100), ghost));
        QCOMPARE(dockWindows(), 2);
    }

    // A compositor announces a drag to the window it starts on. When a
    // floating window is dragged by its own tab, that is the very window being
    // carried. Being offered the drag there must not be mistaken for "this
    // compositor cannot carry windows", or nothing could be floated by
    // dragging from then on.
    void carriedWindowMayBeOfferedTheDragItStartedOn()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        DockDragController *controller = priv(f.manager)->drag;
        QVERIFY(f.manager.floatPanel(p("a"), QRect(60, 60, 320, 240)));
        DockFloatingWindow *window = floatingWindowOf(f, "a");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        DockAreaWidget *area = window->area();
        const QPoint tab(30, 12);

        // Its tab is dragged: the window itself is carried, and hears of the drag.
        QVERIFY(controller->begin(p("a"), false));
        QCOMPARE(controller->windowDraggedWhole(), window);
        controller->setCarriedWindow(window);
        const std::unique_ptr<QMimeData> mime(controller->createMimeData());
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QDragEnterEvent enter(tab, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(area, &enter);
        // The window moves with the pointer: seen from the window, the
        // pointer stays where it is (give or take).
        QDragMoveEvent still(tab + QPoint(2, 1), Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(area, &still);
        QT_WARNING_POP
        QVERIFY(!still.isAccepted());            // it is not its own drop target
        QVERIFY(!area->overlay()->isVisible());
        QVERIFY(!controller->carryingUnsupported());

        // Docked by dropping it on the workspace.
        const NodeId group = f.a->layoutTree().findPanel(p("b"))->id;
        QVERIFY(controller->drop(DropTarget{p("A"), group, DockArea::Left, -1, 0.5}));
        controller->setCarriedWindow(nullptr);
        controller->finish(Qt::MoveAction, nullptr, true);
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);

        // And it can be torn off again like before: a ghost, dropped outside.
        QVERIFY(!controller->carryingUnsupported());
        QVERIFY(controller->begin(p("a"), false));
        QVERIFY(!controller->windowDraggedWhole());
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        controller->finish(Qt::MoveAction, ghost, false);
        QVERIFY(f.manager.panel(p("a"))->isFloating());
        QCOMPARE(f.widgets[p("a")]->window(), ghost.data());
        QCOMPARE(describe(f.a), p("b|c"));
    }

    void windowThePointerTravelsAcrossIsNotCarried()
    {
        // What does show that a compositor is not carrying a window: the
        // pointer moving across it. A ghost in that position is merely in the
        // way; it hides, and the drag ends like any drag without a ghost.
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatsOnOutsideDrop(true);
        DockDragController *controller = priv(f.manager)->drag;
        if (controller->movesCarriedWindows())
            QSKIP("Windows are moved by QFlexDock here: there is no compositor to find out about");
        QVERIFY(controller->begin(p("b"), false));
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost && ghost->isVisible());
        controller->setCarriedWindow(ghost);
        const std::unique_ptr<QMimeData> mime(controller->createMimeData());
        const auto dragOver = [&](QEvent::Type type, const QPoint &pos) {
            QT_WARNING_PUSH
            QT_WARNING_DISABLE_DEPRECATED
            if (type == QEvent::DragEnter) {
                QDragEnterEvent event(pos, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(ghost, &event);
            } else {
                QDragMoveEvent event(pos, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(ghost, &event);
            }
            QT_WARNING_POP
        };

        // Offered the drag, pointer at rest on it: fine.
        dragOver(QEvent::DragEnter, QPoint(18, 12));
        dragOver(QEvent::DragMove, QPoint(20, 14));
        QVERIFY(ghost->isVisible());
        QVERIFY(!controller->carryingUnsupported());

        // The pointer crosses it: it is not following.
        dragOver(QEvent::DragMove, QPoint(70, 60));
        QVERIFY(controller->carryingUnsupported());
        QVERIFY(!ghost->isVisible());
        QVERIFY(!controller->carriesWindows());

        // However the drag is then reported to have ended, that ghost does not
        // turn into a window.
        controller->setCarriedWindow(nullptr);
        controller->finish(Qt::MoveAction, ghost, false);
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
        QTRY_VERIFY(!ghost);

        // Windows other than the carried one are not affected by any of this.
        DockManager other;
        QVERIFY(!DockManagerPrivate::get(&other)->drag->noteDragOver(&f.windowB, QPoint(1, 1)));
    }

    void wholeFloatingWindowCanBeDocked()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Custom);
        DockDragController *controller = priv(f.manager)->drag;

        // A floating window with a split inside: b above d.
        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 320, 240)));
        QVERIFY(f.manager.movePanel(p("d"), p("b"), DockArea::Bottom));
        const QPointer<DockFloatingWindow> window = floatingWindowOf(f, "b");
        const QString id = window->containerId();
        QCOMPARE(describe(window->area()->tree()), p("V(b, d)"));

        QVERIFY(!controller->beginContainer(p("A")));       // not a floating window
        QVERIFY(!controller->beginContainer(p("nowhere")));
        const DragSession *session = controller->beginContainer(id);
        QVERIFY(session);
        QCOMPARE(session->panels, QStringList({p("b"), p("d")}));
        QVERIFY(session->wholeGroup);
        QVERIFY(!session->sourceIsTabs);

        // A split cannot become the tabs of a group, but docks on any edge.
        DockAreaWidget *area = areaOf(f.a);
        const NodeId groupA = f.a->layoutTree().findPanel(p("a"))->id;
        const DockAreas onGroup = priv(f.manager)->allowedDropAreas(*session, p("A"), groupA);
        QVERIFY(!onGroup.testFlag(DockArea::Center));
        QCOMPARE(onGroup & EdgeDockAreas, EdgeDockAreas);
        const QPoint pos = area->groupOfPanel(p("a"))->geometry().center();
        QVERIFY(!area->candidateAt(pos, *session).valid);

        // Put down somewhere that is no dock area: it was moved, that is all.
        controller->finish(Qt::MoveAction, nullptr, true);
        QVERIFY(!controller->isActive());
        QCOMPARE(describe(window->area()->tree()), p("V(b, d)"));
        QVERIFY(window && window->isVisible());

        // Put down on the edge of a group: the whole tree moves in.
        QVERIFY(controller->beginContainer(id));
        QVERIFY(controller->drop(DropTarget{p("A"), groupA, DockArea::Left, -1, 0.5}));
        controller->finish(Qt::MoveAction, nullptr, true);
        QCOMPARE(describe(f.a), p("H(V(b, d), a, c)"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
        QCOMPARE(f.widgets[p("b")]->window(), &f.windowA);
        QCOMPARE(f.widgets[p("d")]->window(), &f.windowA);
        QTRY_VERIFY(!window);

        // A window holding a single tab group can also join another group.
        QVERIFY(f.manager.floatPanel(p("d"), QRect(60, 60, 320, 240)));
        QVERIFY(f.manager.movePanel(p("e"), p("d"), DockArea::Center));
        const QString second = floatingWindowOf(f, "d")->containerId();
        session = controller->beginContainer(second);
        QVERIFY(session && session->sourceIsTabs);
        QVERIFY(priv(f.manager)->allowedDropAreas(*session, p("A"), groupA).testFlag(DockArea::Center));
        QVERIFY(controller->drop(DropTarget{p("A"), groupA, DockArea::Center, -1, -1}));
        QCOMPARE(f.a->layoutTree().findPanel(p("a"))->panels, QStringList({p("a"), p("d"), p("e")}));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);

        // Panels that may not be moved keep their window from being docked.
        QVERIFY(f.manager.floatPanel(p("e"), QRect(60, 60, 320, 240)));
        f.manager.panel(p("e"))->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Movable));
        session = controller->beginContainer(floatingWindowOf(f, "e")->containerId());
        QVERIFY(session);
        QVERIFY(!priv(f.manager)->allowedDropAreas(*session, p("A"), groupA));
        controller->cancel();

        // Where windows cannot be carried along with a drag, the title row
        // just moves the window (through the window system).
        if (!onWayland())
            QVERIFY(!controller->requestWindowDrag(floatingWindowOf(f, "e")->containerId(), QPoint(5, 5)));
    }
};

QTEST_MAIN(tst_Floating)
#include "tst_floating.moc"
