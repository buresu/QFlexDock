// SPDX-License-Identifier: MIT
//
// Workspaces that dock in columns: the bar above a column, columns shrunk to
// a strip of buttons, and the tab group that comes out beside such a strip.
// (Drags into, out of and beside columns are in tst_dragdrop.)
#include "TestUtils.h"

#include "widgets/DockColumn.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockSplitHandle.h"

#include <QtTest/QSignalSpy>
#include <QtWidgets/QToolButton>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

/// H(a, V(b, c)) in workspace A, which docks in columns.
void buildColumns(TwoWindows &f)
{
    f.a->setColumnDocking(true);
    QVERIFY(f.a->addPanel(p("a")));
    QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right, -1, 0.3));
    QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
    QCOMPARE(describe(f.a), p("H(a, V(b, c))"));
}

DockIconStrip *stripOf(DockWorkspace *workspace, DockManager &manager, const char *panel)
{
    DockAreaWidget *area = areaOf(workspace);
    const LayoutNode *group = area->tree().findPanel(p(panel));
    const LayoutNode *column = group ? area->tree().columnOf(group->id) : nullptr;
    Q_UNUSED(manager);
    return column ? area->iconStrip(column->id) : nullptr;
}

DockColumnBar *barOf(DockAreaWidget *area, const char *panel)
{
    const LayoutNode *group = area->tree().findPanel(p(panel));
    const LayoutNode *column = group ? area->tree().columnOf(group->id) : nullptr;
    return column ? area->columnBar(column->id) : nullptr;
}

DockFloatingWindow *onlyFloatingWindow(DockManager &manager)
{
    const auto &windows = priv(manager)->floatingWindows;
    return windows.size() == 1 ? windows.begin().value() : nullptr;
}

} // namespace

class tst_Columns : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aWorkspaceDocksInColumnsOnRequest()
    {
        TwoWindows f;
        QVERIFY(!f.a->isColumnDocking());
        f.a->setColumnDocking(true);
        QVERIFY(f.a->isColumnDocking());
        QVERIFY(!f.b->isColumnDocking());
        QVERIFY(f.b->setProperty("columnDocking", true));
        QVERIFY(f.b->isColumnDocking());
    }

    void everyColumnHasABar()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        QVERIFY(f.b->addPanel(p("d")));
        DockAreaWidget *area = areaOf(f.a);
        QCOMPARE(area->columnBars().size(), 2);
        QVERIFY(areaOf(f.b)->columnBars().isEmpty());

        const int height = priv(f.manager)->columnBarHeight();
        const DockColumnBar *left = barOf(area, "a");
        const DockColumnBar *right = barOf(area, "b");
        QVERIFY(left && right && left != right);
        QCOMPARE(barOf(area, "c"), right);
        QVERIFY(left->isVisible() && right->isVisible());

        // Above the column, as wide as it; the tab group at the top begins below it.
        const DockTabGroup *a = area->groupOfPanel(p("a"));
        const DockTabGroup *b = area->groupOfPanel(p("b"));
        const DockTabGroup *c = area->groupOfPanel(p("c"));
        QCOMPARE(left->geometry(), QRect(a->x(), 0, a->width(), height));
        QCOMPARE(a->y(), height);
        QCOMPARE(right->geometry(), QRect(b->x(), 0, b->width(), height));
        QCOMPARE(b->y(), height);
        QVERIFY(c->y() > b->geometry().bottom());
        QCOMPARE(c->geometry().bottom(), area->contentsRect().bottom());
        QCOMPARE(f.manager.columnPanels(p("c")), QStringList({p("b"), p("c")}));
        QCOMPARE(f.manager.columnPanels(p("a")), QStringList({p("a")}));

        // Turned off, they are gone and the groups have their room.
        f.a->setColumnDocking(false);
        QVERIFY(area->columnBars().isEmpty());
        QVERIFY(!left->isVisible());
        QCOMPARE(a->y(), 0);
    }

    void aColumnThatMayNotBeMovedHasNoBar()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        f.manager.panel(p("a"))->setFeatures({});
        DockAreaWidget *area = areaOf(f.a);
        QTRY_COMPARE(area->columnBars().size(), 1);
        QVERIFY(!barOf(area, "a"));
        QCOMPARE(area->groupOfPanel(p("a"))->y(), 0);
    }

    void theThemeSaysHowHighABarIs()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        DockTheme theme = f.manager.theme();
        theme.columnBarHeight = 22;
        f.manager.setTheme(theme);
        const DockAreaWidget *area = areaOf(f.a);
        QTRY_COMPARE(barOf(areaOf(f.a), "a")->height(), 22);
        QCOMPARE(area->groupOfPanel(p("a"))->y(), 22);
    }

    void theBarShrinksItsColumnToButtons()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        DockAreaWidget *area = areaOf(f.a);
        const QSize before = area->groupOfPanel(p("b"))->size();
        QVERIFY(!f.manager.isColumnIconified(p("b")));

        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);
        QTest::mouseClick(barOf(area, "b")->iconifyButton(), Qt::LeftButton);
        QCOMPARE(changed.size(), 1);
        QVERIFY(f.manager.isColumnIconified(p("b")));
        QVERIFY(f.manager.isColumnIconified(p("c")));
        QVERIFY(!f.manager.isColumnIconified(p("a")));
        QCOMPARE(describe(f.a), p("H(a, V(b, c))")); // the tree is what it was

        // A strip with a button for each panel in place of the two groups.
        DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        QVERIFY(strip && strip->isVisible());
        QCOMPARE(area->iconStrips().size(), 1);
        QVERIFY(!area->groupOfPanel(p("b")));
        QVERIFY(!area->groupOfPanel(p("c")));
        QCOMPARE(strip->buttons().size(), 2);
        QCOMPARE(strip->grips().size(), 2);
        QVERIFY(strip->button(p("b")) && strip->button(p("c")));
        QVERIFY(strip->button(p("b"))->mapTo(strip, QPoint(0, 0)).y()
                < strip->button(p("c"))->mapTo(strip, QPoint(0, 0)).y());
        QVERIFY(!f.widgets[p("b")]->isVisible());
        QVERIFY(!f.widgets[p("c")]->isVisible());
        QVERIFY(f.manager.panel(p("b"))->isOpen());

        // Between the width of its icons and that of its titles, at the
        // border, and what it gave up went to the other column.
        const SizeLimits limits = strip->sizeLimits();
        QVERIFY(limits.min.width() > 0);
        QVERIFY(strip->width() >= limits.min.width() && strip->width() <= limits.max.width());
        QVERIFY(strip->width() < before.width());
        QCOMPARE(strip->geometry().right(), area->contentsRect().right());
        QCOMPARE(strip->geometry().bottom(), area->contentsRect().bottom());
        DockColumnBar *bar = barOf(area, "b");
        QVERIFY(bar && bar->isIconified());
        QCOMPARE(bar->geometry().bottom() + 1, strip->y());
        QCOMPARE(bar->width(), strip->width());
        QCOMPARE(area->groupOfPanel(p("a"))->geometry().right() + 1 + area->handleWidth(),
                 strip->x());

        // The same button brings the panels back, as large as they were.
        QTest::mouseClick(bar->iconifyButton(), Qt::LeftButton);
        QVERIFY(!f.manager.isColumnIconified(p("b")));
        QTRY_VERIFY(area->iconStrips().isEmpty());
        QCOMPARE(area->groupOfPanel(p("b"))->size(), before);
        QVERIFY(f.widgets[p("b")]->isVisible());

        // So does a double click on the bar, and undo.
        QTest::mouseDClick(barOf(area, "b"), Qt::LeftButton, {}, QPoint(40, 4));
        QVERIFY(f.manager.isColumnIconified(p("b")));
        QVERIFY(f.manager.undo());
        QVERIFY(!f.manager.isColumnIconified(p("b")));
        QVERIFY(f.manager.redo());
        QVERIFY(f.manager.isColumnIconified(p("c")));
    }

    void columnsAreIconifiedWithoutBarsToo()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockAreaWidget *area = areaOf(f.a);
        QVERIFY(area->columnBars().isEmpty());
        const DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        QVERIFY(strip && strip->isVisible());
        QCOMPARE(strip->y(), 0);
        QVERIFY(f.manager.setColumnIconified(p("b"), true)); // nothing to do
        QVERIFY(f.manager.setColumnIconified(p("b"), false));
        QVERIFY(area->groupOfPanel(p("b")));

        QVERIFY(!f.manager.setColumnIconified(p("nobody"), true));
        QVERIFY(!f.manager.setColumnIconified(p("c"), true)); // not placed
        QVERIFY(!f.manager.isColumnIconified(p("c")));
        QVERIFY(f.manager.columnPanels(p("c")).isEmpty());
    }

    void aButtonBringsItsGroupOutBesideTheStrip()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        QVERIFY(f.manager.movePanel(p("d"), p("b"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("b")));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockAreaWidget *area = areaOf(f.a);
        DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        QCOMPARE(strip->buttons().size(), 3);
        QCOMPARE(strip->grips().size(), 2); // b and d are one group
        QVERIFY(area->flyout().isNull());

        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);
        QTest::mouseClick(strip->button(p("b")), Qt::LeftButton);
        const NodeId group = area->tree().findPanel(p("b"))->id;
        QCOMPARE(area->flyout(), group);
        DockTabGroup *out = area->groupOfPanel(p("b"));
        QVERIFY(out && out->isVisible() && out->isFlyout());
        QCOMPARE(out->panelIds(), QStringList({p("b"), p("d")}));
        QVERIFY(f.widgets[p("b")]->isVisible());
        QVERIFY(strip->button(p("b"))->isChecked());
        QVERIFY(!strip->button(p("d"))->isChecked());
        QCOMPARE(f.manager.activePanel(), f.manager.panel(p("b")));
        // Beside the strip, towards the middle, over the other column.
        QCOMPARE(out->geometry().right() + 1, strip->x());
        QVERIFY(out->geometry().intersects(area->groupOfPanel(p("a"))->geometry()));
        QVERIFY(area->contentsRect().contains(out->geometry()));
        QVERIFY(out->flyoutButton()->isVisible());
        QVERIFY(!out->maximizeButton()->isVisible());
        // The layout is not concerned.
        QCOMPARE(changed.size(), 0);
        QCOMPARE(describe(f.a), p("H(a, V(b|d, c))"));

        // Another tab of it: by its button, and by its tab.
        QTest::mouseClick(strip->button(p("d")), Qt::LeftButton);
        QCOMPARE(area->flyout(), group);
        QCOMPARE(area->groupOfPanel(p("b")), out);
        QCOMPARE(out->currentPanel(), p("d"));
        QVERIFY(strip->button(p("d"))->isChecked());
        QVERIFY(!strip->button(p("b"))->isChecked());
        QVERIFY(f.widgets[p("d")]->isVisible());

        // Another group takes its place.
        QTest::mouseClick(strip->button(p("c")), Qt::LeftButton);
        QCOMPARE(area->flyout(), area->tree().findPanel(p("c"))->id);
        QVERIFY(!area->groupOfPanel(p("b")));
        QVERIFY(!f.widgets[p("d")]->isVisible());
        QVERIFY(f.widgets[p("c")]->isVisible());
        QVERIFY(strip->button(p("c"))->isChecked());
        QVERIFY(!strip->button(p("d"))->isChecked());

        // Its own button puts it away again; so does the one in its header.
        QTest::mouseClick(strip->button(p("c")), Qt::LeftButton);
        QVERIFY(area->flyout().isNull());
        QVERIFY(!area->groupOfPanel(p("c")));
        QVERIFY(!f.widgets[p("c")]->isVisible());
        QVERIFY(!strip->button(p("c"))->isChecked());
        QTest::mouseClick(strip->button(p("c")), Qt::LeftButton);
        QTest::mouseClick(area->groupOfPanel(p("c"))->flyoutButton(), Qt::LeftButton);
        QVERIFY(area->flyout().isNull());
        QVERIFY(!f.widgets[p("c")]->isVisible());

        // activatePanel() brings a panel out as well.
        QVERIFY(f.manager.activatePanel(p("b")));
        QCOMPARE(area->flyout(), group);
        QVERIFY(f.widgets[p("b")]->isVisible());

        // Shown again, the column has the group that was out where it belongs.
        QVERIFY(f.manager.setColumnIconified(p("b"), false));
        QVERIFY(area->flyout().isNull());
        const DockTabGroup *docked = area->groupOfPanel(p("b"));
        QVERIFY(docked && !docked->isFlyout());
        QCOMPARE(docked->geometry().right(), area->contentsRect().right());
        QVERIFY(!docked->flyoutButton()->isVisible());
        QVERIFY(f.widgets[p("b")]->isVisible());
    }

    void whatComesOutIsAsLargeAsItWas()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        DockAreaWidget *area = areaOf(f.a);
        const QSize before = area->groupOfPanel(p("c"))->size();
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        QVERIFY(f.manager.activatePanel(p("c")));
        const DockTabGroup *out = area->groupOfPanel(p("c"));
        QVERIFY(out && out->isFlyout());
        QCOMPARE(out->size(), before);
        // Level with its buttons, as there is room for that.
        const DockIconStrip *strip = stripOf(f.a, f.manager, "c");
        QCOMPARE(out->y(), strip->mapTo(area, strip->blockRect(out->nodeId()).topLeft()).y());
        QVERIFY(area->contentsRect().contains(out->geometry()));
        // In a smaller window it is moved up to fit in.
        f.windowA.resize(900, before.height() + 40);
        QTRY_COMPARE(out->geometry().bottom(), area->contentsRect().bottom());
        QCOMPARE(out->height(), before.height());
    }

    void aColumnOfOneGroupHasThatGroupOutBesideItsStrip()
    {
        // (Group and column are one node then.)
        TwoWindows f;
        f.show();
        f.a->setColumnDocking(true);
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right, -1, 0.3));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockAreaWidget *area = areaOf(f.a);
        const DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        const QRect narrow = strip->geometry();
        QVERIFY(f.manager.activatePanel(p("b")));
        const DockTabGroup *out = area->groupOfPanel(p("b"));
        QVERIFY(out && out->isFlyout() && out->isVisible());
        QCOMPARE(strip->geometry(), narrow);
        QCOMPARE(out->geometry().right() + 1, strip->x());
        QCOMPARE(out->y(), strip->y());
        QCOMPARE(barOf(area, "b")->geometry(),
                 QRect(narrow.left(), 0, narrow.width(), priv(f.manager)->columnBarHeight()));
    }

    void aStripOnTheLeftHasItsGroupsOnItsRight()
    {
        TwoWindows f;
        f.show();
        f.a->setColumnDocking(true);
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Left, -1, 0.3));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockAreaWidget *area = areaOf(f.a);
        const DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        QCOMPARE(strip->x(), 0);
        QTest::mouseClick(strip->button(p("b")), Qt::LeftButton);
        const DockTabGroup *out = area->groupOfPanel(p("b"));
        QVERIFY(out && out->isVisible());
        QCOMPARE(out->x(), strip->geometry().right() + 1);
    }

    void aWideStripShowsTheTitles()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        f.manager.panel(p("b"))->setTitle(p("A title of some length"));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockAreaWidget *area = areaOf(f.a);
        DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        const SizeLimits limits = strip->sizeLimits();
        QVERIFY(limits.max.width() > limits.min.width());
        // It had a third of the window: as wide as it gets.
        QCOMPARE(strip->width(), limits.max.width());
        QVERIFY(strip->isLabelled());
        QCOMPARE(strip->button(p("b"))->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QCOMPARE(strip->button(p("b"))->text(), p("A title of some length"));

        // Dragged narrow by its boundary, it is icons alone.
        QCOMPARE(area->visibleHandles().size(), 1);
        area->beginHandleDrag(0, false);
        area->moveHandleDrag(2000);
        area->endHandleDrag(false);
        QCOMPARE(strip->width(), limits.min.width());
        QVERIFY(!strip->isLabelled());
        QCOMPARE(strip->button(p("b"))->toolButtonStyle(), Qt::ToolButtonIconOnly);
        // And no narrower, nor wider than its titles.
        area->beginHandleDrag(0, false);
        area->moveHandleDrag(-2000);
        area->endHandleDrag(false);
        QCOMPARE(strip->width(), limits.max.width());
        QVERIFY(strip->isLabelled());
    }

    void aButtonFollowsItsPanel()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        DockPanel *panel = f.manager.panel(p("b"));
        QPixmap picture(16, 16);
        picture.fill(Qt::red);
        panel->setIcon(QIcon(picture));
        panel->setTitle(p("Layers & Masks"));
        panel->setToolTip(p("Layers of the document"));
        const DockIconButton *button = strip->button(p("b"));
        QCOMPARE(button->text(), p("Layers && Masks"));
        QCOMPARE(button->toolTip(), p("Layers of the document"));
        QCOMPARE(button->icon().pixmap(16, 16).toImage().pixelColor(8, 8), QColor(Qt::red));
        // One without an icon has one made of its title.
        QVERIFY(!strip->button(p("c"))->icon().isNull());

        // Closed, a panel has no button; shown again, it is back among them.
        QVERIFY(f.manager.closePanel(p("b")));
        strip = stripOf(f.a, f.manager, "c");
        QVERIFY(strip && !strip->button(p("b")) && strip->button(p("c")));
        QCOMPARE(areaOf(f.a)->iconStrips().size(), 1);
        QVERIFY(f.manager.isColumnIconified(p("c")));
        QVERIFY(f.manager.openPanel(p("b")));
        strip = stripOf(f.a, f.manager, "b");
        QVERIFY(strip && strip->button(p("b")));
        QVERIFY(f.manager.isColumnIconified(p("b")));
        QCOMPARE(describe(f.a), p("H(a, V(b, c))"));
    }

    void aPanelMayBringItsOwnSmallForm()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        auto *compact = new QLabel(p("small"));
        compact->setFixedSize(30, 120);
        QPointer<QWidget> guard(compact);
        DockPanel *panel = f.manager.panel(p("b"));
        panel->setCompactWidget(compact);
        QCOMPARE(panel->compactWidget(), compact);
        QVERIFY(!compact->isVisible());

        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockAreaWidget *area = areaOf(f.a);
        DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        // In place of its button, under the grip of its group.
        QVERIFY(!strip->button(p("b")));
        QVERIFY(strip->button(p("c")));
        QVERIFY(compact->isVisible());
        QVERIFY(strip->isAncestorOf(compact));
        QCOMPARE(strip->grips().size(), 2);
        QVERIFY(strip->sizeLimits().min.width() >= 30);
        QVERIFY(strip->width() >= 30);
        // Nothing comes out for it.
        QVERIFY(f.manager.activatePanel(p("b")));
        QVERIFY(area->flyout().isNull());

        // Without the other panel, the strip is as wide as that form.
        QVERIFY(f.manager.closePanel(p("c")));
        strip = stripOf(f.a, f.manager, "b");
        QCOMPARE(strip->sizeLimits().min.width(), 30);
        QCOMPARE(strip->sizeLimits().max.width(), 30);
        QCOMPARE(strip->width(), 30);

        // Shown again, the column has the panel itself.
        QVERIFY(f.manager.setColumnIconified(p("b"), false));
        QTRY_VERIFY(!compact->isVisible());
        QVERIFY(guard);
        QVERIFY(f.widgets[p("b")]->isVisible());

        // Replaced, and gone with its panel.
        auto *other = new QLabel(p("other"));
        QPointer<QWidget> otherGuard(other);
        panel->setCompactWidget(other);
        QTRY_VERIFY(!guard);
        QVERIFY(f.manager.unregisterPanel(p("b")));
        QTRY_VERIFY(!otherGuard);
    }

    void aPanelThatIsNotThereInAStripIsNotMaximized()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        QVERIFY(f.manager.maximizePanel(p("b")));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        QVERIFY(f.a->maximizedPanel().isEmpty());
        QVERIFY(areaOf(f.a)->groupOfPanel(p("a"))->isVisible());
        QVERIFY(!f.manager.maximizePanel(p("c")));
        // Another one is, and the strip is out of the way for that.
        QVERIFY(f.manager.maximizePanel(p("a")));
        QVERIFY(!stripOf(f.a, f.manager, "b")->isVisible());
        QVERIFY(areaOf(f.a)->columnBars().isEmpty());
        QVERIFY(f.manager.restoreMaximizedPanel());
        QVERIFY(stripOf(f.a, f.manager, "b")->isVisible());
    }

    void aPushingHandleMovesAStripAlong()
    {
        TwoWindows f;
        f.show();
        f.a->setColumnDocking(true);
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Right, -1, 0.3));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right, -1, 0.01));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        QCOMPARE(describe(f.a), p("H(a, b, c)"));
        DockAreaWidget *area = areaOf(f.a);
        const DockIconStrip *strip = stripOf(f.a, f.manager, "b");
        const int narrow = strip->width();
        QCOMPARE(narrow, strip->sizeLimits().min.width());
        const QRect a = area->groupOfPanel(p("a"))->geometry();
        const QRect c = area->groupOfPanel(p("c"))->geometry();
        // The boundary between the strip and c.
        int handle = -1;
        for (DockSplitHandle *each : area->visibleHandles()) {
            if (each->barGeometry().left() > strip->geometry().right())
                handle = each->index();
        }
        QVERIFY(handle >= 0);

        // As a rule it does not go left: the strip is as narrow as it gets.
        QVERIFY(!f.manager.isSplitterPushEnabled());
        area->beginHandleDrag(handle, false);
        area->moveHandleDrag(-80);
        area->endHandleDrag(false);
        QCOMPARE(area->groupOfPanel(p("c"))->geometry(), c);

        // Pushing, the strip goes with it, and a gives the room.
        f.manager.setSplitterPushEnabled(true);
        QVERIFY(f.manager.isSplitterPushEnabled());
        area->beginHandleDrag(handle, false);
        area->moveHandleDrag(-80);
        area->endHandleDrag(false);
        QCOMPARE(strip->width(), narrow);
        QCOMPARE(strip->x(), stripOf(f.a, f.manager, "b")->x());
        QCOMPARE(area->groupOfPanel(p("c"))->width(), c.width() + 80);
        QCOMPARE(area->groupOfPanel(p("a"))->width(), a.width() - 80);
        QCOMPARE(strip->geometry().right() + 1 + area->handleWidth(),
                 area->groupOfPanel(p("c"))->x());
        // One step to undo, like any drag of a handle.
        QVERIFY(f.manager.undo());
        QCOMPARE(area->groupOfPanel(p("c"))->geometry(), c);
    }

    void whatIsHeldAtItsSizeLeavesNoMoreRoomThanItHad()
    {
        TwoWindows f;
        f.widgets[p("c")]->setFixedWidth(40);
        f.manager.panel(p("c"))->setHeaderVisible(false);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right, -1, 0.4));
        // Docked at a quarter of the window, c is still 40 wide (and its frame).
        QVERIFY(f.manager.movePanel(p("c"), f.a, DockArea::Right));
        DockAreaWidget *area = areaOf(f.a);
        const int narrow = area->groupOfPanel(p("c"))->width();
        QVERIFY(narrow >= 40 && narrow < 50);
        const int a = area->groupOfPanel(p("a"))->width();
        const int b = area->groupOfPanel(p("b"))->width();

        // Put at the other side, it leaves the room it had to its neighbour
        // b, and takes as much from the two: not b a quarter of the window wider.
        const int room = narrow + area->handleWidth();
        QVERIFY(f.manager.movePanel(p("c"), f.a, DockArea::Left));
        QCOMPARE(describe(f.a), p("H(c, a, b)"));
        QCOMPARE(area->groupOfPanel(p("c"))->width(), narrow);
        QVERIFY(qAbs(area->groupOfPanel(p("a"))->width() - a) <= room);
        QVERIFY(qAbs(area->groupOfPanel(p("b"))->width() - b) <= room);
        const int a2 = area->groupOfPanel(p("a"))->width();
        const int b2 = area->groupOfPanel(p("b"))->width();

        // Floated or closed, its pixels go to its neighbour, and are what it
        // takes when it is back.
        // (Give or take the boundary that goes with it, which all share.)
        const int near = area->handleWidth() + 1;
        QVERIFY(f.manager.floatPanel(p("c")));
        QVERIFY(qAbs(area->groupOfPanel(p("a"))->width() - (a2 + room)) <= near);
        QVERIFY(qAbs(area->groupOfPanel(p("b"))->width() - b2) <= near);
        QVERIFY(f.manager.dockPanel(p("c")));
        QCOMPARE(describe(f.a), p("H(c, a, b)"));
        QVERIFY(qAbs(area->groupOfPanel(p("a"))->width() - a2) <= near);
        QVERIFY(f.manager.closePanel(p("c")));
        QVERIFY(qAbs(area->groupOfPanel(p("b"))->width() - b2) <= near);
        QVERIFY(f.manager.openPanel(p("c")));
        QVERIFY(qAbs(area->groupOfPanel(p("a"))->width() - a2) <= near);
        QVERIFY(qAbs(area->groupOfPanel(p("b"))->width() - b2) <= near);

        // So for a column that is a strip of buttons.
        f.widgets[p("c")]->setMinimumWidth(0);
        f.widgets[p("c")]->setMaximumWidth(QWIDGETSIZE_MAX);
        f.manager.panel(p("c"))->setHeaderVisible(true);
        QVERIFY(f.manager.movePanel(p("c"), f.a, DockArea::Right));
        QVERIFY(f.manager.setColumnIconified(p("c"), true));
        const int aBeside = area->groupOfPanel(p("a"))->width();
        const int bBeside = area->groupOfPanel(p("b"))->width();
        QVERIFY(f.manager.moveTabGroup(p("c"), f.a, DockArea::Left));
        QVERIFY(f.manager.isColumnIconified(p("c")));
        const int strip = area->iconStrips().value(0)->width() + area->handleWidth();
        QVERIFY(qAbs(area->groupOfPanel(p("a"))->width() - aBeside) <= strip);
        QVERIFY(qAbs(area->groupOfPanel(p("b"))->width() - bBeside) <= strip);
    }

    // --- Floating windows ------------------------------------------------------

    void theBarOfAFloatingColumnIsItsTitleBar()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        QCOMPARE(f.manager.floatingWindowFrame(), DockManager::FloatingWindowFrame::Custom);
        QVERIFY(f.manager.floatPanel(p("c"), QRect(60, 60, 300, 260)));
        DockFloatingWindow *window = onlyFloatingWindow(f.manager);
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // No title row: the bar stands for the window.
        QVERIFY(window->hasMinimalFrame());
        QVERIFY(!window->titleBar());
        DockAreaWidget *area = window->area();
        QCOMPARE(area->columnBars().size(), 1);
        DockColumnBar *bar = area->columnBars().constFirst();
        QVERIFY(bar->closeButton()->isVisible());
        QCOMPARE(bar->geometry().topLeft(), QPoint(0, 0));
        QCOMPARE(bar->width(), area->width());
        QVERIFY(!area->groupOfPanel(p("c"))->maximizeButton()->isHidden()
                || !area->groupOfPanel(p("c"))->closeButton()->isVisible());
        // The bars in the workspace close nothing.
        QVERIFY(!barOf(areaOf(f.a), "a")->closeButton()->isVisible());

        // Two columns in one window: neither bar is the window's.
        QVERIFY(f.manager.movePanel(p("d"), p("c"), DockArea::Right));
        QCOMPARE(area->columnBars().size(), 2);
        for (const DockColumnBar *each : area->columnBars())
            QVERIFY(!each->closeButton()->isVisible());
        QVERIFY(f.manager.closePanel(p("d")));

        // Closing by the bar closes the panels of the window.
        bar = area->columnBars().constFirst();
        QTest::mouseClick(bar->closeButton(), Qt::LeftButton);
        QTRY_VERIFY(!f.manager.panel(p("c"))->isOpen());
        QTRY_VERIFY(priv(f.manager)->floatingWindows.isEmpty());

        // A workspace that does not dock in columns has its windows as ever.
        QVERIFY(f.b->addPanel(p("e")));
        QVERIFY(f.manager.floatPanel(p("e"), QRect(80, 80, 300, 260)));
        QVERIFY(!onlyFloatingWindow(f.manager)->hasMinimalFrame());
        QVERIFY(onlyFloatingWindow(f.manager)->area()->columnBars().isEmpty());
    }

    void anIconifiedWindowIsAsLargeAsItsStrip()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        QVERIFY(f.manager.floatPanel(p("c"), QRect(60, 60, 320, 280)));
        DockFloatingWindow *window = onlyFloatingWindow(f.manager);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(window->size(), QSize(320, 280));
        DockAreaWidget *area = window->area();
        QVERIFY(!area->iconifiedSizeHint().isValid());

        QTest::mouseClick(area->columnBars().constFirst()->iconifyButton(), Qt::LeftButton);
        QVERIFY(f.manager.isColumnIconified(p("c")));
        const DockIconStrip *strip = area->iconStrips().value(0);
        QVERIFY(strip);
        const int border = window->borderWidth();
        const QSize wanted = area->iconifiedSizeHint() + QSize(2 * border, 2 * border);
        QVERIFY(wanted.isValid());
        QTRY_COMPARE(window->size(), wanted);
        QVERIFY(wanted.width() < 320 && wanted.height() < 280);
        QCOMPARE(strip->width(), strip->sizeLimits().max.width());

        // There is no room beside the strip of a window: the window makes
        // it, for as long as the group is out.
        const int narrow = strip->width();
        QTest::mouseClick(strip->button(p("c")), Qt::LeftButton);
        QVERIFY(f.manager.isColumnIconified(p("c")));
        const DockTabGroup *out = area->groupOfPanel(p("c"));
        QVERIFY(out && out->isFlyout() && out->isVisible());
        QTRY_COMPARE(window->size(), area->iconifiedSizeHint() + QSize(2 * border, 2 * border));
        QVERIFY(window->width() >= wanted.width() + out->sizeLimits().min.width());
        QVERIFY(window->height() > wanted.height());
        QTRY_COMPARE(strip->width(), narrow);
        QCOMPARE(strip->x(), 0);
        QCOMPARE(out->x(), strip->geometry().right() + 1);
        QCOMPARE(out->geometry().right(), area->contentsRect().right());
        QVERIFY(f.widgets[p("c")]->isVisible());
        // The bar is still that of the strip, and of the window.
        QCOMPARE(area->columnBars().constFirst()->width(), narrow);
        QVERIFY(area->columnBars().constFirst()->closeButton()->isVisible());

        // Put away, the window is the strip again.
        QTest::mouseClick(strip->button(p("c")), Qt::LeftButton);
        QVERIFY(area->flyout().isNull());
        QTRY_COMPARE(window->size(), wanted);
        QVERIFY(!f.widgets[p("c")]->isVisible());

        // And the bar shows the panels again, at the size the window had.
        QTest::mouseClick(area->columnBars().constFirst()->iconifyButton(), Qt::LeftButton);
        QVERIFY(!f.manager.isColumnIconified(p("c")));
        QTRY_COMPARE(window->size(), QSize(320, 280));
        QVERIFY(f.widgets[p("c")]->isVisible());
    }

    void whatLeavesAnIconifiedColumnForAWindowStaysIconified()
    {
        TwoWindows f;
        f.show();
        buildColumns(f);
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        QVERIFY(f.manager.floatPanel(p("c")));
        QVERIFY(f.manager.panel(p("c"))->isFloating());
        QVERIFY(f.manager.isColumnIconified(p("c")));
        QVERIFY(f.manager.isColumnIconified(p("b")));
        DockFloatingWindow *window = onlyFloatingWindow(f.manager);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(window->area()->iconStrips().size(), 1);
        const int border = window->borderWidth();
        QTRY_COMPARE(window->size(),
                     window->area()->iconifiedSizeHint() + QSize(2 * border, 2 * border));
        // One that is open floats open.
        QVERIFY(f.manager.floatPanel(p("a")));
        QVERIFY(!f.manager.isColumnIconified(p("a")));
    }

    void destroyingAWorkspaceWithAStripLeavesTheContentAlone()
    {
        DockManager manager;
        auto *content = new QLabel(p("content"));
        auto *compact = new QLabel(p("compact"));
        const QPointer<QWidget> contentGuard(content);
        const QPointer<QWidget> compactGuard(compact);
        {
            QMainWindow window;
            DockWorkspace *workspace = manager.createWorkspace(p("W"));
            window.setCentralWidget(workspace);
            workspace->setColumnDocking(true);
            manager.registerPanel(p("x"), content)->setCompactWidget(compact);
            manager.registerPanel(p("y"), new QLabel(p("y")));
            QVERIFY(workspace->addPanel(p("x")));
            QVERIFY(manager.movePanel(p("y"), p("x"), DockArea::Right));
            QVERIFY(manager.setColumnIconified(p("x"), true));
            QVERIFY(manager.setColumnIconified(p("y"), true));
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(manager.activatePanel(p("y")));
            QVERIFY(compact->isVisible());
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(contentGuard);
        QVERIFY(compactGuard);
        QVERIFY(!manager.panel(p("x"))->isOpen());
    }
};

QTEST_MAIN(tst_Columns)
#include "tst_columns.moc"
