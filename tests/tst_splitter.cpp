// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include "widgets/DockSplitHandle.h"

#include <QtTest/QSignalSpy>

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

        const QImage image = f.windowA.grab().toImage();
        const QPoint bar = area->mapTo(&f.windowA, handle->barGeometry().center());
        const QRgb red = QColor(0xff, 0, 0).rgb();
        QCOMPARE(image.pixelColor(bar).rgb(), red);
        for (int offset : {-3, -2, -1, 1, 2, 3})
            QVERIFY2(image.pixelColor(bar + QPoint(offset, 0)).rgb() != red,
                     qPrintable(QStringLiteral("offset %1").arg(offset)));
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
        QVERIFY(f.a->layoutTree().validate());
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
        QVERIFY(f.a->layoutTree().validate());
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
        QVERIFY(f.a->layoutTree().validate());
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
        QVERIFY(f.a->layoutTree().validate());
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
        QVERIFY(f.a->layoutTree().validate());

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

        QVERIFY(f.manager.hidePanel(p("c")));
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
