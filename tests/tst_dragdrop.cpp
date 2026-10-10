// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include "core/DockDragController.h"
#include "core/DropZones.h"
#include "widgets/DockDropOverlay.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockTabBar.h"

#include <QtCore/QMimeData>
#include <QtCore/QRandomGenerator>
#include <QtGui/QDragEnterEvent>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QBoxLayout>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

/// Drives a dock drag the way the platform would, minus the blocking
/// QDrag::exec(): starts a session and delivers drag events to dock areas.
class Drag
{
public:
    explicit Drag(DockManager &manager)
        : m_controller(priv(manager)->drag)
    {
    }
    ~Drag() { m_controller->cancel(); }

    bool begin(const char *panel, bool wholeGroup = false)
    {
        if (!m_controller->begin(p(panel), wholeGroup))
            return false;
        m_mime.reset(m_controller->createMimeData());
        return true;
    }

    const QMimeData *mime() const { return m_mime.get(); }
    const DragSession &session() const { return *m_controller->session(); }

    // Qt routes move and drop events to whoever accepted the last enter, and
    // forgets that target after a drop or leave, exactly as with a real drag.
    // move() and drop() therefore enter the area first when necessary.
    bool enter(DockAreaWidget *area, const QPoint &pos, const QMimeData *mime = nullptr)
    {
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QDragEnterEvent event(pos, Qt::MoveAction, mime ? mime : m_mime.get(), Qt::LeftButton,
                              Qt::NoModifier);
        QT_WARNING_POP
        QCoreApplication::sendEvent(area, &event);
        m_entered = event.isAccepted() ? area : nullptr;
        return event.isAccepted();
    }

    /// True if a drop at `pos` would be taken.
    bool move(DockAreaWidget *area, const QPoint &pos, const QMimeData *mime = nullptr)
    {
        if (m_entered != area && !enter(area, pos, mime))
            return false;
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QDragMoveEvent event(pos, Qt::MoveAction, mime ? mime : m_mime.get(), Qt::LeftButton,
                             Qt::NoModifier);
        QT_WARNING_POP
        QCoreApplication::sendEvent(area, &event);
        return event.isAccepted();
    }

    void leave(DockAreaWidget *area)
    {
        QDragLeaveEvent event;
        QCoreApplication::sendEvent(area, &event);
        m_entered = nullptr;
    }

    bool drop(DockAreaWidget *area, const QPoint &pos, const QMimeData *mime = nullptr)
    {
        if (m_entered != area && !enter(area, pos, mime))
            return false;
        QDropEvent event(pos, Qt::MoveAction, mime ? mime : m_mime.get(), Qt::LeftButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(area, &event);
        m_entered = nullptr;
        return event.isAccepted();
    }

private:
    DockDragController *m_controller;
    std::unique_ptr<QMimeData> m_mime;
    DockAreaWidget *m_entered = nullptr;
};

/// A point inside the given guide area of the tab group holding `panel`.
QPoint zonePoint(DockAreaWidget *area, const char *panel, DockArea zone)
{
    const QRect rect = area->groupOfPanel(p(panel))->geometry();
    const int band = area->overlay()->effectiveStyle().outerBandWidth;
    const QRect inner = rect.intersected(area->contentsRect().adjusted(band, band, -band, -band));
    switch (zone) {
    case DockArea::Left:
        return QPoint(inner.left() + 6, inner.center().y());
    case DockArea::Right:
        return QPoint(inner.right() - 6, inner.center().y());
    case DockArea::Top:
        // Below the title row, which is a target of its own.
        return QPoint(inner.center().x(), inner.top() + 6 + (rect.top() == inner.top() ? 40 : 12));
    case DockArea::Bottom:
        return QPoint(inner.center().x(), inner.bottom() - 6);
    default:
        return inner.center();
    }
}

const DockOverlayScene::Zone *hoveredZone(DockAreaWidget *area)
{
    for (const auto &zone : area->overlay()->scene().zones) {
        if (zone.hovered)
            return &zone;
    }
    return nullptr;
}

DockFeatures without(DockFeature feature)
{
    return AllDockFeatures & ~DockFeatures(feature);
}

} // namespace

class tst_DragDrop : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void hoveringShowsTheGuideButChangesNothing()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Bottom));
        DockAreaWidget *area = areaOf(f.a);
        const QString before = describe(f.a);
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);

        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        QVERIFY(!area->overlay()->isVisible());
        QVERIFY(drag.enter(area, zonePoint(area, "b", DockArea::Center)));
        QVERIFY(drag.move(area, zonePoint(area, "b", DockArea::Center)));

        // Five large areas over the hovered group, plus the four outer bands.
        DockDropOverlay *overlay = area->overlay();
        QVERIFY(overlay->isVisible());
        const QRect groupRect = area->groupOfPanel(p("b"))->geometry();
        int inner = 0;
        int outer = 0;
        QRegion guide;
        for (const auto &zone : overlay->scene().zones) {
            (zone.outer ? outer : inner) += 1;
            if (!zone.outer)
                guide += zone.shape.toPolygon();
        }
        QCOMPARE(inner, 5);
        QCOMPARE(outer, 4);
        // "Large" means the guide spans the target (short of the outer bands
        // it borders on and of its own margin), not a cluster in its middle.
        const int band = overlay->effectiveStyle().outerBandWidth;
        const int margin = overlay->effectiveStyle().zoneMargin;
        QVERIFY(guide.boundingRect().width() >= groupRect.width() - band - 2 * margin - 2);
        QVERIFY(guide.boundingRect().height() >= groupRect.height() - 2 * band - 2 * margin - 2);
        // ...and keeps that margin from the target's border.
        QVERIFY(groupRect.adjusted(margin, margin, -margin, -margin).contains(guide.boundingRect()));
        QVERIFY(hoveredZone(area));
        QCOMPARE(hoveredZone(area)->area, DockArea::Center);
        QCOMPARE(overlay->scene().preview, groupRect);
        grab(&f.windowA, p("overlay-center"));

        // The preview shows what the drop would occupy.
        QVERIFY(drag.move(area, zonePoint(area, "b", DockArea::Left)));
        QCOMPARE(hoveredZone(area)->area, DockArea::Left);
        QVERIFY(!hoveredZone(area)->outer);
        QCOMPARE(overlay->scene().preview, dropPreviewRect(groupRect, DockArea::Left, 0.5));
        QVERIFY(qAbs(overlay->scene().preview.width() * 2 - groupRect.width()) <= 1);
        QCOMPARE(overlay->scene().preview.topLeft(), groupRect.topLeft());
        grab(&f.windowA, p("overlay-left"));

        // The outer band docks onto the workspace as a whole.
        QVERIFY(drag.move(area, QPoint(area->width() / 2, area->height() - 5)));
        QVERIFY(hoveredZone(area)->outer);
        QCOMPARE(hoveredZone(area)->area, DockArea::Bottom);
        QCOMPARE(overlay->scene().preview.width(), area->width());
        grab(&f.windowA, p("overlay-outer"));

        // Nothing has happened to the layout so far.
        QCOMPARE(describe(f.a), before);
        QCOMPARE(changed.size(), 0);

        drag.leave(area);
        QVERIFY(!overlay->isVisible());
        QCOMPARE(describe(f.a), before);
    }

    void dropOnEachArea_data()
    {
        QTest::addColumn<DockArea>("zone");
        QTest::addColumn<QString>("expected");
        QTest::newRow("left") << DockArea::Left << p("V(a, H(c, b))");
        QTest::newRow("right") << DockArea::Right << p("V(a, H(b, c))");
        QTest::newRow("top") << DockArea::Top << p("V(a, c, b)");
        QTest::newRow("bottom") << DockArea::Bottom << p("V(a, b, c)");
        QTest::newRow("center") << DockArea::Center << p("V(a, b|c)");
    }

    void dropOnEachArea()
    {
        QFETCH(DockArea, zone);
        QFETCH(QString, expected);
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Bottom, 0.5));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        DockAreaWidget *area = areaOf(f.a);
        QWidget *widget = f.widgets[p("c")];

        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        const QPoint pos = zonePoint(area, "b", zone);
        QVERIFY(drag.enter(area, pos));
        QVERIFY(drag.move(area, pos));
        QVERIFY(drag.drop(area, pos));

        QCOMPARE(describe(f.a), expected);
        QVERIFY(f.a->layoutTree().validate());
        QVERIFY(!area->overlay()->isVisible());
        QCOMPARE(f.manager.panel(p("c"))->widget(), widget);
        QVERIFY(widget->isVisible());
        QCOMPARE(f.manager.activePanel(), f.manager.panel(p("c")));
        QVERIFY(!priv(f.manager)->drag->isActive());
    }

    void dropOnOuterEdgeDocksAlongTheWholeWorkspace()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        DockAreaWidget *area = areaOf(f.a);

        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        const QPoint pos(area->width() / 2, area->height() - 4);
        QVERIFY(drag.enter(area, pos));
        QVERIFY(drag.drop(area, pos));
        QCOMPARE(describe(f.a), p("V(H(a, b), c)"));
        QVERIFY(qAbs(f.a->layoutTree().root()->children[1].weight - 0.25) < 1e-9);
    }

    void dropOnTabBarInsertsAndReorders()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        QVERIFY(f.a->addPanel(p("d"), DockArea::Right));
        DockAreaWidget *area = areaOf(f.a);
        DockTabBar *bar = area->groupOfPanel(p("a"))->tabBar();
        const auto tabEdge = [&](int index, bool leftHalf) {
            const QRect r = bar->tabRect(index);
            const int x = leftHalf ? r.left() + r.width() / 4 : r.right() - r.width() / 4;
            return bar->mapTo(area, QPoint(x, r.center().y()));
        };

        // From another group, between two tabs.
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("d"));
            const QPoint pos = tabEdge(1, true);
            QVERIFY(drag.enter(area, pos));
            QVERIFY(drag.move(area, pos));
            QVERIFY(area->overlay()->scene().tabIndicator.isValid());
            QVERIFY(drag.drop(area, pos));
            QCOMPARE(describe(f.a), p("a|d|b|c"));
        }
        // Reordering within the group: the last tab to the front.
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            const QPoint pos = tabEdge(0, true);
            QVERIFY(drag.move(area, pos));
            QVERIFY(area->overlay()->scene().tabIndicator.isValid());
            QVERIFY(drag.drop(area, pos));
            QCOMPARE(describe(f.a), p("c|a|d|b"));
        }
        // And the first one behind the third.
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            QVERIFY(drag.drop(area, tabEdge(2, false)));
            QCOMPARE(describe(f.a), p("a|d|c|b"));
            QCOMPARE(f.a->layoutTree().root()->active, p("c"));
        }
    }

    void dropIntoAnotherMainWindow()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.b->addPanel(p("c")));
        QWidget *widget = f.widgets[p("a")];
        DockPanel *panel = f.manager.panel(p("a"));
        QSignalSpy reparented(panel, &DockPanel::reparented);

        Drag drag(f.manager);
        QVERIFY(drag.begin("a"));
        DockAreaWidget *source = areaOf(f.a);
        DockAreaWidget *target = areaOf(f.b);
        // Over the source window first, then across.
        QVERIFY(drag.enter(source, zonePoint(source, "b", DockArea::Center)));
        QVERIFY(source->overlay()->isVisible());
        drag.leave(source);
        QVERIFY(!source->overlay()->isVisible());
        const QPoint pos = zonePoint(target, "c", DockArea::Right);
        QVERIFY(drag.enter(target, pos));
        QVERIFY(target->overlay()->isVisible());
        QVERIFY(drag.drop(target, pos));

        QCOMPARE(describe(f.a), p("b"));
        QCOMPARE(describe(f.b), p("H(c, a)"));
        QCOMPARE(panel->widget(), widget);     // the very same widget...
        QCOMPARE(widget->window(), &f.windowB); // ...now in the other window
        QVERIFY(widget->isVisible());
        QCOMPARE(reparented.size(), 1);
        QCOMPARE(panel->workspace(), f.b);
    }

    void draggingAWholeTabGroup()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Right));
        QVERIFY(f.b->addPanel(p("d")));

        Drag drag(f.manager);
        QVERIFY(drag.begin("a", true));
        QCOMPARE(drag.session().panels, QStringList({p("a"), p("b")}));
        DockAreaWidget *target = areaOf(f.b);
        const QPoint pos = zonePoint(target, "d", DockArea::Left);
        QVERIFY(drag.enter(target, pos));
        QVERIFY(drag.drop(target, pos));
        QCOMPARE(describe(f.a), p("c"));
        QCOMPARE(describe(f.b), p("H(a|b, d)"));
        QCOMPARE(f.widgets[p("a")]->window(), &f.windowB);
        QCOMPARE(f.widgets[p("b")]->window(), &f.windowB);

        // A group cannot be dropped onto itself.
        Drag again(f.manager);
        QVERIFY(again.begin("a", true));
        QVERIFY(!again.move(target, zonePoint(target, "a", DockArea::Right)));
        QVERIFY(again.move(target, zonePoint(target, "a", DockArea::Center))); // "stay"
        QVERIFY(again.move(target, zonePoint(target, "d", DockArea::Center)));
        QVERIFY(again.drop(target, zonePoint(target, "d", DockArea::Center)));
        QCOMPARE(describe(f.b), p("d|a|b"));
    }

    void emptyWorkspaceTakesTheDropAsItsFirstPanel()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        DockAreaWidget *target = areaOf(f.b);

        Drag drag(f.manager);
        QVERIFY(drag.begin("a"));
        const QPoint pos = target->rect().center();
        QVERIFY(drag.enter(target, pos));
        QCOMPARE(target->overlay()->scene().zones.size(), 1);
        QCOMPARE(target->overlay()->scene().preview, target->contentsRect());
        QVERIFY(drag.drop(target, QPoint(5, 5))); // anywhere in it
        QCOMPARE(describe(f.b), p("a"));
    }

    void cancelledDragLeavesEverythingAsItWas()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        const LayoutTree before = f.a->layoutTree();
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);
        DockAreaWidget *area = areaOf(f.a);
        DockDragController *controller = priv(f.manager)->drag;

        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            QVERIFY(controller->isActive());
            QVERIFY(drag.enter(area, zonePoint(area, "b", DockArea::Bottom)));
            QVERIFY(area->overlay()->isVisible());
            controller->cancel(); // what Escape amounts to
            QVERIFY(!controller->isActive());
            QVERIFY(!area->overlay()->isVisible());
            // A drop arriving after the cancel is void.
            QVERIFY(!drag.drop(area, zonePoint(area, "b", DockArea::Bottom)));
        }
        QCOMPARE(describe(f.a), describe(before));
        QCOMPARE(f.a->layoutTree().root()->id, before.root()->id);
        QCOMPARE(changed.size(), 0);
        QVERIFY(f.widgets[p("a")]->isVisible());
        QVERIFY(!f.manager.canUndo() || f.manager.undo()); // only the setup is on the stack
    }

    void onlyTheManagersOwnSessionIsHonoured()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        DockAreaWidget *area = areaOf(f.a);
        const QString before = describe(f.a);
        const QPoint pos = zonePoint(area, "b", DockArea::Bottom);

        // Mime data naming a panel, but no session: ignored.
        QMimeData forged;
        forged.setData(DockDragController::mimeType(), "a");
        Drag idle(f.manager);
        QVERIFY(!idle.enter(area, pos, &forged));
        QVERIFY(!idle.drop(area, pos, &forged));
        QVERIFY(!area->overlay()->isVisible());

        // With a session running, a foreign token still is.
        Drag drag(f.manager);
        QVERIFY(drag.begin("a"));
        QVERIFY(!drag.enter(area, pos, &forged));
        QVERIFY(!drag.drop(area, pos, &forged));
        QMimeData text;
        text.setText(p("a"));
        QVERIFY(!drag.drop(area, pos, &text));

        // So is the token of a session that is over.
        const std::unique_ptr<QMimeData> stale(priv(f.manager)->drag->createMimeData());
        priv(f.manager)->drag->cancel();
        QVERIFY(!drag.drop(area, pos, stale.get()));
        QVERIFY(drag.begin("a"));
        QVERIFY(!drag.drop(area, pos, stale.get()));
        QCOMPARE(describe(f.a), before);

        // Only one drag at a time.
        QVERIFY(!priv(f.manager)->drag->begin(p("b"), false));
        QVERIFY(drag.drop(area, pos));
        QCOMPARE(describe(f.a), p("V(b, a)"));
    }

    void policiesLimitWhereAPanelCanBeDropped()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("d")));
        DockAreaWidget *area = areaOf(f.a);
        DockAreaWidget *other = areaOf(f.b);
        const QString before = describe(f.a);
        DockPanel *c = f.manager.panel(p("c"));

        // Left and right only.
        DockPolicy policy;
        policy.allowedAreas = DockArea::Left | DockArea::Right;
        QVERIFY(f.manager.setDockPolicy(p("c"), policy));
        QCOMPARE(f.manager.dockPolicy(p("c")), policy);
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            QVERIFY(drag.enter(area, zonePoint(area, "b", DockArea::Left)));
            QVERIFY(drag.move(area, zonePoint(area, "b", DockArea::Left)));
            // The guide shows only what is allowed.
            for (const auto &zone : area->overlay()->scene().zones)
                QVERIFY(zone.area == DockArea::Left || zone.area == DockArea::Right);
            QVERIFY(!drag.move(area, zonePoint(area, "b", DockArea::Top)));
            QVERIFY(!hoveredZone(area));
            QVERIFY(!area->overlay()->scene().preview.isValid());
            QVERIFY(!drag.move(area, zonePoint(area, "b", DockArea::Center)));
            QVERIFY(!drag.drop(area, zonePoint(area, "b", DockArea::Center)));
            QCOMPARE(describe(f.a), before);
            QVERIFY(!drag.move(area, QPoint(area->width() - 60, 3))); // outer top, on b's tab bar
            QVERIFY(drag.move(area, QPoint(3, area->height() / 2))); // outer left
            // Reordering inside its own group is not "docking" and stays possible.
            QVERIFY(drag.move(area, QPoint(60, 14)));
        }

        // Not tabbable: no joining, in either direction.
        policy = DockPolicy{};
        policy.features = without(DockFeature::Tabbable);
        c->setPolicy(policy);
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            QVERIFY(!drag.move(area, zonePoint(area, "b", DockArea::Center)));
            QVERIFY(drag.move(area, zonePoint(area, "b", DockArea::Bottom)));
            QVERIFY(drag.move(other, other->rect().center()) == false); // d is tabbable, c is not
        }
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("b"));
            // c shares a group with a; that group now refuses new tabs.
            QVERIFY(!drag.move(area, zonePoint(area, "a", DockArea::Center)));
            QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Bottom)));
        }

        // Not movable at all.
        c->setFeatures(without(DockFeature::Movable));
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            for (DockArea zone : {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                                  DockArea::Center}) {
                QVERIFY(!drag.move(area, zonePoint(area, "b", zone)));
            }
            QVERIFY(!drag.drop(area, zonePoint(area, "b", DockArea::Left)));
        }

        // Confined to one workspace.
        policy = DockPolicy{};
        policy.allowedWorkspaces = QStringList{p("A")};
        c->setPolicy(policy);
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            QVERIFY(!drag.move(other, zonePoint(other, "d", DockArea::Left)));
            QVERIFY(other->overlay()->scene().zones.isEmpty());
            QVERIFY(drag.move(area, zonePoint(area, "b", DockArea::Left)));
        }
        QCOMPARE(describe(f.a), before);

        // Policies restrict the user, not the application.
        c->setFeatures(without(DockFeature::Movable));
        QVERIFY(f.manager.movePanel(p("c"), f.b, DockArea::Left));
        QCOMPARE(describe(f.b), p("H(c, d)"));
    }

    // A workspace as the content of a panel of another one: the place for
    // documents in the middle of the tool panels. Each kind of panel is
    // allowed in its own workspace, and a drag goes to the area that takes it.
    void workspaceInsideAPanelTakesOnlyWhatIsAllowedInIt()
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
        for (const char *id : {"tool", "doc1", "doc2"}) {
            DockPanel *panel = manager.registerPanel(p(id), new QLabel(p(id)));
            DockPolicy policy;
            policy.allowedWorkspaces = {p(id).startsWith(QLatin1String("doc")) ? p("documents")
                                                                              : p("tools")};
            panel->setPolicy(policy);
        }
        QVERIFY(outer->addPanel(p("center")));
        QVERIFY(outer->addPanel(p("tool"), DockArea::Left));
        QVERIFY(inner->addPanel(p("doc1")));
        QVERIFY(inner->addPanel(p("doc2")));
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockAreaWidget *outerArea = areaOf(outer);
        DockAreaWidget *innerArea = areaOf(inner);
        QCOMPARE(innerArea->window(), &window);
        QCOMPARE(describe(outer), p("H(tool, center)"));
        QCOMPARE(describe(inner), p("doc1|doc2"));

        // A document splits the inner workspace, and has no business outside.
        {
            Drag drag(manager);
            QVERIFY(drag.begin("doc2"));
            QVERIFY(!drag.enter(outerArea, zonePoint(outerArea, "tool", DockArea::Center)));
            QVERIFY(!outerArea->overlay()->isVisible());
            QVERIFY(drag.move(innerArea, zonePoint(innerArea, "doc1", DockArea::Right)));
            QVERIFY(innerArea->overlay()->isVisible());
            QVERIFY(drag.drop(innerArea, zonePoint(innerArea, "doc1", DockArea::Right)));
        }
        QCOMPARE(describe(inner), p("H(doc1, doc2)"));
        QCOMPARE(describe(outer), p("H(tool, center)"));

        // A tool over the documents: the inner area lets the drag pass, and
        // the outer one treats the place as what it is to it, the panel in
        // the middle. It can go beside that, never into it.
        {
            Drag drag(manager);
            QVERIFY(drag.begin("tool"));
            const QPoint top(innerArea->width() / 2, 60);
            QVERIFY(drag.move(innerArea, top));
            QVERIFY(!innerArea->overlay()->isVisible());
            QVERIFY(outerArea->overlay()->isVisible());
            const DropCandidate candidate =
                outerArea->candidateAt(innerArea->mapTo(outerArea, top), drag.session());
            QVERIFY(candidate.valid);
            QCOMPARE(candidate.target.area, DockArea::Top);
            QVERIFY(!candidate.zones.testFlag(DockArea::Center));
            QVERIFY(drag.drop(innerArea, top));
        }
        QCOMPARE(describe(outer), p("V(tool, center)"));
        QCOMPARE(describe(inner), p("H(doc1, doc2)"));
        QVERIFY(manager.panel(p("doc1"))->widget()->isVisible());

        // The panel a click lands in is the innermost one.
        QTest::mouseClick(manager.panel(p("doc2"))->widget(), Qt::LeftButton);
        QCOMPARE(manager.activePanel(), manager.panel(p("doc2")));

        // The inner workspace goes with the panel holding it; what was in it
        // is closed, and stays registered.
        QVERIFY(manager.unregisterPanel(p("center")));
        QVERIFY(!manager.workspace(p("documents")));
        QVERIFY(manager.hasPanel(p("doc1")));
        QVERIFY(!manager.panel(p("doc1"))->isOpen());
        QCOMPARE(describe(outer), p("tool"));
    }

    void dropFilterCanVeto()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.b->addPanel(p("c")));
        DockAreaWidget *area = areaOf(f.a);
        DockAreaWidget *other = areaOf(f.b);

        QList<DockDropRequest> seen;
        f.manager.setDropFilter([&](const DockDropRequest &request) {
            seen << request;
            // Nothing may be tabbed with "b", and nothing may enter workspace B.
            return request.workspaceId != p("B")
                && !(request.targetPanel == p("b") && request.area == DockArea::Center);
        });

        Drag drag(f.manager);
        QVERIFY(drag.begin("a"));
        QVERIFY(!drag.move(area, zonePoint(area, "b", DockArea::Center)));
        QCOMPARE(seen.constLast().panels, QStringList{p("a")});
        QCOMPARE(seen.constLast().workspaceId, p("A"));
        QCOMPARE(seen.constLast().targetPanel, p("b"));
        QCOMPARE(seen.constLast().area, DockArea::Center);
        QVERIFY(!seen.constLast().intoFloatingWindow);
        QVERIFY(drag.move(area, zonePoint(area, "b", DockArea::Bottom)));
        QVERIFY(!drag.move(other, zonePoint(other, "c", DockArea::Bottom)));
        QVERIFY(!drag.drop(other, zonePoint(other, "c", DockArea::Bottom)));
        QCOMPARE(describe(f.b), p("c"));

        f.manager.setDropFilter({});
        QVERIFY(drag.move(other, zonePoint(other, "c", DockArea::Bottom)));
    }

    void ownGroupOffersItsCentreAsStayHere()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        DockAreaWidget *area = areaOf(f.a);
        f.manager.clearUndoHistory();
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);
        {
            // The only panel there is. Nowhere to split to, but the centre is
            // shown and highlighted: dropping there leaves it where it is.
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            for (DockArea zone : {DockArea::Left, DockArea::Top, DockArea::Bottom})
                QVERIFY(!drag.move(area, zonePoint(area, "a", zone)));
            QVERIFY(!drag.move(area, QPoint(2, area->height() / 2))); // outer edge
            QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Center)));
            QCOMPARE(area->overlay()->scene().zones.size(), 1);
            QCOMPARE(area->overlay()->scene().zones.constFirst().area, DockArea::Center);
            QVERIFY(hoveredZone(area));
            QVERIFY(drag.drop(area, zonePoint(area, "a", DockArea::Center)));
            QCOMPARE(describe(f.a), p("a"));
        }
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("a"))); // as pressing its tab to drag it does
        f.manager.clearUndoHistory();
        changed.clear();
        {
            // With company: all five areas. The centre still means "stay", and
            // in particular does not move the tab to the end of the group.
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Left)));
            QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Center)));
            QCOMPARE(hoveredZone(area)->area, DockArea::Center);
            int inner = 0;
            for (const auto &zone : area->overlay()->scene().zones)
                inner += zone.outer ? 0 : 1;
            QCOMPARE(inner, 5);
            QVERIFY(drag.drop(area, zonePoint(area, "a", DockArea::Center)));
            QCOMPARE(describe(f.a), p("a|b|c"));
        }
        // Nothing changed, so nothing was announced and nothing can be undone.
        QCOMPARE(changed.size(), 0);
        QVERIFY(!f.manager.canUndo());
        {
            // It is offered whatever the panel's policy says about docking:
            // nothing gets docked.
            DockPolicy policy;
            policy.allowedAreas = DockArea::Left;
            policy.features = without(DockFeature::Tabbable);
            f.manager.panel(p("a"))->setPolicy(policy);
            f.manager.setDropFilter([](const DockDropRequest &) { return false; });
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Center)));
            QVERIFY(!drag.move(area, zonePoint(area, "a", DockArea::Left))); // vetoed by the filter
            f.manager.setDropFilter({});
            QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Left)));
            QVERIFY(!drag.move(area, zonePoint(area, "a", DockArea::Right)));
            f.manager.panel(p("a"))->setPolicy(DockPolicy{});
        }
        {
            // Splitting it off its own group works as before.
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            QVERIFY(drag.drop(area, zonePoint(area, "a", DockArea::Right)));
            QCOMPARE(describe(f.a), p("H(b|c, a)"));
        }
        {
            // A whole group over itself: only "stay".
            Drag drag(f.manager);
            QVERIFY(drag.begin("b", true));
            QVERIFY(!drag.move(area, zonePoint(area, "b", DockArea::Bottom)));
            QVERIFY(drag.move(area, zonePoint(area, "b", DockArea::Center)));
            QVERIFY(drag.drop(area, zonePoint(area, "b", DockArea::Center)));
            QCOMPARE(describe(f.a), p("H(b|c, a)"));
        }
    }

    // Dropping between tabs takes the tabs themselves. The empty rest of a
    // title row must not: someone aiming at the top of a panel, or at the top
    // edge of the window, passes right over it.
    void onlyTheTabsTakeTabDrops()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Bottom, 0.5));
        QVERIFY(f.b->addPanel(p("d")));
        DockAreaWidget *area = areaOf(f.a);
        const auto titlePoint = [&](const char *panel, bool onTabs) {
            const DockTabGroup *group = area->groupOfPanel(p(panel));
            const DockTabBar *bar = group->tabBar();
            const QRect last = bar->tabRect(bar->count() - 1);
            const QPoint local = onTabs ? last.center()
                                        : QPoint(last.right() + 200, bar->height() / 2);
            return bar->mapTo(area, local);
        };

        Drag drag(f.manager);
        QVERIFY(drag.begin("d"));

        // On a tab: insertion between tabs, in the top row and elsewhere.
        for (const char *panel : {"a", "c"}) {
            const DropCandidate onTab = area->candidateAt(titlePoint(panel, true), drag.session());
            QVERIFY(onTab.valid);
            QVERIFY(onTab.tabIndicator.isValid());
            QCOMPARE(onTab.target.area, DockArea::Center);
            QVERIFY(onTab.target.tabIndex >= 0);
        }
        // Just behind the last tab still appends.
        {
            const DockTabBar *bar = area->groupOfPanel(p("c"))->tabBar();
            const QPoint behind = bar->mapTo(area, QPoint(bar->tabRect(0).right() + 6, bar->height() / 2));
            const DropCandidate append = area->candidateAt(behind, drag.session());
            QVERIFY(append.tabIndicator.isValid());
            QCOMPARE(append.target.tabIndex, 1);
        }

        // Beside the tabs, in the title row along the top of the window: the
        // outer band, at its full width, not a tab drop.
        const DropCandidate topRow = area->candidateAt(titlePoint("a", false), drag.session());
        QVERIFY(topRow.valid);
        QVERIFY(!topRow.tabIndicator.isValid());
        QVERIFY(topRow.outer);
        QCOMPARE(topRow.target.area, DockArea::Top);
        QVERIFY(topRow.target.node.isNull());

        // Beside the tabs of a group in the middle of the window: that group's
        // top area.
        const DropCandidate middle = area->candidateAt(titlePoint("c", false), drag.session());
        QVERIFY(middle.valid);
        QVERIFY(!middle.tabIndicator.isValid());
        QVERIFY(!middle.outer);
        QCOMPARE(middle.target.area, DockArea::Top);
        QCOMPARE(middle.target.node, f.a->layoutTree().findPanel(p("c"))->id);
        QVERIFY(drag.drop(area, titlePoint("c", false)));
        QCOMPARE(describe(f.a), p("V(a|b, d, c)"));
    }

    void failedDropRollsBack()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Bottom));
        DockAreaWidget *area = areaOf(f.a);
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);

        // The dragged panel is closed while the drag is in flight.
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            QVERIFY(drag.move(area, zonePoint(area, "c", DockArea::Left)));
            QVERIFY(f.manager.hidePanel(p("a")));
            const QString before = describe(f.a);
            changed.clear();
            QVERIFY(!drag.drop(area, zonePoint(area, "c", DockArea::Left)));
            QCOMPARE(describe(f.a), before);
            QCOMPARE(changed.size(), 0);
            QVERIFY(f.a->layoutTree().validate());
        }
        // The target group disappears before the drop is committed.
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("b"));
            const DropTarget target{p("A"), f.a->layoutTree().findPanel(p("c"))->id,
                                    DockArea::Left, -1, 0.5};
            QVERIFY(priv(f.manager)->dropAllowed(drag.session(), target));
            QVERIFY(f.manager.hidePanel(p("c")));
            const QString before = describe(f.a);
            const DockResult result = priv(f.manager)->drag->drop(target);
            QVERIFY(!result);
            QCOMPARE(describe(f.a), before);
            QVERIFY(f.manager.panel(p("b"))->isOpen());
        }
        // The panel is unregistered mid-drag.
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("b"));
            QVERIFY(f.manager.showPanel(p("c")));
            QVERIFY(f.manager.unregisterPanel(p("b")));
            QVERIFY(!drag.drop(area, zonePoint(area, "c", DockArea::Left)));
            QVERIFY(f.a->layoutTree().validate());
            QVERIFY(!f.a->panels().contains(p("b")));
        }
    }

    // Wherever the pointer is, what the guide offers and what a drop does
    // agree with each other and with the policy.
    void guideHitTestAndPolicyAgree()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        QVERIFY(f.manager.movePanel(p("d"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("e")));
        DockPolicy policy;
        policy.allowedAreas = DockArea::Left | DockArea::Bottom | DockArea::Center;
        f.manager.panel(p("e"))->setPolicy(policy);
        f.manager.panel(p("c"))->setFeatures(without(DockFeature::Tabbable));
        DockAreaWidget *area = areaOf(f.a);

        Drag drag(f.manager);
        QVERIFY(drag.begin("e"));
        QRandomGenerator rng(5);
        int valid = 0;
        for (int i = 0; i < 4000; ++i) {
            const QPoint pos(rng.bounded(area->width()), rng.bounded(area->height()));
            const DropCandidate candidate = area->candidateAt(pos, drag.session());
            QCOMPARE(drag.move(area, pos), candidate.valid);
            if (!candidate.valid) {
                QCOMPARE(candidate.hovered, DockArea::None);
                QVERIFY(!candidate.preview.isValid());
                continue;
            }
            ++valid;
            QVERIFY(policy.allowedAreas.testFlag(candidate.target.area));
            QVERIFY(priv(f.manager)
                        ->allowedDropAreas(drag.session(), p("A"), candidate.target.node)
                        .testFlag(candidate.target.area));
            QVERIFY(area->contentsRect().contains(candidate.preview));
            if (candidate.outer) {
                QVERIFY(candidate.target.node.isNull());
                QVERIFY(candidate.outerZones.testFlag(candidate.hovered));
            } else {
                // The drop goes to the group under the pointer.
                const DockTabGroup *group = area->group(candidate.target.node);
                QVERIFY(group && group->geometry().contains(pos));
                if (group->panelIds().contains(p("c")))
                    QVERIFY(candidate.target.area != DockArea::Center);
            }
        }
        QVERIFY(valid > 1000);
    }

    void maximizedContainerOffersOnlyTheVisibleGroup()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.b->addPanel(p("c")));
        QVERIFY(f.manager.maximizePanel(p("a")));
        DockAreaWidget *area = areaOf(f.a);

        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        QVERIFY(drag.enter(area, area->rect().center()));
        for (const auto &zone : area->overlay()->scene().zones)
            QVERIFY(!zone.outer);
        // Right at the border it is still the maximized group that is hit.
        const DropCandidate candidate = area->candidateAt(QPoint(3, area->height() / 2),
                                                          drag.session());
        QVERIFY(candidate.valid);
        QCOMPARE(candidate.target.node, f.a->layoutTree().findPanel(p("a"))->id);
        QVERIFY(drag.drop(area, QPoint(3, area->height() / 2)));
        QCOMPARE(describe(f.a), p("H(c, a, b)"));
        QCOMPARE(f.manager.maximizedPanel(), QString()); // the new panel must be visible
    }

    void floatingWindowIsADropTargetToo()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Bottom));
        QVERIFY(f.manager.floatPanel(p("b"), QRect(50, 50, 400, 300)));
        DockFloatingWindow *window = priv(f.manager)->floatingWindows.begin().value();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        DockAreaWidget *area = window->area();

        Drag drag(f.manager);
        QVERIFY(drag.begin("a"));
        const QPoint pos = zonePoint(area, "b", DockArea::Right);
        QVERIFY(drag.enter(area, pos));
        QVERIFY(drag.drop(area, pos));
        QCOMPARE(describe(area->tree()), p("H(b, a)"));
        QCOMPARE(f.widgets[p("a")]->window(), window);
        QVERIFY(f.manager.panel(p("a"))->isFloating());

        // A panel that may not float cannot be dragged into a floating window.
        f.manager.panel(p("c"))->setFeatures(without(DockFeature::Floatable));
        Drag second(f.manager);
        QVERIFY(second.begin("c"));
        QVERIFY(!second.move(area, zonePoint(area, "b", DockArea::Left)));
    }

    // The drop filter learns whether a drop goes onto the tabs.
    void dropFilterIsToldAboutDropsOnTabs()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("c")));
        QList<int> indexes;
        bool tabsOnly = false;
        f.manager.setDropFilter([&](const DockDropRequest &request) {
            indexes << request.tabIndex;
            return !tabsOnly || request.tabIndex >= 0;
        });
        DockAreaWidget *area = areaOf(f.a);
        const DockTabBar *bar = area->groupOfPanel(p("a"))->tabBar();
        const QPoint onTabs = bar->mapTo(area, bar->tabRect(1).topLeft() + QPoint(4, 8));

        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        QVERIFY(drag.move(area, onTabs));
        QCOMPARE(indexes.constLast(), 1);
        QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Center)));
        QCOMPARE(indexes.constLast(), -1);
        QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Left)));
        QCOMPARE(indexes.constLast(), -1);

        // Which lets it take nothing but tabs.
        tabsOnly = true;
        QVERIFY(!drag.move(area, zonePoint(area, "a", DockArea::Center)));
        QVERIFY(!drag.move(area, zonePoint(area, "a", DockArea::Left)));
        QVERIFY(drag.drop(area, onTabs));
        QCOMPARE(describe(f.a), p("a|c|b"));
    }

    // What can only become a tab is taken as one by the whole title row.
    void titleRowTakesWhatCanOnlyBecomeATab()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("c")));
        DockAreaWidget *area = areaOf(f.a);
        const DockTabGroup *group = area->groupOfPanel(p("a"));
        const DockTabBar *bar = group->tabBar();
        // On the title row, well beside the tabs.
        const QPoint beside = bar->mapTo(area, QPoint(bar->tabRect(1).right() + 200, bar->height() / 2));
        QVERIFY(group->titleBar()->geometry().contains(group->mapFrom(area, beside)));
        QVERIFY(!bar->tabDropRegion().contains(bar->mapFrom(area, beside)));

        // Ordinarily that spot belongs to the areas that split.
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            QVERIFY(drag.move(area, beside));
            QVERIFY(area->overlay()->scene().tabIndicator.isNull());
        }

        DockPolicy policy;
        policy.allowedAreas = DockArea::Center;
        for (const char *id : {"a", "b", "c"})
            QVERIFY(f.manager.setDockPolicy(p(id), policy));
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            QVERIFY(drag.move(area, beside));
            QVERIFY(area->overlay()->scene().tabIndicator.isValid());
            QVERIFY(drag.drop(area, beside));
            QCOMPARE(describe(f.a), p("a|b|c"));
        }
        // The same goes for putting a tab of the group last.
        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            QVERIFY(drag.drop(area, beside));
            QCOMPARE(describe(f.a), p("b|c|a"));
        }
        // The content below still takes it, unless a drop filter says no.
        QVERIFY(f.manager.movePanel(p("c"), f.b, DockArea::Center));
        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Center)));
        QVERIFY(!drag.move(area, zonePoint(area, "a", DockArea::Left)));
    }

    // Tabs that are joined by the header only, and torn off anywhere else.
    void centreOfAGroupCanBeClosedToDrops()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("c")));
        QVERIFY(f.manager.movePanel(p("d"), p("c"), DockArea::Center));
        DockAreaWidget *area = areaOf(f.a);
        DockAreaWidget *areaB = areaOf(f.b);
        const DockTabBar *bar = area->groupOfPanel(p("a"))->tabBar();
        const QPoint onTabs = bar->mapTo(area, bar->tabRect(1).topLeft() + QPoint(4, 8));
        QVERIFY(f.manager.isCenterDropEnabled());
        f.manager.setCenterDropEnabled(false);
        QVERIFY(!f.manager.isCenterDropEnabled());

        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        // On another group: its sides and its tabs, not its middle.
        QVERIFY(!drag.move(area, zonePoint(area, "a", DockArea::Center)));
        for (const auto &zone : area->overlay()->scene().zones)
            QVERIFY(zone.area != DockArea::Center);
        QVERIFY(drag.move(area, zonePoint(area, "a", DockArea::Left)));
        QVERIFY(drag.move(area, onTabs));
        drag.leave(area);
        // On its own group: among the tabs, to change places. The middle
        // does not put it back; it is no place to drop anything.
        QVERIFY(!drag.move(areaB, zonePoint(areaB, "c", DockArea::Center)));
        const DockTabBar *barB = areaB->groupOfPanel(p("c"))->tabBar();
        QVERIFY(drag.move(areaB, barB->mapTo(areaB, barB->tabRect(1).center() + QPoint(8, 0))));
        drag.leave(areaB);
        // An empty workspace has no header to aim for, and takes the panel.
        QVERIFY(f.manager.hidePanels({p("a"), p("b")}));
        QVERIFY(drag.move(area, area->rect().center()));
        QVERIFY(f.manager.showPanels({p("a"), p("b")}));
        drag.leave(area);

        // A drop that names the middle directly is refused as well.
        DropTarget target;
        target.container = p("A");
        target.node = area->groupOfPanel(p("a"))->nodeId();
        target.area = DockArea::Center;
        QVERIFY(!priv(f.manager)->dropAllowed(drag.session(), target));
        target.tabIndex = 0;
        QVERIFY(priv(f.manager)->dropAllowed(drag.session(), target));

        // With panels that may only be tabs, nothing of the guide is left
        // but the mark between two tabs, and the whole title row takes them.
        DockPolicy policy;
        policy.allowedAreas = DockArea::Center;
        QVERIFY(f.manager.setDockPolicy(p("c"), policy));
        QVERIFY(!drag.move(area, zonePoint(area, "a", DockArea::Left)));
        QVERIFY(area->overlay()->scene().zones.isEmpty());
        const QPoint beside = bar->mapTo(area, QPoint(bar->tabRect(1).right() + 200, bar->height() / 2));
        QVERIFY(drag.move(area, beside));
        QVERIFY(area->overlay()->scene().tabIndicator.isValid());
        QVERIFY(drag.drop(area, beside));
        QCOMPARE(describe(f.a), p("a|b|c"));
        // The API is not bound by any of this.
        QVERIFY(f.manager.movePanel(p("d"), p("a"), DockArea::Center));
        QCOMPARE(describe(f.a), p("a|b|c|d"));
    }

    // A tab drag shown as it would turn out: the tab leaves its bar at once,
    // and the tabs it is held over make room for it.
    void tabDragIsPreviewedInTheTabBars()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("d")));
        QVERIFY(f.manager.movePanel(p("e"), p("d"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("b")));
        QVERIFY(!f.manager.isTabDragPreviewEnabled());
        f.manager.setTabDragPreviewEnabled(true);
        DockAreaWidget *area = areaOf(f.a);
        DockAreaWidget *areaB = areaOf(f.b);
        DockTabGroup *group = area->groupOfPanel(p("a"));
        DockTabGroup *groupB = areaB->groupOfPanel(p("d"));
        DockTabBar *bar = group->tabBar();
        DockTabBar *barB = groupB->tabBar();
        DockDragController *controller = priv(f.manager)->drag;
        const auto tabs = [](const DockTabBar *of) {
            QStringList names;
            for (int i = 0; i < of->count(); ++i)
                names << (of->isGap(i) ? p("_") : of->panelAt(i));
            return names.join(QLatin1Char(' '));
        };
        const auto at = [](const DockAreaWidget *in, const DockTabBar *of, int x) {
            return of->mapTo(in, QPoint(x, of->height() / 2));
        };
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);

        {
            Drag drag(f.manager);
            QVERIFY(drag.begin("b"));
            QCOMPARE(tabs(bar), p("a b c")); // not before the drag has its pictures
            controller->showSourcePreview();

            // Gone from its bar, with the content of the tab behind it shown.
            // Nothing of that is in the layout.
            QCOMPARE(tabs(bar), p("a c"));
            QCOMPARE(group->draggedOut(), p("b"));
            QCOMPARE(group->shownPanel(), p("c"));
            QCOMPARE(bar->panelAt(bar->currentIndex()), p("c"));
            QVERIFY(f.widgets[p("c")]->isVisible());
            QVERIFY(!f.widgets[p("b")]->isVisible());
            QCOMPARE(group->currentPanel(), p("b"));
            QCOMPARE(describe(f.a), p("a|b|c"));

            // Held over the tabs of the other window: room where it would go,
            // as wide as a tab, and no mark besides.
            const int width = barB->tabRect(1).width();
            const int left = barB->tabRect(1).left();
            QVERIFY(drag.move(areaB, at(areaB, barB, left + 4)));
            QCOMPARE(tabs(barB), p("d _ e"));
            QCOMPARE(groupB->dropGap(), 1);
            QVERIFY(barB->tabRect(1).width() > width / 2);
            QCOMPARE(barB->tabRect(2).left(), left + barB->tabRect(1).width());
            QVERIFY(areaB->overlay()->scene().tabIndicator.isNull());
            QVERIFY(!barB->isGap(barB->currentIndex()));
            QCOMPARE(describe(f.b), p("d|e"));
            // The pointer is over the room now, and that changes nothing.
            QVERIFY(drag.move(areaB, at(areaB, barB, barB->tabRect(1).center().x())));
            QCOMPARE(tabs(barB), p("d _ e"));
            // Further along, past the middle of the tab behind it.
            QVERIFY(drag.move(areaB, at(areaB, barB, barB->tabRect(2).center().x() + 4)));
            QCOMPARE(tabs(barB), p("d e _"));
            QVERIFY(drag.move(areaB, at(areaB, barB, 12)));
            QCOMPARE(tabs(barB), p("_ d e"));
            // Off the tabs, the room is given up.
            QVERIFY(drag.move(areaB, zonePoint(areaB, "d", DockArea::Left)));
            QCOMPARE(tabs(barB), p("d e"));
            QVERIFY(drag.move(areaB, at(areaB, barB, 12)));
            QCOMPARE(tabs(barB), p("_ d e"));
            drag.leave(areaB);
            QCOMPARE(tabs(barB), p("d e"));

            // Its own bar makes room the same way; where it came from is one
            // of the places.
            QVERIFY(drag.move(area, at(area, bar, bar->tabRect(1).left() + 4)));
            QCOMPARE(tabs(bar), p("a _ c"));
            QVERIFY(drag.move(area, at(area, bar, 12)));
            QCOMPARE(tabs(bar), p("_ a c"));
            QVERIFY(drag.move(area, at(area, bar, bar->tabRect(2).right() - 3)));
            QCOMPARE(tabs(bar), p("a c _"));
            QCOMPARE(changed.size(), 0);
            QVERIFY(drag.drop(area, at(area, bar, bar->tabRect(2).center().x())));
        }
        // Dropped: now it is in the layout, and the bars show what there is.
        QCOMPARE(describe(f.a), p("a|c|b"));
        QCOMPARE(tabs(bar), p("a c b"));
        QVERIFY(group->draggedOut().isEmpty());
        QCOMPARE(group->dropGap(), -1);
        QVERIFY(f.widgets[p("b")]->isVisible());
        QCOMPARE(changed.size(), 1);

        {
            // Put back where it came from: a drop that changes nothing.
            Drag drag(f.manager);
            QVERIFY(drag.begin("c"));
            controller->showSourcePreview();
            QCOMPARE(tabs(bar), p("a b"));
            QVERIFY(drag.move(area, at(area, bar, bar->tabRect(1).left() + 4)));
            QCOMPARE(tabs(bar), p("a _ b"));
            QVERIFY(drag.drop(area, at(area, bar, bar->tabRect(1).center().x())));
            QCOMPARE(describe(f.a), p("a|c|b"));
            QCOMPARE(tabs(bar), p("a c b"));
        }
        {
            // Into the other window, between its tabs.
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            controller->showSourcePreview();
            QCOMPARE(tabs(bar), p("c b"));
            const QPoint between = at(areaB, barB, barB->tabRect(1).left() + 4);
            QVERIFY(drag.move(areaB, between));
            QCOMPARE(tabs(barB), p("d _ e"));
            QVERIFY(drag.drop(areaB, between));
            QCOMPARE(describe(f.b), p("d|a|e"));
            QCOMPARE(describe(f.a), p("c|b"));
            QCOMPARE(tabs(barB), p("d a e"));
            QCOMPARE(tabs(bar), p("c b"));
        }
        {
            // A cancelled drag puts everything back.
            Drag drag(f.manager);
            QVERIFY(drag.begin("a"));
            controller->showSourcePreview();
            QVERIFY(drag.move(area, at(area, bar, 12)));
            QCOMPARE(tabs(barB), p("d e"));
            QCOMPARE(tabs(bar), p("_ c b"));
        }
        QCOMPARE(tabs(barB), p("d a e"));
        QCOMPARE(tabs(bar), p("c b"));
        QVERIFY(groupB->draggedOut().isEmpty());
        QCOMPARE(describe(f.b), p("d|a|e"));

        {
            // The last tab of a group stays in it, and so does a group that
            // is dragged whole.
            QVERIFY(f.manager.hidePanel(p("b")));
            Drag last(f.manager);
            QVERIFY(last.begin("c"));
            controller->showSourcePreview();
            QCOMPARE(tabs(bar), p("c"));
        }
        {
            Drag whole(f.manager);
            QVERIFY(whole.begin("d", true));
            controller->showSourcePreview();
            QCOMPARE(tabs(barB), p("d a e"));
        }

        // Turned off, a mark between the tabs is all there is.
        f.manager.setTabDragPreviewEnabled(false);
        Drag drag(f.manager);
        QVERIFY(drag.begin("a"));
        controller->showSourcePreview();
        QCOMPARE(tabs(barB), p("d a e"));
        QVERIFY(drag.move(area, at(area, bar, 12)));
        QCOMPARE(tabs(bar), p("c"));
        QVERIFY(area->overlay()->scene().tabIndicator.isValid());
    }

    void tabBarStartsDrags()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        DockTabBar *bar = areaOf(f.a)->groupOfPanel(p("a"))->tabBar();
        QSignalSpy panelDrag(bar, &DockTabBar::panelDragStarted);
        QSignalSpy groupDrag(bar, &DockTabBar::groupDragStarted);

        // Hold a session so the requests below do not start a real,
        // blocking QDrag.
        Drag blocker(f.manager);
        QVERIFY(blocker.begin("a"));

        const QPoint onTab = bar->tabRect(0).center();
        QTest::mousePress(bar, Qt::LeftButton, {}, onTab);
        QTest::mouseMove(bar, onTab + QPoint(2, 0));
        QCOMPARE(panelDrag.size(), 0); // below the drag threshold
        QTest::mouseMove(bar, onTab + QPoint(60, 30));
        QCOMPARE(panelDrag.size(), 1);
        QCOMPARE(panelDrag.at(0).at(0).toString(), p("a"));
        QTest::mouseMove(bar, onTab + QPoint(90, 30));
        QCOMPARE(panelDrag.size(), 1); // one drag per press
        QTest::mouseRelease(bar, Qt::LeftButton, {}, onTab + QPoint(90, 30));

        const QPoint beside(bar->tabRect(1).right() + 40, bar->height() / 2);
        QVERIFY(bar->tabAt(beside) < 0);
        QTest::mousePress(bar, Qt::LeftButton, {}, beside);
        QTest::mouseMove(bar, beside + QPoint(40, 40));
        QTest::mouseRelease(bar, Qt::LeftButton, {}, beside + QPoint(40, 40));
        QCOMPARE(groupDrag.size(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(describe(f.a), p("a|b"));
    }

    // --- The other guides ------------------------------------------------------

    void buttonLayoutIsACrossThatStaysInside()
    {
        const QRect within(0, 0, 400, 300);
        DropButtonLayout cross = DropButtonLayout::compute(QRect(100, 50, 200, 200), within, 30, 4);
        QVERIFY(cross.isValid());
        QCOMPARE(cross.center, QRect(100, 50, 200, 200).center());
        QVERIFY(cross.rect(DockArea::Center).contains(cross.center));
        QVERIFY(qAbs(cross.rect(DockArea::Center).center().x() - cross.center.x()) <= 1);
        QCOMPARE(cross.rect(DockArea::Center).size(), QSize(30, 30));
        QCOMPARE(cross.rect(DockArea::Left).right() + 4, cross.rect(DockArea::Center).left() - 1);
        QCOMPARE(cross.rect(DockArea::Bottom).top() - 4, cross.rect(DockArea::Center).bottom() + 1);
        // The ring around it is one step further out, and has no middle.
        QCOMPARE(cross.rect(DockArea::Right, 1).left() - 4, cross.rect(DockArea::Right).right() + 1);
        QVERIFY(cross.rect(DockArea::Center, 1).isNull());

        // Only a button is a target, the gap around it included.
        QCOMPARE(cross.hitTest(cross.center, AllDockAreas), DockArea::Center);
        QCOMPARE(cross.hitTest(cross.center - QPoint(34, 0), AllDockAreas), DockArea::Left);
        QCOMPARE(cross.hitTest(cross.center - QPoint(34, 34), AllDockAreas), DockArea::None);
        QCOMPARE(cross.hitTest(cross.center - QPoint(34, 0), DockArea::Center | DockArea::Right),
                 DockArea::None);
        QCOMPARE(cross.hitTest(cross.center - QPoint(68, 0), AllDockAreas), DockArea::None);
        QCOMPARE(cross.hitTest(cross.center - QPoint(68, 0), AllDockAreas, 1), DockArea::Left);
        QVERIFY(!cross.contains(cross.center - QPoint(68, 0)));
        QVERIFY(cross.contains(cross.center - QPoint(68, 0), 2));

        // A target at the border: the cross moves in until all of it shows.
        cross = DropButtonLayout::compute(QRect(0, 0, 40, 300), within, 30, 4);
        QCOMPARE(cross.rect(DockArea::Left).left(), 0);
        QCOMPARE(cross.center.y(), 149);
        cross = DropButtonLayout::compute(QRect(0, 0, 40, 300), within, 30, 4, 2);
        QCOMPARE(cross.rect(DockArea::Left, 1).left(), 0);
        // In too little room it is in the middle of that.
        cross = DropButtonLayout::compute(QRect(0, 0, 40, 40), QRect(0, 0, 60, 60), 30, 4);
        QCOMPARE(cross.center, QPoint(29, 29));

        QCOMPARE(outerButtonRect(within, DockArea::Left, 30, 6), QRect(6, 134, 30, 30));
        QCOMPARE(outerButtonRect(within, DockArea::Bottom, 30, 6), QRect(184, 264, 30, 30));
        QVERIFY(!DropButtonLayout{}.isValid());
    }

    void buttonGuideTakesDropsOnItsButtonsOnly()
    {
        TwoWindows f;
        DockTheme theme;
        theme.overlay.guide = DockGuide::Buttons;
        f.manager.setTheme(theme);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("a"), DockArea::Bottom));
        DockAreaWidget *area = areaOf(f.a);
        DockDropOverlay *overlay = area->overlay();
        const DockOverlayStyle style = overlay->effectiveStyle();
        const QRect groupRect = area->groupOfPanel(p("b"))->geometry();
        const QString before = describe(f.a);

        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        QVERIFY(drag.move(area, groupRect.center()));
        // A cross of five buttons in the middle of the group, and one at each
        // border of the workspace.
        int inner = 0;
        int outer = 0;
        QRegion crossRegion;
        for (const auto &zone : overlay->scene().zones) {
            (zone.outer ? outer : inner) += 1;
            QCOMPARE(zone.shape.boundingRect().size().toSize(),
                     QSize(style.buttonSize, style.buttonSize));
            if (!zone.outer)
                crossRegion += zone.shape.boundingRect().toRect();
            else
                QVERIFY(!crossRegion.boundingRect().intersects(zone.shape.boundingRect().toRect()));
        }
        QCOMPARE(inner, 5);
        QCOMPARE(outer, 4);
        QVERIFY((crossRegion.boundingRect().center() - groupRect.center()).manhattanLength() <= 2);
        QVERIFY(crossRegion.boundingRect().width() < groupRect.width() * 2 / 3);
        QCOMPARE(hoveredZone(area)->area, DockArea::Center);
        QCOMPARE(overlay->scene().preview, groupRect);
        grab(&f.windowA, p("buttons-center"));

        // Beside the buttons the group takes nothing.
        const QPoint beside = groupRect.center() + QPoint(style.buttonSize * 2 + 30, style.buttonSize * 2);
        QVERIFY(groupRect.contains(beside));
        QVERIFY(!drag.move(area, beside));
        QVERIFY(overlay->isVisible());
        QVERIFY(!hoveredZone(area));
        QVERIFY(!overlay->scene().preview.isValid());

        // Each button says where: here, the left half of the group.
        const QPoint left = groupRect.center() - QPoint(style.buttonSize + style.zoneGap, 0);
        QVERIFY(drag.move(area, left));
        QCOMPARE(hoveredZone(area)->area, DockArea::Left);
        QCOMPARE(overlay->scene().preview, dropPreviewRect(groupRect, DockArea::Left, 0.5));
        grab(&f.windowA, p("buttons-left"));

        // The one at the bottom border docks along the whole workspace.
        const QRect bottom = outerButtonRect(area->contentsRect(), DockArea::Bottom,
                                             style.buttonSize, style.zoneMargin);
        QVERIFY(drag.move(area, bottom.center()));
        QVERIFY(hoveredZone(area)->outer);
        QCOMPARE(hoveredZone(area)->area, DockArea::Bottom);
        QCOMPARE(overlay->scene().preview.width(), area->width());
        grab(&f.windowA, p("buttons-outer"));
        QCOMPARE(describe(f.a), before);

        // Tabs take a tab as with any guide.
        const DockTabBar *bar = area->groupOfPanel(p("b"))->tabBar();
        QVERIFY(drag.move(area, bar->mapTo(area, bar->tabRect(0).center())));
        QVERIFY(overlay->scene().tabIndicator.isValid());

        // Let go of beside the buttons, the drag is not taken (and goes on
        // here, where nothing ends it).
        QVERIFY(!drag.drop(area, beside));
        QCOMPARE(describe(f.a), before);
        QVERIFY(drag.drop(area, left));
        QCOMPARE(describe(f.a), p("H(a, c, b)"));
    }

    // A cross is as large as it is, also over a group smaller than itself.
    // On one of its buttons the pointer is still on that group's guide,
    // though it has left the group.
    void buttonCrossStaysWithItsGroup()
    {
        TwoWindows f;
        DockTheme theme;
        theme.overlay.guide = DockGuide::Buttons;
        f.manager.setTheme(theme);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right, 0.5));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom, -1, 0.1));
        QVERIFY(f.b->addPanel(p("d")));
        DockAreaWidget *area = areaOf(f.a);
        const DockOverlayStyle style = area->overlay()->effectiveStyle();
        const QRect low = area->groupOfPanel(p("c"))->geometry();
        const QRect above = area->groupOfPanel(p("b"))->geometry();
        QVERIFY(low.height() < style.buttonSize * 2);

        Drag drag(f.manager);
        QVERIFY(drag.begin("d"));
        QVERIFY(drag.move(area, low.center()));
        const DropCandidate shown = area->candidateAt(low.center(), drag.session());
        const QRect topButton = shown.buttons.rect(DockArea::Top);
        QVERIFY(above.contains(topButton.center()));
        QVERIFY(!low.contains(topButton.center()));
        QVERIFY(drag.move(area, topButton.center()));
        const DropCandidate held = area->candidateAt(topButton.center(), drag.session());
        QCOMPARE(held.target.node, area->groupOfPanel(p("c"))->nodeId());
        QCOMPARE(held.target.area, DockArea::Top);
        // Off the buttons, the group under the pointer has the guide.
        const QPoint away = above.topLeft() + QPoint(20, above.height() / 4);
        (void)drag.move(area, away);
        QCOMPARE(area->candidateAt(away, drag.session()).guideNode,
                 area->groupOfPanel(p("b"))->nodeId());
        QVERIFY(drag.drop(area, area->candidateAt(above.center(), drag.session())
                                    .buttons.rect(DockArea::Right).center()));
        QCOMPARE(describe(f.a), p("H(a, V(H(b, d), c))"));
    }

    // A header that names its panel takes a tab with the button guide: the
    // rest of the group is no target there.
    void buttonGuideLetsATitleBarTakeATab()
    {
        TwoWindows f;
        DockTheme theme;
        theme.overlay.guide = DockGuide::Buttons;
        f.manager.setTheme(theme);
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        DockAreaWidget *area = areaOf(f.a);
        const DockTabGroup *group = area->groupOfPanel(p("b"));
        const QPoint onTitle = group->titleBar()->mapTo(area, QPoint(30, group->titleBar()->height() / 2));

        Drag drag(f.manager);
        QVERIFY(drag.begin("a"));
        QVERIFY(drag.move(area, onTitle));
        QCOMPARE(area->overlay()->scene().preview, group->geometry());
        // Its own title bar is where the drag began: nothing to do there.
        const DockTabGroup *own = area->groupOfPanel(p("a"));
        QVERIFY(!drag.move(area, own->titleBar()->mapTo(area, QPoint(30, 4))));
        QVERIFY(drag.drop(area, onTitle));
        QCOMPARE(describe(f.a), p("b|a"));
    }

    // With the button guide a workspace inside a panel leaves nearly all of
    // itself free, and the workspace around it offers its buttons as well:
    // those for the panel that holds the inner one as a ring around the
    // inner cross (beside the inner workspace), and those at its own border.
    void buttonGuidesOfNestedWorkspacesAreShownTogether()
    {
        DockManager manager;
        DockTheme theme;
        theme.overlay.guide = DockGuide::Buttons;
        manager.setTheme(theme);
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
        for (const char *id : {"tool", "tool2", "doc1", "doc2"}) {
            DockPanel *panel = manager.registerPanel(p(id), new QLabel(p(id)));
            if (p(id).startsWith(QLatin1String("doc"))) {
                DockPolicy policy;
                policy.allowedWorkspaces = {p("documents")};
                panel->setPolicy(policy);
            }
        }
        QVERIFY(outer->addPanel(p("center")));
        QVERIFY(outer->addPanel(p("tool"), DockArea::Left));
        QVERIFY(manager.movePanel(p("tool2"), p("tool"), DockArea::Center));
        QVERIFY(inner->addPanel(p("doc1")));
        QVERIFY(inner->addPanel(p("doc2"), DockArea::Right));
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        DockAreaWidget *outerArea = areaOf(outer);
        DockAreaWidget *innerArea = areaOf(inner);
        const DockOverlayStyle style = innerArea->overlay()->effectiveStyle();
        const int step = style.buttonSize + style.zoneGap;
        const QPoint middle = innerArea->groupOfPanel(p("doc1"))->geometry().center();
        const auto count = [](DockAreaWidget *area, bool outerZones) {
            int n = 0;
            for (const auto &zone : area->overlay()->scene().zones)
                n += zone.outer == outerZones ? 1 : 0;
            return n;
        };

        {
            Drag drag(manager);
            QVERIFY(drag.begin("tool2"));
            // In the middle of a document: a tab there.
            QVERIFY(drag.move(innerArea, middle));
            QVERIFY(innerArea->overlay()->isVisible());
            QVERIFY(outerArea->overlay()->isVisible());
            QCOMPARE(count(innerArea, false), 5);
            QCOMPARE(count(innerArea, true), 0);  // its border is the outer workspace's business
            QCOMPARE(count(outerArea, false), 4); // beside the documents; no tab of that panel
            QCOMPARE(count(outerArea, true), 4);
            QCOMPARE(hoveredZone(innerArea)->area, DockArea::Center);
            QVERIFY(!hoveredZone(outerArea));
            // The ring lies around the inner cross.
            for (const auto &zone : outerArea->overlay()->scene().zones) {
                if (zone.outer)
                    continue;
                const QPoint at = outerArea->mapTo(&window, zone.shape.boundingRect().center().toPoint());
                const QPoint offset = at - innerArea->mapTo(&window, middle);
                QCOMPARE(qAbs(offset.x()) + qAbs(offset.y()), 2 * step);
            }
            grab(&window, p("buttons-nested"));

            // One step out: the split of the document. Two: beside them all.
            QVERIFY(drag.move(innerArea, middle + QPoint(0, step)));
            QCOMPARE(hoveredZone(innerArea)->area, DockArea::Bottom);
            QVERIFY(!hoveredZone(outerArea));
            QVERIFY(drag.move(innerArea, middle + QPoint(0, 2 * step)));
            QVERIFY(!hoveredZone(innerArea));
            QCOMPARE(hoveredZone(outerArea)->area, DockArea::Bottom);
            QVERIFY(!hoveredZone(outerArea)->outer);
            const QRect holderRect = outerArea->groupOfPanel(p("center"))->geometry();
            QCOMPARE(outerArea->overlay()->scene().preview,
                     dropPreviewRect(holderRect, DockArea::Bottom, 0.25));
            grab(&window, p("buttons-nested-ring"));
            // Beside the buttons: nothing.
            QVERIFY(!drag.move(innerArea, middle + QPoint(step, step)));
            QVERIFY(!hoveredZone(innerArea));
            QVERIFY(!hoveredZone(outerArea));

            QVERIFY(drag.drop(innerArea, middle + QPoint(0, 2 * step)));
        }
        QCOMPARE(describe(outer), p("H(tool, V(center, tool2))"));
        QCOMPARE(describe(inner), p("H(doc1, doc2)"));
        QVERIFY(!innerArea->overlay()->isVisible());
        QVERIFY(!outerArea->overlay()->isVisible());

        // The border of the outer workspace, where the inner one lies under it.
        {
            Drag drag(manager);
            QVERIFY(drag.begin("tool2"));
            const QRect top = outerButtonRect(outerArea->contentsRect(), DockArea::Top,
                                              style.buttonSize, style.zoneMargin);
            const QPoint at = innerArea->mapFrom(outerArea, top.center());
            QVERIFY(innerArea->rect().contains(at));
            // (It lies over the tabs of the documents, and comes before them.)
            QVERIFY(drag.move(innerArea, at));
            QVERIFY(hoveredZone(outerArea));
            QVERIFY(hoveredZone(outerArea)->outer);
            QVERIFY(innerArea->overlay()->scene().tabIndicator.isNull());
            QVERIFY(drag.drop(innerArea, at));
        }
        QCOMPARE(describe(outer), p("V(tool2, H(tool, center))"));

        // Leaving the inner area takes both guides down.
        {
            Drag drag(manager);
            QVERIFY(drag.begin("tool2"));
            const QPoint onDocument = innerArea->groupOfPanel(p("doc1"))->geometry().center();
            QVERIFY(drag.move(innerArea, onDocument));
            QVERIFY(outerArea->overlay()->isVisible());
            drag.leave(innerArea);
            QVERIFY(!innerArea->overlay()->isVisible());
            QVERIFY(!outerArea->overlay()->isVisible());
            // Into the documents, as a tab.
            QVERIFY(drag.drop(innerArea, onDocument));
        }
        QCOMPARE(describe(inner), p("H(doc1|tool2, doc2)"));
        QCOMPARE(describe(outer), p("H(tool, center)"));

        // A document has no business outside: the inner area is on its own,
        // with buttons at its border.
        {
            Drag drag(manager);
            QVERIFY(drag.begin("doc2"));
            const QPoint onDocument = innerArea->groupOfPanel(p("doc1"))->geometry().center();
            QVERIFY(drag.move(innerArea, onDocument));
            QVERIFY(!outerArea->overlay()->isVisible());
            QCOMPARE(count(innerArea, true), 4);
            QVERIFY(!drag.move(innerArea, onDocument + QPoint(0, 2 * step)));
        }
    }

    // DockGuide::Preview: the areas of the default guide, of which only the
    // place of the drop is shown.
    void previewGuideHasTheLargeAreas()
    {
        TwoWindows f;
        DockTheme theme;
        theme.overlay.guide = DockGuide::Preview;
        f.manager.setTheme(theme);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        QVERIFY(f.b->addPanel(p("c")));
        DockAreaWidget *area = areaOf(f.a);
        const QRect groupRect = area->groupOfPanel(p("b"))->geometry();

        Drag drag(f.manager);
        QVERIFY(drag.begin("c"));
        QVERIFY(drag.move(area, zonePoint(area, "b", DockArea::Left)));
        QCOMPARE(hoveredZone(area)->area, DockArea::Left);
        QCOMPARE(area->overlay()->scene().preview, dropPreviewRect(groupRect, DockArea::Left, 0.5));
        grab(&f.windowA, p("preview-left"));
        QVERIFY(drag.drop(area, zonePoint(area, "b", DockArea::Left)));
        QCOMPARE(describe(f.a), p("H(a, c, b)"));
    }
};

QTEST_MAIN(tst_DragDrop)
#include "tst_dragdrop.moc"
