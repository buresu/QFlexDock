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
        // The group cannot get narrower than what its header holds at its
        // narrowest (the line edit is as wide as there is room for, so its
        // present width says nothing). Hiding an action hides its button.
        const int minimum = group->sizeLimits().min.width();
        QVERIFY(minimum >= group->tabBar()->minimumSizeHint().width()
                               + group->actionBar()->minimumSizeHint().width());
        QVERIFY(group->actionBar()->minimumSizeHint().width()
                >= splitButton->minimumSizeHint().width() + moreButton->minimumSizeHint().width());
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

    // Boundaries in one line stay in one line down to the smallest window
    // only if the groups along them can get equally narrow.
    void titlesDoNotDecideHowNarrowAGroupGets()
    {
        TwoWindows f;
        f.show();
        f.manager.panel(p("a"))->setTitle(p("i"));
        f.manager.panel(p("b"))->setTitle(p("A title that takes far more room"));
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Bottom));
        DockAreaWidget *area = areaOf(f.a);
        QCoreApplication::processEvents();
        QCOMPARE(area->groupOfPanel(p("a"))->sizeLimits().min.width(),
                 area->groupOfPanel(p("b"))->sizeLimits().min.width());
    }

    // Actions before the tabs and right behind them, besides those at the end.
    void titleActionsHaveThreePlaces()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("a")));
        DockTabGroup *group = areaOf(f.a)->groupOfPanel(p("a"));
        DockTabBar *bar = group->tabBar();
        // By default the tabs have the header to themselves.
        const int fullWidth = bar->width();
        QVERIFY(fullWidth > bar->tabRect(1).right() + 200);

        QAction list(p("List"));
        list.setObjectName(p("tabList"));
        QAction add(p("Add"));
        add.setObjectName(p("newTab"));
        int added = 0;
        connect(&add, &QAction::triggered, this, [&added] { ++added; });
        QAction more(p("More"));
        DockPanel *a = f.manager.panel(p("a"));
        a->setTitleActions({&list}, DockTitlePlace::Start);
        a->setTitleActions({&add}, DockTitlePlace::AfterTabs);
        a->setTitleActions({&more});
        QCOMPARE(a->titleActions(DockTitlePlace::Start), QList<QAction *>{&list});
        QCOMPARE(a->titleActions(DockTitlePlace::AfterTabs), QList<QAction *>{&add});
        QCOMPARE(a->titleActions(DockTitlePlace::End), QList<QAction *>{&more});
        QCOMPARE(a->titleActions(), QList<QAction *>{&more});
        QCoreApplication::processEvents();

        QWidget *listButton = group->widgetForAction(&list);
        QWidget *addButton = group->widgetForAction(&add);
        QWidget *moreButton = group->widgetForAction(&more);
        QVERIFY(listButton && addButton && moreButton);
        QCOMPARE(listButton->parentWidget(), group->actionBar(DockTitlePlace::Start));
        QCOMPARE(addButton->parentWidget(), group->actionBar(DockTitlePlace::AfterTabs));
        QCOMPARE(moreButton->parentWidget(), group->actionBar(DockTitlePlace::End));
        QCOMPARE(group->actionBar(DockTitlePlace::Start)->objectName(), p("dockTitleStartActions"));
        QCOMPARE(group->actionBar(DockTitlePlace::AfterTabs)->objectName(), p("dockTabActions"));
        QCOMPARE(group->actionBar()->objectName(), p("dockTitleActions"));
        // A button is told from the others by the name of its action.
        QCOMPARE(addButton->property("action").toString(), p("newTab"));

        // Start, tabs, the button right behind the last tab, and the rest of
        // the header before what is at its end.
        const QRect tabs = inGroup(group, bar);
        QVERIFY(inGroup(group, listButton).right() < tabs.left());
        QCOMPARE(bar->width(), bar->tabRect(1).right() + 1);
        QVERIFY(bar->width() < fullWidth);
        QCOMPARE(inGroup(group, addButton).left(), tabs.right() + 1);
        QVERIFY(inGroup(group, moreButton).left() > inGroup(group, addButton).right() + 200);
        QVERIFY(inGroup(group, moreButton).right() < inGroup(group, group->menuButton()).left());
        grab(&f.windowA, p("header-action-places"));
        QTest::mouseClick(addButton, Qt::LeftButton);
        QCOMPARE(added, 1);

        // A drop a little behind the last tab still appends, although the
        // tab bar ends there.
        const QRect region = bar->tabDropRegion();
        QVERIFY(region.right() > bar->rect().right());

        // The header beside the tabs is the title bar now. It acts like the
        // empty part of a tab bar and stands for the group.
        const QPoint beside = group->titleBar()->mapFrom(
            group, QPoint(inGroup(group, addButton).right() + 60, tabs.center().y()));
        QCOMPARE(group->titleBar()->childAt(beside), nullptr);
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Right));
        QVERIFY(f.manager.maximizedPanel().isEmpty());
        QTest::mouseDClick(group->titleBar(), Qt::LeftButton, {}, beside);
        QCOMPARE(f.manager.maximizedPanel(), p("a"));
        QVERIFY(f.manager.restoreMaximizedPanel());
        // The other tab has none: the tabs take the header again.
        QVERIFY(f.manager.activatePanel(p("b")));
        QCoreApplication::processEvents();
        QVERIFY(!group->actionBar(DockTitlePlace::AfterTabs)->isVisible());
        QVERIFY(!group->actionBar(DockTitlePlace::Start)->isVisible());
        QVERIFY(bar->width() > bar->tabRect(1).right() + 100);

        // An action may be in two places. Destroyed, it drops out of both.
        auto *shared = new QAction(p("Shared"), this);
        DockPanel *b = f.manager.panel(p("b"));
        b->setTitleActions({shared}, DockTitlePlace::Start);
        b->setTitleActions({shared}, DockTitlePlace::End);
        delete shared;
        QVERIFY(b->titleActions(DockTitlePlace::Start).isEmpty());
        QVERIFY(b->titleActions().isEmpty());
        QCoreApplication::processEvents();
        QVERIFY(!group->actionBar()->isVisible());

        // With a title bar the tabs are elsewhere; the actions stay.
        QVERIFY(f.manager.activatePanel(p("a")));
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        QCoreApplication::processEvents();
        QVERIFY(group->widgetForAction(&add)->isVisible());
        QVERIFY(group->widgetForAction(&list)->isVisible());
        QVERIFY(inGroup(group, group->widgetForAction(&list)).right()
                < inGroup(group, group->titleLabel()).left());
        QVERIFY(inGroup(group, group->titleLabel()).right()
                < inGroup(group, group->widgetForAction(&add)).left());
        f.manager.setGroupHeader(DockManager::GroupHeader::Tabs);
        a->setTitleActions({}, DockTitlePlace::Start);
        a->setTitleActions({}, DockTitlePlace::AfterTabs);
        a->setTitleActions({});
    }

    // Tabs of one width that share the bar when it gets crowded.
    void tabsOfAFixedWidthShrinkToShareTheBar()
    {
        TwoWindows f(14);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("a")));
        const DockTabGroup *group = areaOf(f.a)->groupOfPanel(p("a"));
        DockTabBar *bar = group->tabBar();
        const int natural = bar->tabRect(0).width();
        QCOMPARE(bar->tabOverflow(), DockTabOverflow::Scroll);

        DockTheme theme;
        theme.tabWidth = 150;
        f.manager.setTheme(theme);
        QCoreApplication::processEvents();
        QVERIFY(natural != 150);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(bar->tabRect(i).width(), 150);
        QVERIFY(bar->usesScrollButtons());

        theme.tabOverflow = DockTabOverflow::Shrink;
        f.manager.setTheme(theme);
        QCoreApplication::processEvents();
        QCOMPARE(bar->tabOverflow(), DockTabOverflow::Shrink);
        QVERIFY(!bar->usesScrollButtons());
        for (int i = 0; i < 3; ++i)
            QCOMPARE(bar->tabRect(i).width(), 150);
        const auto closeButton = [bar](int index) {
            QWidget *button = bar->tabButton(index, QTabBar::RightSide);
            return button ? button : bar->tabButton(index, QTabBar::LeftSide);
        };
        QVERIFY(closeButton(1) && closeButton(1)->isVisible());

        // More tabs than fit at that width: they all get narrower, by the
        // same amount, and stay inside the bar.
        for (const char *id : {"d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n"})
            QVERIFY(f.manager.movePanel(p(id), p("a"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("a")));
        QCoreApplication::processEvents();
        QCOMPARE(bar->count(), 14);
        const int width = bar->tabRect(0).width();
        QVERIFY(width < 150);
        for (int i = 0; i < bar->count(); ++i) {
            QVERIFY(qAbs(bar->tabRect(i).width() - width) <= 1);
            QVERIFY(bar->rect().contains(bar->tabRect(i)));
        }
        QVERIFY(bar->tabRect(13).right() > bar->width() - 14);
        grab(&f.windowA, p("header-tabs-shrunk"));

        // In a narrow window they are squeezed further than their buttons
        // have room for; only the current tab keeps its own.
        f.windowA.resize(420, 400);
        QTRY_VERIFY(bar->tabRect(0).width() < 40);
        for (int i = 0; i < bar->count(); ++i)
            QVERIFY(bar->rect().contains(bar->tabRect(i)));
        QVERIFY(closeButton(0)->isVisible());
        QVERIFY(!closeButton(1)->isVisible());
        QVERIFY(f.manager.activatePanel(p("b")));
        QCoreApplication::processEvents();
        QVERIFY(!closeButton(0)->isVisible());
        QVERIFY(closeButton(1)->isVisible());
        grab(&f.windowA, p("header-tabs-squeezed"));
        // The group does not need its tabs' full width.
        QVERIFY(group->sizeLimits().min.width() < 300);

        // With room again, the buttons are back.
        f.windowA.resize(900, 600);
        QVERIFY(f.manager.hidePanels({p("d"), p("e"), p("f"), p("g"), p("h"), p("i"), p("j"), p("k"),
                                      p("l"), p("m"), p("n")}));
        QTRY_COMPARE(bar->tabRect(0).width(), 150);
        QTRY_VERIFY(closeButton(0)->isVisible());
        QVERIFY(closeButton(2)->isVisible());
        theme.tabOverflow = DockTabOverflow::Scroll;
        theme.tabWidth = -1;
        f.manager.setTheme(theme);
        QCoreApplication::processEvents();
        QVERIFY(bar->usesScrollButtons());
        QCOMPARE(bar->tabRect(0).width(), natural);
    }

    // The header of a window without a title row maximizes it.
    void minimalWindowIsMaximizedByItsHeader()
    {
        TwoWindows f;
        f.manager.setFloatingWindowFrame(DockManager::FloatingFrame::Minimal);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.floatPanel(p("b"), QRect(40, 40, 420, 300)));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
        DockFloatingWindow *window = onlyFloatingWindow(f.manager);
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const DockTabGroup *group = window->area()->groupOfPanel(p("b"));
        DockTabBar *bar = group->tabBar();
        const QPoint beside(bar->tabRect(1).right() + 60, bar->height() / 2);
        QVERIFY(bar->tabAt(beside) < 0);

        // On a tab a double click is about the panel, as everywhere.
        QTest::mouseDClick(bar, Qt::LeftButton, {}, bar->tabRect(0).center());
        QCoreApplication::processEvents();
        QVERIFY(!window->isMaximized());
        QVERIFY(f.manager.restoreMaximizedPanel());

        const QSize normal = window->size();
        QTest::mouseDClick(bar, Qt::LeftButton, {}, beside);
        if (QTest::qWaitFor([&] { return window->isMaximized() && window->size() != normal; }, 3000)) {
            QVERIFY(f.manager.maximizedPanel().isEmpty());
            QTest::qWait(100);
            QTest::mouseDClick(bar, Qt::LeftButton, {}, QPoint(bar->tabRect(1).right() + 60, 4));
            QTRY_VERIFY(!window->isMaximized());
            // (Let the window system finish before the layout changes again.)
            QTRY_COMPARE(window->size(), normal);
        }

        // With a second group in the window, a header is about its group.
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Right));
        QCoreApplication::processEvents();
        DockTabBar *other = window->area()->groupOfPanel(p("c"))->tabBar();
        // (Without a window manager the window is still "maximized" from above.)
        const bool windowWasMaximized = window->isMaximized();
        QTest::mouseDClick(other, Qt::LeftButton, {},
                           QPoint(other->tabRect(0).right() + 30, other->height() / 2));
        QCOMPARE(f.manager.maximizedPanel(), p("c"));
        QCOMPARE(window->isMaximized(), windowWasMaximized);
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

        // A ghost for such a window is the picture of the group alone.
        QVERIFY(f.manager.dockPanel(p("b")));
        QTRY_VERIFY(!onlyFloatingWindow(f.manager));
        const QSize groupSize = areaOf(f.a)->groupOfPanel(p("b"))->size();
        DockDragController *controller = priv(f.manager)->drag;
        QVERIFY(controller->begin(p("b"), false));
        const QPointer<DockFloatingWindow> ghost = controller->createGhost();
        QVERIFY(ghost);
        QVERIFY(ghost->hasMinimalFrame());
        QTRY_COMPARE(ghost->size(), groupSize);
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
