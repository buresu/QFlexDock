// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include "widgets/DockSplitHandle.h"

#include <QtTest/QSignalSpy>
#include <QtWidgets/QBoxLayout>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

// 2x2 grid in workspace A: V(H(a, b), H(c, d)).
void makeGrid(TwoWindows &f)
{
    QVERIFY(f.a->addPanel(p("a")));
    QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Bottom));
    QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
    QVERIFY(f.manager.movePanel(p("d"), p("c"), DockArea::Right));
    QCOMPARE(describe(f.a), p("V(H(a, b), H(c, d))"));
    f.manager.clearUndoHistory();
}

// The handle to the right of / below the group holding `panel`.
DockSplitHandle *handleAfter(DockAreaWidget *area, const char *panel, Qt::Orientation orientation)
{
    const QRect group = area->groupOfPanel(p(panel))->geometry();
    for (DockSplitHandle *handle : area->visibleHandles()) {
        if (handle->orientation() != orientation)
            continue;
        const QRect r = handle->barGeometry();
        const bool adjacent = orientation == Qt::Horizontal
            ? r.left() == group.right() + 1 && r.top() <= group.center().y()
                  && r.bottom() >= group.center().y()
            : r.top() == group.bottom() + 1;
        if (adjacent)
            return handle;
    }
    return nullptr;
}

int widthOf(DockAreaWidget *area, const char *panel)
{
    return area->groupOfPanel(p(panel))->width();
}

void drag(DockSplitHandle *handle, const QPoint &offset, Qt::KeyboardModifiers modifiers = {},
          bool release = true)
{
    const QPoint start = handle->rect().center();
    QTest::mousePress(handle, Qt::LeftButton, modifiers, start);
    // The handle follows the pointer, so later positions are given relative
    // to where it was pressed.
    const QPoint origin = handle->mapToGlobal(start);
    for (int step = 1; step <= 4; ++step) {
        const QPoint global = origin + offset * step / 4;
        QTest::mouseMove(handle, handle->mapFromGlobal(global));
    }
    if (release)
        QTest::mouseRelease(handle, Qt::LeftButton, modifiers,
                            handle->mapFromGlobal(origin + offset));
}

} // namespace

class tst_Splitter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void handlesMirrorTheSplits()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        QCOMPARE(area->visibleHandles().size(), 3);
        DockSplitHandle *top = handleAfter(area, "a", Qt::Horizontal);
        DockSplitHandle *bottom = handleAfter(area, "c", Qt::Horizontal);
        QVERIFY(top && bottom && top != bottom);
        QCOMPARE(top->barGeometry().x(), bottom->barGeometry().x());
        QCOMPARE(top->barGeometry().width(), area->handleWidth());
        QVERIFY(top->isVisible());
    }

    // Styles may ask for handles a single pixel thin. Such a handle still has
    // to be something the pointer can take hold of, so the events go through
    // the window here, the way real input finds its widget.
    void thinHandlesCanBeGrabbed_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("aim"); // distance of the press from the bar's centre
        QTest::newRow("1px, on the bar") << 1 << 0;
        QTest::newRow("1px, beside it") << 1 << 3;
        QTest::newRow("1px, on the other side") << 1 << -3;
        QTest::newRow("2px, beside it") << 2 << 3;
        QTest::newRow("wide, on the bar") << 12 << 0;
    }

    void thinHandlesCanBeGrabbed()
    {
        QFETCH(int, width);
        QFETCH(int, aim);
        TwoWindows f;
        DockTheme theme;
        theme.splitHandleWidth = width;
        f.manager.setTheme(theme);
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        DockSplitHandle *handle = handleAfter(area, "a", Qt::Horizontal);
        QVERIFY(handle);
        QCOMPARE(handle->barGeometry().width(), width);
        QVERIFY(handle->width() >= qMin(width, DockSplitHandle::MinimumGrabExtent));
        const int a = widthOf(area, "a");
        const int row = area->groupOfPanel(p("a"))->height();

        QWindow *window = f.windowA.windowHandle();
        const QPoint press =
            area->mapTo(&f.windowA, handle->barGeometry().center() + QPoint(aim, 0));
        QCOMPARE(f.windowA.childAt(press), handle);
        // Merely pointing at it shows: the handle lights up (together with the
        // one it is linked to) and offers the resize cursor. Needs a pointer
        // that can be put somewhere, which a Wayland client does not have.
        QCOMPARE(handle->cursor().shape(), Qt::SplitHCursor);
        if (windowPositionsWork()) {
            QVERIFY(!handle->isHovered());
            // (Arriving from elsewhere in the window: on a fresh X server a
            // pointer put straight onto the handle went unnoticed.)
            QCursor::setPos(f.windowA.mapToGlobal(QPoint(8, 8)));
            QTest::mouseMove(window, QPoint(8, 8));
            QCursor::setPos(f.windowA.mapToGlobal(press));
            QTest::mouseMove(window, press);
            QTRY_VERIFY(handle->isHovered());
            QVERIFY(handleAfter(area, "c", Qt::Horizontal)->isHovered());
        }
        QTest::mousePress(window, Qt::LeftButton, {}, press);
        QTest::mouseMove(window, press + QPoint(30, 0));
        QTest::mouseMove(window, press + QPoint(60, 0));
        QTest::mouseRelease(window, Qt::LeftButton, {}, press + QPoint(60, 0));
        QCOMPARE(widthOf(area, "a"), a + 60);
        QCOMPARE(widthOf(area, "c"), a + 60);

        // The boundary between the rows, the same way.
        DockSplitHandle *rows = handleAfter(area, "a", Qt::Vertical);
        QVERIFY(rows);
        QCOMPARE(rows->barGeometry().height(), width);
        const QPoint below =
            area->mapTo(&f.windowA, rows->barGeometry().center() + QPoint(0, aim));
        QCOMPARE(f.windowA.childAt(below), rows);
        QTest::mousePress(window, Qt::LeftButton, {}, below);
        QTest::mouseMove(window, below + QPoint(0, 25));
        QTest::mouseRelease(window, Qt::LeftButton, {}, below + QPoint(0, 25));
        QCOMPARE(area->groupOfPanel(p("a"))->height(), row + 25);
    }

    // The margin a thin handle takes the mouse with is not painted.
    void grabMarginIsNotPainted()
    {
        TwoWindows f;
        DockTheme theme;
        theme.splitHandleWidth = 1;
        f.manager.setTheme(theme);
        f.windowA.setStyleSheet(
            QStringLiteral("QFlexDock--DockSplitHandle { background: #ff0000; }"));
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        DockSplitHandle *handle = handleAfter(area, "a", Qt::Horizontal);
        QVERIFY(handle);
        QVERIFY(handle->width() > 1);

        const QImage image = picture(&f.windowA);
        const QPoint bar = area->mapTo(&f.windowA, handle->barGeometry().center());
        const QRgb red = QColor(0xff, 0, 0).rgb();
        QCOMPARE(image.pixelColor(bar).rgb(), red);
        for (int offset : {-3, -2, -1, 1, 2, 3})
            QVERIFY2(image.pixelColor(bar + QPoint(offset, 0)).rgb() != red,
                     qPrintable(QStringLiteral("offset %1").arg(offset)));
    }

    // A thin boundary may light up wider than it is while it is pointed at.
    void hoveredHandleCanBeDrawnWider()
    {
        TwoWindows f;
        DockTheme theme;
        theme.splitHandleWidth = 1;
        theme.splitHandleHoverWidth = 4;
        f.manager.setTheme(theme);
        f.windowA.setStyleSheet(QStringLiteral(
            "QFlexDock--DockSplitHandle { background: #00ff00; }"
            "QFlexDock--DockSplitHandle[hovered=\"true\"] { background: #ff0000; }"));
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        DockSplitHandle *handle = handleAfter(area, "a", Qt::Horizontal);
        QVERIFY(handle);
        QCOMPARE(handle->barGeometry().width(), 1);
        QCOMPARE(handle->drawnGeometry(), handle->barGeometry());
        const QRect grabArea = handle->geometry();

        const auto redColumns = [&] {
            const QImage image = picture(&f.windowA);
            const QPoint bar = area->mapTo(&f.windowA, handle->barGeometry().center());
            QList<int> offsets;
            for (int offset = -4; offset <= 4; ++offset) {
                if (image.pixelColor(bar + QPoint(offset, 0)) == QColor(0xff, 0, 0))
                    offsets << offset;
            }
            return offsets;
        };
        QVERIFY(redColumns().isEmpty());

        handle->setHovered(true);
        QCOMPARE(handle->drawnGeometry().width(), 4);
        QVERIFY(handle->drawnGeometry().contains(handle->barGeometry()));
        QCOMPARE(handle->drawnGeometry().height(), handle->barGeometry().height());
        QCOMPARE(redColumns(), (QList<int>{-1, 0, 1, 2}));
        // Only what is drawn changes: the layout and the reach of the mouse stay.
        QCOMPARE(handle->barGeometry().width(), 1);
        QCOMPARE(handle->geometry(), grabArea);

        handle->setHovered(false);
        QCOMPARE(handle->drawnGeometry(), handle->barGeometry());
        QVERIFY(redColumns().isEmpty());

        // Without the token a hovered handle is as thick as always.
        theme.splitHandleHoverWidth = -1;
        f.manager.setTheme(theme);
        QCoreApplication::processEvents();
        handle = handleAfter(area, "a", Qt::Horizontal);
        handle->setHovered(true);
        QCOMPARE(handle->drawnGeometry(), handle->barGeometry());
        QCOMPARE(redColumns(), (QList<int>{0}));
    }

    void draggingMovesAlignedHandlesTogether()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        const int a = widthOf(area, "a");
        const int b = widthOf(area, "b");
        QCOMPARE(widthOf(area, "c"), a);

        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(90, 0));
        QCOMPARE(widthOf(area, "a"), a + 90);
        QCOMPARE(widthOf(area, "c"), a + 90);
        QCOMPARE(widthOf(area, "b"), b - 90);
        QCOMPARE(widthOf(area, "d"), b - 90);
        QVERIFY(areaOf(f.a)->tree().validate());
        grab(&f.windowA, p("splitter-linked"));

        // Grabbing the other one of the pair does the same.
        drag(handleAfter(area, "c", Qt::Horizontal), QPoint(-40, 0));
        QCOMPARE(widthOf(area, "a"), a + 50);
        QCOMPARE(widthOf(area, "c"), a + 50);

        // The boundary between the rows has no partner and moves alone.
        const int height = area->groupOfPanel(p("a"))->height();
        drag(handleAfter(area, "a", Qt::Vertical), QPoint(0, 30));
        QCOMPARE(area->groupOfPanel(p("a"))->height(), height + 30);
        QCOMPARE(area->groupOfPanel(p("b"))->height(), height + 30);
        QCOMPARE(widthOf(area, "a"), a + 50);
    }

    void altOrTheSettingMoveASingleHandle()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        const int a = widthOf(area, "a");

        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(60, 0), Qt::AltModifier);
        QCOMPARE(widthOf(area, "a"), a + 60);
        QCOMPARE(widthOf(area, "c"), a);

        // No longer aligned, so no longer linked.
        drag(handleAfter(area, "c", Qt::Horizontal), QPoint(-30, 0));
        QCOMPARE(widthOf(area, "a"), a + 60);
        QCOMPARE(widthOf(area, "c"), a - 30);

        // Bring them back into line: linked again.
        drag(handleAfter(area, "c", Qt::Horizontal), QPoint(90, 0));
        QCOMPARE(widthOf(area, "c"), a + 60);
        drag(handleAfter(area, "c", Qt::Horizontal), QPoint(10, 0));
        QCOMPARE(widthOf(area, "a"), a + 70);
        QCOMPARE(widthOf(area, "c"), a + 70);

        QSignalSpy toggled(&f.manager, &DockManager::linkedSplittersEnabledChanged);
        f.manager.setLinkedSplittersEnabled(false);
        QCOMPARE(toggled.size(), 1);
        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(-20, 0));
        QCOMPARE(widthOf(area, "a"), a + 50);
        QCOMPARE(widthOf(area, "c"), a + 70);
    }

    void hoverLightsUpTheWholeLinkedRun()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        DockSplitHandle *top = handleAfter(area, "a", Qt::Horizontal);
        DockSplitHandle *bottom = handleAfter(area, "c", Qt::Horizontal);
        DockSplitHandle *middle = handleAfter(area, "a", Qt::Vertical);

        area->setHandleHover(top->index(), true);
        QVERIFY(top->isHovered());
        QVERIFY(bottom->isHovered());
        QVERIFY(!middle->isHovered());
        area->setHandleHover(top->index(), false);
        QVERIFY(!top->isHovered() && !bottom->isHovered());
        area->setHandleHover(middle->index(), true);
        QVERIFY(middle->isHovered() && !top->isHovered());
    }

    void sizeLimitsStopTheWholeRun()
    {
        TwoWindows f;
        // (Both above the width a group's own title row needs.)
        f.widgets[p("a")]->setMinimumWidth(240);
        f.widgets[p("c")]->setMinimumWidth(360);
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        const int total = widthOf(area, "a") + widthOf(area, "b");
        const int chrome = widthOf(area, "c") - f.widgets[p("c")]->width();

        // Far to the left: c's minimum stops both, although a could go further.
        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(-2000, 0));
        QCOMPARE(f.widgets[p("c")]->width(), 360);
        QCOMPARE(widthOf(area, "a"), 360 + chrome);
        QCOMPARE(widthOf(area, "a") + widthOf(area, "b"), total);

        // Alone, a goes down to its own minimum.
        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(-2000, 0), Qt::AltModifier);
        QCOMPARE(f.widgets[p("a")]->width(), 240);
        QCOMPARE(f.widgets[p("c")]->width(), 360);

        // A maximum limits just the same: d may grow by 30 more pixels only.
        drag(handleAfter(area, "c", Qt::Horizontal), QPoint(200, 0));
        const int c = f.widgets[p("c")]->width();
        f.widgets[p("d")]->setMaximumWidth(f.widgets[p("d")]->width() + 30);
        QCoreApplication::processEvents();
        drag(handleAfter(area, "c", Qt::Horizontal), QPoint(-2000, 0));
        QCOMPARE(f.widgets[p("c")]->width(), c - 30);
        QCOMPARE(f.widgets[p("d")]->width(), f.widgets[p("d")]->maximumWidth());

        // Far to the right nothing is pushed out of the area either.
        f.widgets[p("d")]->setMaximumWidth(QWIDGETSIZE_MAX);
        QCoreApplication::processEvents();
        drag(handleAfter(area, "c", Qt::Horizontal), QPoint(2000, 0));
        QVERIFY(areaOf(f.a)->tree().validate());
        QRegion covered;
        for (const DockTabGroup *group : area->groups())
            covered += group->geometry();
        for (const SolvedHandle &handle : area->solved().handles)
            covered += handle.rect;
        QCOMPARE(covered, QRegion(area->contentsRect()));
    }

    void escapeCancelsADrag()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        const int a = widthOf(area, "a");
        DockSplitHandle *handle = handleAfter(area, "a", Qt::Horizontal);

        drag(handle, QPoint(120, 0), {}, false);
        QCOMPARE(widthOf(area, "a"), a + 120);
        QTest::keyClick(handle, Qt::Key_Escape);
        QCOMPARE(widthOf(area, "a"), a);
        QCOMPARE(widthOf(area, "c"), a);
        // Moving on with the button still down does nothing any more.
        QTest::mouseMove(handle, handle->rect().center() + QPoint(50, 0));
        QTest::mouseRelease(handle, Qt::LeftButton);
        QCOMPARE(widthOf(area, "a"), a);
        QVERIFY(!f.manager.canUndo());
    }

    // --- squeezing a group out -----------------------------------------------
    // A side area that the handle is pushed far enough against gives way,
    // comes back if the pointer returns, and is closed when the button is
    // let go there.
    void collapsibleGroupGivesWayToTheHandle()
    {
        TwoWindows f;
        f.widgets[p("b")]->setMinimumWidth(200);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Left, 0.35));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Right, 0.25));
        QCOMPARE(describe(f.a), p("H(b, a, c)"));
        f.manager.clearUndoHistory();
        DockAreaWidget *area = areaOf(f.a);
        DockPanel *b = f.manager.panel(p("b"));
        QVERIFY(!b->isCollapsible());
        const int width = widthOf(area, "b");
        const int minimum = area->groupOfPanel(p("b"))->sizeLimits().min.width();
        const int other = widthOf(area, "c");
        QVERIFY(width > minimum + 20 && minimum >= 200);
        const auto press = [&](DockSplitHandle *handle) {
            QTest::mousePress(handle, Qt::LeftButton, {}, handle->rect().center());
            return handle->mapToGlobal(handle->rect().center());
        };
        const auto moveTo = [](DockSplitHandle *handle, const QPoint &global) {
            QTest::mouseMove(handle, handle->mapFromGlobal(global));
        };
        const auto release = [](DockSplitHandle *handle, const QPoint &global) {
            QTest::mouseRelease(handle, Qt::LeftButton, {}, handle->mapFromGlobal(global));
        };

        // Not unless the panel says so: the group stops at its minimum.
        DockSplitHandle *handle = handleAfter(area, "b", Qt::Horizontal);
        QVERIFY(handle);
        drag(handle, QPoint(-width, 0));
        QCOMPARE(widthOf(area, "b"), minimum);
        QVERIFY(b->isOpen());
        QVERIFY(f.manager.undo());
        QCOMPARE(widthOf(area, "b"), width);

        b->setCollapsible(true);
        handle = handleAfter(area, "b", Qt::Horizontal);
        QPoint origin = press(handle);
        // Somewhat past its minimum: it holds.
        moveTo(handle, origin - QPoint(width - minimum + minimum / 2 - 10, 0));
        QVERIFY(area->groupOfPanel(p("b"))->isVisible());
        QCOMPARE(widthOf(area, "b"), minimum);
        // Less than half of it left: gone from view, but only from view.
        moveTo(handle, origin - QPoint(width - minimum / 2 + 10, 0));
        QVERIFY(!area->groupOfPanel(p("b"))->isVisible());
        QVERIFY(b->isOpen());
        QCOMPARE(describe(f.a), p("H(b, a, c)"));
        QCOMPARE(area->groupOfPanel(p("a"))->x(), 0);
        QVERIFY(qAbs(widthOf(area, "c") - other) <= 1);
        // The handle is still held, and shown where the group went.
        QVERIFY(handle->isVisible());
        QVERIFY(handle->isPressed());
        QCOMPARE(handle->barGeometry().left(), 0);
        grab(&f.windowA, p("splitter-squeezed"));
        // Back: there it is again, at its minimum.
        moveTo(handle, origin - QPoint(width - minimum, 0));
        QVERIFY(area->groupOfPanel(p("b"))->isVisible());
        QCOMPARE(widthOf(area, "b"), minimum);
        QCOMPARE(handle->barGeometry().left(), minimum);
        // And out again; this time the button is let go.
        QSignalSpy opened(b, &DockPanel::openChanged);
        moveTo(handle, origin - QPoint(width + 40, 0));
        release(handle, origin - QPoint(width + 40, 0));
        QVERIFY(!b->isOpen());
        QCOMPARE(opened.size(), 1);
        QCOMPARE(describe(f.a), p("H(a, c)"));
        QVERIFY(qAbs(widthOf(area, "c") - other) <= 1);
        QVERIFY(areaOf(f.a)->tree().validate());

        // It comes back as wide as it was before the drag.
        QVERIFY(f.manager.openPanel(p("b")));
        QCOMPARE(describe(f.a), p("H(b, a, c)"));
        QVERIFY(qAbs(widthOf(area, "b") - width) <= 1);
        QVERIFY(qAbs(widthOf(area, "c") - other) <= 1);

        // The whole drag is one undo step.
        f.manager.clearUndoHistory();
        handle = handleAfter(area, "b", Qt::Horizontal);
        drag(handle, QPoint(-width - 40, 0));
        QVERIFY(!b->isOpen());
        QVERIFY(f.manager.undo());
        QVERIFY(b->isOpen());
        QVERIFY(qAbs(widthOf(area, "b") - width) <= 1);
        QVERIFY(!f.manager.canUndo());

        // Escape: nothing happened.
        handle = handleAfter(area, "b", Qt::Horizontal);
        origin = press(handle);
        moveTo(handle, origin - QPoint(width + 40, 0));
        QVERIFY(!area->groupOfPanel(p("b"))->isVisible());
        QTest::keyClick(handle, Qt::Key_Escape);
        QVERIFY(b->isOpen());
        QVERIFY(area->groupOfPanel(p("b"))->isVisible());
        QVERIFY(qAbs(widthOf(area, "b") - width) <= 1);
        QVERIFY(!f.manager.canUndo());

        // The group on the other side goes the other way, tabs and all,
        // if every panel in it may.
        QVERIFY(f.manager.movePanel(p("d"), p("c"), DockArea::Center));
        f.manager.panel(p("c"))->setCollapsible(true);
        handle = handleAfter(area, "a", Qt::Horizontal);
        drag(handle, QPoint(other + 40, 0));
        QVERIFY(f.manager.panel(p("c"))->isOpen()); // d does not allow it
        QVERIFY(f.manager.undo());
        f.manager.panel(p("d"))->setCollapsible(true);
        handle = handleAfter(area, "a", Qt::Horizontal);
        drag(handle, QPoint(other + 40, 0));
        QVERIFY(!f.manager.panel(p("c"))->isOpen());
        QVERIFY(!f.manager.panel(p("d"))->isOpen());
        QCOMPARE(describe(f.a), p("H(b, a)"));
        // Either one brings the group back, the other finds it there.
        QVERIFY(f.manager.openPanel(p("d")));
        QVERIFY(f.manager.openPanel(p("c")));
        QCOMPARE(describe(f.a), p("H(b, a, c|d)"));
        QVERIFY(qAbs(widthOf(area, "c") - other) <= 1);
    }

    // What went to an edge can be pulled out of it again.
    void closedCollapsiblePanelsArePulledOutOfTheirEdge()
    {
        TwoWindows f;
        f.widgets[p("b")]->setMinimumWidth(200);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Left, 0.35));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Right, 0.25));
        QVERIFY(f.manager.movePanel(p("d"), p("c"), DockArea::Center));
        QCOMPARE(describe(f.a), p("H(b, a, c|d)"));
        DockAreaWidget *area = areaOf(f.a);
        DockPanel *b = f.manager.panel(p("b"));
        const int width = widthOf(area, "b");
        const int minimum = area->groupOfPanel(p("b"))->sizeLimits().min.width();
        const int other = widthOf(area, "c");
        QVERIFY(area->visibleEdgeHandles().isEmpty());

        // Closed panels leave an edge to pull at only if they are collapsible.
        QVERIFY(f.manager.closePanel(p("b")));
        QVERIFY(area->visibleEdgeHandles().isEmpty());
        b->setCollapsible(true);
        area->relayout();
        QCOMPARE(area->visibleEdgeHandles().size(), 1);
        DockEdgeHandle *edge = area->visibleEdgeHandles().constFirst();
        QCOMPARE(edge->panels(), QStringList{p("b")});
        QCOMPARE(edge->side(), DockArea::Left);
        QCOMPARE(edge->property("edge").toString(), p("left"));
        QCOMPARE(edge->barGeometry().left(), 0);
        QCOMPARE(edge->barGeometry().height(), area->height());
        QVERIFY(edge->width() >= DockEdgeHandle::GrabExtent);
        // It is what the pointer finds there.
        QCOMPARE(f.windowA.childAt(area->mapTo(&f.windowA, QPoint(3, area->height() / 2))), edge);
        f.manager.clearUndoHistory();

        const QPoint start = edge->rect().center();
        const QPoint origin = edge->mapToGlobal(start);
        const auto pull = [&](int inward) {
            QTest::mouseMove(edge, edge->mapFromGlobal(origin + QPoint(inward, 0)));
        };
        const auto release = [&](int inward) {
            QTest::mouseRelease(edge, Qt::LeftButton, {},
                                edge->mapFromGlobal(origin + QPoint(inward, 0)));
        };

        // A press alone, or the wrong way, does nothing.
        QTest::mousePress(edge, Qt::LeftButton, {}, start);
        QVERIFY(edge->isPressed());
        pull(-40);
        QVERIFY(!b->isOpen());
        // Inwards: out it comes, though not into view before half of its
        // minimum fits.
        pull(minimum / 2 - 20);
        QVERIFY(b->isOpen());
        QVERIFY(!area->groupOfPanel(p("b"))->isVisible());
        QVERIFY(edge->isVisible());
        // Then at its minimum, then as wide as the pointer is far.
        pull(minimum / 2 + 20);
        QVERIFY(area->groupOfPanel(p("b"))->isVisible());
        QCOMPARE(widthOf(area, "b"), minimum);
        pull(minimum + 60);
        QCOMPARE(widthOf(area, "b"), minimum + 60);
        QVERIFY(qAbs(widthOf(area, "c") - other) <= 1);
        grab(&f.windowA, p("splitter-pulled-out"));
        release(minimum + 60);
        QVERIFY(b->isOpen());
        QCOMPARE(describe(f.a), p("H(b, a, c|d)"));
        QCOMPARE(widthOf(area, "b"), minimum + 60);
        QVERIFY(area->visibleEdgeHandles().isEmpty());
        // One undo step, back to before it was pulled out.
        QVERIFY(f.manager.undo());
        QVERIFY(!b->isOpen());
        QVERIFY(!f.manager.canUndo());
        QCOMPARE(area->visibleEdgeHandles().size(), 1);

        // Pulled out and pushed back in: nothing happened.
        edge = area->visibleEdgeHandles().constFirst();
        QTest::mousePress(edge, Qt::LeftButton, {}, start);
        pull(minimum + 30);
        QVERIFY(area->groupOfPanel(p("b"))->isVisible());
        pull(20);
        QVERIFY(!area->groupOfPanel(p("b"))->isVisible());
        release(20);
        QVERIFY(!b->isOpen());
        QVERIFY(!f.manager.canUndo());
        QCOMPARE(describe(f.a), p("H(a, c|d)"));
        QCOMPARE(area->visibleEdgeHandles().size(), 1);
        // Nor with Escape.
        edge = area->visibleEdgeHandles().constFirst();
        QTest::mousePress(edge, Qt::LeftButton, {}, start);
        pull(minimum + 30);
        QVERIFY(b->isOpen());
        QTest::keyClick(edge, Qt::Key_Escape);
        QVERIFY(!b->isOpen());
        QVERIFY(!f.manager.canUndo());
        // Shown by the application, it is as wide as it was when it was closed.
        QVERIFY(f.manager.openPanel(p("b")));
        QVERIFY(qAbs(widthOf(area, "b") - width) <= 1);
        QVERIFY(area->visibleEdgeHandles().isEmpty());

        // Tabs that were closed together come out together, as they were.
        f.manager.panel(p("c"))->setCollapsible(true);
        f.manager.panel(p("d"))->setCollapsible(true);
        QVERIFY(f.manager.activatePanel(p("c")));
        QVERIFY(f.manager.closePanels({p("c"), p("d")}));
        QCOMPARE(describe(f.a), p("H(b, a)"));
        QCOMPARE(area->visibleEdgeHandles().size(), 1);
        edge = area->visibleEdgeHandles().constFirst();
        QCOMPARE(edge->side(), DockArea::Right);
        QCOMPARE(edge->barGeometry().right(), area->width() - 1);
        QCOMPARE(QSet<QString>(edge->panels().cbegin(), edge->panels().cend()),
                 (QSet<QString>{p("c"), p("d")}));
        const QPoint right = edge->mapToGlobal(edge->rect().center());
        QTest::mousePress(edge, Qt::LeftButton, {}, edge->rect().center());
        QTest::mouseMove(edge, edge->mapFromGlobal(right - QPoint(other, 0)));
        QTest::mouseRelease(edge, Qt::LeftButton, {}, edge->mapFromGlobal(right - QPoint(other, 0)));
        QCOMPARE(describe(f.a), p("H(b, a, c|d)"));
        QCOMPARE(area->groupOfPanel(p("c"))->currentPanel(), p("c"));
        QVERIFY(qAbs(widthOf(area, "c") - other) <= 2);

        // A tab that was closed on its own before the others stays closed.
        QVERIFY(f.manager.closePanel(p("d")));
        QVERIFY(area->visibleEdgeHandles().isEmpty()); // its group is still there
        QVERIFY(f.manager.closePanel(p("c")));
        QCOMPARE(area->visibleEdgeHandles().size(), 1);
        QCOMPARE(area->visibleEdgeHandles().constFirst()->panels(), QStringList{p("c")});
    }

    // The point where two boundaries meet can squeeze a group out as well.
    void cornerDragSqueezesToo()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Left, 0.3));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Bottom, -1, 0.3));
        QCOMPARE(describe(f.a), p("H(b, V(a, c))"));
        f.manager.panel(p("c"))->setCollapsible(true);
        DockAreaWidget *area = areaOf(f.a);
        QCOMPARE(area->visibleCorners().size(), 1);
        DockSplitCorner *corner = area->visibleCorners().constFirst();
        const int height = area->groupOfPanel(p("c"))->height();
        const int width = widthOf(area, "b");

        const QPoint start = corner->rect().center();
        const QPoint origin = corner->mapToGlobal(start);
        QTest::mousePress(corner, Qt::LeftButton, {}, start);
        QTest::mouseMove(corner, corner->mapFromGlobal(origin + QPoint(30, height + 20)));
        QVERIFY(!area->groupOfPanel(p("c"))->isVisible());
        QVERIFY(corner->isVisible()); // it still has the mouse
        QCOMPARE(widthOf(area, "b"), width + 30);
        QCOMPARE(area->groupOfPanel(p("a"))->height(), area->height());
        QTest::mouseMove(corner, corner->mapFromGlobal(origin + QPoint(30, 10)));
        QVERIFY(area->groupOfPanel(p("c"))->isVisible());
        QCOMPARE(area->groupOfPanel(p("c"))->height(), height - 10);
        QTest::mouseMove(corner, corner->mapFromGlobal(origin + QPoint(30, height + 20)));
        QTest::mouseRelease(corner, Qt::LeftButton, {},
                            corner->mapFromGlobal(origin + QPoint(30, height + 20)));
        QVERIFY(!f.manager.panel(p("c"))->isOpen());
        QCOMPARE(describe(f.a), p("H(b, a)"));
        QCOMPARE(widthOf(area, "b"), width + 30);
        QTRY_VERIFY(area->visibleCorners().isEmpty());
        QVERIFY(f.manager.openPanel(p("c")));
        QCOMPARE(describe(f.a), p("H(b, V(a, c))"));
        QVERIFY(qAbs(area->groupOfPanel(p("c"))->height() - height) <= 1);
    }

    // --- a dock area inside a panel --------------------------------------------
    // Where a boundary of the inner area ends on one of the area around it,
    // the two are taken hold of together, as if they were in one layout.
    void cornersReachIntoAnAreaInsideAPanel()
    {
        DockManager manager;
        QMainWindow window;
        DockWorkspace *outer = manager.createWorkspace(p("tools"));
        window.setCentralWidget(outer);
        auto *holder = new QWidget;
        auto *holderLayout = new QVBoxLayout(holder);
        holderLayout->setContentsMargins(0, 0, 0, 0);
        DockWorkspace *inner = manager.createWorkspace(p("documents"), holder);
        holderLayout->addWidget(inner);
        DockPanel *center = manager.registerPanel(p("center"), holder);
        center->setFeatures({});
        center->setHeaderVisible(false);
        for (const char *id : {"side", "below", "doc1", "doc2", "doc3"})
            manager.registerPanel(p(id), new QLabel(p(id)));
        QVERIFY(outer->addPanel(p("center")));
        QVERIFY(outer->addPanel(p("side"), DockArea::Left, 0.25));
        QVERIFY(manager.movePanel(p("below"), p("center"), DockArea::Bottom, -1, 0.3));
        QVERIFY(inner->addPanel(p("doc1")));
        QVERIFY(manager.movePanel(p("doc2"), p("doc1"), DockArea::Right));
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockAreaWidget *outerArea = areaOf(outer);
        DockAreaWidget *innerArea = areaOf(inner);
        QCOMPARE(describe(outer), p("H(side, V(center, below))"));
        QCOMPARE(describe(inner), p("H(doc1, doc2)"));

        // Two corners: the outer area's own (side | center / below), and the
        // one where the bar between the documents ends on the bar above "below".
        QTRY_COMPARE(outerArea->visibleCorners().size(), 2);
        QVERIFY(innerArea->visibleCorners().isEmpty());
        DockSplitHandle *between = innerArea->visibleHandles().constFirst();
        DockSplitHandle *above = handleAfter(outerArea, "center", Qt::Vertical);
        QVERIFY(above);
        const int x = innerArea->mapTo(outerArea, between->barGeometry().topLeft()).x();
        DockSplitCorner *corner = nullptr;
        for (DockSplitCorner *c : outerArea->visibleCorners()) {
            if (c->patchGeometry().x() == x)
                corner = c;
        }
        QVERIFY(corner);
        QCOMPARE(corner->patchGeometry().y(), above->barGeometry().y());
        // It is on top of everything there: this is what the pointer finds.
        QCOMPARE(window.childAt(outerArea->mapTo(&window, corner->patchGeometry().center())), corner);

        // Pointing at it lights up the bars of both areas.
        QEnterEvent enter(QPointF(2, 2), QPointF(2, 2), QPointF(2, 2));
        QCoreApplication::sendEvent(corner, &enter);
        QVERIFY(between->isHovered());
        QVERIFY(above->isHovered());
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(corner, &leave);
        QVERIFY(!between->isHovered());
        QVERIFY(!above->isHovered());

        // Dragging it moves both, each along its own axis, as one undo step.
        manager.clearUndoHistory();
        const int doc1 = innerArea->groupOfPanel(p("doc1"))->width();
        const int below = outerArea->groupOfPanel(p("below"))->height();
        const int side = outerArea->groupOfPanel(p("side"))->width();
        const QPoint start = corner->rect().center();
        const QPoint origin = corner->mapToGlobal(start);
        QTest::mousePress(corner, Qt::LeftButton, {}, start);
        QTest::mouseMove(corner, corner->mapFromGlobal(origin + QPoint(60, -40)));
        QCOMPARE(innerArea->groupOfPanel(p("doc1"))->width(), doc1 + 60);
        QCOMPARE(outerArea->groupOfPanel(p("below"))->height(), below + 40);
        // The corner follows the bars it stands for.
        QCOMPARE(corner->patchGeometry().x(), x + 60);
        QTest::mouseRelease(corner, Qt::LeftButton, {},
                            corner->mapFromGlobal(origin + QPoint(60, -40)));
        QCOMPARE(innerArea->groupOfPanel(p("doc1"))->width(), doc1 + 60);
        QCOMPARE(outerArea->groupOfPanel(p("side"))->width(), side);
        QVERIFY(manager.undo());
        QCOMPARE(innerArea->groupOfPanel(p("doc1"))->width(), doc1);
        QCOMPARE(outerArea->groupOfPanel(p("below"))->height(), below);
        QVERIFY(!manager.canUndo());

        // Escape puts both back.
        QTest::mousePress(corner, Qt::LeftButton, {}, corner->rect().center());
        QTest::mouseMove(corner, corner->rect().center() + QPoint(-50, 30));
        QVERIFY(innerArea->groupOfPanel(p("doc1"))->width() != doc1);
        QTest::keyClick(corner, Qt::Key_Escape);
        QCOMPARE(innerArea->groupOfPanel(p("doc1"))->width(), doc1);
        QCOMPARE(outerArea->groupOfPanel(p("below"))->height(), below);

        // A boundary between rows of documents ends on the bar beside "side".
        QVERIFY(manager.movePanel(p("doc3"), p("doc1"), DockArea::Bottom));
        QCOMPARE(describe(inner), p("H(V(doc1, doc3), doc2)"));
        QTRY_COMPARE(outerArea->visibleCorners().size(), 3);
        // The corners come and go with the inner layout.
        QVERIFY(manager.closePanel(p("doc2")));
        QVERIFY(manager.closePanel(p("doc3")));
        QTRY_COMPARE(outerArea->visibleCorners().size(), 1);
        // And there are none of them with corner resizing turned off.
        QVERIFY(manager.openPanel(p("doc2")));
        QTRY_COMPARE(outerArea->visibleCorners().size(), 2);
        manager.setCornerResizeEnabled(false);
        QTRY_VERIFY(outerArea->visibleCorners().isEmpty());
    }

    // --- corners -----------------------------------------------------------
    // Where the column boundary meets the row boundary both can be dragged at
    // once. Events go through the window, as in thinHandlesCanBeGrabbed.
    void cornerMovesBothBoundaries_data()
    {
        QTest::addColumn<int>("width"); // of the handles; -1: the style's
        QTest::newRow("style") << -1;
        QTest::newRow("1px") << 1;
    }

    void cornerMovesBothBoundaries()
    {
        QFETCH(int, width);
        TwoWindows f;
        DockTheme theme;
        theme.splitHandleWidth = width;
        f.manager.setTheme(theme);
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        QCOMPARE(area->visibleCorners().size(), 1);
        DockSplitCorner *corner = area->visibleCorners().constFirst();
        QVERIFY(corner->isVisible());
        QCOMPARE(corner->cursor().shape(), Qt::SizeAllCursor);
        QVERIFY(corner->width() >= DockSplitCorner::MinimumGrabExtent);
        QVERIFY(corner->height() >= DockSplitCorner::MinimumGrabExtent);
        DockSplitHandle *top = handleAfter(area, "a", Qt::Horizontal);
        DockSplitHandle *bottom = handleAfter(area, "c", Qt::Horizontal);
        DockSplitHandle *rows = handleAfter(area, "a", Qt::Vertical);
        QCOMPARE(corner->patchGeometry().x(), top->barGeometry().x());
        QCOMPARE(corner->patchGeometry().y(), rows->barGeometry().y());

        const QSize a = area->groupOfPanel(p("a"))->size();
        const QSize d = area->groupOfPanel(p("d"))->size();
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);

        QWindow *window = f.windowA.windowHandle();
        const QPoint press = area->mapTo(&f.windowA, corner->patchGeometry().center());
        QCOMPARE(f.windowA.childAt(press), corner);
        // A little off the patch it is still the corner, further along a bar
        // it is that bar's handle again.
        QCOMPARE(f.windowA.childAt(press + QPoint(3, -3)), corner);
        QCOMPARE(f.windowA.childAt(press + QPoint(0, -40)), top);
        QCOMPARE(f.windowA.childAt(press + QPoint(0, 40)), bottom);
        QCOMPARE(f.windowA.childAt(press + QPoint(40, 0)), rows);

        if (windowPositionsWork()) {
            // Pointing at it lights up everything it would move.
            QCursor::setPos(f.windowA.mapToGlobal(QPoint(8, 8)));
            QTest::mouseMove(window, QPoint(8, 8));
            QCursor::setPos(f.windowA.mapToGlobal(press));
            QTest::mouseMove(window, press);
            QTRY_VERIFY(top->isHovered());
            QVERIFY(bottom->isHovered());
            QVERIFY(rows->isHovered());
        }

        QTest::mousePress(window, Qt::LeftButton, {}, press);
        QVERIFY(corner->isPressed());
        QTest::mouseMove(window, press + QPoint(30, 10));
        QTest::mouseMove(window, press + QPoint(70, -40));
        QCOMPARE(area->groupOfPanel(p("a"))->size(), a + QSize(70, -40));
        QTest::mouseRelease(window, Qt::LeftButton, {}, press + QPoint(70, -40));
        QVERIFY(!corner->isPressed());

        QCOMPARE(area->groupOfPanel(p("a"))->size(), a + QSize(70, -40));
        QCOMPARE(area->groupOfPanel(p("b"))->size(), QSize(d.width() - 70, a.height() - 40));
        QCOMPARE(area->groupOfPanel(p("c"))->size(), QSize(a.width() + 70, d.height() + 40));
        QCOMPARE(area->groupOfPanel(p("d"))->size(), d + QSize(-70, 40));
        QVERIFY(areaOf(f.a)->tree().validate());
        grab(&f.windowA, p("splitter-corner"));
        // The corner went along with the bars.
        QCOMPARE(area->visibleCorners().size(), 1);
        QCOMPARE(corner->patchGeometry().topLeft(),
                 QPoint(top->barGeometry().x(), rows->barGeometry().y()));

        // One step, announced once.
        QCOMPARE(changed.size(), 1);
        QVERIFY(f.manager.undo());
        QCOMPARE(area->groupOfPanel(p("a"))->size(), a);
        QCOMPARE(area->groupOfPanel(p("d"))->size(), d);
        QVERIFY(!f.manager.canUndo());
    }

    void cornerDragHonoursLimitsOnEachAxis()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        f.widgets[p("a")]->setMinimumSize(200, 150);
        QCoreApplication::processEvents();
        const QSize b = area->groupOfPanel(p("b"))->size();
        const int total = widthOf(area, "a") + widthOf(area, "b");

        DockSplitCorner *corner = area->visibleCorners().constFirst();
        const QPoint start = corner->rect().center();
        QTest::mousePress(corner, Qt::LeftButton, {}, start);
        const QPoint origin = corner->mapToGlobal(start);
        // Far up and to the left: a stops at its minimum in both directions.
        QTest::mouseMove(corner, corner->mapFromGlobal(origin + QPoint(-3000, -3000)));
        QCOMPARE(f.widgets[p("a")]->size(), QSize(200, 150));
        // Back to the right while still far up: only the width follows.
        QTest::mouseMove(corner, corner->mapFromGlobal(origin + QPoint(0, -3000)));
        QCOMPARE(f.widgets[p("a")]->height(), 150);
        QCOMPARE(widthOf(area, "b"), b.width());
        QTest::mouseRelease(corner, Qt::LeftButton, {}, corner->mapFromGlobal(origin + QPoint(0, -3000)));
        QCOMPARE(widthOf(area, "a") + widthOf(area, "b"), total);
        QVERIFY(areaOf(f.a)->tree().validate());
    }

    void escapeCancelsACornerDrag()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        const QSize a = area->groupOfPanel(p("a"))->size();
        DockSplitCorner *corner = area->visibleCorners().constFirst();
        const QPoint start = corner->rect().center();
        QTest::mousePress(corner, Qt::LeftButton, {}, start);
        const QPoint origin = corner->mapToGlobal(start);
        QTest::mouseMove(corner, corner->mapFromGlobal(origin + QPoint(60, 45)));
        QCOMPARE(area->groupOfPanel(p("a"))->size(), a + QSize(60, 45));
        QTest::keyClick(corner, Qt::Key_Escape);
        QCOMPARE(area->groupOfPanel(p("a"))->size(), a);
        QTest::mouseMove(corner, corner->mapFromGlobal(origin + QPoint(90, 90)));
        QTest::mouseRelease(corner, Qt::LeftButton);
        QCOMPARE(area->groupOfPanel(p("a"))->size(), a);
        QVERIFY(!f.manager.canUndo());
    }

    // Three rows split at the same place: V(H(a, b), H(c, d), H(e, f)). Each of
    // the two corners holds the bars of the rows it lies between.
    void cornerTakesTheLinkedRunAlongUnlessAltIsHeld()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Bottom));
        QVERIFY(f.manager.movePanel(p("e"), p("c"), DockArea::Bottom));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("d"), p("c"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("f"), p("e"), DockArea::Right));
        QCOMPARE(describe(f.a), p("V(H(a, b), H(c, d), H(e, f))"));
        DockAreaWidget *area = areaOf(f.a);
        QCOMPARE(area->visibleCorners().size(), 2);
        const int a = widthOf(area, "a");
        const int height = area->groupOfPanel(p("a"))->height();

        // The upper corner is the one level with the bar below a.
        const auto upperCorner = [&]() -> DockSplitCorner * {
            const int y = handleAfter(area, "a", Qt::Vertical)->barGeometry().y();
            for (DockSplitCorner *corner : area->visibleCorners()) {
                if (corner->patchGeometry().y() == y)
                    return corner;
            }
            return nullptr;
        };
        const auto dragCorner = [&](const QPoint &offset, Qt::KeyboardModifiers modifiers) {
            DockSplitCorner *corner = upperCorner();
            QVERIFY(corner);
            const QPoint start = corner->rect().center();
            QTest::mousePress(corner, Qt::LeftButton, modifiers, start);
            const QPoint origin = corner->mapToGlobal(start);
            QTest::mouseMove(corner, corner->mapFromGlobal(origin + offset));
            QTest::mouseRelease(corner, Qt::LeftButton, modifiers,
                                corner->mapFromGlobal(origin + offset));
        };

        dragCorner(QPoint(50, 20), {});
        QCOMPARE(widthOf(area, "a"), a + 50);
        QCOMPARE(widthOf(area, "c"), a + 50);
        QCOMPARE(widthOf(area, "e"), a + 50); // in line with the other two
        QCOMPARE(area->groupOfPanel(p("a"))->height(), height + 20);

        dragCorner(QPoint(-30, 0), Qt::AltModifier);
        QCOMPARE(widthOf(area, "a"), a + 20);
        QCOMPARE(widthOf(area, "c"), a + 20);
        QCOMPARE(widthOf(area, "e"), a + 50); // does not meet that corner
        QVERIFY(areaOf(f.a)->tree().validate());

        // With linking switched off, a plain drag does the same.
        f.manager.setLinkedSplittersEnabled(false);
        dragCorner(QPoint(-10, 0), {});
        QCOMPARE(widthOf(area, "a"), a + 10);
        QCOMPARE(widthOf(area, "e"), a + 50);
    }

    void cornersComeAndGoWithTheLayout()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        DockAreaWidget *area = areaOf(f.a);
        QVERIFY(area->visibleCorners().isEmpty());
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(area->visibleCorners().isEmpty()); // one boundary, nothing to meet
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        QCOMPARE(area->visibleCorners().size(), 1);
        DockSplitCorner *corner = area->visibleCorners().constFirst();
        QVERIFY(corner->isVisible());

        // Maximized, there are no boundaries at all.
        QVERIFY(f.manager.maximizePanel(p("a")));
        QVERIFY(area->visibleCorners().isEmpty());
        QVERIFY(!corner->isVisible());
        QVERIFY(f.manager.restoreMaximizedPanel());
        QCOMPARE(area->visibleCorners().size(), 1);

        QVERIFY(f.manager.closePanel(p("c")));
        QVERIFY(area->visibleCorners().isEmpty());
        QVERIFY(!corner->isVisible());
    }

    void oneDragIsOneUndoStep()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        const int a = widthOf(area, "a");
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);

        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(100, 0));
        QCOMPARE(changed.size(), 1); // announced once, at the end
        QVERIFY(f.manager.canUndo());
        QVERIFY(f.manager.undo());
        QCOMPARE(widthOf(area, "a"), a);
        QCOMPARE(widthOf(area, "c"), a);
        QVERIFY(!f.manager.canUndo());
        QVERIFY(f.manager.redo());
        QCOMPARE(widthOf(area, "a"), a + 100);
    }

    // Content that cannot get as narrow as its neighbours holds its own row's
    // bar back. The bar of the other row comes along: a line stays a line.
    void linesSurviveContentThatCannotShrink()
    {
        TwoWindows f;
        f.widgets.value(p("a"))->setMinimumWidth(200);
        f.widgets.value(p("d"))->setMinimumWidth(400);
        // A workspace that is resized as a widget: a window is as wide as
        // the compositor lets it be, and not on every platform for good.
        QWidget host;
        host.resize(1200, 440);
        DockWorkspace *workspace = f.manager.createWorkspace(p("C"));
        workspace->setParent(&host);
        workspace->setGeometry(0, 0, 900, 400);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QVERIFY(workspace->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Bottom));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("d"), p("c"), DockArea::Right));
        f.manager.clearUndoHistory();
        DockAreaWidget *area = areaOf(workspace);

        const auto minimumOf = [&](const char *panel) {
            return area->groupOfPanel(p(panel))->sizeLimits().min.width();
        };
        const auto inLine = [&] {
            const DockSplitHandle *top = handleAfter(area, "a", Qt::Horizontal);
            return top
                && top->barGeometry().x() == area->groupOfPanel(p("c"))->geometry().right() + 1;
        };
        // The narrowest the area can be with the two bars in one line.
        const int narrowest = qMax(minimumOf("a"), minimumOf("c")) + area->handleWidth()
            + qMax(minimumOf("b"), minimumOf("d"));
        const int around = workspace->width() - area->width();
        const auto setWidth = [&](int width) {
            workspace->resize(width + around, 400);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QCOMPARE(area->width(), width);
        };
        QTRY_VERIFY(inLine());

        for (int width : {narrowest + 20, narrowest, narrowest + 300}) {
            setWidth(width);
            QVERIFY(inLine());
            QVERIFY(widthOf(area, "a") >= minimumOf("a"));
            QVERIFY(widthOf(area, "d") >= minimumOf("d"));
            QCOMPARE(widthOf(area, "a") + area->handleWidth() + widthOf(area, "b"), width);
        }

        // It is the wide content below that has pushed the line to the left.
        setWidth(narrowest + 20);
        QVERIFY(inLine());
        QVERIFY(widthOf(area, "a") < widthOf(area, "b"));
        // Bars that do not move together are not kept together either.
        f.manager.setLinkedSplittersEnabled(false);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QVERIFY(!inLine());
        QCOMPARE(widthOf(area, "a"), widthOf(area, "b"));
        f.manager.setLinkedSplittersEnabled(true);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QVERIFY(inLine());

        // Wide again, every bar is back where its weights have it.
        setWidth(narrowest + 400);
        QVERIFY(inLine());
        QVERIFY(qAbs(widthOf(area, "a") - widthOf(area, "b")) <= 1);

        // The line is dragged as one, from where it is held: not to the
        // right, where the wide content is at its minimum, but to the left.
        setWidth(narrowest + 20);
        const int held = widthOf(area, "a");
        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(60, 0));
        QCOMPARE(widthOf(area, "a"), held);
        QVERIFY(inLine());
        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(-10, 0));
        QCOMPARE(widthOf(area, "a"), held - 10);
        QCOMPARE(widthOf(area, "c"), held - 10);
        QVERIFY(f.manager.undo());
        QCOMPARE(widthOf(area, "a"), held);
        QVERIFY(inLine());
    }

    void ratiosSurviveWindowResizes()
    {
        TwoWindows f;
        f.show();
        makeGrid(f);
        DockAreaWidget *area = areaOf(f.a);
        drag(handleAfter(area, "a", Qt::Horizontal), QPoint(-150, 0));
        const double ratio = double(widthOf(area, "a")) / area->width();

        for (const QSize &size : {QSize(1400, 900), QSize(500, 300), QSize(60, 40), QSize(900, 600)}) {
            f.windowA.resize(size);
            QCoreApplication::processEvents();
            // No overlap and nothing outside, however extreme the size.
            QRegion covered;
            for (const DockTabGroup *group : area->groups()) {
                QVERIFY(!covered.intersects(group->geometry()));
                covered += group->geometry();
            }
            QVERIFY(QRegion(area->contentsRect()).subtracted(covered).boundingRect().width()
                    <= area->width());
            // The two bars stay in line.
            QCOMPARE(handleAfter(area, "a", Qt::Horizontal)->barGeometry().x(),
                     area->groupOfPanel(p("c"))->geometry().right() + 1);
        }
        QVERIFY(qAbs(double(widthOf(area, "a")) / area->width() - ratio) < 0.01);
    }
};

QTEST_MAIN(tst_Splitter)
#include "tst_splitter.moc"
