// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include "widgets/DockAutoHide.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockTabBar.h"

#include <QtTest/QSignalSpy>
#include <QtWidgets/QMenu>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>

using namespace QFlexDock;
using namespace TestUtils;

class tst_Manager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registerPanels()
    {
        DockManager manager;
        QMainWindow window;
        DockWorkspace *workspace = manager.createWorkspace(p("main"));
        window.setCentralWidget(workspace);
        QCOMPARE(workspace->workspaceId(), p("main"));
        QCOMPARE(manager.workspace(p("main")), workspace);
        QVERIFY(!manager.createWorkspace(p("main"))); // id taken

        auto *content = new QLabel(p("x"));
        DockPanel *panel = manager.registerPanel(p("x"), content, p("Title X"));
        QVERIFY(panel);
        QCOMPARE(panel->id(), p("x"));
        QCOMPARE(panel->title(), p("Title X"));
        QCOMPARE(panel->widget(), content);
        QCOMPARE(manager.panel(p("x")), panel);
        QVERIFY(!panel->isOpen());
        QVERIFY(content->isHidden());
        QVERIFY(content->parent()); // owned by the manager from now on

        QLabel other;
        QVERIFY(!manager.registerPanel(p("x"), &other));
        QCOMPARE(manager.lastError().error(), DockError::DuplicatePanel);
        QVERIFY(!manager.registerPanel(QString(), &other));
        QCOMPARE(manager.lastError().error(), DockError::InvalidArgument);
        QVERIFY(!manager.registerPanel(p("y"), nullptr));
        QVERIFY(!manager.registerPanel(p("y"), content)); // one widget, one panel
        QCOMPARE(manager.panels().size(), 1);
        QVERIFY(!other.parent());

        // The default title is the id.
        QCOMPARE(manager.registerPanel(p("z"), new QLabel)->title(), p("z"));
    }

    void specUsage()
    {
        // The basic usage shown in the README, two windows sharing one manager.
        DockManager manager;
        QMainWindow windowA;
        QMainWindow windowB;
        auto *workspaceA = manager.createWorkspace();
        auto *workspaceB = manager.createWorkspace();
        windowA.setCentralWidget(workspaceA);
        windowB.setCentralWidget(workspaceB);
        QVERIFY(workspaceA->workspaceId() != workspaceB->workspaceId());

        auto *sceneWidget = new QLabel;
        auto *inspectorWidget = new QLabel;
        manager.registerPanel(p("scene"), sceneWidget);
        manager.registerPanel(p("inspector"), inspectorWidget);
        QVERIFY(workspaceA->addPanel(p("scene"), DockPosition::Center));
        QVERIFY(workspaceB->addPanel(p("inspector"), DockPosition::Center));
        QVERIFY(manager.movePanel(p("inspector"), workspaceA, DockPosition::Right));
        QCOMPARE(describe(workspaceA), p("H(scene, inspector)"));
        QCOMPARE(describe(workspaceB), p("<empty>"));
        QVERIFY(manager.togglePanel(p("inspector")));
        QCOMPARE(describe(workspaceA), p("scene"));
    }

    void buildNestedLayout()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        QVERIFY(f.manager.movePanel(p("d"), p("a"), DockArea::Center));
        QVERIFY(f.a->addPanel(p("e"), DockArea::Left, 0.2));
        QVERIFY(f.a->addPanel(p("f"), DockArea::Bottom));
        QCOMPARE(describe(f.a), p("V(H(e, a|d, V(b, c)), f)"));
        QCOMPARE(f.a->panels(), QStringList({p("e"), p("a"), p("d"), p("b"), p("c"), p("f")}));
        QVERIFY(f.a->layoutTree().validate());

        // The widgets follow: groups and handles tile the dock area exactly.
        DockAreaWidget *area = areaOf(f.a);
        QCOMPARE(area->groups().size(), 5);
        QRegion covered;
        for (const DockTabGroup *group : area->groups()) {
            QVERIFY(group->isVisible());
            QVERIFY(!covered.intersects(group->geometry()));
            covered += group->geometry();
        }
        for (const SolvedHandle &handle : area->solved().handles) {
            QVERIFY(!covered.intersects(handle.rect));
            covered += handle.rect;
        }
        QCOMPARE(covered, QRegion(area->contentsRect()));

        // Current tabs are on screen, the others are hidden but kept alive.
        QVERIFY(f.widgets[p("d")]->isVisible());
        QVERIFY(!f.widgets[p("a")]->isVisible());
        QVERIFY(f.widgets[p("f")]->isVisible());
        // "e" asked for 20% of the width; it gets that, or what its title row
        // needs if the host style makes that wider.
        const DockTabGroup *e = area->groupOfPanel(p("e"));
        const int share = int(std::lround((area->width() - 2 * area->handleWidth()) * 0.2));
        QVERIFY(qAbs(e->width() - qMax(share, e->sizeLimits().min.width())) <= 1);
        grab(&f.windowA, p("nested-layout"));
    }

    void moveAcrossWindowsKeepsTheWidget()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.b->addPanel(p("c")));

        DockPanel *panel = f.manager.panel(p("a"));
        QWidget *widget = f.widgets[p("a")];
        QCOMPARE(widget->window(), &f.windowA);
        QCOMPARE(panel->workspace(), f.a);

        QSignalSpy aboutToReparent(panel, &DockPanel::aboutToBeReparented);
        QSignalSpy reparented(panel, &DockPanel::reparented);
        QSignalSpy topLevelChanged(panel, &DockPanel::topLevelChanged);
        QSignalSpy aboutToMove(&f.manager, &DockManager::panelAboutToMove);
        QSignalSpy moved(&f.manager, &DockManager::panelMoved);
        QSignalSpy windowChanged(&f.manager, &DockManager::panelWindowChanged);

        QVERIFY(f.manager.movePanel(p("a"), f.b, DockArea::Right));

        // Same panel object, same widget instance, new window.
        QCOMPARE(f.manager.panel(p("a")), panel);
        QCOMPARE(panel->widget(), widget);
        QCOMPARE(widget->window(), &f.windowB);
        QVERIFY(widget->isVisible());
        QCOMPARE(panel->workspace(), f.b);
        QCOMPARE(describe(f.a), p("b"));
        QCOMPARE(describe(f.b), p("H(c, a)"));

        // Moved in one step: exactly one reparent, straight to the new group.
        QCOMPARE(aboutToReparent.size(), 1);
        QCOMPARE(reparented.size(), 1);
        QCOMPARE(reparented.at(0).at(0).toBool(), true);
        QCOMPARE(topLevelChanged.size(), 1);
        QCOMPARE(topLevelChanged.at(0).at(0).value<QWidget *>(), &f.windowB);
        QCOMPARE(windowChanged.size(), 1);
        QCOMPARE(aboutToMove.size(), 1);
        QCOMPARE(moved.size(), 1);
        QCOMPARE(moved.at(0).at(0).value<DockPanel *>(), panel);

        // Within the same window the top level stays the same.
        QVERIFY(f.manager.movePanel(p("a"), p("c"), DockArea::Top));
        QCOMPARE(describe(f.b), p("V(a, c)"));
        QCOMPARE(reparented.size(), 2);
        QCOMPARE(reparented.at(1).at(0).toBool(), false);
        QCOMPARE(topLevelChanged.size(), 1);
    }

    void moveTabGroupAcrossWindows()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Right));
        QVERIFY(f.b->addPanel(p("d")));
        QCOMPARE(describe(f.a), p("H(a|b, c)"));

        QVERIFY(f.manager.moveTabGroup(p("a"), p("d"), DockArea::Bottom));
        QCOMPARE(describe(f.a), p("c"));
        QCOMPARE(describe(f.b), p("V(d, a|b)"));
        QCOMPARE(f.widgets[p("a")]->window(), &f.windowB);
        QCOMPARE(f.widgets[p("b")]->window(), &f.windowB);
        QVERIFY(f.widgets[p("b")]->isVisible()); // b was and stays the current tab

        // A whole group can also be merged into another, or docked on a workspace.
        QVERIFY(f.manager.moveTabGroup(p("a"), p("c"), DockArea::Center));
        QCOMPARE(describe(f.a), p("c|a|b"));
        QVERIFY(f.manager.moveTabGroup(p("c"), f.b, DockArea::Left));
        QCOMPARE(describe(f.a), p("<empty>"));
        QCOMPARE(describe(f.b), p("H(c|a|b, d)"));
        QCOMPARE(f.manager.moveTabGroup(p("a"), p("b"), DockArea::Left).error(),
                 DockError::InvalidArgument); // onto itself
    }

    void hideAndShowReturnToTheSamePlace()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("d"), p("b"), DockArea::Bottom));
        QCOMPARE(describe(f.a), p("H(a, V(b|c, d))"));

        DockPanel *c = f.manager.panel(p("c"));
        QSignalSpy openChanged(c, &DockPanel::openChanged);
        QVERIFY(f.manager.hidePanel(p("c")));
        QVERIFY(!c->isOpen());
        QVERIFY(!f.widgets[p("c")]->isVisible());
        QCOMPARE(describe(f.a), p("H(a, V(b, d))"));
        QVERIFY(f.manager.hidePanel(p("c"))); // closing a closed panel is fine

        QVERIFY(f.manager.showPanel(p("c")));
        QCOMPARE(describe(f.a), p("H(a, V(b|c, d))"));
        QCOMPARE(openChanged.size(), 2);
        QCOMPARE(f.manager.activePanel(), c);

        // Close the whole group: the first one back re-creates the split, the
        // second finds its former tab neighbour.
        QVERIFY(f.manager.hidePanel(p("c")));
        QVERIFY(f.manager.hidePanel(p("b")));
        QCOMPARE(describe(f.a), p("H(a, d)"));
        QVERIFY(f.manager.showPanel(p("b")));
        QCOMPARE(describe(f.a), p("H(a, V(b, d))"));
        QVERIFY(f.manager.showPanel(p("c")));
        QCOMPARE(describe(f.a), p("H(a, V(b|c, d))"));

        // A panel that was never placed goes to the first workspace.
        QVERIFY(f.manager.showPanel(p("e")));
        QVERIFY(f.a->panels().contains(p("e")));
    }

    void floatAndDockBack()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right, -1, 0.3));
        DockPanel *b = f.manager.panel(p("b"));
        QSignalSpy floatingChanged(b, &DockPanel::floatingChanged);

        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 320, 240)));
        QVERIFY(b->isFloating());
        QVERIFY(b->isOpen());
        QCOMPARE(b->workspace(), f.a); // owner of the floating window
        QCOMPARE(describe(f.a), p("a"));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 1);
        DockFloatingWindow *window = priv(f.manager)->floatingWindows.begin().value();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(f.widgets[p("b")]->window(), window);
        QVERIFY(f.widgets[p("b")]->isVisible());
        QCOMPARE(window->size(), QSize(320, 240));
        QCOMPARE(window->windowTitle(), p("b"));
        QCOMPARE(floatingChanged.size(), 1);

        // Other panels can be docked into the floating window.
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        QCOMPARE(describe(window->area()->tree()), p("V(b, c)"));
        QCOMPARE(f.widgets[p("c")]->window(), window);

        // Docking back returns to the remembered spot, with its share.
        const QPointer<DockFloatingWindow> guard(window);
        QVERIFY(f.manager.dockPanel(p("b")));
        QVERIFY(!b->isFloating());
        QCOMPARE(describe(f.a), p("H(a, b)"));
        QVERIFY(qAbs(f.a->layoutTree().root()->children[1].weight - 0.3) < 1e-9);
        QVERIFY(f.manager.dockPanel(p("c")));
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 0);
        QTRY_VERIFY(!guard); // the emptied window is gone

        // A whole tab group floats together.
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
        QVERIFY(f.manager.floatTabGroup(p("b")));
        QCOMPARE(describe(f.a), p("a"));
        QVERIFY(b->isFloating());
        QVERIFY(f.manager.panel(p("c"))->isFloating());
        QCOMPARE(f.widgets[p("b")]->window(), f.widgets[p("c")]->window());
    }

    void closingAFloatingWindowClosesItsPanels()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 320, 240)));
        const QPointer<QWidget> content = f.widgets[p("b")];
        const QPointer<DockFloatingWindow> window =
            priv(f.manager)->floatingWindows.begin().value();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QVERIFY(window->close());
        DockPanel *b = f.manager.panel(p("b"));
        QVERIFY(b);               // still registered
        QVERIFY(!b->isOpen());
        QVERIFY(content);         // the content is not destroyed
        QVERIFY(!content->isVisible());
        QTRY_VERIFY(!window);

        // Showing it again brings back the floating window where it was.
        QVERIFY(f.manager.showPanel(p("b")));
        QVERIFY(b->isFloating());
        QCOMPARE(content->window()->size(), QSize(320, 240));
        QCOMPARE(describe(f.a), p("a"));
    }

    void anUnclosablePanelKeepsItsFloatingWindowOpen()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        f.manager.panel(p("b"))->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Closable));
        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 320, 240)));
        DockFloatingWindow *window = priv(f.manager)->floatingWindows.begin().value();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QVERIFY(!window->close());
        QVERIFY(window->isVisible());
        QVERIFY(f.manager.panel(p("b"))->isOpen());
        // The application itself may still close it.
        QVERIFY(f.manager.hidePanel(p("b")));
        QVERIFY(!f.manager.panel(p("b"))->isOpen());
    }

    void maximizeAndRestore()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        const QString before = describe(f.a);
        DockAreaWidget *area = areaOf(f.a);
        const QRect normal = area->groupOfPanel(p("b"))->geometry();

        QVERIFY(f.manager.maximizePanel(p("b")));
        QCOMPARE(f.manager.maximizedPanel(), p("b"));
        QCOMPARE(f.manager.maximizedPanel(f.a), p("b"));
        QCOMPARE(f.manager.maximizedPanel(f.b), QString());
        QCOMPARE(describe(f.a), before); // the tree is untouched
        QCOMPARE(area->groupOfPanel(p("b"))->geometry(), area->contentsRect());
        QVERIFY(area->groupOfPanel(p("b"))->isMaximized());
        QVERIFY(!area->groupOfPanel(p("a"))->isVisible());
        QVERIFY(!area->groupOfPanel(p("c"))->isVisible());
        QVERIFY(area->visibleHandles().isEmpty());
        grab(&f.windowA, p("maximized"));

        // Activating a panel hidden by the maximized one maximizes that instead.
        QVERIFY(f.manager.activatePanel(p("a")));
        QCOMPARE(f.manager.maximizedPanel(), p("a"));
        QVERIFY(area->groupOfPanel(p("a"))->isVisible());

        QVERIFY(f.manager.restoreMaximizedPanel());
        QCOMPARE(f.manager.maximizedPanel(), QString());
        QCOMPARE(describe(f.a), before);
        QCOMPARE(area->groupOfPanel(p("b"))->geometry(), normal);
        QVERIFY(area->groupOfPanel(p("a"))->isVisible());

        // Closing the maximized panel ends the maximized state.
        QVERIFY(f.manager.maximizePanel(p("c")));
        QVERIFY(f.manager.hidePanel(p("c")));
        QCOMPARE(f.manager.maximizedPanel(), QString());
        QVERIFY(area->groupOfPanel(p("a"))->isVisible());
    }

    void activePanelFollowsApiAndClicks()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
        QSignalSpy activeChanged(&f.manager, &DockManager::activePanelChanged);

        QVERIFY(f.manager.activatePanel(p("a")));
        QCOMPARE(f.manager.activePanel(), f.manager.panel(p("a")));
        QVERIFY(f.manager.panel(p("a"))->isActive());
        QVERIFY(areaOf(f.a)->groupOfPanel(p("a"))->isActive());
        QVERIFY(!areaOf(f.a)->groupOfPanel(p("b"))->isActive());

        // Activating a background tab brings it to the front.
        QVERIFY(f.manager.activatePanel(p("b")));
        QCOMPARE(f.a->layoutTree().findPanel(p("b"))->active, p("b"));
        QVERIFY(f.widgets[p("b")]->isVisible());
        QVERIFY(!f.widgets[p("c")]->isVisible());
        QVERIFY(!f.manager.panel(p("a"))->isActive());

        // A click inside a panel makes it the active one.
        QTest::mouseClick(f.widgets[p("a")], Qt::LeftButton);
        QCOMPARE(f.manager.activePanel(), f.manager.panel(p("a")));
        QVERIFY(activeChanged.size() >= 3);

        QCOMPARE(f.manager.activatePanel(p("nope")).error(), DockError::UnknownPanel);
        QCOMPARE(f.manager.activatePanel(p("d")).error(), DockError::NotPlaced);
    }

    void tabBarInteraction()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        QVERIFY(f.a->addPanel(p("d"), DockArea::Right));
        DockTabGroup *group = areaOf(f.a)->groupOfPanel(p("a"));
        DockTabBar *bar = group->tabBar();
        QCOMPARE(bar->count(), 3);
        QCOMPARE(bar->currentIndex(), 2);

        // Click a tab.
        QTest::mouseClick(bar, Qt::LeftButton, {}, bar->tabRect(0).center());
        QCOMPARE(f.a->layoutTree().findPanel(p("a"))->active, p("a"));
        QVERIFY(f.widgets[p("a")]->isVisible());
        QCOMPARE(f.manager.activePanel(), f.manager.panel(p("a")));

        // Its close button.
        const auto side = QTabBar::ButtonPosition(
            bar->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, bar));
        QWidget *closeButton = bar->tabButton(1, side);
        QVERIFY(closeButton);
        QTest::mouseClick(closeButton, Qt::LeftButton);
        QVERIFY(!f.manager.panel(p("b"))->isOpen());
        QCOMPARE(describe(f.a), p("H(a|c, d)"));

        // Middle click closes too, but not a panel that may not be closed.
        f.manager.panel(p("c"))->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Closable));
        QVERIFY(!bar->tabButton(1, side));
        QTest::mouseClick(bar, Qt::MiddleButton, {}, bar->tabRect(1).center());
        QVERIFY(f.manager.panel(p("c"))->isOpen());
        QTest::mouseClick(bar, Qt::MiddleButton, {}, bar->tabRect(0).center());
        QVERIFY(!f.manager.panel(p("a"))->isOpen());

        // Double click toggles maximize; so does the button.
        group = areaOf(f.a)->groupOfPanel(p("c"));
        bar = group->tabBar();
        QTest::mouseDClick(bar, Qt::LeftButton, {}, bar->tabRect(0).center());
        QCOMPARE(f.manager.maximizedPanel(), p("c"));
        QTest::mouseClick(group->maximizeButton(), Qt::LeftButton);
        QCOMPARE(f.manager.maximizedPanel(), QString());
    }

    void unregisterAndRegisterAgain()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
        const QPointer<QWidget> content = f.widgets[p("c")];
        const QPointer<DockPanel> panel = f.manager.panel(p("c"));
        QSignalSpy unregistering(&f.manager, &DockManager::panelAboutToBeUnregistered);

        QVERIFY(f.manager.unregisterPanel(p("c")));
        QVERIFY(!f.manager.hasPanel(p("c")));
        QVERIFY(!content); // destroyed with the panel
        QCOMPARE(unregistering.size(), 1);
        QCOMPARE(describe(f.a), p("H(a, b)"));
        QTRY_VERIFY(!panel);
        QCOMPARE(f.manager.unregisterPanel(p("c")).error(), DockError::UnknownPanel);

        // A plugin coming back: the panel reappears where it was.
        auto *again = new QLabel(p("C2"));
        QVERIFY(f.manager.registerPanel(p("c"), again));
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QVERIFY(f.manager.panel(p("c"))->isOpen());

        // Unless its place was deliberately forgotten.
        QVERIFY(f.manager.unregisterPanel(p("c"), DockManager::PlacementMemory::Forget));
        QVERIFY(f.manager.registerPanel(p("c"), new QLabel(p("C3"))));
        QVERIFY(!f.manager.panel(p("c"))->isOpen());
        QCOMPARE(describe(f.a), p("H(a, b)"));
    }

    void releasePanelHandsTheWidgetBack()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        const QPointer<QWidget> content = f.widgets[p("a")];

        QWidget *released = f.manager.releasePanel(p("a"));
        QCOMPARE(released, content.data());
        QVERIFY(!released->parent());
        QVERIFY(released->isHidden());
        QVERIFY(!f.manager.hasPanel(p("a")));
        QCOMPARE(describe(f.a), p("b"));
        QVERIFY(!f.manager.releasePanel(p("a")));
        delete released; // ours again
        QCOMPARE(describe(f.a), p("b"));
    }

    void destroyedContentUnregistersItsPanel()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        delete f.widgets[p("a")];
        QVERIFY(!f.manager.hasPanel(p("a")));
        QCOMPARE(describe(f.a), p("b"));
        QVERIFY(f.a->layoutTree().validate());
    }

    void destroyingAWorkspaceClosesItsPanels()
    {
        DockManager manager;
        QMainWindow keep;
        DockWorkspace *first = manager.createWorkspace(p("first"));
        keep.setCentralWidget(first);
        auto *window = new QMainWindow;
        DockWorkspace *second = manager.createWorkspace(p("second"));
        window->setCentralWidget(second);
        QSignalSpy removed(&manager, &DockManager::workspaceRemoved);

        const QPointer<QWidget> a = new QLabel(p("a"));
        const QPointer<QWidget> b = new QLabel(p("b"));
        manager.registerPanel(p("a"), a);
        manager.registerPanel(p("b"), b);
        QVERIFY(second->addPanel(p("a")));
        QVERIFY(second->addPanel(p("b"), DockArea::Right));
        QVERIFY(manager.setPanelAutoHide(p("b"), true));
        keep.show();
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        delete window;
        QCOMPARE(manager.workspaces(), QList<DockWorkspace *>({first}));
        QCOMPARE(removed.size(), 1);
        QVERIFY(a && b); // closed, not destroyed
        QVERIFY(manager.hasPanel(p("a")));
        QVERIFY(!manager.panel(p("a"))->isOpen());
        QVERIFY(!manager.panel(p("b"))->isOpen());

        // They can be shown in a workspace that still exists.
        QVERIFY(manager.showPanel(p("a")));
        QCOMPARE(describe(first), p("a"));
        QCOMPARE(a->window(), &keep);
    }

    void managerDestroyedBeforeItsWorkspaces()
    {
        QMainWindow window;
        auto *manager = new DockManager;
        DockWorkspace *workspace = manager->createWorkspace(p("main"));
        window.setCentralWidget(workspace);
        const QPointer<QWidget> content = new QLabel(p("a"));
        manager->registerPanel(p("a"), content);
        manager->registerPanel(p("b"), new QLabel(p("b")));
        QVERIFY(workspace->addPanel(p("a")));
        QVERIFY(workspace->addPanel(p("b"), DockArea::Right));
        QVERIFY(manager->floatPanel(p("b"), QRect(40, 40, 200, 150)));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        delete manager;
        QVERIFY(!content); // the manager owned it
        QVERIFY(!workspace->manager());
        QVERIFY(!workspace->addPanel(p("a")));
        QCoreApplication::processEvents(); // the empty workspace keeps working
    }

    void failedOperationsChangeNothing()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        const QString before = describe(f.a);
        QSignalSpy aboutToChange(&f.manager, &DockManager::layoutAboutToChange);
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);
        const bool couldUndo = f.manager.canUndo();

        QCOMPARE(f.manager.movePanel(p("nope"), f.a, DockArea::Left).error(),
                 DockError::UnknownPanel);
        QCOMPARE(f.manager.movePanel(p("a"), p("zzz"), DockArea::Left).error(),
                 DockError::NotPlaced);
        QCOMPARE(f.manager.addPanel(p("a"), nullptr).error(), DockError::UnknownWorkspace);
        QCOMPARE(f.manager.movePanel(p("a"), f.a, DockArea::None).error(),
                 DockError::InvalidArgument);
        QCOMPARE(f.manager.movePanel(p("a"), p("a"), DockArea::Left).error(),
                 DockError::InvalidArgument); // alone in its group
        QCOMPARE(f.manager.floatPanel(p("c")).error(), DockError::NotPlaced);
        QCOMPARE(f.manager.maximizePanel(p("c")).error(), DockError::NotPlaced);
        QCOMPARE(f.manager.setPanelAutoHide(p("a"), true, DockArea::Center).error(),
                 DockError::InvalidArgument);
        DockManager other;
        QMainWindow otherWindow;
        DockWorkspace *foreign = other.createWorkspace();
        otherWindow.setCentralWidget(foreign);
        QCOMPARE(f.manager.addPanel(p("a"), foreign).error(), DockError::UnknownWorkspace);

        QCOMPARE(describe(f.a), before);
        QCOMPARE(aboutToChange.size(), 0);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(f.manager.canUndo(), couldUndo);
        QVERIFY(f.widgets[p("a")]->isVisible());

        // A successful change is announced exactly once on each side.
        QVERIFY(f.manager.movePanel(p("a"), p("b"), DockArea::Bottom));
        QCOMPARE(aboutToChange.size(), 1);
        QCOMPARE(changed.size(), 1);
    }

    void layoutCannotBeChangedWhileItIsBeingApplied()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        DockResult nested;
        const auto connection = connect(&f.manager, &DockManager::layoutAboutToChange, this,
                                        [&] { nested = f.manager.hidePanel(p("a")); });
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        // The windows going away at the end of this test change the layout too.
        disconnect(connection);
        QCOMPARE(nested.error(), DockError::Busy);
        QCOMPARE(describe(f.a), p("H(a, b)"));
    }

    void factoryCreatesContentOnDemand()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        int created = 0;
        DockPanel *lazy = f.manager.registerPanelFactory(p("lazy"), [&](const PanelId &id) {
            ++created;
            return new QLabel(id);
        }, p("Lazy"));
        QVERIFY(lazy);
        QVERIFY(!lazy->widget());
        QCOMPARE(lazy->title(), p("Lazy"));
        QVERIFY(!f.manager.registerPanelFactory(p("lazy"), [](const PanelId &) { return nullptr; }));
        QVERIFY(!f.manager.registerPanelFactory(p("none"), {}));
        QSignalSpy widgetCreated(lazy, &DockPanel::widgetCreated);

        QVERIFY(f.manager.movePanel(p("lazy"), p("a"), DockArea::Center));
        QCOMPARE(created, 1);
        QCOMPARE(widgetCreated.size(), 1);
        QVERIFY(lazy->widget());
        QVERIFY(lazy->widget()->isVisible());

        // The same instance from then on.
        QWidget *content = lazy->widget();
        QVERIFY(f.manager.hidePanel(p("lazy")));
        QVERIFY(f.manager.showPanel(p("lazy")));
        QVERIFY(f.manager.movePanel(p("lazy"), f.b, DockArea::Center));
        QCOMPARE(created, 1);
        QCOMPARE(lazy->widget(), content);
    }

    void undoAndRedo()
    {
        TwoWindows f;
        f.show();
        QVERIFY(!f.manager.canUndo());
        QVERIFY(!f.manager.undo());
        QSignalSpy undoState(&f.manager, &DockManager::undoStateChanged);

        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        QVERIFY(f.manager.movePanel(p("b"), f.b, DockArea::Center));
        QCOMPARE(describe(f.a), p("H(a, c)"));
        QVERIFY(f.manager.canUndo());
        QVERIFY(undoState.size() >= 4);

        QVERIFY(f.manager.undo());
        QCOMPARE(describe(f.a), p("H(a, V(b, c))"));
        QCOMPARE(describe(f.b), p("<empty>"));
        QCOMPARE(f.widgets[p("b")]->window(), &f.windowA);
        QVERIFY(f.manager.undo());
        QCOMPARE(describe(f.a), p("H(a, b)"));
        QVERIFY(!f.manager.panel(p("c"))->isOpen());
        QVERIFY(f.manager.canRedo());

        QVERIFY(f.manager.redo());
        QVERIFY(f.manager.redo());
        QCOMPARE(describe(f.a), p("H(a, c)"));
        QCOMPARE(describe(f.b), p("b"));
        QVERIFY(!f.manager.canRedo());
        QVERIFY(!f.manager.redo());

        // A new change discards what could have been redone.
        QVERIFY(f.manager.undo());
        QVERIFY(f.manager.hidePanel(p("a")));
        QVERIFY(!f.manager.canRedo());

        // Undo copes with panels that have been unregistered since.
        QVERIFY(f.manager.unregisterPanel(p("c")));
        QVERIFY(f.manager.undo());
        QVERIFY(f.a->layoutTree().validate());
        QVERIFY(!f.a->panels().contains(p("c")));
        QVERIFY(f.a->panels().contains(p("a")));

        f.manager.setUndoLimit(2);
        for (int i = 0; i < 5; ++i)
            QVERIFY(f.manager.togglePanel(p("d")));
        QVERIFY(f.manager.undo());
        QVERIFY(f.manager.undo());
        QVERIFY(!f.manager.canUndo());
        f.manager.clearUndoHistory();
        QVERIFY(!f.manager.canRedo());
    }

    void presetsAndReset()
    {
        TwoWindows f;
        f.show();
        QVERIFY(!f.manager.resetLayout()); // nothing saved as default yet
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        f.manager.saveDefaultLayout();
        QSignalSpy presetsChanged(&f.manager, &DockManager::presetsChanged);
        QVERIFY(f.manager.savePreset(p("coding")));
        QVERIFY(!f.manager.savePreset(QString()));

        QVERIFY(f.manager.movePanel(p("b"), f.b, DockArea::Center));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Bottom));
        QVERIFY(f.manager.savePreset(p("review")));
        QCOMPARE(f.manager.presetNames(), QStringList({p("coding"), p("review")}));
        QCOMPARE(presetsChanged.size(), 2);

        QVERIFY(f.manager.applyPreset(p("coding")));
        QCOMPARE(describe(f.a), p("H(a, b)"));
        QCOMPARE(describe(f.b), p("<empty>"));
        QVERIFY(!f.manager.panel(p("c"))->isOpen());
        QVERIFY(f.manager.applyPreset(p("review")));
        QCOMPARE(describe(f.a), p("V(a, c)"));
        QCOMPARE(describe(f.b), p("b"));
        QVERIFY(!f.manager.applyPreset(p("unknown")));

        // Presets survive being stored by the application.
        const QByteArray stored = f.manager.savePresets();
        QVERIFY(f.manager.removePreset(p("coding")));
        QVERIFY(f.manager.removePreset(p("review")));
        QVERIFY(!f.manager.removePreset(p("review")));
        QVERIFY(f.manager.presetNames().isEmpty());
        QVERIFY(!f.manager.restorePresets("{ not json"));
        QVERIFY(!f.manager.restorePresets("{\"format\":\"something else\"}"));
        QVERIFY(f.manager.presetNames().isEmpty());
        QVERIFY(f.manager.restorePresets(stored));
        QCOMPARE(f.manager.presetNames(), QStringList({p("coding"), p("review")}));
        QVERIFY(f.manager.applyPreset(p("coding")));
        QCOMPARE(describe(f.a), p("H(a, b)"));

        QVERIFY(f.manager.hidePanel(p("a")));
        QVERIFY(f.manager.resetLayout());
        QCOMPARE(describe(f.a), p("H(a, b)"));
    }

    void autoHideAndPin()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right, 0.3));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Bottom));
        const QString before = describe(f.a);
        DockPanel *b = f.manager.panel(p("b"));
        DockAutoHideContainer *autoHide = DockManagerPrivate::get(f.a)->autoHide;
        QVERIFY(!autoHide->bar(DockArea::Right)->isVisible());
        QSignalSpy autoHiddenChanged(b, &DockPanel::autoHiddenChanged);

        // No edge given: the nearest border.
        QVERIFY(f.manager.setPanelAutoHide(p("b"), true));
        QVERIFY(b->isAutoHidden());
        QVERIFY(b->isOpen());
        QCOMPARE(describe(f.a), p("V(a, c)"));
        QVERIFY(autoHide->bar(DockArea::Right)->isVisible());
        QCOMPARE(autoHide->bar(DockArea::Right)->tabs().size(), 1);
        QVERIFY(!f.widgets[p("b")]->isVisible());
        QCOMPARE(autoHiddenChanged.size(), 1);

        // Clicking its tab slides it out over the dock area; again collapses it.
        DockAutoHideTab *tab = autoHide->bar(DockArea::Right)->tab(p("b"));
        QVERIFY(tab);
        QCOMPARE(tab->text(), p("b"));
        QTest::mouseClick(tab, Qt::LeftButton);
        QCOMPARE(autoHide->expandedPanel(), p("b"));
        QVERIFY(autoHide->popup()->isVisible());
        QVERIFY(f.widgets[p("b")]->isVisible());
        QVERIFY(autoHide->popup()->isAncestorOf(f.widgets[p("b")]));
        QCOMPARE(autoHide->popup()->geometry().right(), areaOf(f.a)->geometry().right());
        grab(&f.windowA, p("auto-hide"));
        QTest::mouseClick(tab, Qt::LeftButton);
        QVERIFY(!autoHide->popup()->isVisible());
        QVERIFY(!f.widgets[p("b")]->isVisible());

        // activatePanel() slides it out; a click elsewhere collapses it.
        QVERIFY(f.manager.activatePanel(p("b")));
        QVERIFY(autoHide->popup()->isVisible());
        QTest::mouseClick(f.widgets[p("a")], Qt::LeftButton);
        QVERIFY(!autoHide->popup()->isVisible());

        // Pinning from the popup puts it back where it was docked.
        QVERIFY(f.manager.activatePanel(p("b")));
        QTest::mouseClick(autoHide->popup()->pinButton(), Qt::LeftButton);
        QVERIFY(!b->isAutoHidden());
        QCOMPARE(describe(f.a), before);
        QVERIFY(f.widgets[p("b")]->isVisible());
        QVERIFY(!autoHide->bar(DockArea::Right)->isVisible());
        QVERIFY(!autoHide->popup()->isVisible());

        // An explicit edge, closing from the bar, and showing again.
        QVERIFY(f.manager.setPanelAutoHide(p("c"), true, DockArea::Top));
        QCOMPARE(autoHide->bar(DockArea::Top)->tabs().size(), 1);
        QVERIFY(f.manager.hidePanel(p("c")));
        QVERIFY(!autoHide->bar(DockArea::Top)->isVisible());
        QVERIFY(f.manager.showPanel(p("c")));
        QVERIFY(f.manager.panel(p("c"))->isAutoHidden());
        QCOMPARE(autoHide->expandedPanel(), p("c"));
        QVERIFY(f.manager.setPanelAutoHide(p("c"), false));
        QCOMPARE(describe(f.a), before);
    }

    void contextMenu()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        DockPanel *a = f.manager.panel(p("a"));
        QSignalSpy requested(&f.manager, &DockManager::panelContextMenuRequested);
        connect(&f.manager, &DockManager::panelContextMenuRequested, this,
                [](DockPanel *, QMenu *menu) { menu->addAction(QStringLiteral("Custom")); });

        const auto action = [](QMenu *menu, const char *name) {
            return menu->findChild<QAction *>(QLatin1String(name));
        };
        std::unique_ptr<QMenu> menu(priv(f.manager)->createPanelMenu(a, nullptr));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(menu->actions().constLast()->text(), p("Custom"));
        QVERIFY(action(menu.get(), "dockActionClose")->isEnabled());
        QVERIFY(action(menu.get(), "dockActionFloat")->isEnabled());
        QVERIFY(action(menu.get(), "dockActionAutoHide"));
        QVERIFY(!action(menu.get(), "dockActionDock"));

        action(menu.get(), "dockActionCloseOthers")->trigger();
        QCOMPARE(describe(f.a), p("a"));
        action(menu.get(), "dockActionFloat")->trigger();
        QVERIFY(a->isFloating());

        // The menu follows the panel's state and policy.
        a->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Closable));
        menu.reset(priv(f.manager)->createPanelMenu(a, nullptr));
        QVERIFY(!action(menu.get(), "dockActionClose")->isEnabled());
        QVERIFY(!action(menu.get(), "dockActionFloat"));
        action(menu.get(), "dockActionDock")->trigger();
        QVERIFY(!a->isFloating());
        QCOMPARE(describe(f.a), p("a"));
        menu.reset(priv(f.manager)->createPanelMenu(a, nullptr));
        action(menu.get(), "dockActionMaximize")->trigger();
        QCOMPARE(f.manager.maximizedPanel(), p("a"));
    }

    void tabMetadata()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        DockPanel *a = f.manager.panel(p("a"));
        DockTabBar *bar = areaOf(f.a)->groupOfPanel(p("a"))->tabBar();
        const auto side = QTabBar::ButtonPosition(
            bar->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, bar));

        a->setTitle(p("Scene & View"));
        QCOMPARE(bar->tabText(0), p("Scene && View")); // no accidental mnemonic
        a->setToolTip(p("tip"));
        QCOMPARE(bar->tabToolTip(0), p("tip"));
        a->setDirty(true);
        QVERIFY(bar->tabText(0).endsWith(QChar(0x25CF)));
        a->setDirty(false);
        QCOMPARE(bar->tabText(0), p("Scene && View"));

        QVERIFY(bar->tabButton(0, side));
        a->setPinnedTab(true);
        QCOMPARE(bar->tabButton(0, side)->objectName(), p("dockTabPin"));
        a->setPinnedTab(false);
        QTRY_VERIFY(bar->tabButton(0, side)
                    && bar->tabButton(0, side)->objectName() != p("dockTabPin"));

        a->setPreviewTab(true);
        QVERIFY(a->isPreviewTab());
        grab(&f.windowA, p("tab-metadata"));
        QIcon icon(QPixmap(16, 16));
        a->setIcon(icon);
        QVERIFY(!bar->tabIcon(0).isNull());
    }

    void minimumSizesReachTheWindow()
    {
        TwoWindows f;
        f.widgets[p("a")]->setMinimumSize(300, 200);
        f.widgets[p("b")]->setMinimumSize(250, 150);
        f.widgets[p("c")]->setMinimumSize(100, 180);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        DockAreaWidget *area = areaOf(f.a);
        const int handle = area->handleWidth();
        // What a group adds around its content: frame and title row.
        const DockTabGroup *group = area->groupOfPanel(p("a"));
        const QSize chrome = group->size() - group->contentHost()->size();

        const QSize minimum = area->layoutMinimumSize();
        QCOMPARE(minimum.width(), 300 + 250 + 2 * chrome.width() + handle);
        QCOMPARE(minimum.height(), 150 + 180 + 2 * chrome.height() + handle);
        QTRY_VERIFY(f.windowA.minimumSizeHint().width() >= minimum.width());

        // A maximum size is honoured as well: "b" stops at 160, "c" below it
        // takes what that leaves of their column.
        const int column = f.widgets[p("b")]->height() + f.widgets[p("c")]->height();
        f.widgets[p("b")]->setMaximumHeight(160);
        QTRY_COMPARE(f.widgets[p("b")]->height(), 160);
        QTRY_COMPARE(f.widgets[p("c")]->height(), column - 160);
        f.widgets[p("b")]->setMaximumHeight(QWIDGETSIZE_MAX);

        // Even squeezed, nobody goes below their minimum. (Where the platform
        // lets a client resize its own window at all.)
        f.windowA.resize(f.windowA.minimumSizeHint());
        if (QTest::qWaitFor([&] { return f.windowA.size() == f.windowA.minimumSizeHint(); }, 2000)) {
            QVERIFY(f.widgets[p("a")]->width() >= 300);
            QVERIFY(f.widgets[p("b")]->width() >= 250);
            QVERIFY(f.widgets[p("c")]->height() >= 180);
            QVERIFY(f.widgets[p("b")]->height() >= 150);
        }
    }
};

QTEST_MAIN(tst_Manager)
#include "tst_manager.moc"
