// SPDX-License-Identifier: MIT
#include "widgets/DockAreaWidget.h"

#include "core/DockDragController.h"
#include "core/DropZones.h"
#include "widgets/DockColumn.h"
#include "widgets/DockDropOverlay.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockSplitHandle.h"
#include "widgets/DockTabBar.h"
#include "widgets/DockTabGroup.h"

#include <QtGui/QDragEnterEvent>
#include <QtGui/QWindow>
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
    for (DockColumnBar *bar : std::as_const(m_bars))
        bar->detachFromManager();
    for (DockIconStrip *strip : std::as_const(m_strips))
        strip->detachFromManager();
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
    return limitsProvider(m_columns);
}

// The limits of a tab group or of the strip of an iconified column, and with
// them the bar above a column: the node at its top makes room for that.
LimitsProvider DockAreaWidget::limitsProvider(const std::vector<Column> &columns) const
{
    QSet<NodeId> tops;
    for (const Column &column : columns)
        tops.insert(column.top);
    const int bar = tops.isEmpty() || !m_manager ? 0 : m_manager->columnBarHeight();
    return [this, tops, bar](const LayoutNode &leaf) {
        SizeLimits limits;
        if (leaf.iconified) {
            if (const DockIconStrip *strip = m_strips.value(leaf.id))
                limits = strip->sizeLimits();
        } else if (const DockTabGroup *g = m_groups.value(leaf.id)) {
            limits = g->sizeLimits();
        }
        if (tops.contains(leaf.id)) {
            limits.min.rheight() += bar;
            if (limits.max.height() < UnboundedSize)
                limits.max.rheight() += bar;
        }
        return limits;
    };
}

const LayoutNode *DockAreaWidget::maximizedNode() const
{
    const LayoutNode *node = m_maximized.isEmpty() ? nullptr : m_tree.findPanel(m_maximized);
    // (Not what is shown as a button of a strip.)
    return node && !m_iconifiedOf.contains(node->id) ? node : nullptr;
}

QSize DockAreaWidget::layoutMinimumSize() const
{
    if (const LayoutNode *node = maximizedNode())
        return limitsProvider({})(*node).min;
    if (const LayoutNode *root = m_tree.root()) {
        QSize least =
            LayoutSolver::limits(*root, handleWidth(), limitsProvider(columnsOf(m_tree))).min;
        if (flyoutSharesTheArea()) {
            const QSize out = m_groups.value(m_flyout)->sizeLimits().min;
            least = QSize(least.width() + out.width(), qMax(least.height(), out.height()));
        }
        return least;
    }
    return QSize(0, 0);
}

void DockAreaWidget::setLayoutState(const ContainerState &container)
{
    m_tree = container.tree;
    m_maximized = container.maximized;

    // The tab groups of an iconified column are not shown as groups: their
    // column is a strip of buttons, and at most one of them is out beside it.
    QHash<NodeId, NodeId> iconifiedOf;
    std::vector<const LayoutNode *> iconified;
    const auto note = [&](const auto &self, const LayoutNode &node, NodeId column) -> void {
        if (column.isNull() && node.iconified) {
            column = node.id;
            iconified.push_back(&node);
        }
        if (node.isTabs() && !column.isNull())
            iconifiedOf.insert(node.id, column);
        for (const auto &child : node.children)
            self(self, child, column);
    };
    if (const LayoutNode *root = m_tree.root())
        note(note, *root, NodeId());
    for (auto it = iconifiedOf.cbegin(); it != iconifiedOf.cend(); ++it) {
        const DockTabGroup *g = m_groups.value(it.key());
        if (g && g->isVisible() && !m_iconifiedOf.contains(it.key()))
            m_expandedSizes.insert(it.key(), g->size());
    }
    m_expandedSizes.removeIf([this](const auto &entry) { return !m_tree.findNode(entry.key()); });
    m_iconifiedOf = iconifiedOf;
    // A group that is out when its column is expanded is simply a group again.
    if (!m_flyout.isNull() && !m_iconifiedOf.contains(m_flyout)) {
        if (DockTabGroup *g = m_groups.value(m_flyout))
            g->setFlyout(false);
        m_flyout = {};
    }

    if (!m_tree.root() || !m_tree.root()->iconified)
        m_stripWidth = 0;

    QSet<NodeId> alive;
    for (const LayoutNode *node : m_tree.tabNodes()) {
        if (m_iconifiedOf.contains(node->id) && node->id != m_flyout)
            continue;
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

    QSet<NodeId> strips;
    for (const LayoutNode *node : iconified) {
        strips.insert(node->id);
        DockIconStrip *&strip = m_strips[node->id];
        if (!strip)
            strip = new DockIconStrip(m_manager, this);
        strip->setColumn(*node);
        strip->setShown(m_flyout);
    }
    for (auto it = m_strips.begin(); it != m_strips.end();) {
        if (strips.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->retire();
        it = m_strips.erase(it);
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
    for (DockIconStrip *strip : std::as_const(m_strips))
        strip->refreshAppearance();
    for (DockColumnBar *bar : std::as_const(m_bars))
        bar->refreshAppearance();
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
    QRect bounds = contentsRect();
    // A strip that is all there is keeps its width at the side, and leaves
    // the rest to the tab group that is out beside it.
    if (flyoutSharesTheArea()) {
        const SizeLimits strip = m_strips.value(m_tree.root()->id)->sizeLimits();
        const int room = qMax(1, bounds.width() - m_groups.value(m_flyout)->sizeLimits().min.width());
        bounds.setWidth(qMin(qBound(strip.min.width(), m_stripWidth, strip.max.width()), room));
    }
    const LayoutNode *maximized = maximizedNode();
    // What a drag has squeezed out is shown as gone already, its room with
    // the node across the handle. The tree itself changes when the drag ends.
    const bool squeezing = !maximized && !m_squeezed.empty();
    // Bars that move together also stay together.
    const bool linesKept = m_manager && m_manager->linkedSplitters;
    LayoutTree shown;
    if (maximized) {
        // Display state only: one group fills the area, the tree is untouched.
        m_columns.clear();
        m_solved = SolvedLayout{};
        m_solved.handleWidth = handleWidth();
        m_solved.rects.insert(maximized->id, bounds);
        m_solved.limits.insert(maximized->id, limitsProvider()(*maximized));
    } else if (squeezing) {
        shown = m_tree;
        for (const SplitterCoordinator::Squeezed &gone : m_squeezed)
            (void)shown.takeNode(gone.node, gone.heir);
        m_columns = columnsOf(shown);
        m_solved = LayoutSolver::solve(shown, bounds, handleWidth(), limitsProvider(), linesKept);
    } else {
        m_columns = columnsOf(m_tree);
        m_solved = LayoutSolver::solve(m_tree, bounds, handleWidth(), limitsProvider(), linesKept);
    }

    for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
        if (it.key() == m_flyout)
            continue; // beside its strip: see placeFlyout()
        DockTabGroup *g = it.value();
        const bool visible = maximized ? maximized->id == it.key() : m_solved.rects.contains(it.key());
        if (visible)
            g->setGeometry(nodeRect(it.key()));
        g->setVisible(visible);
    }
    placeColumns();

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
    placeFlyout();

    m_overlay->setGeometry(rect());
    if (m_overlay->isVisible())
        m_overlay->raise();
}

// --- Columns -----------------------------------------------------------------

QStringList DockAreaWidget::panelsOf(NodeId id) const
{
    QStringList panels;
    const auto collect = [&panels](const auto &self, const LayoutNode &node) -> void {
        if (node.isTabs())
            panels += node.panels;
        for (const auto &child : node.children)
            self(self, child);
    };
    if (const LayoutNode *node = m_tree.findNode(id))
        collect(collect, *node);
    return panels;
}

QList<DockColumnBar *> DockAreaWidget::columnBars() const
{
    QList<DockColumnBar *> bars;
    for (const Column &column : m_columns) {
        if (DockColumnBar *bar = m_bars.value(column.node))
            bars.append(bar);
    }
    return bars;
}

// The columns of `tree` that get a bar: those the user may move.
std::vector<DockAreaWidget::Column> DockAreaWidget::columnsOf(const LayoutTree &tree) const
{
    std::vector<Column> columns;
    if (!m_manager || !m_manager->columnDockingFor(m_containerId))
        return columns;
    for (const LayoutNode *group : tree.tabNodes()) {
        const LayoutNode *column = tree.columnOf(group->id);
        const bool known = !column
            || std::any_of(columns.begin(), columns.end(),
                           [column](const Column &c) { return c.node == column->id; });
        if (known)
            continue;
        QStringList panels;
        const auto collect = [&panels](const auto &self, const LayoutNode &node) -> void {
            if (node.isTabs())
                panels += node.panels;
            for (const auto &child : node.children)
                self(self, child);
        };
        collect(collect, *column);
        const DockManagerPrivate *manager = m_manager;
        const bool movable = std::all_of(panels.cbegin(), panels.cend(), [manager](const PanelId &id) {
            return manager->userMay(id, DockFeature::Movable);
        });
        // (The first tab group found in a column is the one at its top.)
        if (movable)
            columns.push_back({column->id, column->iconified ? column->id : group->id,
                               column->iconified});
    }
    return columns;
}

QRect DockAreaWidget::nodeRect(NodeId node) const
{
    QRect rect = m_solved.rects.value(node);
    const bool below = std::any_of(m_columns.begin(), m_columns.end(),
                                   [node](const Column &column) { return column.top == node; });
    if (below && m_manager)
        rect.adjust(0, qMin(m_manager->columnBarHeight(), rect.height()), 0, 0);
    return rect;
}

// The strips of iconified columns, and the bars above all columns.
void DockAreaWidget::placeColumns()
{
    const bool hidden = maximizedNode() != nullptr;
    for (auto it = m_strips.cbegin(); it != m_strips.cend(); ++it) {
        const bool shown = !hidden && m_solved.rects.contains(it.key());
        if (shown)
            it.value()->setGeometry(nodeRect(it.key()));
        it.value()->setVisible(shown);
    }

    // A window that is one column has the bar of that column for a title bar.
    const auto *floating = qobject_cast<const DockFloatingWindow *>(window());
    const bool standsForWindow = floating && floating->area() == this
        && floating->isMovedByHeaders() && m_tree.root() && m_columns.size() == 1 && m_columns.front().node == m_tree.root()->id;
    const int height = m_manager ? m_manager->columnBarHeight() : 0;
    QSet<NodeId> wanted;
    for (const Column &column : m_columns) {
        const QRect rect = m_solved.rects.value(column.node);
        if (!rect.isValid())
            continue;
        wanted.insert(column.node);
        DockColumnBar *&bar = m_bars[column.node];
        if (!bar)
            bar = new DockColumnBar(m_manager, this);
        // The mark on its button points to the side the column is on while
        // the column is open, and away from it while it is iconified.
        const bool onTheRight = rect.center().x() > contentsRect().center().x();
        bar->configure(column.node, column.iconified, onTheRight != column.iconified,
                       standsForWindow);
        bar->setGeometry(rect.x(), rect.y(), rect.width(), qMin(height, rect.height()));
        bar->show();
    }
    for (auto it = m_bars.begin(); it != m_bars.end();) {
        if (wanted.contains(it.key())) {
            ++it;
            continue;
        }
        // (Not deleted on the spot: it may be what the user just clicked.)
        it.value()->hide();
        it.value()->deleteLater();
        it = m_bars.erase(it);
    }
}

// The tab group that is out lies beside the strip of its column, on the side
// with more room, over whatever is there.
void DockAreaWidget::placeFlyout()
{
    DockTabGroup *out = m_flyout.isNull() ? nullptr : m_groups.value(m_flyout);
    if (!out)
        return;
    const DockIconStrip *strip = m_strips.value(m_iconifiedOf.value(m_flyout));
    if (!strip || strip->isHidden() || maximizedNode()) {
        out->hide();
        return;
    }
    const QRect bounds = contentsRect();
    const QRect beside = strip->geometry();
    if (flyoutSharesTheArea()) {
        out->setFlyout(true, DockArea::Right);
        out->setGeometry(QRect(QPoint(beside.right() + 1, bounds.top()), bounds.bottomRight()));
        out->show();
        out->raise();
        return;
    }
    const QRect block(strip->mapTo(this, strip->blockRect(m_flyout).topLeft()),
                      strip->blockRect(m_flyout).size());
    const int roomLeft = beside.left() - bounds.left();
    const int roomRight = bounds.right() - beside.right();
    const bool onTheLeft = roomLeft >= roomRight;
    out->setFlyout(true, onTheLeft ? DockArea::Left : DockArea::Right);

    const QSize size =
        flyoutSize().boundedTo(QSize(qMax(onTheLeft ? roomLeft : roomRight, 1), bounds.height()));
    const int x = onTheLeft ? beside.left() - size.width() : beside.right() + 1;
    const int y = qBound(bounds.top(), block.top(), bounds.bottom() + 1 - size.height());
    out->setGeometry(x, y, size.width(), size.height());
    out->show();
    out->raise();
}

bool DockAreaWidget::flyoutSharesTheArea() const
{
    const LayoutNode *root = m_tree.root();
    return root && root->iconified && !m_flyout.isNull() && m_groups.contains(m_flyout)
        && m_strips.contains(root->id) && !maximizedNode();
}

// As large as its group was before the column was iconified, or as it likes.
QSize DockAreaWidget::flyoutSize() const
{
    const DockTabGroup *out = m_groups.value(m_flyout);
    if (!out)
        return {};
    const SizeLimits limits = out->sizeLimits();
    return m_expandedSizes.value(m_flyout, out->preferredSize())
        .expandedTo(limits.min).boundedTo(limits.max);
}

void DockAreaWidget::showFlyout(NodeId node)
{
    const LayoutNode *group = m_iconifiedOf.contains(node) ? m_tree.findNode(node) : nullptr;
    // Nothing comes out for a panel that is there in its small form.
    const DockPanel *current = group && m_manager ? m_manager->panels.value(group->active) : nullptr;
    if (!current || current->compactWidget())
        node = {};
    if (node == m_flyout)
        return;

    if (DockTabGroup *out = m_flyout.isNull() ? nullptr : m_groups.take(m_flyout)) {
        // The layout has not changed, so nobody else sees to its content.
        if (m_manager) {
            for (const PanelId &id : out->panelIds()) {
                if (DockPanel *panel = m_manager->panels.value(id))
                    m_manager->parkIfHostedBy(panel, out->contentHost());
            }
        }
        out->releaseTitleActions();
        out->detachFromManager(); // it holds nothing of the manager's any more
        out->hide();
        out->deleteLater();
    }
    // A strip that is all there is, is as wide as it is now for as long as
    // something is out beside it, and after.
    const DockIconStrip *alone = m_tree.root() ? m_strips.value(m_tree.root()->id) : nullptr;
    if (alone && m_flyout.isNull() && !alone->isHidden() && alone->width() > 0)
        m_stripWidth = alone->width();
    m_flyout = node;
    if (!node.isNull()) {
        auto *out = new DockTabGroup(m_manager, this);
        out->setFlyout(true);
        m_groups.insert(node, out);
        out->setNode(*group, false);
    }
    for (DockIconStrip *strip : std::as_const(m_strips))
        strip->setShown(m_flyout);
    relayout();
    m_layout->invalidate();
    // A floating window makes the room, and takes it back.
    if (auto *floating = qobject_cast<DockFloatingWindow *>(window());
        floating && floating->area() == this) {
        floating->fitIconified();
    }
    if (m_manager)
        m_manager->refreshActiveMarks();
}

QWidget *DockAreaWidget::dragSource(const DragSession &session) const
{
    if (const DockIconStrip *strip = m_strips.value(m_iconifiedOf.value(session.sourceNode));
        strip && session.sourceNode != m_flyout) {
        if (QWidget *button = session.wholeGroup ? nullptr : strip->button(session.primary))
            return button;
        return strip->block(session.sourceNode);
    }
    if (DockIconStrip *strip = m_strips.value(session.sourceNode))
        return strip;
    return m_groups.value(session.sourceNode);
}

QSize DockAreaWidget::iconifiedSizeHint() const
{
    const LayoutNode *root = m_tree.root();
    const DockIconStrip *strip = root && root->iconified ? m_strips.value(root->id) : nullptr;
    if (!strip)
        return {};
    QSize size = strip->preferredSize();
    if (m_stripWidth > 0)
        size.setWidth(qBound(strip->sizeLimits().min.width(), m_stripWidth, size.width()));
    if (m_manager && !columnsOf(m_tree).empty())
        size.rheight() += m_manager->columnBarHeight();
    if (flyoutSharesTheArea()) {
        const QSize out = flyoutSize();
        size = QSize(size.width() + out.width(), qMax(size.height(), out.height()));
    }
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

void DockAreaWidget::refreshPanel(const PanelId &panel)
{
    const LayoutNode *node = m_tree.findPanel(panel);
    if (!node)
        return;
    if (DockTabGroup *g = m_groups.value(node->id))
        g->refreshPanel(panel);
    if (DockIconStrip *strip = m_strips.value(m_iconifiedOf.value(node->id)))
        strip->refreshPanel(panel);
}

// A button of a strip: its panel's group comes out, or goes back if that
// panel is what is out.
void DockAreaWidget::iconButtonClicked(NodeId group, const PanelId &panel)
{
    if (!m_manager)
        return;
    const LayoutNode *node = m_tree.findNode(group);
    if (m_flyout == group && node && node->active == panel)
        showFlyout({});
    else
        (void)m_manager->activate(panel, Activation::Focus);
}

// See DockTabGroup::moveWindowInstead(), which this is the whole of.
bool DockAreaWidget::moveWindowInstead(qsizetype draggedPanels, bool byHeader)
{
    const auto *floating = qobject_cast<const DockFloatingWindow *>(window());
    if (!m_manager || !floating || floating->area() != this || !floating->isMovedByHeaders()
        || !floating->windowHandle() || m_tree.panels().size() != draggedPanels) {
        return false;
    }
    const DockDragController *drag = m_manager->drag;
    const bool leftToTheHeader = byHeader && drag->movesCarriedWindows();
    if (!leftToTheHeader && (drag->carriesWindows() || m_manager->floatOnOutsideDrop))
        return false;
    return floating->windowHandle()->startSystemMove();
}

void DockAreaWidget::startIconDrag(const PanelId &panel, bool wholeGroup, QWidget *pictured)
{
    if (!m_manager)
        return;
    const QStringList moved = wholeGroup ? m_manager->groupPanels(panel) : QStringList{panel};
    for (const PanelId &id : moved) {
        if (!m_manager->userMay(id, DockFeature::Movable))
            return;
    }
    if (moved.isEmpty() || moveWindowInstead(moved.size(), wholeGroup))
        return;
    const QPixmap picture = pictured ? pictured->grab() : QPixmap();
    if (wholeGroup)
        m_manager->drag->requestGroupDrag(panel, picture);
    else
        m_manager->drag->requestPanelDrag(panel, picture);
}

void DockAreaWidget::startColumnDrag(NodeId column)
{
    if (!m_manager)
        return;
    const QStringList moved = panelsOf(column);
    for (const PanelId &id : moved) {
        if (!m_manager->userMay(id, DockFeature::Movable))
            return;
    }
    if (moved.isEmpty() || moveWindowInstead(moved.size(), true))
        return;
    DockColumnBar *bar = m_bars.value(column);
    m_manager->drag->requestColumnDrag(moved.constFirst(), bar ? bar->grab() : QPixmap());
}

void DockAreaWidget::toggleColumnIconified(NodeId column)
{
    const QStringList panels = panelsOf(column);
    const LayoutNode *node = m_tree.findNode(column);
    if (m_manager && node && !panels.isEmpty())
        (void)m_manager->setColumnIconified(panels.constFirst(), !node->iconified);
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

// How high the header of a tab group would be here, where there is none to
// ask: a row of tabs, as this area's style has them.
int DockAreaWidget::headerHeight() const
{
    if (!m_headerProbe) {
        m_headerProbe = new DockTabBar(const_cast<DockAreaWidget *>(this));
        m_headerProbe->hide();
        m_headerProbe->addTab(QStringLiteral("X"));
    }
    m_headerProbe->ensurePolished();
    return m_headerProbe->sizeHint().height();
}

DockAreaWidget *DockAreaWidget::areaAround(const DragSession &session) const
{
    if (!m_manager || m_overlay->effectiveStyle().guide != DockGuide::Buttons)
        return nullptr;
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        auto *around = qobject_cast<DockAreaWidget *>(w);
        if (!around)
            continue;
        const bool offers = around->m_manager == m_manager
            && around->m_overlay->effectiveStyle().guide == DockGuide::Buttons
            && m_manager->containerAdmits(session, around->m_containerId);
        return offers ? around : nullptr;
    }
    return nullptr;
}

DropCandidate DockAreaWidget::candidateAt(const QPoint &pos, const DragSession &session,
                                          const DropButtonLayout *inside) const
{
    DropCandidate c;
    c.target.container = m_containerId;
    if (!m_manager)
        return c;

    const QRect bounds = contentsRect();
    const DockOverlayStyle style = m_overlay->effectiveStyle();
    const bool buttons = style.guide == DockGuide::Buttons;
    const DockAreas wholeAreas = m_manager->allowedDropAreas(session, m_containerId, NodeId{});
    // With an area around this one offering its buttons too, the border of
    // this one is not a target: beside it, in the area around, is.
    const bool shared = buttons && !inside && areaAround(session);
    const int rings = shared || inside ? 2 : 1;

    if (m_tree.isEmpty()) {
        // Nothing here yet: the whole area is one target. Where the middle
        // of a tab group takes no drop, it is taken where the tabs of the
        // first group will be: along the top, as high as a row of tabs.
        c.zoneRect = bounds;
        c.zones = wholeAreas & DockAreas(DockArea::Center);
        if (buttons) {
            c.buttons = DropButtonLayout::compute(bounds, bounds, style.buttonSize, style.zoneGap,
                                                  rings);
        }
        QRect row = bounds;
        const bool byHeader = !buttons && !m_manager->centerDropFor(m_containerId);
        if (byHeader)
            row.setHeight(qMin(bounds.height(), headerHeight()));
        const bool hit = buttons ? c.buttons.hitTest(pos, c.zones) == DockArea::Center
                                 : row.contains(pos);
        if (c.zones && hit) {
            c.hovered = DockArea::Center;
            c.target.area = DockArea::Center;
            c.preview = bounds;
            c.aimedAt = bounds;
            if (byHeader) {
                c.header = row;
                c.byHeader = true;
            }
        }
    } else {
        const LayoutNode *maximized = maximizedNode();
        const int band = maximized ? 0 : qMin(style.outerBandWidth,
                                              qMin(bounds.width(), bounds.height()) / 4);
        if (band > 0 && !shared)
            c.outerZones = wholeAreas & EdgeDockAreas;

        const auto overTabs = [this, &pos](const DockTabGroup *g) {
            const DockTabBar *bar = g->tabBar();
            const QRect region = bar->tabDropRegion();
            return QRect(bar->mapTo(this, region.topLeft()), region.size()).contains(pos);
        };
        DockArea outerHit = DockArea::None;
        if (buttons) {
            // A button in the middle of each border.
            const int around = (style.zoneGap + 1) / 2;
            for (DockArea edge : DockEdges) {
                const QRect button =
                    outerButtonRect(bounds, edge, style.buttonSize, style.zoneMargin);
                if (c.outerZones.testFlag(edge)
                    && button.adjusted(-around, -around, around, around).contains(pos)) {
                    outerHit = edge;
                }
            }
        } else {
            // Tabs along the border lie inside the outer band. Over them the
            // band shrinks to a thin strip at the very edge, so that a tab
            // can still be dropped between tabs. The empty rest of a title
            // row belongs to the band like everything else there.
            // (So does all of a title row where that row is the one way to
            // become a tab: the middle of the groups takes no drop.)
            const bool headersTakeTabs = !m_manager->centerDropFor(m_containerId);
            int hitBand = band;
            for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
                if (maximized && maximized->id != it.key())
                    continue;
                const QWidget *row = it.value()->titleBar();
                const bool onRow = headersTakeTabs && row->isVisible()
                    && QRect(row->mapTo(this, QPoint(0, 0)), row->size()).contains(pos);
                if (overTabs(it.value()) || onRow)
                    hitBand = qMin(band, OuterStripOnTitle);
            }
            outerHit = outerBandAt(bounds, pos, hitBand);
        }
        if (outerHit != DockArea::None && c.outerZones.testFlag(outerHit)) {
            c.outer = true;
            c.hovered = outerHit;
            c.target.area = outerHit;
            c.target.fraction = OuterDropFraction;
            c.preview = dropPreviewRect(bounds, outerHit, OuterDropFraction);
            c.aimedAt = bounds;
        }

        // The tab group under the pointer. Its guide is shown even while an
        // outer band is hovered, so both kinds of target stay visible.
        NodeId under;
        // What is out beside a strip lies over everything else.
        const DockTabGroup *out = maximized || m_flyout.isNull() ? nullptr : m_groups.value(m_flyout);
        const bool onFlyout = out && out->isVisible() && out->geometry().contains(pos);
        if (onFlyout)
            under = m_flyout;
        // In a strip: the buttons of one tab group, or the room below them all.
        bool belowButtons = false;
        for (auto it = m_strips.cbegin(); under.isNull() && !maximized && it != m_strips.cend();
             ++it) {
            const DockIconStrip *strip = it.value();
            if (!strip->isHidden() && m_solved.rects.value(it.key()).contains(pos))
                under = strip->groupAt(strip->mapFrom(this, pos), &belowButtons);
        }
        for (auto it = m_groups.cbegin(); under.isNull() && it != m_groups.cend(); ++it) {
            if ((maximized && maximized->id != it.key()) || it.key() == m_flyout)
                continue;
            if (m_solved.rects.value(it.key()).contains(pos)) {
                under = it.key();
                break;
            }
        }
        // A cross of buttons may reach beyond its group: on one of them, the
        // pointer has not left for the group underneath.
        if (buttons && !inside && !m_guideNode.isNull() && m_groups.contains(m_guideNode)
            && (!maximized || maximized->id == m_guideNode)
            && m_guideButtons.contains(pos, m_guideRings)) {
            under = m_guideNode;
        }

        if (!under.isNull()) {
            const DockIconStrip *strip =
                onFlyout ? nullptr : m_strips.value(m_iconifiedOf.value(under));
            const DockTabGroup *g = strip ? nullptr : m_groups.value(under);
            const QRect rect = strip ? QRect(strip->mapTo(this, strip->blockRect(under).topLeft()),
                                             strip->blockRect(under).size())
                                     : onFlyout ? out->geometry() : nodeRect(under);
            c.guideNode = under;
            c.zones = m_manager->allowedDropAreas(session, m_containerId, under);
            // Where tab groups are in columns, the sides of a group are
            // those of its column: what is dropped there goes beside all of
            // it. So it does for an iconified column anywhere, which is not
            // something to be split.
            const bool inColumn = m_manager->columnDockingFor(m_containerId)
                || m_iconifiedOf.contains(under);
            const LayoutNode *column = inColumn ? m_tree.columnOf(under) : nullptr;
            const DockAreas sides = DockAreas(DockArea::Left) | DockArea::Right;
            if (column && column->id != under) {
                c.zones = (c.zones & ~sides)
                    | (m_manager->allowedDropAreas(session, m_containerId, column->id) & sides);
            }
            // What is out is there to become a tab of, nothing else.
            if (onFlyout)
                c.zones &= DockAreas(DockArea::Center);
            if (!buttons) {
                // The guide stays clear of the outer bands so the two never overlap.
                c.zoneRect = c.outerZones
                    ? rect.intersected(bounds.adjusted(band, band, -band, -band)) : rect;
            } else if (inside) {
                // Around the cross of the area inside: beside the group that
                // holds it, which nothing can become a tab of from out here.
                c.zoneRect = rect;
                c.buttons = *inside;
                c.ring = 1;
                c.zones &= EdgeDockAreas;
            } else {
                c.zoneRect = rect;
                c.buttons = DropButtonLayout::compute(rect, bounds, style.buttonSize,
                                                      style.zoneGap, rings);
            }

            if (!c.outer) {
                // Onto the tabs: join at a specific position, or reorder
                // within the panel's own group. Only the tabs themselves
                // count for this; elsewhere on the title row the five areas
                // apply as usual. That is, if there are any besides the
                // centre: what can only become a tab here is taken as one by
                // the whole row.
                const bool ownGroup = session.sourceContainer == m_containerId
                    && session.sourceNode == under;
                const bool reorder = ownGroup && !session.wholeGroup;
                const QWidget *title = g ? g->titleBar() : nullptr;
                const QRect header = title && title->isVisible()
                    ? QRect(title->mapTo(this, QPoint(0, 0)), title->size()) : QRect();
                const bool overHeader = header.contains(pos);
                const bool tabsInHeader = g && g->tabBar()->parentWidget() == title;
                const bool tabsOnly = !(c.zones & EdgeDockAreas);
                const bool mayJoin = !ownGroup && c.zones.testFlag(DockArea::Center);
                const bool tabDrop = g && !inside
                    && (overTabs(g) || (overHeader && tabsInHeader && tabsOnly))
                    && (reorder || mayJoin);
                // With buttons, a header that names its panel takes a tab
                // as well: the rest of the group, the buttons aside, takes
                // nothing.
                // The middle of a group may be closed to drops: then the
                // header is the one way to become a tab, a title bar that
                // names its panel as well, and the guide shows no centre.
                const bool centerDrop = m_manager->centerDropFor(m_containerId);
                const bool headerDrop =
                    (buttons || !centerDrop) && !inside && !tabDrop && overHeader && mayJoin;
                if (!centerDrop)
                    c.zones &= ~DockAreas(DockArea::Center);

                DockArea hit = DockArea::None;
                if (buttons) {
                    hit = c.buttons.hitTest(pos, c.zones, c.ring);
                } else if (belowButtons) {
                    // Below the last buttons of a strip: more of them.
                    if (c.zones.testFlag(DockArea::Bottom))
                        hit = DockArea::Bottom;
                } else if (!tabDrop) {
                    hit = DropZoneLayout::compute(c.zoneRect, style.edgeFraction, style.zoneMargin,
                                                  style.edgeExtent)
                              .hitTest(pos, c.zones);
                }
                if (hit != DockArea::None) {
                    // Beside a group that holds a dock area: beside that
                    // area, at the share an edge of a workspace takes.
                    const double fraction = inside ? OuterDropFraction : GroupDropFraction;
                    c.hovered = hit;
                    c.target.node = under;
                    c.target.area = hit;
                    c.target.fraction = fraction;
                    QRect whole = rect;
                    if (column && column->id != under && sides.testFlag(hit)) {
                        c.target.node = column->id;
                        whole = m_solved.rects.value(column->id);
                    }
                    c.preview = dropPreviewRect(whole, hit, fraction);
                    c.aimedAt = whole;
                    if (hit == DockArea::Center) {
                        c.header = header;
                        c.ownGroup = ownGroup;
                        // Where tabs show a drag as it would turn out, the
                        // tab that stays is back where it left.
                        if (reorder && g && m_manager->tabDragPreview
                            && g->draggedOut() == session.primary) {
                            c.stays = true;
                            c.tabGap = int(g->panelIds().indexOf(session.primary));
                        }
                    }
                } else if (tabDrop) {
                    DockTabBar *bar = g->tabBar();
                    const int index = g->dropIndexAt(bar->mapFrom(this, pos), &c.tabGap);
                    c.hovered = DockArea::Center;
                    c.target.node = under;
                    c.target.area = DockArea::Center;
                    c.target.tabIndex = index;
                    c.tabIndicator =
                        QRect(bar->mapTo(this, bar->insertIndicatorRect(c.tabGap).topLeft()),
                              bar->insertIndicatorRect(c.tabGap).size());
                    c.preview = reorder ? QRect() : rect;
                    c.byHeader = true;
                    c.aimedAt = rect;
                    c.header = header;
                    c.ownGroup = ownGroup;
                } else if (headerDrop) {
                    c.aimedAt = rect;
                    c.header = header;
                    c.hovered = DockArea::Center;
                    c.target.node = under;
                    c.target.area = DockArea::Center;
                    c.target.tabIndex = int(g->panelIds().size());
                    c.preview = rect;
                    c.byHeader = true;
                }
            }
        }
    }

    c.valid = c.hovered != DockArea::None && m_manager->dropAllowed(session, c.target);
    if (!c.valid) {
        c.hovered = DockArea::None;
        c.preview = QRect();
        c.tabIndicator = QRect();
        c.tabGap = -1;
        c.outer = false;
        c.byHeader = false;
        c.aimedAt = QRect();
        c.header = QRect();
        c.ownGroup = false;
        c.stays = false;
    }
    return c;
}

DropCandidate DockAreaWidget::resolveDrag(const QPoint &pos, const DragSession &session, bool show)
{
    DropCandidate candidate = candidateAt(pos, session);
    DockAreaWidget *around = areaAround(session);
    DropCandidate outside;
    if (around) {
        DropButtonLayout cross = candidate.buttons;
        cross.center = mapTo(around, cross.center);
        outside = around->candidateAt(mapTo(around, pos), session, &cross);
        // A button of this area comes first, then one of the area around
        // (which may lie over a header in here), then the headers.
        const auto pass = [](DropCandidate &c) {
            c.valid = false;
            c.hovered = DockArea::None;
            c.outer = false;
            c.byHeader = false;
            c.preview = QRect();
            c.tabIndicator = QRect();
            c.tabGap = -1;
            c.aimedAt = QRect();
            c.header = QRect();
            c.ownGroup = false;
            c.stays = false;
        };
        if (candidate.valid && !(candidate.byHeader && outside.valid))
            pass(outside);
        else if (outside.valid)
            pass(candidate);
    }
    if (show) {
        if (m_guideAround && m_guideAround != around)
            m_guideAround->hideOverlay();
        m_guideAround = around;
        showOverlay(candidate);
        if (around)
            around->showOverlay(outside);
    }
    return candidate.valid || !around ? candidate : outside;
}

void DockAreaWidget::showOverlay(const DropCandidate &candidate)
{
    const QRect bounds = contentsRect();
    const DockOverlayStyle style = m_overlay->effectiveStyle();
    const bool buttons = style.guide == DockGuide::Buttons;
    DockOverlayScene scene;
    scene.bounds = rect();

    if (candidate.zoneRect.isValid()) {
        const DropZoneLayout zones = DropZoneLayout::compute(candidate.zoneRect, style.edgeFraction,
                                                             style.zoneMargin, style.edgeExtent);
        for (DockArea area : {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                              DockArea::Center}) {
            if (!candidate.zones.testFlag(area))
                continue;
            DockOverlayScene::Zone zone;
            zone.area = area;
            zone.shape = buttons ? QPolygonF(QRectF(candidate.buttons.rect(area, candidate.ring)))
                                 : zones.polygon(area);
            zone.hovered = !candidate.outer && candidate.hovered == area
                && (candidate.tabGap < 0 || candidate.stays);
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
            zone.shape = buttons ? QPolygonF(QRectF(outerButtonRect(bounds, edge, style.buttonSize,
                                                                    style.zoneMargin)))
                                 : outer.polygon(edge);
            zone.outer = true;
            zone.hovered = candidate.outer && candidate.hovered == edge;
            scene.zones.append(zone);
        }
    }
    scene.preview = candidate.preview;
    scene.target = candidate.aimedAt;
    scene.header = candidate.header;
    scene.ownGroup = candidate.ownGroup;
    // With the tabs making room themselves, no mark is needed between them.
    const bool tabsMakeRoom = m_manager && m_manager->tabDragPreview;
    if (!tabsMakeRoom)
        scene.tabIndicator = candidate.tabIndicator;
    showDropGap(tabsMakeRoom ? candidate.target.node : NodeId(), candidate.tabGap);
    if (const DockTabGroup *g = tabsMakeRoom ? m_groups.value(candidate.target.node) : nullptr) {
        const DockTabBar *bar = g->tabBar();
        const int gap = bar->gapIndex();
        if (gap >= 0 && bar->isVisible()) {
            const QRect place = bar->tabRect(gap).intersected(bar->rect());
            scene.tabGap = QRect(bar->mapTo(this, place.topLeft()), place.size());
        }
    }

    // A cross that is another area's (ring 1) follows that area's pointer.
    const bool ownCross = buttons && candidate.ring == 0;
    m_guideNode = ownCross ? candidate.guideNode : NodeId();
    m_guideButtons = ownCross ? candidate.buttons : DropButtonLayout();
    m_guideRings = ownCross && m_guideAround ? 2 : 1;

    m_overlay->setScene(scene);
    m_overlay->setGeometry(rect());
    m_overlay->show();
    m_overlay->raise();
}

void DockAreaWidget::hideOverlay()
{
    m_overlay->hide();
    m_overlay->setScene({});
    showDropGap({}, -1);
    m_guideNode = {};
    m_guideButtons = {};
    m_guideRings = 1;
    if (DockAreaWidget *around = m_guideAround.data()) {
        m_guideAround.clear();
        around->hideOverlay();
    }
}

// Room among the tabs of `node` for what is held over them, and nowhere else.
void DockAreaWidget::showDropGap(NodeId node, int index)
{
    for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it)
        it.value()->setDropGap(it.key() == node ? index : -1);
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
    const DropCandidate candidate = resolveDrag(event->position().toPoint(), *session, true);
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
    if (m_manager && m_manager->drag->noteDragOver(this, event->position().toPoint())) {
        hideOverlay();
        event->ignore();
        return;
    }
    const DragSession *session =
        m_manager ? m_manager->drag->sessionFor(event->mimeData()) : nullptr;
    if (!session) {
        hideOverlay();
        event->ignore();
        return;
    }
    // Where it goes is worked out with everything as it was shown: tabs that
    // made room are where the pointer found them.
    const DropCandidate candidate = resolveDrag(event->position().toPoint(), *session, false);
    hideOverlay();
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
    const bool push = m_manager->splitterPush;
    auto updates =
        SplitterCoordinator::moveHandles(m_tree, m_dragStart, m_dragGroup, delta.x(), push);
    auto rows =
        SplitterCoordinator::moveHandles(m_tree, m_dragStart, m_dragCrossGroup, delta.y(), push);
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
