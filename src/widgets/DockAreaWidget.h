// SPDX-License-Identifier: MIT
#pragma once

#include "core/DockManager_p.h"
#include "core/LayoutSolver.h"

#include <QtCore/QHash>
#include <QtWidgets/QWidget>

namespace QFlexDock {

class DockDropOverlay;
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
    [[nodiscard]] DockDropOverlay *overlay() const { return m_overlay; }

    void relayout();
    /// A tab group's size limits changed.
    void contentLimitsChanged();
    void refreshAppearance();
    [[nodiscard]] int handleWidth() const;
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

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    class Layout;
    void handleDrag(QDragMoveEvent *event);
    [[nodiscard]] std::vector<int> linkedGroup(int handleIndex, bool linked) const;
    [[nodiscard]] std::vector<int> linkedGroup(const std::vector<int> &handles, bool linked) const;
    void highlightHandles(const std::vector<int> &handles);
    [[nodiscard]] LimitsProvider limitsProvider() const;
    [[nodiscard]] const LayoutNode *maximizedNode() const;
    void placeWidgets();

    DockManagerPrivate *m_manager;
    QString m_containerId;
    LayoutTree m_tree;
    PanelId m_maximized;
    SolvedLayout m_solved;
    QHash<NodeId, DockTabGroup *> m_groups;
    QList<DockSplitHandle *> m_handles;
    std::vector<SplitterCoordinator::Corner> m_corners;
    QList<DockSplitCorner *> m_cornerWidgets;
    DockDropOverlay *m_overlay;
    Layout *m_layout;

    bool m_placing = false;
    bool m_placeAgain = false;

    // Handle drag in progress. A corner drag also has handles moving along
    // the other axis.
    SolvedLayout m_dragStart;
    std::vector<int> m_dragGroup;
    std::vector<int> m_dragCrossGroup;
};

} // namespace QFlexDock
