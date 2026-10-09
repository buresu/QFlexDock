// SPDX-License-Identifier: MIT
#include <QFlexDock/DockManager.h>

#include "core/DockDragController.h"
#include "core/DockManager_p.h"
#include "persistence/LayoutSerializer.h"
#include "widgets/DockAreaWidget.h"
#include "widgets/DockAutoHide.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockIcons.h"
#include "widgets/DockTabGroup.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtGui/QMouseEvent>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QStyle>

#include <algorithm>

namespace QFlexDock {

namespace {

DockResult fail(DockError error, const QString &message)
{
    return DockResult::failure(error, message);
}

DockResult unknownPanel(const PanelId &id)
{
    return fail(DockError::UnknownPanel, QStringLiteral("no panel '%1' is registered").arg(id));
}

// Share of the target an edge dock takes when the caller does not say.
constexpr double DefaultWorkspaceEdgeFraction = 0.25;
constexpr double DefaultGroupEdgeFraction = 0.5;

QList<QRect> screenGeometries()
{
    QList<QRect> result;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *screen : screens)
        result << screen->availableGeometry();
    return result;
}

} // namespace

// =============================================================================
// DockManagerPrivate
// =============================================================================

DockManagerPrivate::DockManagerPrivate(DockManager *manager)
    : q(manager)
{
    drag = new DockDragController(this);
    // Where a drop outside every dock area can be told apart from a cancelled
    // drag (see docs/platform-notes.md): X11, and Wayland when a window is
    // carried along with the drag.
    const QString platform = QGuiApplication::platformName();
    floatOnOutsideDrop = platform == QLatin1String("xcb")
        || platform.startsWith(QLatin1String("wayland"));
}

DockManagerPrivate::~DockManagerPrivate() = default;

// --- Transactions ------------------------------------------------------------

DockResult DockManagerPrivate::apply(LayoutState next, bool recordUndo)
{
    if (committing) {
        return fail(DockError::Busy,
                    QStringLiteral("the layout cannot be changed while a change is being applied"));
    }
    reconcile(next);
    next.normalize();
    if (DockResult r = next.validate(); !r)
        return r;

    committing = true;
    QList<QPointer<DockPanel>> moving;
    for (DockPanel *panel : std::as_const(panels)) {
        if (state.locate(panel->id()) != next.locate(panel->id()))
            moving.append(panel);
    }
    Q_EMIT q->layoutAboutToChange();
    for (const QPointer<DockPanel> &panel : std::as_const(moving)) {
        if (panel)
            Q_EMIT q->panelAboutToMove(panel);
    }

    if (recordUndo)
        pushUndo(state);
    const QPointer<QWidget> focusBefore = QApplication::focusWidget();
    state = std::move(next);
    syncViews();
    committing = false;

    updatePanelStates(true);
    // Moving widgets around can drop keyboard focus; hand it back if the
    // widget that had it is still on screen.
    if (focusBefore && focusBefore != QApplication::focusWidget() && focusBefore->isVisible()
        && focusBefore->window()->isActiveWindow()) {
        focusBefore->setFocus(Qt::OtherFocusReason);
    }
    for (const QPointer<DockPanel> &panel : std::as_const(moving)) {
        if (panel)
            Q_EMIT q->panelMoved(panel);
    }
    Q_EMIT q->layoutChanged();
    return DockResult::success();
}

void DockManagerPrivate::reconcile(LayoutState &next, DockRestoreReport *report) const
{
    QSet<QString> existing;
    for (DockWorkspace *workspace : workspaces)
        existing.insert(workspace->workspaceId());

    // Workspaces named by the layout that are not there: their panels are
    // closed, but remembered.
    for (size_t i = 0; i < next.containers.size();) {
        const ContainerState &c = next.containers[i];
        if (c.kind != ContainerKind::Workspace || existing.contains(c.id)) {
            ++i;
            continue;
        }
        QStringList lost = c.tree.panels();
        for (const QStringList &bar : c.autoHide)
            lost += bar;
        for (const PanelId &panel : std::as_const(lost)) {
            next.memory.insert(panel, next.capture(panel));
            if (report && !panels.contains(panel))
                report->missingPanels << panel;
        }
        if (report)
            report->unknownWorkspaces << c.id;
        next.containers.erase(next.containers.begin() + qsizetype(i));
    }
    for (DockWorkspace *workspace : workspaces) {
        if (!next.find(workspace->workspaceId())) {
            ContainerState c;
            c.id = workspace->workspaceId();
            next.containers.push_back(std::move(c));
        }
    }
    for (auto &c : next.containers) {
        if (c.kind == ContainerKind::Floating && !existing.contains(c.owner))
            c.owner = defaultWorkspaceId();
    }

    // Panels named by the layout that are not registered (yet): keep their
    // place in memory so they return to it when they are.
    const QStringList placed = next.placedPanels();
    for (const PanelId &panel : placed) {
        if (panels.contains(panel))
            continue;
        if (next.detach(panel, true))
            next.memory[panel].reopen = true;
        if (report)
            report->missingPanels << panel;
    }
}

void DockManagerPrivate::syncViews()
{
    syncing = true;

    QList<DockFloatingWindow *> created;
    for (const auto &c : state.containers) {
        if (c.kind == ContainerKind::Floating && !floatingWindows.contains(c.id)) {
            auto *window = new DockFloatingWindow(this, c.id, floatingFrame);
            floatingWindows.insert(c.id, window);
            created.append(window);
        }
    }

    // Every dock area first, so content moving between two groups goes
    // straight from one to the other.
    for (const auto &c : state.containers) {
        if (c.kind == ContainerKind::Floating) {
            DockFloatingWindow *window = floatingWindows.value(c.id);
            window->setLayoutState(c);
            // The geometry in the state is that of the window as a plain
            // window. Maximized (or the like) it is somewhere else, and
            // stays there.
            const bool plain = !(window->windowState()
                                 & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized));
            if (!created.contains(window) && plain && c.geometry.isValid()
                && window->geometry() != c.geometry) {
                window->setGeometry(c.geometry);
            }
        } else if (DockWorkspace *workspace = workspaceFor(c.id)) {
            get(workspace)->area->setLayoutState(c);
        }
    }
    for (const auto &c : state.containers) {
        if (c.kind != ContainerKind::Workspace)
            continue;
        if (DockWorkspace *workspace = workspaceFor(c.id))
            get(workspace)->autoHide->setLayoutState(c);
    }

    // Content nobody shows any more waits in the parking widget. This has to
    // happen before the views that held it are destroyed.
    for (DockPanel *panel : std::as_const(panels)) {
        QWidget *widget = get(panel)->widget;
        if (!widget || widget->parentWidget() == parking)
            continue;
        const std::optional<PanelLocation> location = state.locate(panel->id());
        bool hosted = location.has_value();
        if (location && location->isAutoHidden()) {
            DockWorkspace *workspace = workspaceFor(location->container);
            hosted = workspace && get(workspace)->autoHide->expandedPanel() == panel->id();
        }
        if (!hosted)
            reparentContent(panel, parkingWidget());
    }

    for (auto it = floatingWindows.begin(); it != floatingWindows.end();) {
        if (state.find(it.key())) {
            ++it;
            continue;
        }
        DockFloatingWindow *window = it.value();
        window->detachFromManager();
        window->hide();
        window->deleteLater();
        it = floatingWindows.erase(it);
    }

    for (DockFloatingWindow *window : std::as_const(created)) {
        const ContainerState *c = state.find(window->containerId());
        DockWorkspace *owner = workspaceFor(c->id);
        window->present(c->geometry, owner ? owner->window() : nullptr);
    }

    syncing = false;
    refreshActiveMarks();
}

void DockManagerPrivate::pushUndo(LayoutState previous)
{
    if (undoLimit <= 0)
        return;
    undoStack.push_back(std::move(previous));
    while (int(undoStack.size()) > undoLimit)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
    Q_EMIT q->undoStateChanged();
}

// --- Operations --------------------------------------------------------------

DockResult DockManagerPrivate::placePanel(const PanelId &panel, DropTarget target)
{
    if (!panels.contains(panel))
        return unknownPanel(panel);
    LayoutState next = state;
    if (!next.find(target.container)) {
        return fail(DockError::UnknownWorkspace,
                    QStringLiteral("no container '%1'").arg(target.container));
    }
    if (target.fraction <= 0.0) {
        target.fraction = target.node.isNull() ? DefaultWorkspaceEdgeFraction
                                               : DefaultGroupEdgeFraction;
    }

    const std::optional<PanelLocation> location = next.locate(panel);
    const bool ownGroup = location && !location->isAutoHidden() && !target.node.isNull()
        && location->container == target.container && location->node == target.node;
    if (ownGroup) {
        ContainerState *container = next.find(target.container);
        const LayoutNode *group = container->tree.findNode(target.node);
        if (target.area == DockArea::Center) {
            // Reordering within the group.
            const int from = int(group->panels.indexOf(panel));
            int to = target.tabIndex < 0 ? int(group->panels.size()) : target.tabIndex;
            to = qBound(0, to > from ? to - 1 : to, int(group->panels.size()) - 1);
            if (to != from)
                (void)container->tree.moveTab(panel, to);
            (void)container->tree.setActivePanel(panel);
            return apply(std::move(next), true);
        }
        if (group->panels.size() < 2) {
            return fail(DockError::InvalidArgument,
                        QStringLiteral("a panel cannot be split off a group it is alone in"));
        }
    }

    if (location) {
        if (DockResult r = next.detach(panel, false); !r)
            return r;
    }
    ContainerState *container = next.find(target.container);
    if (!container) {
        return fail(DockError::UnknownNode,
                    QStringLiteral("the target disappears when the panel leaves it"));
    }
    // "Into this workspace" without a group in mind: join where the user last
    // worked there.
    if (target.node.isNull() && target.area == DockArea::Center) {
        if (const LayoutNode *group = container->tree.findPanel(lastActiveIn.value(container->id)))
            target.node = group->id;
    }
    if (DockResult r = container->tree.insertPanel(panel, target.node, target.area,
                                                   target.tabIndex, target.fraction);
        !r) {
        return r;
    }
    container->maximized.clear();
    return apply(std::move(next), true);
}

DockResult DockManagerPrivate::placeGroup(const PanelId &anyPanel, DropTarget target,
                                          NodeId sourceNode)
{
    if (!panels.contains(anyPanel))
        return unknownPanel(anyPanel);
    const std::optional<PanelLocation> location = state.locate(anyPanel);
    if (!location || location->isAutoHidden()) {
        return fail(DockError::NotPlaced,
                    QStringLiteral("panel '%1' is not in a tab group").arg(anyPanel));
    }
    const NodeId moving = sourceNode.isNull() ? location->node : sourceNode;
    if (location->container == target.container && moving == target.node) {
        return fail(DockError::InvalidArgument,
                    QStringLiteral("a tab group cannot be docked onto itself"));
    }
    if (target.fraction <= 0.0) {
        target.fraction = target.node.isNull() ? DefaultWorkspaceEdgeFraction
                                               : DefaultGroupEdgeFraction;
    }

    LayoutState next = state;
    std::optional<LayoutNode> taken = next.find(location->container)->tree.takeNode(moving);
    if (!taken)
        return fail(DockError::UnknownNode, QStringLiteral("the dragged node no longer exists"));
    ContainerState *container = next.find(target.container);
    if (!container) {
        return fail(DockError::UnknownWorkspace,
                    QStringLiteral("no container '%1'").arg(target.container));
    }
    if (target.node.isNull() && target.area == DockArea::Center) {
        if (const LayoutNode *group = container->tree.findPanel(lastActiveIn.value(container->id)))
            target.node = group->id;
    }
    if (DockResult r = container->tree.insertNode(std::move(*taken), target.node, target.area,
                                                  target.tabIndex, target.fraction);
        !r) {
        return r;
    }
    container->maximized.clear();
    return apply(std::move(next), true);
}

DockResult DockManagerPrivate::floatPanels(const PanelId &panel, bool wholeGroup, QRect geometry,
                                           DockFloatingWindow *adopt)
{
    if (!panels.contains(panel))
        return unknownPanel(panel);
    const std::optional<PanelLocation> location = state.locate(panel);
    if (!location) {
        // A closed panel is shown, in a window of its own.
        if (adopt) {
            return fail(DockError::NotPlaced,
                        QStringLiteral("panel '%1' is not placed").arg(panel));
        }
        LayoutState next = state;
        const PanelMemory memory = next.memory.take(panel);
        if (!geometry.isValid())
            geometry = memory.floating && memory.geometry.isValid() ? memory.geometry
                                                                    : QRect(120, 120, 480, 360);
        ContainerState &floating = next.addFloating(defaultWorkspaceId(), geometry);
        floating.tree = LayoutTree(LayoutNode::makeTabs({panel}));
        return apply(std::move(next), true);
    }

    const bool inGroup = !location->isAutoHidden();
    QStringList moved{panel};
    if (wholeGroup && inGroup)
        moved = state.find(location->container)->tree.findNode(location->node)->panels;

    LayoutState next = state;
    if (location->isFloating() && state.find(location->container)->tree.panels() == moved) {
        // Already floating by itself; at most the window moves. No second
        // window is wanted for it, so one offered for adoption is refused
        // (and thereby left to the caller to dispose of).
        if (adopt) {
            return fail(DockError::InvalidArgument,
                        QStringLiteral("panel '%1' already has a floating window to itself").arg(panel));
        }
        if (!geometry.isValid())
            return DockResult::success();
        next.find(location->container)->geometry = geometry;
        return apply(std::move(next), false);
    }

    if (!geometry.isValid()) {
        // Default: slightly offset from where the panel is now.
        geometry = QRect(120, 120, 480, 360);
        if (DockAreaWidget *area = areaFor(location->container)) {
            if (const DockTabGroup *group = area->groupOfPanel(panel)) {
                geometry = QRect(group->mapToGlobal(QPoint(32, 32)),
                                 group->size().expandedTo(QSize(320, 240)));
            }
        }
    }
    const QString owner = workspaceIdFor(location->container);

    // Remember where docked panels came from, for dockPanel(). All of them
    // before any is removed, so each still sees its neighbours.
    for (const PanelId &id : std::as_const(moved)) {
        const std::optional<PanelLocation> l = next.locate(id);
        if (l && l->isDocked())
            next.memory.insert(id, next.capture(id));
    }

    LayoutNode node;
    if (wholeGroup && inGroup) {
        node = *next.find(location->container)->tree.takeNode(location->node);
    } else {
        if (DockResult r = next.detach(panel, false); !r)
            return r;
        node = LayoutNode::makeTabs({panel});
    }
    ContainerState &floating = next.addFloating(owner, geometry);
    floating.tree = LayoutTree(std::move(node));
    if (!adopt)
        return apply(std::move(next), true);

    // The window is there already (it was the ghost of the drag that led
    // here): it becomes the view of the new container instead of a new one.
    const QString id = floating.id;
    floatingWindows.insert(id, adopt);
    adopt->adoptAs(id);
    const DockResult result = apply(std::move(next), true);
    if (!result)
        floatingWindows.remove(id);
    return result;
}

DockResult DockManagerPrivate::dockBack(const PanelId &panel)
{
    if (!panels.contains(panel))
        return unknownPanel(panel);
    const std::optional<PanelLocation> location = state.locate(panel);
    if (!location)
        return fail(DockError::NotPlaced, QStringLiteral("panel '%1' is not placed").arg(panel));
    if (location->isDocked())
        return DockResult::success();

    LayoutState next = state;
    QString fallback = workspaceIdFor(location->container);
    if (fallback.isEmpty())
        fallback = defaultWorkspaceId();
    PanelMemory memory = next.memory.value(panel);
    if (memory.floating || memory.container.isEmpty()) {
        // The memory does not describe a docked position.
        memory = PanelMemory{};
        memory.container = fallback;
    }
    memory.autoHideEdge = DockArea::None;
    memory.reopen = false;

    if (DockResult r = next.detach(panel, false); !r)
        return r;
    next.memory.insert(panel, memory);
    if (DockResult r = next.reattach(panel, fallback); !r)
        return r;
    return apply(std::move(next), true);
}

DockResult DockManagerPrivate::setAutoHide(const PanelId &panel, bool autoHide, DockArea edge)
{
    if (!panels.contains(panel))
        return unknownPanel(panel);
    const std::optional<PanelLocation> location = state.locate(panel);
    if (!location)
        return fail(DockError::NotPlaced, QStringLiteral("panel '%1' is not placed").arg(panel));
    if (!autoHide)
        return location->isAutoHidden() ? dockBack(panel) : DockResult::success();
    if (edge != DockArea::None && !isEdgeArea(edge))
        return fail(DockError::InvalidArgument, QStringLiteral("auto-hide needs an edge"));

    const QString workspaceId = workspaceIdFor(location->container);
    LayoutState next = state;
    if (!next.find(workspaceId) || next.find(workspaceId)->kind != ContainerKind::Workspace) {
        return fail(DockError::UnknownWorkspace,
                    QStringLiteral("there is no workspace to auto-hide the panel in"));
    }

    if (edge == DockArea::None) {
        edge = location->isAutoHidden() ? location->autoHideEdge : DockArea::Left;
        // The border the panel is closest to.
        DockAreaWidget *area = location->isDocked() ? areaFor(location->container) : nullptr;
        if (const DockTabGroup *group = area ? area->groupOfPanel(panel) : nullptr) {
            const QRect bounds = area->rect();
            const QPoint c = group->geometry().center();
            const int distances[4] = {c.x() - bounds.left(), bounds.right() - c.x(),
                                      c.y() - bounds.top(), bounds.bottom() - c.y()};
            int best = 0;
            for (int i = 1; i < 4; ++i) {
                if (distances[i] < distances[best])
                    best = i;
            }
            edge = DockEdges[size_t(best)];
        }
    }

    // The docked position stays in memory for when the panel is pinned again.
    const PanelMemory memory = location->isDocked() ? next.capture(panel)
                                                    : next.memory.value(panel);
    if (DockResult r = next.detach(panel, false); !r)
        return r;
    PanelMemory kept = memory;
    kept.autoHideEdge = DockArea::None;
    next.memory.insert(panel, kept);
    next.find(workspaceId)->autoHide[size_t(edgeIndex(edge))].append(panel);
    return apply(std::move(next), true);
}

DockResult DockManagerPrivate::setMaximized(const PanelId &panel, bool maximized)
{
    LayoutState next = state;
    if (maximized) {
        if (!panels.contains(panel))
            return unknownPanel(panel);
        const std::optional<PanelLocation> location = next.locate(panel);
        if (!location || location->isAutoHidden()) {
            return fail(DockError::NotPlaced,
                        QStringLiteral("panel '%1' is not in a tab group").arg(panel));
        }
        ContainerState *container = next.find(location->container);
        container->maximized = panel;
        (void)container->tree.setActivePanel(panel);
    } else {
        for (auto &c : next.containers) {
            if (panel.isEmpty() || c.tree.containsPanel(panel))
                c.maximized.clear();
        }
    }
    return apply(std::move(next), true);
}

DockResult DockManagerPrivate::closePanels(const QStringList &ids)
{
    LayoutState next = state;
    // Where each of them is, noted before any of them leaves: panels that go
    // together remember each other, and the group as it was.
    QStringList leaving;
    QHash<PanelId, PanelMemory> memories;
    for (const PanelId &id : ids) {
        if (!panels.contains(id))
            return unknownPanel(id);
        if (next.isPlaced(id) && !leaving.contains(id)) {
            leaving.append(id);
            memories.insert(id, next.capture(id));
        }
    }
    for (const PanelId &id : std::as_const(leaving)) {
        if (DockResult r = next.detach(id, false); !r)
            return r;
    }
    for (const PanelId &id : std::as_const(leaving))
        next.memory.insert(id, memories.value(id));
    return leaving.isEmpty() ? DockResult::success() : apply(std::move(next), true);
}

DockResult DockManagerPrivate::showPanels(const QStringList &ids)
{
    for (const PanelId &id : ids) {
        if (!panels.contains(id))
            return unknownPanel(id);
    }
    LayoutState next = state;
    if (DockResult r = next.reattachAll(ids, defaultWorkspaceId()); !r)
        return r;
    return apply(std::move(next), true);
}

DockResult DockManagerPrivate::activate(const PanelId &id, bool focus)
{
    DockPanel *panel = panels.value(id);
    if (!panel)
        return unknownPanel(id);
    const std::optional<PanelLocation> location = state.locate(id);
    if (!location)
        return fail(DockError::NotPlaced, QStringLiteral("panel '%1' is not placed").arg(id));

    if (location->isAutoHidden()) {
        if (DockWorkspace *workspace = workspaceFor(location->container))
            get(workspace)->autoHide->expand(id);
    } else {
        const ContainerState *container = state.find(location->container);
        const bool hiddenByMaximize = !container->maximized.isEmpty()
            && container->tree.findPanel(container->maximized)->id != location->node;
        if (container->tree.findNode(location->node)->active != id || hiddenByMaximize) {
            LayoutState next = state;
            ContainerState *c = next.find(location->container);
            (void)c->tree.setActivePanel(id);
            if (hiddenByMaximize)
                c->maximized = id; // keep it visible: maximize its group instead
            if (DockResult r = apply(std::move(next), false); !r)
                return r;
        }
    }

    setActivePanel(panel);
    if (focus) {
        if (QWidget *content = get(panel)->widget) {
            QWidget *window = content->window();
            if (!window->isActiveWindow() && window->isVisible()) {
                window->raise();
                window->activateWindow();
            }
            QWidget *target = content->focusWidget() ? content->focusWidget() : content;
            target->setFocus(Qt::OtherFocusReason);
        }
    }
    return DockResult::success();
}

bool DockManagerPrivate::userMay(const PanelId &id, DockFeature feature) const
{
    const DockPanel *panel = panels.value(id);
    return panel && panel->features().testFlag(feature);
}

QStringList DockManagerPrivate::groupPanels(const PanelId &panel) const
{
    const std::optional<PanelLocation> location = state.locate(panel);
    if (!location || location->isAutoHidden())
        return {};
    return state.find(location->container)->tree.findNode(location->node)->panels;
}

// --- Drag and drop -----------------------------------------------------------

bool DockManagerPrivate::containerAdmits(const DragSession &session, const QString &containerId) const
{
    const ContainerState *container = state.find(containerId);
    if (!container)
        return false;
    const QString workspaceId = workspaceIdFor(containerId);
    for (const PanelId &id : session.panels) {
        const DockPanel *panel = panels.value(id);
        if (!panel)
            return false;
        const DockPolicy policy = panel->policy();
        if (!policy.features.testFlag(DockFeature::Movable))
            return false;
        if (!policy.allowedWorkspaces.isEmpty() && !policy.allowedWorkspaces.contains(workspaceId))
            return false;
        if (container->kind == ContainerKind::Floating
            && !policy.features.testFlag(DockFeature::Floatable)
            && session.sourceContainer != containerId) {
            return false;
        }
    }
    return true;
}

DockAreas DockManagerPrivate::allowedDropAreas(const DragSession &session, const QString &containerId,
                                               NodeId node) const
{
    const ContainerState *container = state.find(containerId);
    if (!container || !containerAdmits(session, containerId))
        return {};
    const LayoutNode *group = node.isNull() ? nullptr : container->tree.findNode(node);
    if (!node.isNull() && (!group || !group->isTabs()))
        return {};

    // What the dragged panels' own policies permit.
    DockAreas areas = AllDockAreas;
    bool draggedTabbable = true;
    for (const PanelId &id : session.panels) {
        const DockPolicy policy = panels.value(id)->policy();
        areas &= policy.allowedAreas;
        draggedTabbable = draggedTabbable && policy.features.testFlag(DockFeature::Tabbable);
    }

    const bool sameContainer = session.sourceContainer == containerId;
    if (group) {
        // Joining tabs needs the consent of both sides.
        bool targetTabbable = true;
        for (const PanelId &id : group->panels)
            targetTabbable = targetTabbable && userMay(id, DockFeature::Tabbable);
        if (!draggedTabbable || !targetTabbable || !session.sourceIsTabs)
            areas &= ~DockAreas(DockArea::Center);

        if (sameContainer && session.sourceNode == node) {
            // Onto its own group. The centre is always there and means "leave
            // it where it is"; splitting off needs someone to stay behind.
            const bool canSplit = !session.wholeGroup && group->panels.size() >= 2;
            areas = (canSplit ? areas & EdgeDockAreas : DockAreas()) | DockArea::Center;
        }
    } else {
        if (container->tree.isEmpty())
            return areas & DockAreas(DockArea::Center);
        // The container as a whole offers its outer edges only.
        areas &= EdgeDockAreas;
        // Dragging everything there is around itself changes nothing.
        if (sameContainer && container->tree.panels().size() == session.panels.size())
            areas = {};
    }
    return areas;
}

bool DockManagerPrivate::dropAllowed(const DragSession &session, const DropTarget &target) const
{
    const ContainerState *container = state.find(target.container);
    if (!container || target.area == DockArea::None)
        return false;

    // With the middle of a group taking no drops, a panel becomes a tab (or
    // changes places with one) by the header only.
    if (!centerDrop && !target.node.isNull() && target.area == DockArea::Center
        && target.tabIndex < 0) {
        return false;
    }

    const bool ownCenter = !target.node.isNull() && target.area == DockArea::Center
        && session.sourceContainer == target.container && session.sourceNode == target.node;
    if (ownCenter) {
        // Back onto its own group: staying put, or reordering its tabs.
        // Nothing gets docked anywhere, so neither the area policies nor the
        // drop filter have a say.
        return std::all_of(session.panels.begin(), session.panels.end(), [this](const PanelId &id) {
            return userMay(id, DockFeature::Movable);
        });
    }
    if (!allowedDropAreas(session, target.container, target.node).testFlag(target.area))
        return false;

    if (dropFilter) {
        DockDropRequest request;
        request.panels = session.panels;
        request.workspaceId = workspaceIdFor(target.container);
        request.intoFloatingWindow = container->kind == ContainerKind::Floating;
        if (const LayoutNode *group = container->tree.findNode(target.node))
            request.targetPanel = group->active;
        request.area = target.area;
        request.tabIndex = target.area == DockArea::Center ? target.tabIndex : -1;
        if (!dropFilter(request))
            return false;
    }
    return true;
}

DockResult DockManagerPrivate::commitDrop(const DragSession &session, const DropTarget &target)
{
    // Checked again here: the layout may have changed since the guide was shown.
    if (!dropAllowed(session, target))
        return fail(DockError::PolicyViolation, QStringLiteral("this drop is not allowed"));
    for (const PanelId &panel : session.panels) {
        if (!state.isPlaced(panel)) {
            return fail(DockError::NotPlaced,
                        QStringLiteral("panel '%1' is no longer placed").arg(panel));
        }
    }
    // Dropped back onto the middle of its own group (not between its tabs):
    // it stays where it is. A successful drop that changes nothing.
    const bool ownCenter = !target.node.isNull() && target.area == DockArea::Center
        && session.sourceContainer == target.container && session.sourceNode == target.node;
    if (ownCenter && (session.wholeGroup || target.tabIndex < 0))
        return DockResult::success();
    return session.wholeGroup ? placeGroup(session.primary, target, session.sourceNode)
                              : placePanel(session.primary, target);
}

void DockManagerPrivate::setDragInProgress(bool inProgress)
{
    if (dragInProgress == inProgress)
        return;
    dragInProgress = inProgress;
    // Native child windows paint over every widget, the drop guide included,
    // and on some platforms take the drag events themselves. Panels that say
    // so get their content hidden until the drag is over.
    for (DockPanel *panel : std::as_const(panels)) {
        DockPanel::Private *p = get(panel);
        if (!p->hideContentDuringDrag || !p->widget)
            continue;
        if (inProgress) {
            p->widget->hide();
        } else if (const std::optional<PanelLocation> location = state.locate(p->id)) {
            if (location->isAutoHidden()) {
                DockWorkspace *workspace = workspaceFor(location->container);
                if (workspace && get(workspace)->autoHide->expandedPanel() == p->id)
                    p->widget->show();
            } else if (DockAreaWidget *area = areaFor(location->container)) {
                if (DockTabGroup *group = area->group(location->node))
                    group->updateContentGeometry();
            }
        }
    }
}

bool DockManagerPrivate::contentSuspended(const DockPanel *panel) const
{
    return dragInProgress && panel->d->hideContentDuringDrag;
}

void DockManagerPrivate::hideAllOverlays()
{
    for (DockWorkspace *workspace : std::as_const(workspaces))
        get(workspace)->area->hideOverlay();
    for (DockFloatingWindow *window : std::as_const(floatingWindows))
        window->area()->hideOverlay();
}

void DockManagerPrivate::showDraggedOut(const DragSession *session)
{
    const bool one = session && tabDragPreview && !session->wholeGroup;
    const auto show = [&](DockAreaWidget *area, const QString &container) {
        const QList<DockTabGroup *> groups = area->groups();
        for (DockTabGroup *group : groups) {
            const bool source = one && container == session->sourceContainer
                && group->nodeId() == session->sourceNode;
            group->setDraggedOut(source ? session->primary : PanelId());
        }
    };
    for (DockWorkspace *workspace : std::as_const(workspaces))
        show(get(workspace)->area, workspace->workspaceId());
    for (auto it = floatingWindows.cbegin(); it != floatingWindows.cend(); ++it)
        show(it.value()->area(), it.key());
}

// --- Interactive resizing ----------------------------------------------------

void DockManagerPrivate::beginResize()
{
    resizeStart = state;
}

void DockManagerPrivate::setWeights(const QString &containerId,
                                    const std::vector<SplitterCoordinator::WeightUpdate> &updates)
{
    ContainerState *container = state.find(containerId);
    if (!container || committing)
        return;
    for (const auto &update : updates)
        (void)container->tree.setWeights(update.split, update.weights);
    // Weights only: no structure changes, so just this area needs a new pass.
    if (DockAreaWidget *area = areaFor(containerId))
        area->setLayoutState(*container);
}

std::vector<ReopenEdge> DockManagerPrivate::reopenEdges(const QString &containerId) const
{
    std::vector<ReopenEdge> edges;
    QSet<PanelId> taken;
    for (const PanelId &id : panelOrder) {
        const auto memory = state.memory.constFind(id);
        if (memory == state.memory.constEnd() || taken.contains(id))
            continue;
        // The panels that went together with this one: each of them names
        // all the others as the tabs it had beside it. A tab that was closed
        // on its own before the rest is not among them.
        QStringList together{id};
        together += memory->tabSiblings;
        const bool whole = std::all_of(together.cbegin(), together.cend(), [&](const PanelId &member) {
            const DockPanel *panel = panels.value(member);
            const auto m = state.memory.constFind(member);
            if (!panel || !panel->isCollapsible() || m == state.memory.constEnd()
                || state.isPlaced(member)) {
                return false;
            }
            QStringList others = together;
            others.removeAll(member);
            return QSet<PanelId>(m->tabSiblings.cbegin(), m->tabSiblings.cend())
                == QSet<PanelId>(others.cbegin(), others.cend());
        });
        if (!whole)
            continue;
        const std::optional<ReturnPlace> place = state.returnPlace(id);
        if (!place || place->container != containerId)
            continue;
        // One per edge: the first to claim it.
        const bool claimed = std::any_of(edges.cbegin(), edges.cend(), [&](const ReopenEdge &e) {
            return e.anchor == place->anchor && e.side == place->side;
        });
        if (claimed)
            continue;
        edges.push_back({together, place->anchor, place->side});
        for (const PanelId &member : std::as_const(together))
            taken.insert(member);
    }
    return edges;
}

DockResult DockManagerPrivate::beginReopen(const QStringList &ids)
{
    LayoutState before = state;
    LayoutState next = state;
    if (DockResult r = next.reattachAll(ids, defaultWorkspaceId()); !r)
        return r;
    if (DockResult r = apply(std::move(next), false); !r)
        return r;
    reopenStart = std::move(before);
    reopening = ids;
    return DockResult::success();
}

void DockManagerPrivate::endResize(bool cancel, const std::vector<ResizeClose> &closing)
{
    if (!resizeStart)
        return;
    LayoutState start = std::move(*resizeStart);
    resizeStart.reset();
    // A drag that began by pulling panels out of an edge goes back, when
    // undone or given up, to before they were shown.
    LayoutState origin = reopenStart ? std::move(*reopenStart) : start;
    const QStringList pulledOut = std::exchange(reopening, {});
    reopenStart.reset();
    // Pulled out and pushed back in again: nothing has happened.
    const bool pushedBack = std::any_of(closing.begin(), closing.end(), [&](const ResizeClose &c) {
        const ContainerState *container = start.find(c.container);
        const LayoutNode *group = container ? container->tree.findNode(c.node) : nullptr;
        return group && std::any_of(group->panels.cbegin(), group->panels.cend(),
                                    [&](const PanelId &id) { return pulledOut.contains(id); });
    });
    if (cancel || pushedBack) {
        (void)apply(std::move(origin), false);
        return;
    }
    if (closing.empty()) {
        pushUndo(std::move(origin)); // one undo step for the whole drag
        Q_EMIT q->layoutChanged();
        return;
    }

    // The drag squeezed tab groups out: their panels are closed. What is
    // remembered of each is where it was when the drag began, at the size it
    // had then, beside the node that now has its room.
    LayoutState next = state;
    for (const ResizeClose &close : closing) {
        ContainerState *container = next.find(close.container);
        const ContainerState *before = start.find(close.container);
        if (!container || !before)
            continue;
        const LayoutNode *group = before->tree.findNode(close.node);
        const LayoutNode *heir = before->tree.findNode(close.heir);
        const LayoutNode *split = before->tree.parentOf(close.node);
        if (!group || !group->isTabs() || !heir || !split || !container->tree.findNode(close.node))
            continue;
        bool groupFirst = true;
        for (const LayoutNode &child : split->children) {
            if (child.id == close.node || child.id == close.heir) {
                groupFirst = child.id == close.node;
                break;
            }
        }
        const QStringList neighbors = LayoutTree(*heir).panels();
        for (const PanelId &panel : group->panels) {
            PanelMemory memory = start.capture(panel);
            memory.neighbors = neighbors;
            if (split->orientation == Qt::Horizontal)
                memory.neighborArea = groupFirst ? DockArea::Left : DockArea::Right;
            else
                memory.neighborArea = groupFirst ? DockArea::Top : DockArea::Bottom;
            next.memory.insert(panel, memory);
        }
        (void)container->tree.takeNode(close.node, close.heir);
    }
    pushUndo(std::move(origin)); // still one undo step
    (void)apply(std::move(next), false);
}

// --- Floating windows --------------------------------------------------------

void DockManagerPrivate::floatingGeometryChanged(const QString &containerId, const QRect &geometry)
{
    if (syncing)
        return;
    if (ContainerState *container = state.find(containerId))
        container->geometry = geometry;
}

bool DockManagerPrivate::closeFloatingByUser(const QString &containerId)
{
    const ContainerState *container = state.find(containerId);
    if (!container)
        return true;
    const QStringList inside = container->tree.panels();
    for (const PanelId &panel : inside) {
        if (!userMay(panel, DockFeature::Closable))
            return false;
    }
    return closePanels(inside).ok();
}

// --- Panels ------------------------------------------------------------------

DockPanel *DockManagerPrivate::createPanel(const PanelId &id, const QString &title)
{
    if (id.isEmpty()) {
        lastError = fail(DockError::InvalidArgument, QStringLiteral("a panel needs a non-empty id"));
        return nullptr;
    }
    if (panels.contains(id)) {
        lastError = fail(DockError::DuplicatePanel,
                         QStringLiteral("panel '%1' is already registered").arg(id));
        return nullptr;
    }
    auto *panel = new DockPanel(q, id);
    if (!title.isEmpty())
        get(panel)->title = title;
    panels.insert(id, panel);
    panelOrder.append(id);
    lastError = DockResult::success();
    return panel;
}

void DockManagerPrivate::adoptWidget(DockPanel *panel, QWidget *widget)
{
    get(panel)->widget = widget;
    panelByWidget.insert(widget, panel);
    // The content waits here, hidden, until a view shows it.
    widget->setParent(parkingWidget());
    widget->hide();
    widget->installEventFilter(panel);
    const PanelId id = panel->id();
    QObject::connect(widget, &QObject::destroyed, q, [this, id, widget] {
        panelByWidget.remove(widget);
        contentDestroyed(id);
    });
}

QWidget *DockManagerPrivate::ensureWidget(DockPanel *panel)
{
    DockPanel::Private *p = get(panel);
    if (p->widget || !p->factory)
        return p->widget;
    QWidget *widget = p->factory(p->id);
    if (!widget || panelByWidget.contains(widget))
        return nullptr;
    adoptWidget(panel, widget);
    Q_EMIT panel->widgetCreated(widget);
    return widget;
}

void DockManagerPrivate::reparentContent(DockPanel *panel, QWidget *parent)
{
    QWidget *widget = get(panel)->widget;
    if (!widget || widget->parentWidget() == parent)
        return;
    // The parking widget is no window as far as the application is concerned.
    const auto topLevelOf = [this](const QWidget *w) -> QWidget * {
        QWidget *window = w ? w->window() : nullptr;
        return window == parking ? nullptr : window;
    };
    QWidget *before = topLevelOf(widget->parentWidget());
    Q_EMIT panel->aboutToBeReparented();
    widget->setParent(parent);
    QWidget *after = topLevelOf(parent);
    Q_EMIT panel->reparented(before != after);
    if (before != after) {
        Q_EMIT panel->topLevelChanged(after);
        Q_EMIT q->panelWindowChanged(panel, after);
    }
}

void DockManagerPrivate::parkIfHostedBy(DockPanel *panel, const QWidget *host)
{
    QWidget *widget = get(panel)->widget;
    if (widget && widget->parentWidget() == host)
        reparentContent(panel, parkingWidget());
}

QWidget *DockManagerPrivate::parkingWidget()
{
    if (!parking) {
        parking = new QWidget;
        parking->setObjectName(QStringLiteral("QFlexDockParking"));
    }
    return parking;
}

QWidget *DockManagerPrivate::removePanel(const PanelId &id, DockManager::PlacementMemory memory,
                                         bool keepWidget)
{
    DockPanel *panel = panels.value(id);
    if (!panel) {
        lastError = unknownPanel(id);
        return nullptr;
    }
    if (committing) {
        lastError = fail(DockError::Busy,
                         QStringLiteral("panels cannot be unregistered while a change is applied"));
        return nullptr;
    }
    Q_EMIT q->panelAboutToBeUnregistered(panel);

    if (state.isPlaced(id)) {
        LayoutState next = state;
        (void)next.detach(id, true);
        next.memory[id].reopen = true;
        (void)apply(std::move(next), false);
    }
    if (memory == DockManager::PlacementMemory::Forget)
        state.memory.remove(id);
    if (activePanel == panel)
        setActivePanel(nullptr);

    QWidget *widget = get(panel)->widget;
    panels.remove(id);
    panelOrder.removeAll(id);
    if (widget) {
        panelByWidget.remove(widget);
        widget->removeEventFilter(panel);
        QObject::disconnect(widget, &QObject::destroyed, q, nullptr);
        widget->hide();
        widget->setParent(nullptr);
        if (!keepWidget) {
            delete widget;
            widget = nullptr;
        }
    }
    // Possibly called from one of the panel's own signals.
    panel->deleteLater();
    lastError = DockResult::success();
    return widget;
}

void DockManagerPrivate::contentDestroyed(const PanelId &id)
{
    if (destroying)
        return;
    DockPanel *panel = panels.value(id);
    if (!panel || get(panel)->factory)
        return; // a factory can simply make a new one
    // The content is gone, so the panel is too; its place is remembered.
    (void)removePanel(id, DockManager::PlacementMemory::Keep, true);
}

DockPanel *DockManagerPrivate::panelContaining(QWidget *widget) const
{
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (DockPanel *panel = panelByWidget.value(w))
            return panel;
        if (const auto *group = qobject_cast<const DockTabGroup *>(w))
            return panels.value(group->currentPanel());
        if (w->isWindow())
            break;
    }
    return nullptr;
}

void DockManagerPrivate::setActivePanel(DockPanel *panel)
{
    if (panel) {
        if (const std::optional<PanelLocation> location = state.locate(panel->id()))
            lastActiveIn.insert(location->container, panel->id());
    }
    if (activePanel == panel)
        return;
    const QPointer<DockPanel> previous = activePanel;
    activePanel = panel;
    if (previous) {
        get(previous)->active = false;
        Q_EMIT previous->activeChanged(false);
    }
    if (panel) {
        get(panel)->active = true;
        Q_EMIT panel->activeChanged(true);
    }
    refreshActiveMarks();
    Q_EMIT q->activePanelChanged(panel);
}

void DockManagerPrivate::updatePanelStates(bool emitSignals)
{
    // A closed panel is nobody's active panel.
    if (activePanel && !state.isPlaced(activePanel->id()))
        setActivePanel(nullptr);

    const QList<DockPanel *> all = panels.values();
    for (DockPanel *panel : all) {
        DockPanel::Private *p = get(panel);
        const std::optional<PanelLocation> before = p->location;
        const std::optional<PanelLocation> after = state.locate(p->id);
        if (before == after)
            continue;
        p->location = after;
        if (!emitSignals)
            continue;
        const QPointer<DockPanel> guard(panel);
        if (before.has_value() != after.has_value()) {
            Q_EMIT panel->openChanged(after.has_value());
            if (guard)
                Q_EMIT q->panelOpenChanged(panel, after.has_value());
        }
        if (guard && (before && before->isFloating()) != (after && after->isFloating()))
            Q_EMIT panel->floatingChanged(after && after->isFloating());
        if (guard && (before && before->isAutoHidden()) != (after && after->isAutoHidden()))
            Q_EMIT panel->autoHiddenChanged(after && after->isAutoHidden());
    }
}

void DockManagerPrivate::panelAppearanceChanged(DockPanel *panel)
{
    const std::optional<PanelLocation> location = state.locate(panel->id());
    if (!location)
        return;
    if (location->isAutoHidden()) {
        if (DockWorkspace *workspace = workspaceFor(location->container))
            get(workspace)->autoHide->refreshPanel(panel->id());
        return;
    }
    if (DockAreaWidget *area = areaFor(location->container)) {
        if (DockTabGroup *group = area->group(location->node))
            group->refreshPanel(panel->id());
    }
    if (DockFloatingWindow *window = floatingWindows.value(location->container))
        window->updateTitle();
}

void DockManagerPrivate::refreshActiveMarks()
{
    const PanelId active = activePanel ? activePanel->id() : PanelId();
    const auto mark = [&active](DockAreaWidget *area) {
        const QList<DockTabGroup *> groups = area->groups();
        for (DockTabGroup *group : groups)
            group->setActive(!active.isEmpty() && group->panelIds().contains(active));
    };
    for (DockWorkspace *workspace : std::as_const(workspaces))
        mark(get(workspace)->area);
    for (DockFloatingWindow *window : std::as_const(floatingWindows))
        mark(window->area());
}

void DockManagerPrivate::refreshAllAppearance()
{
    for (DockWorkspace *workspace : std::as_const(workspaces)) {
        get(workspace)->area->refreshAppearance();
        get(workspace)->autoHide->refreshAppearance();
    }
    for (DockFloatingWindow *window : std::as_const(floatingWindows)) {
        window->area()->refreshAppearance();
        window->refreshAppearance();
    }
}

void DockManagerPrivate::mousePressedOn(QWidget *widget)
{
    for (DockWorkspace *workspace : std::as_const(workspaces))
        get(workspace)->autoHide->pressedElsewhere(widget);
    if (DockPanel *panel = panelContaining(widget))
        setActivePanel(panel);
}

// --- Workspaces --------------------------------------------------------------

void DockManagerPrivate::workspaceDestroyed(DockWorkspace *workspace)
{
    workspaces.removeAll(workspace);
    const QString id = workspace->workspaceId();
    if (destroying)
        return;

    get(workspace)->autoHide->collapse();
    // Take the content out before the workspace's children are destroyed.
    for (DockPanel *panel : std::as_const(panels)) {
        QWidget *widget = get(panel)->widget;
        if (widget && workspace->isAncestorOf(widget))
            reparentContent(panel, parkingWidget());
    }
    get(workspace)->area->detachFromManager();
    get(workspace)->autoHide->detachFromManager();

    // reconcile() drops the container and closes (but remembers) its panels.
    (void)apply(state, false);
    lastActiveIn.remove(id);
    Q_EMIT q->workspaceRemoved(id);
}

DockAreaWidget *DockManagerPrivate::areaFor(const QString &containerId) const
{
    if (DockFloatingWindow *window = floatingWindows.value(containerId))
        return window->area();
    for (DockWorkspace *workspace : workspaces) {
        if (workspace->workspaceId() == containerId)
            return get(workspace)->area;
    }
    return nullptr;
}

QString DockManagerPrivate::workspaceIdFor(const QString &containerId) const
{
    const ContainerState *container = state.find(containerId);
    if (!container)
        return {};
    return container->kind == ContainerKind::Floating ? container->owner : container->id;
}

DockWorkspace *DockManagerPrivate::workspaceFor(const QString &containerId) const
{
    const QString id = workspaceIdFor(containerId);
    for (DockWorkspace *workspace : workspaces) {
        if (workspace->workspaceId() == id)
            return workspace;
    }
    return nullptr;
}

QString DockManagerPrivate::defaultWorkspaceId() const
{
    return workspaces.isEmpty() ? QString() : workspaces.first()->workspaceId();
}

QWidget *DockManagerPrivate::windowFor(const QString &containerId) const
{
    const DockAreaWidget *area = areaFor(containerId);
    return area ? area->window() : nullptr;
}

// --- Menus and icons ---------------------------------------------------------

QMenu *DockManagerPrivate::createPanelMenu(DockPanel *panel, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    const PanelId id = panel->id();
    const std::optional<PanelLocation> location = state.locate(id);
    const DockFeatures features = panel->features();

    const auto add = [&](const char *name, const QString &text, bool enabled, auto &&action) {
        QAction *a = menu->addAction(text);
        a->setObjectName(QLatin1String(name));
        a->setEnabled(enabled);
        QObject::connect(a, &QAction::triggered, q, std::forward<decltype(action)>(action));
        return a;
    };

    add("dockActionClose", DockManager::tr("Close"),
        location.has_value() && features.testFlag(DockFeature::Closable),
        [this, id] { (void)closePanels({id}); });

    QStringList others;
    for (const PanelId &other : groupPanels(id)) {
        if (other != id && userMay(other, DockFeature::Closable))
            others << other;
    }
    add("dockActionCloseOthers", DockManager::tr("Close Others"), !others.isEmpty(),
        [this, others] { (void)closePanels(others); });

    menu->addSeparator();
    if (location && location->isDocked()) {
        add("dockActionFloat", DockManager::tr("Float"), features.testFlag(DockFeature::Floatable),
            [this, id] { (void)floatPanels(id, false, {}); });
        add("dockActionAutoHide", DockManager::tr("Auto Hide"),
            features.testFlag(DockFeature::AutoHideable),
            [this, id] { (void)setAutoHide(id, true, DockArea::None); });
    } else if (location && location->isAutoHidden()) {
        add("dockActionPin", DockManager::tr("Pin"), true,
            [this, id] { (void)setAutoHide(id, false, DockArea::None); });
        add("dockActionFloat", DockManager::tr("Float"), features.testFlag(DockFeature::Floatable),
            [this, id] { (void)floatPanels(id, false, {}); });
    } else if (location) {
        add("dockActionDock", DockManager::tr("Dock"), features.testFlag(DockFeature::Movable),
            [this, id] { (void)dockBack(id); });
    }

    if (location && !location->isAutoHidden()) {
        const bool maximized = !state.find(location->container)->maximized.isEmpty();
        add("dockActionMaximize", maximized ? DockManager::tr("Restore") : DockManager::tr("Maximize"),
            features.testFlag(DockFeature::Maximizable),
            [this, id, maximized] { (void)setMaximized(id, !maximized); });
    }

    Q_EMIT q->panelContextMenuRequested(panel, menu);
    return menu;
}

QIcon DockManagerPrivate::icon(DockIcon which, const QWidget *styledBy) const
{
    const QIcon themed = theme.icons.value(which);
    if (!themed.isNull())
        return themed;
    // Drawn in the widget's own text colour rather than taken from the style's
    // fixed title-bar pixmaps, which stay dark on a dark palette.
    const QPalette palette = styledBy ? styledBy->palette() : QApplication::palette();
    return makeGlyphIcon(which, palette.color(QPalette::Active, QPalette::WindowText));
}

int DockManagerPrivate::handleWidth(const QWidget *styledBy) const
{
    if (theme.splitHandleWidth >= 0)
        return theme.splitHandleWidth;
    const QStyle *style = styledBy ? styledBy->style() : QApplication::style();
    return qMax(1, style->pixelMetric(QStyle::PM_SplitterWidth, nullptr, styledBy));
}

// =============================================================================
// DockManager
// =============================================================================

DockManager::DockManager(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<DockManagerPrivate>(this))
{
    Q_ASSERT_X(qobject_cast<QApplication *>(QCoreApplication::instance()), "DockManager",
               "a QApplication must exist before a DockManager is created");
    // Which panel the user is working in: follow keyboard focus and clicks.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (DockPanel *panel = d->panelContaining(now))
            d->setActivePanel(panel);
    });
    qApp->installEventFilter(this);
}

DockManager::~DockManager()
{
    d->destroying = true;
    d->committing = true; // nothing may change the layout from here on
    d->drag->cancel();

    // Views outlive us (workspaces belong to the application's windows): cut
    // them loose first so nothing calls back into a dying manager.
    for (DockWorkspace *workspace : std::as_const(d->workspaces)) {
        DockManagerPrivate::get(workspace)->area->detachFromManager();
        DockManagerPrivate::get(workspace)->autoHide->detachFromManager();
        DockManagerPrivate::get(workspace)->manager = nullptr;
    }
    for (DockFloatingWindow *window : std::as_const(d->floatingWindows))
        window->detachFromManager();

    // The content widgets are ours, wherever they currently sit.
    const QList<DockPanel *> all = d->panels.values();
    for (DockPanel *panel : all)
        delete DockManagerPrivate::get(panel)->widget.data();
    qDeleteAll(all);
    d->panels.clear();
    qDeleteAll(d->floatingWindows);
    d->floatingWindows.clear();
    delete d->parking.data();
}

bool DockManager::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        // A press travels up the parent chain when ignored; act on the first
        // (innermost) receiver only.
        const quint64 timestamp = static_cast<QMouseEvent *>(event)->timestamp();
        if (timestamp == 0 || timestamp != d->lastPressTimestamp) {
            d->lastPressTimestamp = timestamp;
            d->mousePressedOn(static_cast<QWidget *>(watched));
        }
    }
    return false;
}

// --- Workspaces --------------------------------------------------------------

DockWorkspace *DockManager::createWorkspace(const QString &id, QWidget *parent)
{
    QString workspaceId = id;
    while (workspaceId.isEmpty() || (id.isEmpty() && workspace(workspaceId)))
        workspaceId = QStringLiteral("workspace-%1").arg(++d->workspaceCounter);
    if (workspace(workspaceId) || d->state.find(workspaceId)) {
        d->lastError = fail(DockError::InvalidArgument,
                            QStringLiteral("workspace id '%1' is already in use").arg(workspaceId));
        return nullptr;
    }
    auto *created = new DockWorkspace(this, workspaceId, parent);
    d->workspaces.append(created);
    (void)d->apply(d->state, false); // reconcile() adds its container
    d->lastError = DockResult::success();
    Q_EMIT workspaceAdded(created);
    return created;
}

QList<DockWorkspace *> DockManager::workspaces() const
{
    return d->workspaces;
}

DockWorkspace *DockManager::workspace(const QString &id) const
{
    for (DockWorkspace *w : d->workspaces) {
        if (w->workspaceId() == id)
            return w;
    }
    return nullptr;
}

// --- Registration ------------------------------------------------------------

DockPanel *DockManager::registerPanel(const PanelId &id, QWidget *content, const QString &title)
{
    // A bad id is reported as such whatever else is wrong with the call.
    if (id.isEmpty()) {
        d->lastError = fail(DockError::InvalidArgument, QStringLiteral("a panel needs a non-empty id"));
        return nullptr;
    }
    if (d->panels.contains(id)) {
        d->lastError = fail(DockError::DuplicatePanel,
                            QStringLiteral("panel '%1' is already registered").arg(id));
        return nullptr;
    }
    if (!content) {
        d->lastError = fail(DockError::InvalidArgument, QStringLiteral("no content widget given"));
        return nullptr;
    }
    if (d->panelByWidget.contains(content)) {
        d->lastError = fail(DockError::InvalidArgument,
                            QStringLiteral("the widget already is the content of a panel"));
        return nullptr;
    }
    DockPanel *panel = d->createPanel(id, title);
    if (!panel)
        return nullptr;
    d->adoptWidget(panel, content);
    Q_EMIT panelRegistered(panel);

    // A layout restored earlier may be waiting for this panel.
    if (d->state.memory.value(id).reopen) {
        LayoutState next = d->state;
        if (next.reattach(id, d->defaultWorkspaceId()))
            (void)d->apply(std::move(next), false);
    }
    return panel;
}

DockPanel *DockManager::registerPanelFactory(const PanelId &id, DockPanelFactory factory,
                                             const QString &title)
{
    if (!factory) {
        d->lastError = fail(DockError::InvalidArgument, QStringLiteral("no factory given"));
        return nullptr;
    }
    DockPanel *panel = d->createPanel(id, title);
    if (!panel)
        return nullptr;
    DockManagerPrivate::get(panel)->factory = std::move(factory);
    Q_EMIT panelRegistered(panel);

    if (d->state.memory.value(id).reopen) {
        LayoutState next = d->state;
        if (next.reattach(id, d->defaultWorkspaceId()))
            (void)d->apply(std::move(next), false);
    }
    return panel;
}

DockResult DockManager::unregisterPanel(const PanelId &id, PlacementMemory memory)
{
    d->removePanel(id, memory, false);
    return d->lastError;
}

QWidget *DockManager::releasePanel(const PanelId &id, PlacementMemory memory)
{
    return d->removePanel(id, memory, true);
}

DockPanel *DockManager::panel(const PanelId &id) const
{
    return d->panels.value(id);
}

QList<DockPanel *> DockManager::panels() const
{
    QList<DockPanel *> result;
    for (const PanelId &id : d->panelOrder)
        result << d->panels.value(id);
    return result;
}

bool DockManager::hasPanel(const PanelId &id) const
{
    return d->panels.contains(id);
}

DockResult DockManager::lastError() const
{
    return d->lastError;
}

// --- Placement ---------------------------------------------------------------

DockResult DockManager::addPanel(const PanelId &id, DockWorkspace *workspace, DockArea area,
                                 double fraction)
{
    return movePanel(id, workspace, area, fraction);
}

DockResult DockManager::movePanel(const PanelId &id, DockWorkspace *workspace, DockArea area,
                                  double fraction)
{
    if (!workspace || workspace->manager() != this) {
        return fail(DockError::UnknownWorkspace,
                    QStringLiteral("the workspace does not belong to this manager"));
    }
    DropTarget target;
    target.container = workspace->workspaceId();
    target.area = area;
    target.fraction = fraction;
    return d->placePanel(id, target);
}

DockResult DockManager::movePanel(const PanelId &id, const PanelId &relativeTo, DockArea area,
                                  int tabIndex, double fraction)
{
    const std::optional<PanelLocation> location = d->state.locate(relativeTo);
    if (!location || location->isAutoHidden()) {
        return fail(DockError::NotPlaced,
                    QStringLiteral("panel '%1' is not in a tab group").arg(relativeTo));
    }
    DropTarget target;
    target.container = location->container;
    target.node = location->node;
    target.area = area;
    target.tabIndex = tabIndex;
    target.fraction = fraction;
    return d->placePanel(id, target);
}

DockResult DockManager::moveTabGroup(const PanelId &anyPanelOfGroup, DockWorkspace *workspace,
                                     DockArea area, double fraction)
{
    if (!workspace || workspace->manager() != this) {
        return fail(DockError::UnknownWorkspace,
                    QStringLiteral("the workspace does not belong to this manager"));
    }
    DropTarget target;
    target.container = workspace->workspaceId();
    target.area = area;
    target.fraction = fraction;
    return d->placeGroup(anyPanelOfGroup, target);
}

DockResult DockManager::moveTabGroup(const PanelId &anyPanelOfGroup, const PanelId &relativeTo,
                                     DockArea area, int tabIndex, double fraction)
{
    const std::optional<PanelLocation> location = d->state.locate(relativeTo);
    if (!location || location->isAutoHidden()) {
        return fail(DockError::NotPlaced,
                    QStringLiteral("panel '%1' is not in a tab group").arg(relativeTo));
    }
    DropTarget target;
    target.container = location->container;
    target.node = location->node;
    target.area = area;
    target.tabIndex = tabIndex;
    target.fraction = fraction;
    return d->placeGroup(anyPanelOfGroup, target);
}

DockResult DockManager::floatPanel(const PanelId &id, const QRect &geometry)
{
    return d->floatPanels(id, false, geometry);
}

DockResult DockManager::floatTabGroup(const PanelId &anyPanelOfGroup, const QRect &geometry)
{
    return d->floatPanels(anyPanelOfGroup, true, geometry);
}

DockResult DockManager::dockPanel(const PanelId &id)
{
    return d->dockBack(id);
}

DockResult DockManager::showPanel(const PanelId &id)
{
    if (!d->panels.contains(id))
        return unknownPanel(id);
    if (!d->state.isPlaced(id)) {
        LayoutState next = d->state;
        if (DockResult r = next.reattach(id, d->defaultWorkspaceId()); !r)
            return r;
        if (DockResult r = d->apply(std::move(next), true); !r)
            return r;
    }
    return d->activate(id, true);
}

DockResult DockManager::hidePanel(const PanelId &id)
{
    return d->closePanels({id});
}

DockResult DockManager::showPanels(const QStringList &ids)
{
    return d->showPanels(ids);
}

DockResult DockManager::hidePanels(const QStringList &ids)
{
    return d->closePanels(ids);
}

DockResult DockManager::togglePanel(const PanelId &id)
{
    if (!d->panels.contains(id))
        return unknownPanel(id);
    return d->state.isPlaced(id) ? hidePanel(id) : showPanel(id);
}

DockResult DockManager::activatePanel(const PanelId &id)
{
    return d->activate(id, true);
}

DockPanel *DockManager::activePanel() const
{
    return d->activePanel;
}

QStringList DockManager::tabGroupPanels(const PanelId &id) const
{
    return d->groupPanels(id);
}

DockResult DockManager::maximizePanel(const PanelId &id)
{
    return d->setMaximized(id, true);
}

DockResult DockManager::restoreMaximizedPanel()
{
    return d->setMaximized({}, false);
}

PanelId DockManager::maximizedPanel(const DockWorkspace *workspace) const
{
    for (const auto &c : d->state.containers) {
        if (c.maximized.isEmpty())
            continue;
        if (!workspace || c.id == workspace->workspaceId())
            return c.maximized;
    }
    return {};
}

DockResult DockManager::setPanelAutoHide(const PanelId &id, bool autoHide, DockArea edge)
{
    return d->setAutoHide(id, autoHide, edge);
}

// --- Policies ----------------------------------------------------------------

DockResult DockManager::setDockPolicy(const PanelId &id, const DockPolicy &policy)
{
    DockPanel *p = panel(id);
    if (!p)
        return unknownPanel(id);
    p->setPolicy(policy);
    return DockResult::success();
}

DockPolicy DockManager::dockPolicy(const PanelId &id) const
{
    const DockPanel *p = panel(id);
    return p ? p->policy() : DockPolicy{};
}

void DockManager::setDropFilter(DockDropFilter filter)
{
    d->dropFilter = std::move(filter);
}

// --- Persistence -------------------------------------------------------------

QByteArray DockManager::saveLayout() const
{
    LayoutSerializer::Document document;
    document.state = d->state;
    for (const DockWorkspace *workspace : std::as_const(d->workspaces)) {
        const QWidget *window = workspace->window();
        WindowPlacement placement;
        placement.maximized = window->isMaximized();
        placement.fullScreen = window->isFullScreen();
        const QRect normal = window->normalGeometry();
        placement.geometry = normal.isValid() ? normal : window->geometry();
        document.windows.insert(workspace->workspaceId(), placement);
    }
    return LayoutSerializer::toBytes(document);
}

DockResult DockManager::saveLayout(const QString &filePath) const
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return fail(DockError::IoError, file.errorString());
    file.write(saveLayout());
    if (!file.commit())
        return fail(DockError::IoError, file.errorString());
    return DockResult::success();
}

DockResult DockManager::restoreLayout(const QByteArray &json, DockRestoreReport *report)
{
    LayoutSerializer::Document document;
    QStringList warnings;
    if (DockResult r = LayoutSerializer::fromBytes(json, &document, &warnings); !r)
        return r;

    DockRestoreReport collected;
    collected.warnings = warnings;
    d->reconcile(document.state, &collected);

    // Windows must not come back on a monitor that is no longer there.
    const QList<QRect> screens = screenGeometries();
    for (auto &c : document.state.containers) {
        if (c.kind == ContainerKind::Floating)
            c.geometry = LayoutSerializer::fitToScreens(c.geometry, screens);
    }

    if (DockResult r = d->apply(std::move(document.state), true); !r)
        return r;

    if (d->restoreWindowGeometry) {
        for (DockWorkspace *workspace : std::as_const(d->workspaces)) {
            const auto it = document.windows.constFind(workspace->workspaceId());
            QWidget *window = workspace->window();
            if (it == document.windows.constEnd() || !window->isWindow())
                continue;
            if (it->geometry.isValid())
                window->setGeometry(LayoutSerializer::fitToScreens(it->geometry, screens));
            Qt::WindowStates windowState = window->windowState();
            windowState.setFlag(Qt::WindowMaximized, it->maximized);
            windowState.setFlag(Qt::WindowFullScreen, it->fullScreen);
            window->setWindowState(windowState);
        }
    }
    if (report)
        *report = collected;
    return DockResult::success();
}

DockResult DockManager::loadLayout(const QString &filePath, DockRestoreReport *report)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(DockError::IoError, file.errorString());
    return restoreLayout(file.readAll(), report);
}

bool DockManager::restoresWindowGeometry() const
{
    return d->restoreWindowGeometry;
}

void DockManager::setRestoresWindowGeometry(bool enabled)
{
    d->restoreWindowGeometry = enabled;
}

// --- Presets -----------------------------------------------------------------

DockResult DockManager::savePreset(const QString &name)
{
    if (name.isEmpty())
        return fail(DockError::InvalidArgument, QStringLiteral("a preset needs a name"));
    d->presets.insert(name, d->state);
    Q_EMIT presetsChanged();
    return DockResult::success();
}

DockResult DockManager::applyPreset(const QString &name)
{
    const auto it = d->presets.constFind(name);
    if (it == d->presets.constEnd())
        return fail(DockError::InvalidArgument, QStringLiteral("no preset '%1'").arg(name));
    return d->apply(*it, true);
}

DockResult DockManager::removePreset(const QString &name)
{
    if (d->presets.remove(name) == 0)
        return fail(DockError::InvalidArgument, QStringLiteral("no preset '%1'").arg(name));
    Q_EMIT presetsChanged();
    return DockResult::success();
}

QStringList DockManager::presetNames() const
{
    return d->presets.keys();
}

QByteArray DockManager::savePresets() const
{
    QJsonObject presets;
    for (auto it = d->presets.cbegin(); it != d->presets.cend(); ++it) {
        LayoutSerializer::Document document;
        document.state = it.value();
        presets.insert(it.key(), LayoutSerializer::toJson(document));
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("QFlexDock.Presets"));
    root.insert(QStringLiteral("schemaVersion"), LayoutSerializer::SchemaVersion);
    root.insert(QStringLiteral("presets"), presets);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

DockResult DockManager::restorePresets(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(DockError::ParseError, parseError.errorString());
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("QFlexDock.Presets")
        || !root.value(QStringLiteral("presets")).isObject()) {
        return fail(DockError::ParseError, QStringLiteral("not a QFlexDock preset collection"));
    }

    // All or nothing: one unreadable preset rejects the whole collection.
    QMap<QString, LayoutState> loaded;
    const QJsonObject presets = root.value(QStringLiteral("presets")).toObject();
    for (auto it = presets.begin(); it != presets.end(); ++it) {
        LayoutSerializer::Document preset;
        if (DockResult r = LayoutSerializer::fromJson(it.value().toObject(), &preset); !r)
            return r;
        loaded.insert(it.key(), std::move(preset.state));
    }
    d->presets = std::move(loaded);
    Q_EMIT presetsChanged();
    return DockResult::success();
}

void DockManager::saveDefaultLayout()
{
    d->defaultLayout = d->state;
}

DockResult DockManager::resetLayout()
{
    if (!d->defaultLayout) {
        return fail(DockError::InvalidArgument,
                    QStringLiteral("no default layout has been saved"));
    }
    return d->apply(*d->defaultLayout, true);
}

// --- Undo / redo -------------------------------------------------------------

bool DockManager::canUndo() const
{
    return !d->undoStack.empty();
}

bool DockManager::canRedo() const
{
    return !d->redoStack.empty();
}

DockResult DockManager::undo()
{
    if (d->undoStack.empty())
        return fail(DockError::InvalidArgument, QStringLiteral("nothing to undo"));
    LayoutState target = std::move(d->undoStack.back());
    d->undoStack.pop_back();
    LayoutState current = d->state;
    const DockResult result = d->apply(target, false);
    if (result)
        d->redoStack.push_back(std::move(current));
    else
        d->undoStack.push_back(std::move(target));
    Q_EMIT undoStateChanged();
    return result;
}

DockResult DockManager::redo()
{
    if (d->redoStack.empty())
        return fail(DockError::InvalidArgument, QStringLiteral("nothing to redo"));
    LayoutState target = std::move(d->redoStack.back());
    d->redoStack.pop_back();
    LayoutState current = d->state;
    const DockResult result = d->apply(target, false);
    if (result)
        d->undoStack.push_back(std::move(current));
    else
        d->redoStack.push_back(std::move(target));
    Q_EMIT undoStateChanged();
    return result;
}

void DockManager::clearUndoHistory()
{
    d->undoStack.clear();
    d->redoStack.clear();
    Q_EMIT undoStateChanged();
}

int DockManager::undoLimit() const
{
    return d->undoLimit;
}

void DockManager::setUndoLimit(int limit)
{
    d->undoLimit = qMax(0, limit);
    while (int(d->undoStack.size()) > d->undoLimit)
        d->undoStack.erase(d->undoStack.begin());
    Q_EMIT undoStateChanged();
}

// --- Behaviour and look ------------------------------------------------------

bool DockManager::linkedSplittersEnabled() const
{
    return d->linkedSplitters;
}

void DockManager::setLinkedSplittersEnabled(bool enabled)
{
    if (d->linkedSplitters == enabled)
        return;
    d->linkedSplitters = enabled;
    Q_EMIT linkedSplittersEnabledChanged(enabled);
}

bool DockManager::isCornerResizeEnabled() const
{
    return d->cornerResize;
}

void DockManager::setCornerResizeEnabled(bool enabled)
{
    if (d->cornerResize == enabled)
        return;
    d->cornerResize = enabled;
    d->refreshAllAppearance();
}

DockManager::GroupHeader DockManager::groupHeader() const
{
    return d->groupHeader;
}

void DockManager::setGroupHeader(GroupHeader header)
{
    if (d->groupHeader == header)
        return;
    d->groupHeader = header;
    d->refreshAllAppearance();
}

bool DockManager::isCenterDropEnabled() const
{
    return d->centerDrop;
}

void DockManager::setCenterDropEnabled(bool enabled)
{
    d->centerDrop = enabled;
}

bool DockManager::isTabDragPreviewEnabled() const
{
    return d->tabDragPreview;
}

void DockManager::setTabDragPreviewEnabled(bool enabled)
{
    d->tabDragPreview = enabled;
}

bool DockManager::floatsOnOutsideDrop() const
{
    return d->floatOnOutsideDrop;
}

void DockManager::setFloatsOnOutsideDrop(bool enabled)
{
    d->floatOnOutsideDrop = enabled;
}

DockManager::FloatingFrame DockManager::floatingWindowFrame() const
{
    return d->floatingFrame;
}

void DockManager::setFloatingWindowFrame(FloatingFrame frame)
{
    d->floatingFrame = frame;
}

bool DockManager::isDragGhostEnabled() const
{
    return d->dragGhostEnabled;
}

void DockManager::setDragGhostEnabled(bool enabled)
{
    d->dragGhostEnabled = enabled;
}

DockTheme DockManager::theme() const
{
    return d->theme;
}

void DockManager::setTheme(const DockTheme &theme)
{
    d->theme = theme;
    d->refreshAllAppearance();
    Q_EMIT themeChanged();
}

void DockManager::setOverlayPainter(std::shared_ptr<DockOverlayPainter> painter)
{
    d->overlayPainter = std::move(painter);
}

} // namespace QFlexDock
