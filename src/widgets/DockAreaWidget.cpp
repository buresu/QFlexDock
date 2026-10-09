// SPDX-License-Identifier: MIT
#include "widgets/DockAreaWidget.h"

#include "core/DockDragController.h"
#include "core/DropZones.h"
#include "widgets/DockDropOverlay.h"
#include "widgets/DockSplitHandle.h"
#include "widgets/DockTabBar.h"
#include "widgets/DockTabGroup.h"

#include <QtGui/QDragEnterEvent>
#include <QtWidgets/QLayout>

namespace QFlexDock {

namespace {

// Share of a workspace an outer-edge drop takes, and of a group an edge drop takes.
constexpr double OuterDropFraction = 0.25;
constexpr double GroupDropFraction = 0.5;
// Width left of the outer band where it overlaps a tab group's title row.
constexpr int OuterStripOnTitle = 7;
// How far the bars of a dock area inside a panel may end from the bars around
// that panel and still meet them: the frame of the tab group lies between.
constexpr int NestedBarReach = 8;

} // namespace

/// Thin QLayout around LayoutSolver. Its items are the tab groups; handles and
/// the overlay are positioned alongside them but are not layout items.
class DockAreaWidget::Layout : public QLayout
{
public:
    explicit Layout(DockAreaWidget *area)
        : QLayout(area)
        , m_area(area)
    {
        setContentsMargins(0, 0, 0, 0);
    }

    ~Layout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }

    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }

    QSize minimumSize() const override { return m_area->layoutMinimumSize(); }
    QSize sizeHint() const override { return QSize(640, 420).expandedTo(minimumSize()); }
    Qt::Orientations expandingDirections() const override
    {
        return Qt::Horizontal | Qt::Vertical;
    }

    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        m_area->relayout();
    }

private:
    DockAreaWidget *m_area;
    QList<QLayoutItem *> m_items;
};

DockAreaWidget::DockAreaWidget(DockManagerPrivate *manager, const QString &containerId,
                               QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
    , m_containerId(containerId)
{
    setAcceptDrops(true);
    m_layout = new Layout(this);
    m_overlay = new DockDropOverlay(manager, this);
}

DockAreaWidget::~DockAreaWidget() = default;

void DockAreaWidget::detachFromManager()
{
    m_manager = nullptr;
    m_overlay->setManager(nullptr);
    for (DockTabGroup *group : std::as_const(m_groups))
        group->detachFromManager();
}

DockTabGroup *DockAreaWidget::groupOfPanel(const PanelId &panel) const
{
    const LayoutNode *node = m_tree.findPanel(panel);
    return node ? m_groups.value(node->id) : nullptr;
}

QList<DockSplitHandle *> DockAreaWidget::visibleHandles() const
{
    return m_handles.mid(0, qsizetype(m_solved.handles.size()));
}

QList<DockEdgeHandle *> DockAreaWidget::visibleEdgeHandles() const
{
    QList<DockEdgeHandle *> shown;
    for (DockEdgeHandle *handle : m_edgeHandles) {
        if (!handle->isHidden())
            shown.append(handle);
    }
    return shown;
}

QList<DockSplitCorner *> DockAreaWidget::visibleCorners() const
{
    return m_cornerWidgets.mid(0, qsizetype(m_corners.size()));
}

int DockAreaWidget::handleWidth() const
{
    return m_manager ? m_manager->handleWidth(this) : 4;
}

int DockAreaWidget::handleHoverWidth() const
{
    return m_manager ? m_manager->theme.splitHandleHoverWidth : -1;
}

LimitsProvider DockAreaWidget::limitsProvider() const
{
    return [this](const LayoutNode &tabs) {
        const DockTabGroup *g = m_groups.value(tabs.id);
        return g ? g->sizeLimits() : SizeLimits{};
    };
}

const LayoutNode *DockAreaWidget::maximizedNode() const
{
    return m_maximized.isEmpty() ? nullptr : m_tree.findPanel(m_maximized);
}

QSize DockAreaWidget::layoutMinimumSize() const
{
    if (const LayoutNode *node = maximizedNode())
        return limitsProvider()(*node).min;
    if (const LayoutNode *root = m_tree.root())
        return LayoutSolver::limits(*root, handleWidth(), limitsProvider()).min;
    return QSize(0, 0);
}

void DockAreaWidget::setLayoutState(const ContainerState &container)
{
    m_tree = container.tree;
    m_maximized = container.maximized;

    QSet<NodeId> alive;
    for (const LayoutNode *node : m_tree.tabNodes()) {
        alive.insert(node->id);
        DockTabGroup *&g = m_groups[node->id];
        if (!g) {
            g = new DockTabGroup(m_manager, this);
            m_layout->addWidget(g);
        }
        g->setNode(*node, !m_maximized.isEmpty() && node->panels.contains(m_maximized));
    }
    for (auto it = m_groups.begin(); it != m_groups.end();) {
        if (alive.contains(it.key())) {
            ++it;
            continue;
        }
        // Not deleted on the spot: the group may be the origin of the user
        // action that led here. Content it still holds is collected by the
        // manager before the deferred delete runs.
        DockTabGroup *gone = it.value();
        gone->releaseTitleActions();
        gone->hide();
        m_layout->removeWidget(gone);
        gone->deleteLater();
        it = m_groups.erase(it);
    }

    relayout();
    m_layout->invalidate(); // the minimum size may have changed
}

void DockAreaWidget::contentLimitsChanged()
{
    m_layout->invalidate();
}

void DockAreaWidget::refreshAppearance()
{
    for (DockTabGroup *g : std::as_const(m_groups))
        g->refreshAppearance();
    m_layout->invalidate();
    m_overlay->update();
}

void DockAreaWidget::relayout()
{
    // Showing a child for the first time makes Qt activate this widget's
    // layout, which may resize the window and comes straight back here. That
    // call must not rearrange what the running pass is still walking through
    // (how many corners there are depends on the size): it only asks for
    // another pass.
    if (m_placing) {
        m_placeAgain = true;
        return;
    }
    m_placing = true;
    int passes = 0;
    do {
        m_placeAgain = false;
        placeWidgets();
    } while (m_placeAgain && ++passes < 8);
    m_placing = false;

    // Bars of this area may end on those of an area it lies within.
    if (m_manager && m_manager->cornerResize) {
        for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
            if (auto *outer = qobject_cast<DockAreaWidget *>(w))
                outer->nestedLayoutChanged();
        }
    }
}

void DockAreaWidget::nestedLayoutChanged()
{
    if (m_placing) {
        m_placeAgain = true;
        return;
    }
    m_placing = true;
    m_placeAgain = false;
    updateCorners();
    m_placing = false;
    if (m_placeAgain)
        relayout();
}

void DockAreaWidget::placeWidgets()
{
    const QRect bounds = contentsRect();
    const LayoutNode *maximized = maximizedNode();
    // What a drag has squeezed out is shown as gone already, its room with
    // the node across the handle. The tree itself changes when the drag ends.
    const bool squeezing = !maximized && !m_squeezed.empty();
    LayoutTree shown;
    if (maximized) {
        // Display state only: one group fills the area, the tree is untouched.
        m_solved = SolvedLayout{};
        m_solved.handleWidth = handleWidth();
        m_solved.rects.insert(maximized->id, bounds);
        m_solved.limits.insert(maximized->id, limitsProvider()(*maximized));
    } else if (squeezing) {
        shown = m_tree;
        for (const SplitterCoordinator::Squeezed &gone : m_squeezed)
            (void)shown.takeNode(gone.node, gone.heir);
        m_solved = LayoutSolver::solve(shown, bounds, handleWidth(), limitsProvider());
    } else {
        m_solved = LayoutSolver::solve(m_tree, bounds, handleWidth(), limitsProvider());
    }

    for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
        DockTabGroup *g = it.value();
        const bool visible = maximized ? maximized->id == it.key() : m_solved.rects.contains(it.key());
        if (visible)
            g->setGeometry(m_solved.rects.value(it.key()));
        g->setVisible(visible);
    }

    // Below the handles: where there is one, that is what the pointer finds.
    updateEdgeHandles();

    // A handle that is being dragged keeps its widget, whatever becomes of
    // the boundary it stands for: hidden, it would lose the mouse.
    const int needed = int(m_solved.handles.size());
    QList<DockSplitHandle *> pool;
    DockSplitHandle *held = nullptr;
    for (DockSplitHandle *handle : std::as_const(m_handles)) {
        if (squeezing && handle->isPressed())
            held = handle;
        else
            pool.append(handle);
    }
    while (pool.size() < needed) {
        pool.append(new DockSplitHandle(this));
        m_handles.append(pool.constLast());
    }
    for (int i = 0; i < pool.size(); ++i) {
        DockSplitHandle *handle = pool.at(i);
        if (i < needed) {
            const SolvedHandle &solved = m_solved.handles[size_t(i)];
            handle->configure(i, solved.orientation, solved.rect);
            handle->show();
            handle->raise();
        } else if (!handle->isPressed()) {
            handle->hide();
        }
        // Meanwhile the widgets stand for other handles than they did.
        if (squeezing)
            handle->setHovered(false);
    }
    if (held) {
        held->place(heldHandleBar(held, shown));
        held->raise();
    } else if (m_wasSqueezing && !squeezing) {
        highlightHandles(std::vector<int>(m_highlighted));
    }
    m_wasSqueezing = squeezing;

    updateCorners();

    m_overlay->setGeometry(rect());
    if (m_overlay->isVisible())
        m_overlay->raise();
}

// Where a dragged handle is shown while a group next to it is squeezed out.
QRect DockAreaWidget::heldHandleBar(const DockSplitHandle *held, const LayoutTree &shown) const
{
    // The boundary it stands for, in the tree as it (still) is.
    const int index = held->index();
    if (index < 0 || index >= int(m_dragStart.handles.size()))
        return held->barGeometry();
    const SolvedHandle &real = m_dragStart.handles[size_t(index)];
    const LayoutNode *split = m_tree.findNode(real.split);
    if (!split || real.index + 1 >= int(split->children.size()))
        return held->barGeometry();
    const NodeId before = split->children[size_t(real.index)].id;
    const NodeId after = split->children[size_t(real.index) + 1].id;

    // Both sides are still shown: it is between them.
    for (const SolvedHandle &handle : m_solved.handles) {
        const LayoutNode *s = shown.findNode(handle.split);
        if (s && handle.index + 1 < int(s->children.size())
            && s->children[size_t(handle.index)].id == before
            && s->children[size_t(handle.index) + 1].id == after) {
            return handle.rect;
        }
    }
    // One side is gone: along the edge of the other, where that side was.
    const bool beforeGone = !m_solved.rects.contains(before);
    const QRect rest = m_solved.rects.value(beforeGone ? after : before);
    const int width = qMax(1, m_solved.handleWidth);
    if (real.orientation == Qt::Horizontal)
        return QRect(beforeGone ? rest.left() : rest.right() - width + 1, rest.top(), width,
                     rest.height());
    return QRect(rest.left(), beforeGone ? rest.top() : rest.bottom() - width + 1, rest.width(),
                 width);
}

// Where closed collapsible panels would come back: a strip along that edge
// of what they would come back beside, to pull them out by.
void DockAreaWidget::updateEdgeHandles()
{
    struct Edge
    {
        QStringList panels;
        DockArea side;
        QRect bar;
    };
    QList<Edge> edges;
    if (m_manager && !maximizedNode()) {
        const int width = qMax(1, m_solved.handleWidth);
        for (const ReopenEdge &edge : m_manager->reopenEdges(m_containerId)) {
            const QRect beside = m_solved.rects.value(edge.anchor);
            if (!beside.isValid())
                continue;
            QRect bar;
            switch (edge.side) {
            case DockArea::Left:
                bar = QRect(beside.left(), beside.top(), width, beside.height());
                break;
            case DockArea::Right:
                bar = QRect(beside.right() - width + 1, beside.top(), width, beside.height());
                break;
            case DockArea::Top:
                bar = QRect(beside.left(), beside.top(), beside.width(), width);
                break;
            default:
                bar = QRect(beside.left(), beside.bottom() - width + 1, beside.width(), width);
                break;
            }
            // Not where a split handle runs along that edge already: there
            // is something else in the place they went to.
            const Qt::Orientation along = splitOrientation(edge.side);
            const bool taken = std::any_of(m_solved.handles.begin(), m_solved.handles.end(),
                                           [&](const SolvedHandle &handle) {
                return handle.orientation == along
                    && handle.rect.adjusted(-2, -2, 2, 2).intersects(bar);
            });
            if (!taken)
                edges.append({edge.panels, edge.side, bar});
        }
    }

    // One that is being dragged stays as it is, though the panels it pulled
    // out are no longer closed.
    QList<DockEdgeHandle *> pool;
    for (DockEdgeHandle *handle : std::as_const(m_edgeHandles)) {
        if (!handle->isPressed())
            pool.append(handle);
    }
    while (pool.size() < edges.size()) {
        pool.append(new DockEdgeHandle(this));
        m_edgeHandles.append(pool.constLast());
    }
    for (int i = 0; i < pool.size(); ++i) {
        DockEdgeHandle *handle = pool.at(i);
        if (i < edges.size()) {
            handle->configure(edges.at(i).panels, edges.at(i).side, edges.at(i).bar);
            handle->show();
            handle->raise();
        } else {
            handle->hide();
        }
    }
}

QList<DockAreaWidget *> DockAreaWidget::areasWithin() const
{
    QList<DockAreaWidget *> inner;
    if (!m_manager)
        return inner;
    for (DockWorkspace *workspace : std::as_const(m_manager->workspaces)) {
        DockAreaWidget *area = DockManagerPrivate::get(workspace)->area;
        if (area != this && isAncestorOf(area) && area->isVisibleTo(this))
            inner.append(area);
    }
    return inner;
}

// Corners lie on top of the handles that meet in them.
void DockAreaWidget::updateCorners()
{
    m_corners.clear();
    if (m_manager && m_manager->cornerResize && !m_solved.handles.empty()) {
        // This area's bars, and those of the areas inside its panels: where
        // one of theirs ends on one of these, the two are moved together.
        std::vector<SplitterCoordinator::Bar> bars;
        std::vector<std::pair<DockAreaWidget *, int>> owners;
        for (int i = 0; i < int(m_solved.handles.size()); ++i) {
            const SolvedHandle &handle = m_solved.handles[size_t(i)];
            bars.push_back({handle.rect, handle.orientation, 0});
            owners.emplace_back(this, i);
        }
        const QList<DockAreaWidget *> inner = areasWithin();
        for (DockAreaWidget *area : inner) {
            const QPoint offset = area->mapTo(this, QPoint(0, 0));
            for (int i = 0; i < int(area->m_solved.handles.size()); ++i) {
                const SolvedHandle &handle = area->m_solved.handles[size_t(i)];
                bars.push_back({handle.rect.translated(offset), handle.orientation, NestedBarReach});
                owners.emplace_back(area, i);
            }
        }

        for (const SplitterCoordinator::Corner &found : SplitterCoordinator::corners(bars)) {
            Corner corner;
            corner.rect = found.rect;
            corner.parts.push_back(CornerPart{this, {}, {}});
            const auto part = [&corner](DockAreaWidget *area) -> CornerPart & {
                for (CornerPart &p : corner.parts) {
                    if (p.area == area)
                        return p;
                }
                corner.parts.push_back(CornerPart{area, {}, {}});
                return corner.parts.back();
            };
            for (int index : found.columns)
                part(owners[size_t(index)].first).columns.push_back(owners[size_t(index)].second);
            for (int index : found.rows)
                part(owners[size_t(index)].first).rows.push_back(owners[size_t(index)].second);
            // Bars of an inner area meeting among themselves are its own business.
            const CornerPart &own = corner.parts.front();
            if (!own.columns.empty() || !own.rows.empty())
                m_corners.push_back(std::move(corner));
        }
    }

    // As with the handles: a corner that is being dragged stays as it is.
    const int corners = int(m_corners.size());
    const bool squeezing = !m_squeezed.empty();
    QList<DockSplitCorner *> pool;
    for (DockSplitCorner *corner : std::as_const(m_cornerWidgets)) {
        if (!(squeezing && corner->isPressed()))
            pool.append(corner);
    }
    while (pool.size() < corners) {
        pool.append(new DockSplitCorner(this));
        m_cornerWidgets.append(pool.constLast());
    }
    for (int i = 0; i < pool.size(); ++i) {
        DockSplitCorner *corner = pool.at(i);
        if (i < corners) {
            corner->configure(i, m_corners[size_t(i)].rect);
            corner->show();
            corner->raise();
        } else if (!corner->isPressed()) {
            corner->hide();
        }
    }
}

// --- Drag and drop -----------------------------------------------------------

DropCandidate DockAreaWidget::candidateAt(const QPoint &pos, const DragSession &session) const
{
    DropCandidate c;
    c.target.container = m_containerId;
    if (!m_manager)
        return c;

    const QRect bounds = contentsRect();
    const DockOverlayStyle style = m_overlay->effectiveStyle();
    const DockAreas wholeAreas = m_manager->allowedDropAreas(session, m_containerId, NodeId{});

    if (m_tree.isEmpty()) {
        // Nothing here yet: the whole area is one target.
        c.zoneRect = bounds;
        c.zones = wholeAreas & DockAreas(DockArea::Center);
        if (c.zones && bounds.contains(pos)) {
            c.hovered = DockArea::Center;
            c.target.area = DockArea::Center;
            c.preview = bounds;
        }
    } else {
        const LayoutNode *maximized = maximizedNode();
        const int band = maximized ? 0 : qMin(style.outerBandWidth,
                                              qMin(bounds.width(), bounds.height()) / 4);
        if (band > 0)
            c.outerZones = wholeAreas & EdgeDockAreas;

        // Tabs along the border lie inside the outer band. Over them the band
        // shrinks to a thin strip at the very edge, so that a tab can still
        // be dropped between tabs. The empty rest of a title row belongs to
        // the band like everything else there.
        const auto overTabs = [this, &pos](const DockTabGroup *g) {
            const DockTabBar *bar = g->tabBar();
            const QRect region = bar->tabDropRegion();
            return QRect(bar->mapTo(this, region.topLeft()), region.size()).contains(pos);
        };
        int hitBand = band;
        for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
            if (maximized && maximized->id != it.key())
                continue;
            if (overTabs(it.value()))
                hitBand = qMin(band, OuterStripOnTitle);
        }
        const DockArea outerHit = outerBandAt(bounds, pos, hitBand);
        if (outerHit != DockArea::None && c.outerZones.testFlag(outerHit)) {
            c.outer = true;
            c.hovered = outerHit;
            c.target.area = outerHit;
            c.target.fraction = OuterDropFraction;
            c.preview = dropPreviewRect(bounds, outerHit, OuterDropFraction);
        }

        // The tab group under the pointer. Its guide is shown even while an
        // outer band is hovered, so both kinds of target stay visible.
        for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
            if (maximized && maximized->id != it.key())
                continue;
            const QRect rect = m_solved.rects.value(it.key());
            if (!rect.contains(pos))
                continue;
            const DockTabGroup *g = it.value();
            // The guide stays clear of the outer bands so the two never overlap.
            c.zoneRect = c.outerZones ? rect.intersected(bounds.adjusted(band, band, -band, -band))
                                      : rect;
            c.zones = m_manager->allowedDropAreas(session, m_containerId, it.key());
            if (c.outer)
                break;

            // Onto the tabs: join at a specific position, or reorder within
            // the panel's own group. Only the tabs themselves count for this;
            // elsewhere on the title row the five areas apply as usual.
            const bool ownGroup = session.sourceContainer == m_containerId
                && session.sourceNode == it.key();
            const bool reorder = ownGroup && !session.wholeGroup;
            const bool tabDrop = overTabs(g)
                && (reorder || (!ownGroup && c.zones.testFlag(DockArea::Center)));
            if (tabDrop) {
                DockTabBar *bar = g->tabBar();
                const int index = bar->insertIndexAt(bar->mapFrom(this, pos));
                c.hovered = DockArea::Center;
                c.target.node = it.key();
                c.target.area = DockArea::Center;
                c.target.tabIndex = index;
                c.tabIndicator = QRect(bar->mapTo(this, bar->insertIndicatorRect(index).topLeft()),
                                       bar->insertIndicatorRect(index).size());
                c.preview = reorder ? QRect() : rect;
            } else {
                const DropZoneLayout zones = DropZoneLayout::compute(c.zoneRect, style.edgeFraction,
                                                                     style.zoneMargin);
                const DockArea hit = zones.hitTest(pos, c.zones);
                if (hit != DockArea::None) {
                    c.hovered = hit;
                    c.target.node = it.key();
                    c.target.area = hit;
                    c.target.fraction = GroupDropFraction;
                    c.preview = dropPreviewRect(rect, hit, GroupDropFraction);
                }
            }
            break;
        }
    }

    c.valid = c.hovered != DockArea::None && m_manager->dropAllowed(session, c.target);
    if (!c.valid) {
        c.hovered = DockArea::None;
        c.preview = QRect();
        c.tabIndicator = QRect();
        c.outer = false;
    }
    return c;
}

void DockAreaWidget::showOverlay(const DropCandidate &candidate)
{
    const QRect bounds = contentsRect();
    const DockOverlayStyle style = m_overlay->effectiveStyle();
    DockOverlayScene scene;
    scene.bounds = rect();

    if (candidate.zoneRect.isValid()) {
        const DropZoneLayout zones = DropZoneLayout::compute(candidate.zoneRect, style.edgeFraction,
                                                             style.zoneMargin);
        for (DockArea area : {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                              DockArea::Center}) {
            if (!candidate.zones.testFlag(area))
                continue;
            DockOverlayScene::Zone zone;
            zone.area = area;
            zone.shape = zones.polygon(area);
            zone.hovered = !candidate.outer && candidate.hovered == area
                && !candidate.tabIndicator.isValid();
            scene.zones.append(zone);
        }
    }
    if (candidate.outerZones) {
        const int band = qMin(style.outerBandWidth, qMin(bounds.width(), bounds.height()) / 4);
        // Same trapezoid construction as the group guide, just very shallow.
        DropZoneLayout outer;
        outer.bounds = bounds;
        outer.visible = bounds;
        outer.center = bounds.adjusted(band, band, -band, -band);
        for (DockArea edge : DockEdges) {
            if (!candidate.outerZones.testFlag(edge))
                continue;
            DockOverlayScene::Zone zone;
            zone.area = edge;
            zone.shape = outer.polygon(edge);
            zone.outer = true;
            zone.hovered = candidate.outer && candidate.hovered == edge;
            scene.zones.append(zone);
        }
    }
    scene.preview = candidate.preview;
    scene.tabIndicator = candidate.tabIndicator;

    m_overlay->setScene(scene);
    m_overlay->setGeometry(rect());
    m_overlay->show();
    m_overlay->raise();
}

void DockAreaWidget::hideOverlay()
{
    m_overlay->hide();
    m_overlay->setScene({});
}

void DockAreaWidget::handleDrag(QDragMoveEvent *event)
{
    const DragSession *session =
        m_manager ? m_manager->drag->sessionFor(event->mimeData()) : nullptr;
    if (!session) {
        event->ignore();
        return;
    }
    // Preview only: nothing in the layout changes until the drop.
    const DropCandidate candidate = candidateAt(event->position().toPoint(), *session);
    showOverlay(candidate);
    if (candidate.valid) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void DockAreaWidget::dragEnterEvent(QDragEnterEvent *event)
{
    // The window carried along with the drag is never its target. It keeps
    // receiving the drag, though, to notice if it is not being carried.
    if (m_manager && m_manager->drag->noteDragOver(this, event->position().toPoint())) {
        event->accept();
        return;
    }
    // What may not be dropped anywhere in here is none of this area's
    // business. Left alone, the drag goes on to a dock area this one lies
    // within: a workspace that is the content of a panel of another one.
    const DragSession *session =
        m_manager ? m_manager->drag->sessionFor(event->mimeData()) : nullptr;
    if (!session || !m_manager->containerAdmits(*session, m_containerId)) {
        event->ignore();
        return;
    }
    handleDrag(event);
    // Stay the drag target even over a spot that takes no drop, so that moves
    // keep arriving and the guide can follow the pointer.
    event->accept();
}

void DockAreaWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (m_manager && m_manager->drag->noteDragOver(this, event->position().toPoint())) {
        event->ignore();
        return;
    }
    handleDrag(event);
}

void DockAreaWidget::dragLeaveEvent(QDragLeaveEvent *)
{
    hideOverlay();
}

void DockAreaWidget::dropEvent(QDropEvent *event)
{
    hideOverlay();
    if (m_manager && m_manager->drag->noteDragOver(this, event->position().toPoint())) {
        event->ignore();
        return;
    }
    const DragSession *session =
        m_manager ? m_manager->drag->sessionFor(event->mimeData()) : nullptr;
    if (!session) {
        event->ignore();
        return;
    }
    const DropCandidate candidate = candidateAt(event->position().toPoint(), *session);
    if (candidate.valid && m_manager->drag->drop(candidate.target)) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void DockAreaWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange) {
        // The handle width comes from the style.
        m_layout->invalidate();
    }
}

// --- Split handles -----------------------------------------------------------

std::vector<int> DockAreaWidget::linkedGroup(int handleIndex, bool linked) const
{
    if (handleIndex < 0 || handleIndex >= int(m_solved.handles.size()))
        return {};
    if (linked && m_manager && m_manager->linkedSplitters)
        return SplitterCoordinator::linkedHandles(m_solved, handleIndex);
    return {handleIndex};
}

std::vector<int> DockAreaWidget::linkedGroup(const std::vector<int> &handles, bool linked) const
{
    std::vector<int> all;
    for (int handle : handles) {
        for (int index : linkedGroup(handle, linked)) {
            if (std::find(all.begin(), all.end(), index) == all.end())
                all.push_back(index);
        }
    }
    return all;
}

void DockAreaWidget::highlightHandles(const std::vector<int> &handles)
{
    m_highlighted = handles;
    for (DockSplitHandle *handle : std::as_const(m_handles))
        handle->setHovered(false);
    for (int index : handles) {
        if (index >= 0 && index < m_handles.size())
            m_handles.at(index)->setHovered(true);
    }
}

void DockAreaWidget::setHandleHover(int handleIndex, bool hovered)
{
    highlightHandles(hovered ? linkedGroup(handleIndex, true) : std::vector<int>());
}

void DockAreaWidget::beginHandleDrag(int handleIndex, bool linked)
{
    m_dragOthers.clear();
    // (One run of handles, moved by one number: which of the two lists it
    // is in makes no difference.)
    beginPartDrag({handleIndex}, {}, linked);
    if (m_manager && !m_dragGroup.empty())
        m_manager->beginResize();
}

void DockAreaWidget::moveHandleDrag(int delta)
{
    movePartDrag(QPoint(delta, delta));
}

bool DockAreaWidget::beginEdgeReopen(const QStringList &panels, DockArea side)
{
    if (!m_manager || panels.isEmpty() || !m_manager->beginReopen(panels))
        return false;
    // They are back, and the layout shows it. The handle between them and
    // what they came back beside is the one that is dragged from here on.
    const LayoutNode *group = m_tree.findPanel(panels.constFirst());
    const Qt::Orientation along = splitOrientation(side);
    const bool before = side == DockArea::Left || side == DockArea::Top;
    std::vector<int> handles;
    for (int i = 0; group && i < int(m_solved.handles.size()); ++i) {
        const SolvedHandle &handle = m_solved.handles[size_t(i)];
        const LayoutNode *split = m_tree.findNode(handle.split);
        if (handle.orientation != along || !split
            || handle.index + 1 >= int(split->children.size())) {
            continue;
        }
        if (split->children[size_t(handle.index) + (before ? 0 : 1)].id == group->id) {
            handles.push_back(i);
            break;
        }
    }
    m_dragOthers.clear();
    beginPartDrag(handles, {}, false);
    m_manager->beginResize();
    m_reopening = true;
    m_reopenBefore = before;
    const QRect rect = group ? m_solved.rects.value(group->id) : QRect();
    m_reopenExtent = along == Qt::Horizontal ? rect.width() : rect.height();
    highlightHandles(m_dragGroup);
    return true;
}

void DockAreaWidget::moveEdgeReopen(int inward)
{
    // As wide as the pointer is from the edge. Less than half of their
    // minimum, and they give way again, as with any handle pushed that far.
    const int delta = m_reopenBefore ? inward - m_reopenExtent : m_reopenExtent - inward;
    movePartDrag(QPoint(delta, delta));
}

void DockAreaWidget::endHandleDrag(bool cancel)
{
    const bool dragging = !(m_dragGroup.empty() && m_dragCrossGroup.empty())
        || !m_dragOthers.isEmpty() || m_reopening;
    if (std::exchange(m_reopening, false))
        highlightHandles({});
    std::vector<ResizeClose> closing;
    for (const SplitterCoordinator::Squeezed &gone : endPartDrag())
        closing.push_back({m_containerId, gone.node, gone.heir});
    for (const QPointer<DockAreaWidget> &other : std::as_const(m_dragOthers)) {
        if (!other)
            continue;
        for (const SplitterCoordinator::Squeezed &gone : other->endPartDrag())
            closing.push_back({other->m_containerId, gone.node, gone.heir});
    }
    m_dragOthers.clear();
    if (m_manager && dragging)
        m_manager->endResize(cancel, closing);
}

// --- One area's share of a drag ------------------------------------------------

void DockAreaWidget::beginPartDrag(const std::vector<int> &columns, const std::vector<int> &rows,
                                   bool linked)
{
    m_dragStart = m_solved;
    m_dragGroup = linkedGroup(columns, linked);
    m_dragCrossGroup = linkedGroup(rows, linked);
    m_squeezed.clear();
}

void DockAreaWidget::movePartDrag(const QPoint &delta)
{
    if (!m_manager || (m_dragGroup.empty() && m_dragCrossGroup.empty()))
        return;
    // Always computed from the layout at the start of the drag, so rounding
    // cannot accumulate while the mouse moves. The two runs of a corner drag
    // change different splits (those of the columns and those of the rows),
    // and neither axis limits the other, so each is worked out on its own.
    auto updates = SplitterCoordinator::moveHandles(m_tree, m_dragStart, m_dragGroup, delta.x());
    auto rows = SplitterCoordinator::moveHandles(m_tree, m_dragStart, m_dragCrossGroup, delta.y());
    updates.insert(updates.end(), std::make_move_iterator(rows.begin()),
                   std::make_move_iterator(rows.end()));

    // A group pushed well past its minimum size gives way, if its panels
    // say it may. It is back as soon as the pointer returns.
    const DockManagerPrivate *manager = m_manager;
    const auto mayGo = [manager](const LayoutNode &group) {
        return std::all_of(group.panels.begin(), group.panels.end(), [manager](const PanelId &id) {
            DockPanel *panel = manager->panels.value(id);
            return panel && panel->isCollapsible();
        });
    };
    m_squeezed = SplitterCoordinator::squeezed(m_tree, m_dragStart, m_dragGroup, delta.x(), mayGo);
    for (const auto &gone :
         SplitterCoordinator::squeezed(m_tree, m_dragStart, m_dragCrossGroup, delta.y(), mayGo)) {
        if (std::find(m_squeezed.begin(), m_squeezed.end(), gone) == m_squeezed.end())
            m_squeezed.push_back(gone);
    }
    // (This lays the area out anew, squeezed groups and all.)
    m_manager->setWeights(m_containerId, updates);
}

std::vector<SplitterCoordinator::Squeezed> DockAreaWidget::endPartDrag()
{
    m_dragGroup.clear();
    m_dragCrossGroup.clear();
    return std::exchange(m_squeezed, {});
}

// --- Corners -----------------------------------------------------------------

void DockAreaWidget::setCornerHover(int cornerIndex, bool hovered)
{
    for (const QPointer<DockAreaWidget> &other : std::as_const(m_hoverOthers)) {
        if (other)
            other->highlightHandles({});
    }
    m_hoverOthers.clear();

    std::vector<int> own;
    if (hovered && cornerIndex >= 0 && cornerIndex < int(m_corners.size())) {
        for (const CornerPart &part : m_corners[size_t(cornerIndex)].parts) {
            if (!part.area)
                continue;
            std::vector<int> handles = part.area->linkedGroup(part.columns, true);
            const std::vector<int> rows = part.area->linkedGroup(part.rows, true);
            handles.insert(handles.end(), rows.begin(), rows.end());
            if (part.area == this) {
                own = handles;
            } else {
                part.area->highlightHandles(handles);
                m_hoverOthers.append(part.area);
            }
        }
    }
    highlightHandles(own);
}

void DockAreaWidget::beginCornerDrag(int cornerIndex, bool linked)
{
    (void)endPartDrag();
    m_dragOthers.clear();
    if (cornerIndex < 0 || cornerIndex >= int(m_corners.size()))
        return;
    const Corner corner = m_corners[size_t(cornerIndex)];
    for (const CornerPart &part : corner.parts) {
        if (!part.area)
            continue;
        part.area->beginPartDrag(part.columns, part.rows, linked);
        if (part.area != this)
            m_dragOthers.append(part.area);
    }
    if (m_manager)
        m_manager->beginResize();
}

void DockAreaWidget::moveCornerDrag(const QPoint &delta)
{
    movePartDrag(delta);
    for (const QPointer<DockAreaWidget> &other : std::as_const(m_dragOthers)) {
        if (other)
            other->movePartDrag(delta);
    }
}

} // namespace QFlexDock
