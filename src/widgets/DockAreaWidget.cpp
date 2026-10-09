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

QList<DockSplitCorner *> DockAreaWidget::visibleCorners() const
{
    return m_cornerWidgets.mid(0, qsizetype(m_corners.size()));
}

int DockAreaWidget::handleWidth() const
{
    return m_manager ? m_manager->handleWidth(this) : 4;
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
}

void DockAreaWidget::placeWidgets()
{
    const QRect bounds = contentsRect();
    const LayoutNode *maximized = maximizedNode();
    if (maximized) {
        // Display state only: one group fills the area, the tree is untouched.
        m_solved = SolvedLayout{};
        m_solved.handleWidth = handleWidth();
        m_solved.rects.insert(maximized->id, bounds);
        m_solved.limits.insert(maximized->id, limitsProvider()(*maximized));
    } else {
        m_solved = LayoutSolver::solve(m_tree, bounds, handleWidth(), limitsProvider());
    }

    for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
        DockTabGroup *g = it.value();
        const bool shown = !maximized || maximized->id == it.key();
        if (shown)
            g->setGeometry(m_solved.rects.value(it.key()));
        g->setVisible(shown);
    }

    const int needed = int(m_solved.handles.size());
    while (m_handles.size() < needed)
        m_handles.append(new DockSplitHandle(this));
    for (int i = 0; i < m_handles.size(); ++i) {
        DockSplitHandle *handle = m_handles.at(i);
        if (i < needed) {
            const SolvedHandle &solved = m_solved.handles[size_t(i)];
            handle->configure(i, solved.orientation, solved.rect);
            handle->show();
            handle->raise();
        } else {
            handle->hide();
        }
    }

    // Corners lie on top of the handles that meet in them.
    m_corners.clear();
    if (m_manager && m_manager->cornerResize)
        m_corners = SplitterCoordinator::corners(m_solved);
    const int corners = int(m_corners.size());
    while (m_cornerWidgets.size() < corners)
        m_cornerWidgets.append(new DockSplitCorner(this));
    for (int i = 0; i < m_cornerWidgets.size(); ++i) {
        DockSplitCorner *corner = m_cornerWidgets.at(i);
        if (i < corners) {
            corner->configure(i, m_corners[size_t(i)].rect);
            corner->show();
            corner->raise();
        } else {
            corner->hide();
        }
    }

    m_overlay->setGeometry(rect());
    if (m_overlay->isVisible())
        m_overlay->raise();
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
    handleDrag(event);
    // Stay the drag target even over a spot that takes no drop, so that moves
    // keep arriving and the guide can follow the pointer.
    if (m_manager && m_manager->drag->sessionFor(event->mimeData()))
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
    m_dragStart = m_solved;
    m_dragGroup = linkedGroup(handleIndex, linked);
    m_dragCrossGroup.clear();
    if (m_manager && !m_dragGroup.empty())
        m_manager->beginResize();
}

void DockAreaWidget::moveHandleDrag(int delta)
{
    if (!m_manager || m_dragGroup.empty())
        return;
    // Always computed from the layout at the start of the drag, so rounding
    // cannot accumulate while the mouse moves.
    m_manager->setWeights(m_containerId,
                          SplitterCoordinator::moveHandles(m_tree, m_dragStart, m_dragGroup, delta));
}

void DockAreaWidget::endHandleDrag(bool cancel)
{
    if (m_manager && !(m_dragGroup.empty() && m_dragCrossGroup.empty()))
        m_manager->endResize(cancel);
    m_dragGroup.clear();
    m_dragCrossGroup.clear();
}

// --- Corners -----------------------------------------------------------------

void DockAreaWidget::setCornerHover(int cornerIndex, bool hovered)
{
    std::vector<int> handles;
    if (hovered && cornerIndex >= 0 && cornerIndex < int(m_corners.size())) {
        const SplitterCoordinator::Corner &corner = m_corners[size_t(cornerIndex)];
        handles = linkedGroup(corner.columns, true);
        const std::vector<int> rows = linkedGroup(corner.rows, true);
        handles.insert(handles.end(), rows.begin(), rows.end());
    }
    highlightHandles(handles);
}

void DockAreaWidget::beginCornerDrag(int cornerIndex, bool linked)
{
    m_dragGroup.clear();
    m_dragCrossGroup.clear();
    if (cornerIndex < 0 || cornerIndex >= int(m_corners.size()))
        return;
    const SplitterCoordinator::Corner &corner = m_corners[size_t(cornerIndex)];
    m_dragStart = m_solved;
    m_dragGroup = linkedGroup(corner.columns, linked);
    m_dragCrossGroup = linkedGroup(corner.rows, linked);
    if (m_manager && !(m_dragGroup.empty() && m_dragCrossGroup.empty()))
        m_manager->beginResize();
}

void DockAreaWidget::moveCornerDrag(const QPoint &delta)
{
    if (!m_manager || (m_dragGroup.empty() && m_dragCrossGroup.empty()))
        return;
    // The two runs change different splits (those of the columns and those of
    // the rows), and neither axis limits the other, so each is worked out on
    // its own from the layout at the start of the drag.
    auto updates = SplitterCoordinator::moveHandles(m_tree, m_dragStart, m_dragGroup, delta.x());
    auto rows = SplitterCoordinator::moveHandles(m_tree, m_dragStart, m_dragCrossGroup, delta.y());
    updates.insert(updates.end(), std::make_move_iterator(rows.begin()),
                   std::make_move_iterator(rows.end()));
    m_manager->setWeights(m_containerId, updates);
}

} // namespace QFlexDock
