// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include "core/LayoutState.h"
#include "core/SplitterCoordinator.h"

#include <QtCore/QMap>
#include <QtCore/QPointer>
#include <QtGui/QAction>

#include <array>
#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
class QMimeData;
QT_END_NAMESPACE

namespace QFlexDock {

class DockAreaWidget;
class DockAutoHideContainer;
class DockDragController;
class DockFloatingWindow;

struct DockPanel::Private
{
    DockManager *manager = nullptr;
    PanelId id;
    QString title;
    QIcon icon;
    QString toolTip;
    QPointer<QWidget> widget;
    DockPanelFactory factory;
    DockPolicy policy;
    bool dirty = false;
    bool pinnedTab = false;
    bool previewTab = false;
    bool hideContentDuringDrag = false;
    bool headerVisible = true;
    bool collapsible = false;
    /// Indexed by DockTitlePlace.
    std::array<QList<QPointer<QAction>>, 3> titleActions;

    // Derived from the layout state after every commit.
    std::optional<PanelLocation> location;
    bool active = false;
    bool contentVisible = false;
};

/// Where a drop (or a programmatic move) goes.
struct DropTarget
{
    QString container;
    /// Target tab group; null means the container as a whole.
    NodeId node;
    DockArea area = DockArea::None;
    int tabIndex = -1;
    /// Share of the target taken by an edge drop; negative picks the default.
    double fraction = -1.0;
};

/// A tab group that a split handle drag squeezed out, to be closed when the
/// drag ends. Its room goes to `heir`, the node across the dragged handle.
struct ResizeClose
{
    QString container;
    NodeId node;
    NodeId heir;
};

/// Closed panels that went together, and the edge they went to: the side
/// `side` of node `anchor`, where they would come back.
struct ReopenEdge
{
    QStringList panels;
    NodeId anchor;
    DockArea side = DockArea::None;
};

/// One drag in progress. The token is what travels in the QMimeData; a drop is
/// only honoured if it names the session the manager itself started.
struct DragSession
{
    QByteArray token;
    bool wholeGroup = false;
    QStringList panels;
    /// The dragged panel, or the active panel of the dragged group.
    PanelId primary;
    QString sourceContainer;
    /// The dragged tab group, or, when a whole floating window is dragged, the
    /// root of its tree.
    NodeId sourceNode;
    /// False when `sourceNode` is a split: that cannot become tabs of a group.
    bool sourceIsTabs = true;
};

struct DockWorkspace::Private
{
    QPointer<DockManager> manager;
    QString id;
    DockAreaWidget *area = nullptr;
    DockAutoHideContainer *autoHide = nullptr;
    /// Set where the workspace is not to have the manager's.
    std::optional<DockManager::GroupHeader> groupHeader;
    std::optional<DockTitleButtons> titleButtons;
};

class QFLEXDOCK_EXPORT DockManagerPrivate
{
public:
    explicit DockManagerPrivate(DockManager *manager);
    ~DockManagerPrivate();

    static DockManagerPrivate *get(DockManager *manager) { return manager->d.get(); }
    static const DockManagerPrivate *get(const DockManager *manager) { return manager->d.get(); }
    static DockPanel::Private *get(DockPanel *panel) { return panel->d.get(); }
    static DockWorkspace::Private *get(DockWorkspace *workspace) { return workspace->d.get(); }

    // --- Transactions --------------------------------------------------------
    /// The one way the layout changes: reconcile `next` with what exists,
    /// validate it, swap it in and bring the widgets in line. On failure
    /// nothing has changed.
    DockResult apply(LayoutState next, bool recordUndo);
    /// Makes `next` fit reality: containers for exactly the existing
    /// workspaces, and no placed panel that is not registered.
    void reconcile(LayoutState &next, DockRestoreReport *report = nullptr) const;
    void syncViews();
    void pushUndo(LayoutState previous);

    // --- Operations shared by the API, drag and drop and the menus ----------
    DockResult placePanel(const PanelId &panel, DropTarget target);
    /// `sourceNode` null: the tab group `anyPanel` is in. Otherwise that
    /// subtree of the panel's container (the root, for a whole window).
    DockResult placeGroup(const PanelId &anyPanel, DropTarget target, NodeId sourceNode = {});
    /// `adopt`: an existing window (a drag ghost) to use for the new floating
    /// container instead of creating one.
    DockResult floatPanels(const PanelId &panel, bool wholeGroup, QRect geometry,
                           DockFloatingWindow *adopt = nullptr);
    DockResult dockBack(const PanelId &panel);
    /// Several panels as one change; those that are docked stay.
    DockResult dockBack(const QStringList &panels);
    DockResult setAutoHide(const PanelId &panel, bool autoHide, DockArea edge);
    /// Several panels into one auto-hide bar, as one change. `edge` None:
    /// the border nearest to the first of them.
    DockResult autoHidePanels(const QStringList &panels, DockArea edge);
    DockResult setMaximized(const PanelId &panel, bool maximized);
    DockResult closePanels(const QStringList &panels);
    DockResult showPanels(const QStringList &panels);
    DockResult activate(const PanelId &panel, bool focus);

    /// Whether the user may do `feature` with the panel.
    [[nodiscard]] bool userMay(const PanelId &panel, DockFeature feature) const;
    [[nodiscard]] QStringList groupPanels(const PanelId &panel) const;

    // --- Drag and drop -------------------------------------------------------
    /// Whether what is dragged may be dropped anywhere in the container at
    /// all, going by the policies of the dragged panels.
    [[nodiscard]] bool containerAdmits(const DragSession &session, const QString &container) const;
    /// Areas of `node` (null: the container as a whole) the session may drop
    /// on, by policy and by what would be a no-op.
    [[nodiscard]] DockAreas allowedDropAreas(const DragSession &session, const QString &container,
                                             NodeId node) const;
    [[nodiscard]] bool dropAllowed(const DragSession &session, const DropTarget &target) const;
    DockResult commitDrop(const DragSession &session, const DropTarget &target);
    void hideAllOverlays();
    /// Preview of a tab drag: the tab `session` drags is shown as gone from
    /// its group (null: every group shows what it holds).
    void showDraggedOut(const DragSession *session);
    /// Called when a drag session starts and ends.
    void setDragInProgress(bool inProgress);
    /// The panel's content must stay hidden right now (see above).
    [[nodiscard]] bool contentSuspended(const DockPanel *panel) const;

    // --- Interactive resizing -------------------------------------------------
    void beginResize();
    void setWeights(const QString &container,
                    const std::vector<SplitterCoordinator::WeightUpdate> &updates);
    void endResize(bool cancel, const std::vector<ResizeClose> &closing = {});
    /// Collapsible panels of `container` that are closed and can be pulled
    /// back out of the edge they went to.
    [[nodiscard]] std::vector<ReopenEdge> reopenEdges(const QString &container) const;
    /// Shows `panels` as the start of a drag that pulls them out of their
    /// edge. What the drag makes of it is settled by endResize().
    DockResult beginReopen(const QStringList &panels);

    // --- Floating windows ----------------------------------------------------
    void floatingGeometryChanged(const QString &container, const QRect &geometry);
    /// The user closes a floating window. False if a panel in it may not be
    /// closed.
    bool closeFloatingByUser(const QString &container);

    // --- Panels and their content --------------------------------------------
    DockPanel *createPanel(const PanelId &id, const QString &title);
    void adoptWidget(DockPanel *panel, QWidget *widget);
    /// The content widget, created from the factory if necessary.
    QWidget *ensureWidget(DockPanel *panel);
    /// Moves a panel's content under `parent`, with lifecycle signals.
    void reparentContent(DockPanel *panel, QWidget *parent);
    /// Moves content that is still a child of `host` to the parking widget.
    void parkIfHostedBy(DockPanel *panel, const QWidget *host);
    QWidget *parkingWidget();
    QWidget *removePanel(const PanelId &id, DockManager::PlacementMemory memory, bool keepWidget);
    void contentDestroyed(const PanelId &id);
    [[nodiscard]] DockPanel *panelContaining(QWidget *widget) const;
    void setActivePanel(DockPanel *panel);
    void updatePanelStates(bool emitSignals);
    /// Title, icon or tab state of a panel changed: refresh whatever shows it.
    void panelAppearanceChanged(DockPanel *panel);
    void refreshActiveMarks();
    void refreshAllAppearance();
    /// A mouse press somewhere in the application (seen once per press).
    void mousePressedOn(QWidget *widget);

    // --- Workspaces ----------------------------------------------------------
    void workspaceDestroyed(DockWorkspace *workspace);
    [[nodiscard]] DockAreaWidget *areaFor(const QString &container) const;
    /// The workspace a container belongs to (the owner, for a floating one).
    [[nodiscard]] DockWorkspace *workspaceFor(const QString &container) const;
    [[nodiscard]] QString workspaceIdFor(const QString &container) const;
    /// What the tab groups of a container have at their top.
    [[nodiscard]] DockManager::GroupHeader groupHeaderFor(const QString &container) const;
    /// The built-in buttons their headers have.
    [[nodiscard]] DockTitleButtons titleButtonsFor(const QString &container) const;
    [[nodiscard]] QString defaultWorkspaceId() const;
    [[nodiscard]] QWidget *windowFor(const QString &container) const;

    // --- Menus and icons -----------------------------------------------------
    /// Builds the context menu of a panel; the caller owns it.
    QMenu *createPanelMenu(DockPanel *panel, QWidget *parent);
    [[nodiscard]] QIcon icon(DockIcon which, const QWidget *styledBy) const;
    [[nodiscard]] int handleWidth(const QWidget *styledBy) const;

    DockManager *q;
    LayoutState state;
    QHash<PanelId, DockPanel *> panels;
    QStringList panelOrder;
    QHash<QWidget *, DockPanel *> panelByWidget;
    QList<DockWorkspace *> workspaces;
    QHash<QString, DockFloatingWindow *> floatingWindows;
    QPointer<QWidget> parking;
    QPointer<DockPanel> activePanel;
    DockDragController *drag = nullptr;

    DockResult lastError;
    DockDropFilter dropFilter;
    DockTheme theme;
    std::shared_ptr<DockOverlayPainter> overlayPainter;
    bool linkedSplitters = true;
    bool cornerResize = true;
    bool centerDrop = true;
    bool tabDragPreview = false;
    DockManager::GroupHeader groupHeader = DockManager::GroupHeader::Tabs;
    bool titleBarMovesGroup = false;
    DockManager::AutoHideReveal autoHideReveal = DockManager::AutoHideReveal::Over;
    bool floatOnOutsideDrop = false;
    bool dragGhostEnabled = true;
    DockManager::FloatingFrame floatingFrame = DockManager::FloatingFrame::Custom;
    DockManager::FloatingWindowType floatingWindowType = DockManager::FloatingWindowType::Window;
    bool restoreWindowGeometry = true;

    std::vector<LayoutState> undoStack;
    std::vector<LayoutState> redoStack;
    int undoLimit = 50;
    std::optional<LayoutState> resizeStart;
    /// The state before the panels in `reopening` were pulled out of an edge.
    std::optional<LayoutState> reopenStart;
    QStringList reopening;
    QMap<QString, LayoutState> presets;
    std::optional<LayoutState> defaultLayout;

    /// Group of each container the user last worked in; where a plain
    /// "add to this workspace" tabs into.
    QHash<QString, PanelId> lastActiveIn;
    quint64 lastPressTimestamp = 0;

    bool dragInProgress = false;
    bool committing = false;
    bool syncing = false;
    bool destroying = false;
    int workspaceCounter = 0;
};

} // namespace QFlexDock
