// SPDX-License-Identifier: MIT
//
// What a tab group has at its top: tabs or a title bar, which buttons, or
// nothing at all; and the floating frame that goes with a title bar.
#include "TestUtils.h"

#include "core/DockDragController.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockSplitHandle.h"
#include "widgets/DockTabBar.h"

#include <QtTest/QSignalSpy>
#include <QtWidgets/QLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidgetAction>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

QRect inGroup(const DockTabGroup *group, const QWidget *part)
{
    return QRect(part->mapTo(group, QPoint(0, 0)), part->size());
}

DockFloatingWindow *onlyFloatingWindow(DockManager &manager)
{
    const auto &windows = priv(manager)->floatingWindows;
    return windows.size() == 1 ? windows.begin().value() : nullptr;
}

} // namespace

class tst_Header : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tabsAreTheDefaultHeader()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QCOMPARE(f.manager.groupHeader(), DockManager::GroupHeader::Tabs);
        const DockTabGroup *group = areaOf(f.a)->groupOfPanel(p("a"));
        QVERIFY(group->isHeaderVisible());
        QVERIFY(group->tabBar()->isVisible());
        QCOMPARE(group->tabBar()->parentWidget(), group->titleBar());
        QVERIFY(!group->titleLabel()->isVisible());
        QVERIFY(group->menuButton()->isVisible());
        QVERIFY(group->maximizeButton()->isVisible());
        QVERIFY(!group->floatButton()->isVisible());
        QVERIFY(!group->closeButton()->isVisible());
    }

    void titleBarShowsTheCurrentPanelAndTabsOnlyWhenStacked()
    {
        TwoWindows f;
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        f.manager.panel(p("a"))->setTitle(p("Alpha"));
        f.manager.panel(p("b"))->setTitle(p("Beta"));
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        DockAreaWidget *area = areaOf(f.a);
        const DockTabGroup *group = area->groupOfPanel(p("a"));

        // One panel: a title, no tabs.
        QVERIFY(group->titleBar()->isVisible());
        QVERIFY(group->titleLabel()->isVisible());
        QCOMPARE(group->titleLabel()->text(), p("Alpha"));
        QVERIFY(!group->tabBar()->isVisible());
        QCOMPARE(inGroup(group, group->contentHost()).bottom(), group->contentsRect().bottom());
        // No tabs, so nowhere to drop between them.
        QVERIFY(group->tabBar()->tabDropRegion().isEmpty());

        // Two: tabs below the content, the title follows the current one.
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QCOMPARE(area->groupOfPanel(p("b")), group);
        QTRY_VERIFY(group->tabBar()->isVisible());
        QCOMPARE(group->tabBar()->count(), 2);
        QCOMPARE(group->titleLabel()->text(), p("Beta"));
        const QRect title = inGroup(group, group->titleBar());
        const QRect host = inGroup(group, group->contentHost());
        const QRect tabs = inGroup(group, group->tabBar());
        QCOMPARE(title.top(), group->contentsRect().top());
        QCOMPARE(host.top(), title.bottom() + 1);
        QCOMPARE(tabs.top(), host.bottom() + 1);
        QCOMPARE(tabs.bottom(), group->contentsRect().bottom());
        QCOMPARE(f.widgets[p("b")]->size(), host.size());
        QVERIFY(!group->tabBar()->tabDropRegion().isEmpty());
        // They are plain tabs: closing is the title bar's business.
        const auto side = QTabBar::ButtonPosition(
            group->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, group->tabBar()));
        QVERIFY(!group->tabBar()->tabButton(0, side));
        grab(&f.windowA, p("header-titlebar-stacked"));

        QVERIFY(f.manager.activatePanel(p("a")));
        QCOMPARE(group->titleLabel()->text(), p("Alpha"));
        f.manager.panel(p("a"))->setTitle(p("Alpha 2"));
        QCOMPARE(group->titleLabel()->text(), p("Alpha 2"));

        // Back to one: the tabs go again.
        QVERIFY(f.manager.hidePanel(p("b")));
        QTRY_VERIFY(!group->tabBar()->isVisible());
        QTRY_COMPARE(inGroup(group, group->contentHost()).bottom(), group->contentsRect().bottom());
    }

    void headerCanBeSwitchedWhileInUse()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        const DockTabGroup *group = areaOf(f.a)->groupOfPanel(p("a"));
        const auto side = QTabBar::ButtonPosition(
            group->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, group->tabBar()));
        QVERIFY(group->tabBar()->tabButton(0, side));

        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        QTRY_VERIFY(group->titleLabel()->isVisible());
        QCOMPARE(group->tabBar()->parentWidget(), group);
        QVERIFY(inGroup(group, group->tabBar()).top() > inGroup(group, group->contentHost()).top());
        QCOMPARE(group->tabBar()->count(), 2);

        f.manager.setGroupHeader(DockManager::GroupHeader::Tabs);
        QTRY_VERIFY(!group->titleLabel()->isVisible());
        QCOMPARE(group->tabBar()->parentWidget(), group->titleBar());
        QTRY_VERIFY(inGroup(group, group->tabBar()).bottom()
                    < inGroup(group, group->contentHost()).top());
        QCOMPARE(group->tabBar()->count(), 2);
        // Closable tabs have their buttons back.
        QTRY_VERIFY(group->tabBar()->tabButton(0, side));
        QVERIFY(f.a->layoutTree().validate());
    }

    void titleButtonsFollowTheThemeAndThePanel()
    {
        TwoWindows f;
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        DockTheme theme;
        theme.titleButtons = DockTitleButton::Float | DockTitleButton::Close;
        f.manager.setTheme(theme);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        DockAreaWidget *area = areaOf(f.a);
        const DockTabGroup *a = area->groupOfPanel(p("a"));
        QVERIFY(!a->menuButton()->isVisible());
        QVERIFY(!a->maximizeButton()->isVisible());
        QVERIFY(a->floatButton()->isVisible());
        QVERIFY(a->closeButton()->isVisible());
        QVERIFY(!a->floatButton()->icon().isNull());
        QVERIFY(!a->closeButton()->icon().isNull());

        // A panel that may not be floated or closed has no button for it.
        f.manager.panel(p("a"))->setFeatures(AllDockFeatures
                                             & ~DockFeatures(DockFeature::Floatable));
        QVERIFY(!a->floatButton()->isVisible());
        QVERIFY(a->closeButton()->isVisible());
        f.manager.panel(p("a"))->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Closable));
        QVERIFY(a->floatButton()->isVisible());
        QVERIFY(!a->closeButton()->isVisible());

        // The buttons act on the current panel.
        const DockTabGroup *b = area->groupOfPanel(p("b"));
        QTest::mouseClick(b->closeButton(), Qt::LeftButton);
        QVERIFY(!f.manager.panel(p("b"))->isOpen());
        QCOMPARE(describe(f.a), p("a"));

        f.manager.setTheme(DockTheme{});
        QVERIFY(a->menuButton()->isVisible());
        QVERIFY(a->maximizeButton()->isVisible());
        QVERIFY(!a->floatButton()->isVisible());
    }

    // The application's own actions in the header, per panel.
    void titleActionsFollowTheCurrentPanel()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("a")));
        DockAreaWidget *area = areaOf(f.a);
        DockTabGroup *group = area->groupOfPanel(p("a"));
        QVERIFY(!group->actionBar()->isVisible());

        QAction split(p("Split"));
        int triggered = 0;
        connect(&split, &QAction::triggered, this, [&triggered] { ++triggered; });
        QAction separator;
        separator.setSeparator(true);
        QMenu menu;
        menu.addAction(p("Close All"));
        QAction more(p("More"));
        more.setMenu(&menu);
        QWidgetAction filter(nullptr);
        auto *edit = new QLineEdit;
        filter.setDefaultWidget(edit);

        DockPanel *a = f.manager.panel(p("a"));
        DockPanel *b = f.manager.panel(p("b"));
        QSignalSpy metadata(a, &DockPanel::metadataChanged);
        a->setTitleActions({&filter, &split, &separator, &more});
        QCOMPARE(metadata.size(), 1);
        QCOMPARE(a->titleActions(), (QList<QAction *>{&filter, &split, &separator, &more}));
        auto *otherAction = new QAction(p("Other"), this);
        b->setTitleActions({otherAction});

        // In order, between the tabs and the built-in buttons.
        QVERIFY(group->actionBar()->isVisible());
        auto *splitButton = qobject_cast<QToolButton *>(group->widgetForAction(&split));
        QVERIFY(splitButton);
        QCOMPARE(splitButton->objectName(), p("dockActionButton"));
        QCOMPARE(splitButton->defaultAction(), &split);
        QCOMPARE(group->widgetForAction(&filter), edit);
        QVERIFY(edit->isVisible());
        QCOMPARE(group->widgetForAction(&separator)->objectName(), p("dockActionSeparator"));
        auto *moreButton = qobject_cast<QToolButton *>(group->widgetForAction(&more));
        QVERIFY(moreButton);
        QCOMPARE(moreButton->popupMode(), QToolButton::InstantPopup);
        QVERIFY(!group->widgetForAction(otherAction));
        QCoreApplication::processEvents();
        const QRect tabs = inGroup(group, group->tabBar());
        const QRect actions = inGroup(group, group->actionBar());
        const QRect builtIn = inGroup(group, group->menuButton());
        QVERIFY(tabs.right() < actions.left());
        QVERIFY(actions.right() < builtIn.left());
        QVERIFY(inGroup(group, edit).right() < inGroup(group, splitButton).left());
        QVERIFY(inGroup(group, splitButton).right() < inGroup(group, moreButton).left());
        grab(&f.windowA, p("header-title-actions"));

        QTest::mouseClick(splitButton, Qt::LeftButton);
        QCOMPARE(triggered, 1);
        // Hiding an action hides its button; the group cannot get narrower
        // than what its header holds.
        const int minimum = group->sizeLimits().min.width();
        QVERIFY(minimum >= actions.width());
        split.setVisible(false);
        QVERIFY(!splitButton->isVisible());
        split.setVisible(true);
        QVERIFY(splitButton->isVisible());

        // The other tab brings its own.
        QVERIFY(f.manager.activatePanel(p("b")));
        QVERIFY(!group->widgetForAction(&split));
        QVERIFY(group->widgetForAction(otherAction));
        QVERIFY(!edit->isVisible());
        QVERIFY(edit->parentWidget() != group->actionBar()); // back with its action
        QVERIFY(f.manager.activatePanel(p("a")));
        QCOMPARE(group->widgetForAction(&filter), edit);

        // The widget goes along when the panel moves to another group.
        QVERIFY(f.manager.movePanel(p("a"), p("b"), DockArea::Right));
        DockTabGroup *moved = area->groupOfPanel(p("a"));
        QVERIFY(moved != group);
        QTRY_COMPARE(moved->widgetForAction(&filter), edit);
        QTRY_VERIFY(edit->isVisible());
        QVERIFY(moved->widgetForAction(&split));
        QVERIFY(!group->widgetForAction(&split));
        // With a title bar instead of tabs they are there just the same.
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        QVERIFY(moved->widgetForAction(&split)->isVisible());
        f.manager.setGroupHeader(DockManager::GroupHeader::Tabs);

        // A destroyed action drops out; so does everything on request.
        delete otherAction;
        QVERIFY(b->titleActions().isEmpty());
        QVERIFY(!group->actionBar()->isVisible());
        a->setTitleActions({});
        QVERIFY(!moved->actionBar()->isVisible());
        QVERIFY(!edit->isVisible());
        // The widget of a QWidgetAction survives the group it was shown in.
        QVERIFY(f.manager.hidePanel(p("a")));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(filter.defaultWidget(), edit);
    }

    void floatButtonAndDoubleClickToggleFloating()
    {
        TwoWindows f;
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        DockTheme theme;
        theme.titleButtons = DockTitleButton::Float;
        f.manager.setTheme(theme);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        DockPanel *panel = f.manager.panel(p("b"));
        const auto groupOfB = [&]() -> DockTabGroup * {
            if (DockFloatingWindow *window = onlyFloatingWindow(f.manager))
                return window->area()->groupOfPanel(p("b"));
            return areaOf(f.a)->groupOfPanel(p("b"));
        };

        QTest::mouseClick(groupOfB()->floatButton(), Qt::LeftButton);
        QVERIFY(panel->isFloating());
        QCOMPARE(describe(f.a), p("a"));
        // In the floating window the same button docks it again.
        QVERIFY(groupOfB()->floatButton()->isVisible());
        QTest::mouseClick(groupOfB()->floatButton(), Qt::LeftButton);
        QVERIFY(!panel->isFloating());
        QCOMPARE(describe(f.a), p("H(a, b)"));

        // So does a double click on the title bar, either way.
        QTest::mouseDClick(groupOfB()->titleBar(), Qt::LeftButton);
        QVERIFY(panel->isFloating());
        QTest::mouseDClick(groupOfB()->titleBar(), Qt::LeftButton);
        QVERIFY(!panel->isFloating());
        QCOMPARE(describe(f.a), p("H(a, b)"));

        // Not for a panel that may not float.
        panel->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Floatable));
        QTest::mouseDClick(groupOfB()->titleBar(), Qt::LeftButton);
        QVERIFY(!panel->isFloating());
    }

    // Content that just stays where the application put it: no header to take
    // hold of, no dock features, other panels around it.
    void panelWithoutHeaderIsFixedContent()
    {
        TwoWindows f;
        f.widgets[p("a")]->setMinimumSize(200, 120);
        DockPanel *central = f.manager.panel(p("a"));
        central->setFeatures({});
        QVERIFY(central->isHeaderVisible());
        QSignalSpy metadata(central, &DockPanel::metadataChanged);
        central->setHeaderVisible(false);
        QCOMPARE(metadata.size(), 1);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Left));
        DockAreaWidget *area = areaOf(f.a);
        DockTabGroup *group = area->groupOfPanel(p("a"));

        QVERIFY(!group->isHeaderVisible());
        QCOMPARE(group->property("headerVisible").toBool(), false);
        QVERIFY(!group->titleBar()->isVisible());
        QVERIFY(!group->tabBar()->isVisible());
        // The content has the whole group, and the group asks for no more
        // room than the content does.
        QCOMPARE(inGroup(group, group->contentHost()), group->contentsRect());
        const QMargins frame = group->contentsMargins();
        QCOMPARE(group->sizeLimits().min,
                 QSize(200 + frame.left() + frame.right(), 120 + frame.top() + frame.bottom()));
        QVERIFY(area->groupOfPanel(p("b"))->isHeaderVisible());
        grab(&f.windowA, p("header-none"));

        // Docks go next to it, never into it.
        DockDragController *controller = priv(f.manager)->drag;
        QVERIFY(controller->begin(p("b"), false));
        const QRect rect = group->geometry();
        const DropCandidate middle = area->candidateAt(rect.center(), *controller->session());
        QVERIFY(!middle.zones.testFlag(DockArea::Center));
        QVERIFY(middle.zones.testFlag(DockArea::Bottom));
        const DropCandidate below =
            area->candidateAt(QPoint(rect.center().x(), rect.bottom() - 40), *controller->session());
        QVERIFY(below.valid);
        QCOMPARE(below.target.area, DockArea::Bottom);
        controller->cancel();

        // With the header back, it is a group like any other.
        central->setHeaderVisible(true);
        QVERIFY(group->isHeaderVisible());
        QTRY_VERIFY(group->titleBar()->isVisible());
        QTRY_VERIFY(inGroup(group, group->contentHost()).top() > group->contentsRect().top());
        central->setHeaderVisible(false);
        QTRY_VERIFY(!group->titleBar()->isVisible());

        // Sharing a group (the application may do that), it has to show: the
        // tabs are the only way to the other panel.
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        QVERIFY(group->isHeaderVisible());
        QTRY_VERIFY(group->titleBar()->isVisible());
    }

    void minimalFrameLeavesTheHeadersToMoveTheWindow()
    {
        TwoWindows f;
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Minimal);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.floatPanel(p("b"), QRect(40, 40, 320, 240)));
        DockFloatingWindow *window = onlyFloatingWindow(f.manager);
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(window->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(window->hasCustomFrame());
        QVERIFY(window->hasMinimalFrame());
        QVERIFY(!window->titleBar());
        // All there is around the dock area is the border to resize it by.
        const QMargins margins = window->layout()->contentsMargins();
        QVERIFY(margins.left() > 0 && margins.top() > 0);
        QCOMPARE(window->area()->geometry(), window->rect().marginsRemoved(margins));
        const DockTabGroup *group = window->area()->groupOfPanel(p("b"));
        QVERIFY(group->titleBar()->isVisible());
        QCOMPARE(group->titleLabel()->text(), f.manager.panel(p("b"))->title());
        grab(window, p("header-minimal-frame"));

        // A ghost for such a window is the picture of the group plus that border.
        QVERIFY(f.manager.dockPanel(p("b")));
        QTRY_VERIFY(!onlyFloatingWindow(f.manager));
        const QSize groupSize = areaOf(f.a)->groupOfPanel(p("b"))->size();
        DockDragController *controller = priv(f.manager)->drag;
        QVERIFY(controller->begin(p("b"), false));
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        QVERIFY(ghost->hasMinimalFrame());
        QTRY_COMPARE(ghost->size(), groupSize + QSize(margins.left() + margins.right(),
                                                     margins.top() + margins.bottom()));
        controller->finish(Qt::IgnoreAction, ghost, false);
        QTRY_VERIFY(!ghost);
    }

    void cornerResizeCanBeTurnedOff()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        DockAreaWidget *area = areaOf(f.a);
        QVERIFY(f.manager.isCornerResizeEnabled());
        QCOMPARE(area->visibleCorners().size(), 1);
        DockSplitCorner *corner = area->visibleCorners().constFirst();

        f.manager.setCornerResizeEnabled(false);
        QTRY_VERIFY(area->visibleCorners().isEmpty());
        QVERIFY(!corner->isVisible());
        // The handles are all that is left to grab there.
        const QPoint at = area->mapTo(&f.windowA, corner->patchGeometry().center());
        QVERIFY(qobject_cast<DockSplitHandle *>(f.windowA.childAt(at)));

        f.manager.setCornerResizeEnabled(true);
        QTRY_COMPARE(area->visibleCorners().size(), 1);
    }
};

QTEST_MAIN(tst_Header)
#include "tst_header.moc"
