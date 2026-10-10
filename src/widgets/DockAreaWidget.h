// SPDX-License-Identifier: MIT
#pragma once

#include "core/DockManager_p.h"
#include "core/DropZones.h"
#include "core/LayoutSolver.h"

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

namespace QFlexDock {

class DockColumnBar;
class DockDropOverlay;
class DockEdgeHandle;
class DockIconStrip;
class DockSplitCorner;
class DockSplitHandle;
class DockTabBar;
class DockTabGroup;

/// What a drag hovering over a dock area would do, and what to show for it.
struct DropCandidate
{
    /// A drop at this position would be accepted.
    bool valid = false;
    DropTarget target;

    // Presentation only.
    /// Region the five large areas are laid over (the hovered tab group).
    QRect zoneRect;
    DockAreas zones;
    /// The tab group the guide is for; null over none.
    NodeId guideNode;
    /// With DockGuide::Buttons: the cross of buttons for that group, and
    /// which ring of it this area's buttons are (1: the cross itself is that
    /// of a dock area inside the group, see candidateAt()).
    DropButtonLayout buttons;
    int ring = 0;
    /// Enabled bands along the border of the whole dock area.
    DockAreas outerZones;
    bool outer = false;
    DockArea hovered = DockArea::None;
    QRect preview;
    QRect tabIndicator;
    /// For a drop among tabs: where among the tabs shown there it goes.
    int tabGap = -1;
    /// The header of the group takes the drop (its tabs, or its title bar),
    /// not an area of the guide.
    bool byHeader = false;
    /// All of what the drop is aimed at: a group, a column, the area.
    QRect aimedAt;
    /// For a drop that joins a group: the header of that group.
    QRect header;
    /// The drop leaves the panel in the group it comes from.
    bool ownGroup = false;
    /// It leaves it where it is, even: `tabGap` is the place it left.
    bool stays = false;
};

/// Shows one layout tree: a tab group widget per tab node, a handle per split
/// boundary, and the drop overlay on top. Used by workspaces and by floating
/// windows alike.
///
/// Geometry comes from LayoutSolver through a small QLayout subclass, so the
/// tree's minimum size propagates to the window like any other layout's.
class QFLEXDOCK_EXPORT DockAreaWidget : public QWidget
{
    Q_OBJECT

public:
    DockAreaWidget(DockManagerPrivate *manager, const QString &containerId,
                   QWidget *parent = nullptr);
    ~DockAreaWidget() override;

    [[nodiscard]] QString containerId() const { return m_containerId; }
    /// Only for a window that existed before its container did (a drag ghost).
    void setContainerId(const QString &containerId) { m_containerId = containerId; }
    void detachFromManager();

    /// Brings the widgets in line with `container`. Tab groups are matched by
    /// node id, so unaffected groups (and the content in them) are untouched.
    void setLayoutState(const ContainerState &container);
    [[nodiscard]] const LayoutTree &tree() const { return m_tree; }
    [[nodiscard]] const SolvedLayout &solved() const { return m_solved; }
    [[nodiscard]] DockTabGroup *group(NodeId node) const { return m_groups.value(node); }
    [[nodiscard]] DockTabGroup *groupOfPanel(const PanelId &panel) const;
    [[nodiscard]] QList<DockTabGroup *> groups() const { return m_groups.values(); }
    [[nodiscard]] QList<DockSplitHandle *> visibleHandles() const;
    [[nodiscard]] QList<DockSplitCorner *> visibleCorners() const;
    [[nodiscard]] QList<DockEdgeHandle *> visibleEdgeHandles() const;
    [[nodiscard]] DockDropOverlay *overlay() const { return m_overlay; }

    // --- Columns ---------------------------------------------------------------
    /// The bars above the columns shown, where the container docks in columns.
    [[nodiscard]] QList<DockColumnBar *> columnBars() const;
    [[nodiscard]] DockColumnBar *columnBar(NodeId column) const { return m_bars.value(column); }
    /// The strips of buttons that iconified columns are shown as.
    [[nodiscard]] QList<DockIconStrip *> iconStrips() const { return m_strips.values(); }
    [[nodiscard]] DockIconStrip *iconStrip(NodeId column) const { return m_strips.value(column); }
    /// Whether tab group `node` is in an iconified column.
    [[nodiscard]] bool isIconified(NodeId node) const { return m_iconifiedOf.contains(node); }
    /// The tab group of an iconified column that is out beside its strip.
    /// Which one is view state, like the panel that slid out of an auto-hide
    /// bar: the layout says nothing about it.
    [[nodiscard]] NodeId flyout() const { return m_flyout; }
    /// Brings tab group `node` out beside the strip of its column; null puts
    /// away the one that is out.
    void showFlyout(NodeId node);
    /// Where a node of the layout is shown: a tab group or a strip without
    /// the bar above it, a split with everything in it.
    [[nodiscard]] QRect nodeRect(NodeId node) const;
    /// What a drag of `session` took hold of in this area: a tab group, or
    /// the buttons of one in a strip. For picturing it.
    [[nodiscard]] QWidget *dragSource(const DragSession &session) const;
    /// The size of a window that holds an iconified column and nothing else,
    /// with the tab group that is out beside its strip, if one is; invalid
    /// if this area holds something else.
    [[nodiscard]] QSize iconifiedSizeHint() const;
    void refreshPanel(const PanelId &panel);
    // Requests of bars and strips.
    void iconButtonClicked(NodeId group, const PanelId &panel);
    void startIconDrag(const PanelId &panel, bool wholeGroup, QWidget *pictured);
    void startColumnDrag(NodeId column);
    void toggleColumnIconified(NodeId column);
    /// Whether a drag of `draggedPanels` panels is all that is in a floating
    /// window that its headers move, and has the window system move that
    /// window instead. `byHeader`: by what stands for all of it, not a tab.
    [[nodiscard]] bool moveWindowInstead(qsizetype draggedPanels, bool byHeader);

    void relayout();
    /// A dock area inside one of this area's panels laid itself out anew.
    void nestedLayoutChanged();
    /// A tab group's size limits changed.
    void contentLimitsChanged();
    void refreshAppearance();
    [[nodiscard]] int handleWidth() const;
    [[nodiscard]] int handleHoverWidth() const;
    [[nodiscard]] QSize layoutMinimumSize() const;

    // --- Drag and drop -------------------------------------------------------
    /// What a drop at `pos` would do in this area. With `inside`, the cross
    /// of buttons that a dock area inside one of this area's panels shows for
    /// the same drag (in this area's coordinates): the buttons for the group
    /// holding that area then go around that cross.
    [[nodiscard]] DropCandidate candidateAt(const QPoint &pos, const DragSession &session,
                                            const DropButtonLayout *inside = nullptr) const;
    /// With the button guide, the dock area this one lies within, if that
    /// takes what `session` drags as well: its buttons are then offered along
    /// with this area's, which leave nearly all of it free.
    [[nodiscard]] DockAreaWidget *areaAround(const DragSession &session) const;
    /// What a drop at `pos` would do, the area around this one included, and
    /// with `show` the guides for it.
    [[nodiscard]] DropCandidate resolveDrag(const QPoint &pos, const DragSession &session,
                                            bool show);
    void showOverlay(const DropCandidate &candidate);
    void hideOverlay();
    /// Preview of a tab drag: see DockTabGroup::setDropGap().
    void showDropGap(NodeId node, int index);

    // --- Split handle dragging -----------------------------------------------
    void setHandleHover(int handleIndex, bool hovered);
    /// `linked` false moves only this handle even if others are aligned.
    void beginHandleDrag(int handleIndex, bool linked);
    void moveHandleDrag(int delta);
    /// Ends a handle drag or a corner drag.
    void endHandleDrag(bool cancel);
    // A corner is dragged like the handles meeting in it, all at once.
    void setCornerHover(int cornerIndex, bool hovered);
    /// `linked` false moves only the handles that meet in the corner.
    void beginCornerDrag(int cornerIndex, bool linked);
    void moveCornerDrag(const QPoint &delta);
    // Closed panels pulled back out of the edge they went to: they are shown,
    // and the drag goes on as one of the handle beside them, `inward` pixels
    // from the edge. Ended like any handle drag.
    bool beginEdgeReopen(const QStringList &panels, DockArea side);
    void moveEdgeReopen(int inward);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    class Layout;
    /// The bars of one dock area that meet in a corner.
    struct CornerPart
    {
        QPointer<DockAreaWidget> area;
        std::vector<int> columns;
        std::vector<int> rows;
    };
    /// A place where bars meet: this area's own (the first part), and those
    /// of dock areas that lie within it and end on them.
    struct Corner
    {
        QRect rect;
        std::vector<CornerPart> parts;
    };

    /// A column that has a bar above it, and the node at its top.
    struct Column
    {
        NodeId node;
        NodeId top;
        bool iconified = false;
    };
    [[nodiscard]] std::vector<Column> columnsOf(const LayoutTree &tree) const;
    [[nodiscard]] LimitsProvider limitsProvider(const std::vector<Column> &columns) const;
    void placeColumns();
    void placeFlyout();
    /// The area is one iconified column, with a tab group of it out: there
    /// is no room beside the strip but what the area makes for it.
    [[nodiscard]] bool flyoutSharesTheArea() const;
    [[nodiscard]] QSize flyoutSize() const;
    [[nodiscard]] QStringList panelsOf(NodeId node) const;

    [[nodiscard]] int headerHeight() const;
    void handleDrag(QDragMoveEvent *event);
    [[nodiscard]] std::vector<int> linkedGroup(int handleIndex, bool linked) const;
    [[nodiscard]] std::vector<int> linkedGroup(const std::vector<int> &handles, bool linked) const;
    void highlightHandles(const std::vector<int> &handles);
    [[nodiscard]] LimitsProvider limitsProvider() const;
    [[nodiscard]] const LayoutNode *maximizedNode() const;
    void placeWidgets();
    void updateCorners();
    void updateEdgeHandles();
    [[nodiscard]] QList<DockAreaWidget *> areasWithin() const;
    [[nodiscard]] QRect heldHandleBar(const DockSplitHandle *held, const LayoutTree &shown) const;
    // One area's share of a drag: its handles along x and along y.
    void beginPartDrag(const std::vector<int> &columns, const std::vector<int> &rows, bool linked);
    void movePartDrag(const QPoint &delta);
    [[nodiscard]] std::vector<SplitterCoordinator::Squeezed> endPartDrag();

    DockManagerPrivate *m_manager;
    QString m_containerId;
    LayoutTree m_tree;
    PanelId m_maximized;
    SolvedLayout m_solved;
    QHash<NodeId, DockTabGroup *> m_groups;
    QList<DockSplitHandle *> m_handles;
    std::vector<Corner> m_corners;
    QList<DockSplitCorner *> m_cornerWidgets;
    QList<DockEdgeHandle *> m_edgeHandles;
    int m_edgeCount = 0;
    /// The columns of the layout as it is shown, with their bars.
    std::vector<Column> m_columns;
    QHash<NodeId, DockColumnBar *> m_bars;
    /// By the iconified node each stands for.
    QHash<NodeId, DockIconStrip *> m_strips;
    /// Tab groups that are in an iconified column, and that column.
    QHash<NodeId, NodeId> m_iconifiedOf;
    NodeId m_flyout;
    /// The size tab groups had before their column was iconified: what they
    /// come out with.
    QHash<NodeId, QSize> m_expandedSizes;
    /// How wide the strip is while it shares the area with what is out
    /// beside it (and was before); 0 when that is not known.
    int m_stripWidth = 0;
    DockDropOverlay *m_overlay;
    Layout *m_layout;
    /// A row of tabs that is never shown, for its height.
    mutable QPointer<DockTabBar> m_headerProbe;
    // The button guide as it is shown: the cross stays with its group while
    // the pointer is on one of its buttons, wherever those lie.
    NodeId m_guideNode;
    DropButtonLayout m_guideButtons;
    int m_guideRings = 1;
    /// The area around this one whose guide this one put up.
    QPointer<DockAreaWidget> m_guideAround;

    bool m_placing = false;
    bool m_placeAgain = false;

    // Handle drag in progress. A corner drag also has handles moving along
    // the other axis.
    SolvedLayout m_dragStart;
    std::vector<int> m_dragGroup;
    std::vector<int> m_dragCrossGroup;
    /// Other areas with bars in the corner that is dragged (or hovered).
    QList<QPointer<DockAreaWidget>> m_dragOthers;
    QList<QPointer<DockAreaWidget>> m_hoverOthers;
    /// Tab groups the drag has squeezed out for now. They are shown as gone;
    /// the tree changes only when the drag ends.
    std::vector<SplitterCoordinator::Squeezed> m_squeezed;
    std::vector<int> m_highlighted;
    bool m_wasSqueezing = false;
    // A drag that began at an edge handle: the size the panels came back
    // with, and whether they lie before the handle that is now dragged.
    bool m_reopening = false;
    bool m_reopenBefore = false;
    int m_reopenExtent = 0;
};

} // namespace QFlexDock
