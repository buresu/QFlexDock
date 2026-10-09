// SPDX-License-Identifier: MIT
#pragma once

#include "core/DockManager_p.h"
#include "core/LayoutSolver.h"

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

namespace QFlexDock {

class DockDropOverlay;
class DockEdgeHandle;
class DockSplitCorner;
class DockSplitHandle;
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
    /// Enabled bands along the border of the whole dock area.
    DockAreas outerZones;
    bool outer = false;
    DockArea hovered = DockArea::None;
    QRect preview;
    QRect tabIndicator;
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
    [[nodiscard]] DropCandidate candidateAt(const QPoint &pos, const DragSession &session) const;
    void showOverlay(const DropCandidate &candidate);
    void hideOverlay();

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
    DockDropOverlay *m_overlay;
    Layout *m_layout;

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
