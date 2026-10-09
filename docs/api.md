# Public API

Everything is in namespace `QFlexDock`. The header comments are the reference; this page gives the
overview and the rules that are hard to see from the headers.

| Header | Contents |
|---|---|
| `<QFlexDock/DockManager.h>` | `DockManager`, `DockRestoreReport`, `DockPanelFactory` |
| `<QFlexDock/DockWorkspace.h>` | `DockWorkspace` |
| `<QFlexDock/DockPanel.h>` | `DockPanel` |
| `<QFlexDock/DockPolicy.h>` | `DockPolicy`, `DockDropRequest`, `DockDropFilter` |
| `<QFlexDock/DockTheme.h>` | `DockTheme`, `DockOverlayStyle`, `DockOverlayPainter` |
| `<QFlexDock/LayoutModel.h>` | `LayoutNode`, `LayoutTree` (for reading and inspecting a layout) |
| `<QFlexDock/NativeWindowAdapter.h>` | `NativeWindowAdapter` |
| `<QFlexDock/Global.h>` | `PanelId`, `NodeId`, `DockArea`, `DockFeature`, `DockError`, `DockResult` |
| `<QFlexDockQuick/QmlDockController.h>`, `<QFlexDockQuick/QmlPanelAdapter.h>` | The `QFlexDock::Quick` module |

## Conventions

- **Threads** — call everything from the GUI thread.
- **Order** — create the `DockManager` after the `QApplication`.
- **Errors** — operations that can fail return a `DockResult` (converts to `bool`; `error()`, `message()`).
  **A failed operation changes nothing.** No exceptions are thrown. When a function returns `nullptr`,
  `DockManager::lastError()` says why.
- **Re-entrancy** — the layout cannot be changed from a slot of `layoutAboutToChange()` or
  `panelAboutToMove()` (`DockError::Busy`). It can from `layoutChanged()` and `panelMoved()`.
- **Policies restrict the user, not the application** — `DockPolicy` and `DockFeature` apply to drags,
  tab buttons, menus and closing a floating window. API calls are not restricted.

## Ownership

| Object | Owner | |
|---|---|---|
| `DockPanel`, content widgets (also those made by a factory) | `DockManager` | `registerPanel()` hands the widget over; `releasePanel()` gives it back, parentless and hidden |
| `DockWorkspace` | its Qt parent | When destroyed, its panels are closed, not destroyed |
| Floating windows | `DockManager` | Created and destroyed to match the layout |
| `QQmlEngine` | the caller | Must outlive the panels that use it |
| A `QWindow` given to `NativeWindowAdapter` | the container widget, hence the manager | |

A content widget is **the same instance** for as long as its panel is registered: it only moves between
tab groups, workspaces and floating windows. Deleting it from outside unregisters the panel.

Unregistering a panel keeps its place in the layout by default (`PlacementMemory::Keep`): register the same
id again and it reappears where it was. That is how reloading a plugin works. `PlacementMemory::Forget` drops it.

## DockManager

**Workspaces** — `createWorkspace(id, parent)`, `workspaces()`, `workspace(id)`. The id links a workspace to
saved layouts, so give it a stable name.

**Panels** — `registerPanel(id, widget, title)`, `registerPanelFactory(id, factory, title)` (the widget is
created when the panel is first shown), `unregisterPanel()`, `releasePanel()`, `panel(id)`, `panels()`.
Registering does not show a panel.

**Placement**

| Function | |
|---|---|
| `addPanel(id, workspace, area, fraction)`, `movePanel(id, workspace, area, fraction)` | Dock against the whole workspace: an edge, or `Center` to join the tab group used last |
| `movePanel(id, relativeTo, area, tabIndex, fraction)` | Dock against the tab group of `relativeTo`: split it on an edge, or `Center` to become a tab |
| `moveTabGroup(anyPanel, …)` | The same for a whole tab group |
| `floatPanel(id, geometry)`, `floatTabGroup(anyPanel, geometry)` | Into a new floating window; a closed panel is shown in one |
| `dockPanel(id)` | From floating or auto-hide back to where it was docked last |
| `showPanel(id)`, `hidePanel(id)`, `togglePanel(id)` | Reopen a closed panel in its old place / close it (it stays registered) |
| `showPanels(ids)`, `hidePanels(ids)` | The same for several panels as one change and one undo step |
| `activatePanel(id)`, `activePanel()` | Bring the tab to the front, raise the window, give focus |
| `tabGroupPanels(id)` | The panels sharing a tab group with `id`, in the order of their tabs |
| `maximizePanel(id)`, `restoreMaximizedPanel()` | Let the tab group fill its container; the layout tree is not changed |
| `setPanelAutoHide(id, on, edge)` | Collapse into an auto-hide bar / pin back |

`fraction` is the share the new side takes: 0.25 by default against a workspace, 0.5 when splitting a group.

A panel that is closed leaves its room to the neighbour it was split off from; nothing else changes size.
`showPanel()` puts it back at the size it had. Side areas that can be put away are just that:
`hidePanels()` on what is in them, `showPanels()` to bring them back, in any order. Panels closed together
return in their old tab order, with the same tab in front. With `DockPanel::setCollapsible(true)` the user
can put them away as well, by pushing a split handle against them, and pull them out again.

**Policies** — `setDockPolicy(id, policy)`, `setDropFilter(filter)` (called for every drop; return `false` to
refuse). The request names the panels, the target and its area, and for a drop on a header the position
among the tabs there (`tabIndex`, -1 elsewhere).

```cpp
QFlexDock::DockPolicy policy;
policy.features = QFlexDock::AllDockFeatures & ~QFlexDock::DockFeatures(QFlexDock::DockFeature::Closable);
policy.allowedAreas = QFlexDock::DockArea::Left | QFlexDock::DockArea::Right;
policy.allowedWorkspaces = {"main"};      // empty: no restriction
manager.setDockPolicy("scene", policy);
```

Features: `Movable`, `Closable`, `Floatable`, `Tabbable` (both the dragged and the receiving panel must allow it),
`AutoHideable`, `Maximizable`. A zone that is not allowed is neither shown nor accepted.

**Layouts** — `saveLayout()`, `restoreLayout()`, `loadLayout()`, presets, `resetLayout()`: see [persistence.md](persistence.md).
`undo()` / `redo()` cover layout changes; dragging a splitter is one step, switching tabs and moving a
floating window are none.

**Behaviour and looks** — `setLinkedSplittersEnabled()`, `setCornerResizeEnabled()`, `setFloatsOnOutsideDrop()`,
`setCenterDropEnabled()`, `setTabDragPreviewEnabled()`, `setFloatingWindowFrame(FloatingFrame::Native | Custom | Minimal)`,
`setDragGhostEnabled()`, `setTheme()`, `setOverlayPainter()`. See [styling.md](styling.md) and
[platform-notes.md](platform-notes.md).

**Windows of tabs.** Floating windows need no workspace, so an application can consist of them alone.
With `setCenterDropEnabled(false)` the middle of a tab group takes no drop: a panel becomes a tab by the
header only, and let go of anywhere else it floats. Panels that allow `DockArea::Center` only never split a
window, and for them the whole title row takes a tab, not just the tabs. `examples/chrome-style` is built
this way:

```cpp
manager.setFloatingWindowFrame(QFlexDock::DockManager::FloatingFrame::Minimal);  // the tabs are the title
manager.setCenterDropEnabled(false);
manager.setFloatsOnOutsideDrop(true);
manager.setTabDragPreviewEnabled(true);                       // tabs show the drag as it will turn out

QFlexDock::DockPanel *tab = manager.registerPanel("tab-1", page, "New Tab");
QFlexDock::DockPolicy policy;
policy.allowedAreas = QFlexDock::DockArea::Center;
tab->setPolicy(policy);
manager.floatPanel("tab-1", QRect(100, 80, 1200, 800));       // a window
manager.movePanel("tab-2", "tab-1", QFlexDock::DockArea::Center);   // another tab in it
```

A window is gone with its last panel. In a `Minimal` window the header of its one group stands in for the
title bar: dragged beside the tabs it moves the window, a double click there maximizes it.

`setTabDragPreviewEnabled(true)` (for any layout, not only this one) lets the tab bars show a tab drag as
it would turn out: the dragged tab is out of its bar at once and the tabs behind it close up, and the tabs
it is held over make room where it would go, in place of the mark between two tabs. Only what is shown
changes; the layout changes with the drop, and Esc puts everything back.

`setGroupHeader()` chooses what tab groups have at their top:

| `GroupHeader` | |
|---|---|
| `Tabs` (default) | The tabs, always. Drag a tab to move a panel, the empty part of the bar to move the group |
| `TitleBar` | A title bar naming the current panel; drag it to move that panel, double click to float it or dock it again. Tabs appear below the content once a group holds more than one panel |

Which built-in buttons the header has is a theme token (`DockTheme::titleButtons`); a panel adds its own
with `DockPanel::setTitleActions()`.

**Signals** — `layoutAboutToChange()` / `layoutChanged()`, `panelAboutToMove()` / `panelMoved()`,
`panelOpenChanged()`, `panelWindowChanged()`, `activePanelChanged()`, `panelRegistered()` /
`panelAboutToBeUnregistered()`, `workspaceAdded()` / `workspaceRemoved()`, `undoStateChanged()`,
`presetsChanged()`, `themeChanged()`, and `panelContextMenuRequested(panel, menu)` to add items to a tab's menu.

## DockPanel

Title, icon, tool tip, policy, and state (`isOpen()`, `isActive()`, `isFloating()`, `isAutoHidden()`,
`workspace()`), all as properties that QML can bind to. `setDirty()`, `setPinnedTab()` and `setPreviewTab()`
only change how the tab is drawn; the application supplies the state.

**Fixed content.** `setHeaderVisible(false)` leaves a panel without a header while it is alone in its group.
With no dock features either, it is content that stays where the application put it, and other panels dock around it:

```cpp
QFlexDock::DockPanel *view = manager.registerPanel("view", viewWidget);
view->setFeatures({});              // not movable, closable, floatable or tabbable
view->setHeaderVisible(false);
workspace->addPanel("view", QFlexDock::DockArea::Center);
```

**Collapsible.** `setCollapsible(true)` lets a split handle push the panel's tab group out of the way: once
the drag would leave the group less than half of its minimum size, it is shown as gone, and it is back if the
pointer returns. Let go there, its panels are closed (`panelOpenChanged()` tells), and `showPanel()` brings
them back at the size the group had before the drag. Every panel of a group has to be collapsible for the
group to go. The whole drag is one undo step.

Closed collapsible panels, however they were closed, leave an edge to pull at: dragging inwards from the
side they went to shows them again, as wide as the pointer is far from the edge. What comes out is what was
closed together (one `hidePanels()` call, or one group pushed away), not a tab that was closed on its own
before. There is no such edge where a split handle already runs.

**Buttons of your own in the header.** `setTitleActions()` takes `QAction`s that are shown in the header of
the panel's tab group while the panel is the current one there. An action with a menu opens it, a separator
becomes a line, and a `QWidgetAction` puts its widget there. The actions stay yours. There are three places,
each with its own list: `DockTitlePlace::End` (the default, before the built-in buttons), `Start` (before
the tabs) and `AfterTabs` (right behind the last tab; the tabs then take only the room they need).

```cpp
auto *split = new QAction(splitIcon, "Split Right", panel);
auto *filter = new QWidgetAction(panel);
filter->setDefaultWidget(new QLineEdit);
panel->setTitleActions({filter, split});
panel->setTitleActions({newTab}, QFlexDock::DockTitlePlace::AfterTabs);
```

**A workspace inside a panel.** The content of a panel may hold another workspace: a place for documents in
the middle of the tool panels. With `allowedWorkspaces`, each kind of panel stays in its own workspace, and a
drag goes to the workspace that takes what is dragged. Split handles work across the two: where a boundary
of the inner workspace ends on one of the outer workspace, the point can be dragged to move both.
`examples/vscode-style` is built this way.

```cpp
auto *holder = new QWidget;
auto *documents = manager.createWorkspace("documents", holder);   // lay it out in `holder`
QFlexDock::DockPanel *center = manager.registerPanel("center", holder);
center->setFeatures({});
center->setHeaderVisible(false);
tools->addPanel("center", QFlexDock::DockArea::Center);

QFlexDock::DockPolicy policy;
policy.allowedWorkspaces = {"documents"};
manager.setDockPolicy("readme", policy);
documents->addPanel("readme");
```

Signals about the content's lifecycle, mainly for GPU and native-window content:

| Signal | When |
|---|---|
| `aboutToBeReparented()` | Just before the content gets a new parent; a native surface may be destroyed |
| `reparented(bool topLevelChanged)` | Just after |
| `topLevelChanged(QWidget *)` | The content is now in another top-level window (`nullptr` when closed) |
| `visibilityChanged(bool)` | It really appeared on / disappeared from the screen |
| `widgetCreated(QWidget *)` | A factory created the content |

## QFlexDock::Quick

```cpp
QQmlEngine engine;
QFlexDock::DockManager manager;
QFlexDock::QmlDockController controller(&manager);
controller.installInto(&engine);          // `dock` in QML; enums via `import QFlexDock`, e.g. `Dock.Left`
QFlexDock::QmlPanelAdapter::registerPanel(&manager, "qml", &engine, QUrl("qrc:/Panel.qml"), "QML");
```

`QmlDockController` mirrors `DockManager` and has no state of its own. From QML: `dock.showPanel(id)`,
`hidePanel`, `showPanels(ids)`, `hidePanels(ids)`, `togglePanel`, `activatePanel`, `tabGroupPanels(id)`, `movePanel(id, relativeTo, Dock.Bottom)`, `movePanelToWorkspace`,
`floatPanel`, `dockPanel`, `maximizePanel`, `restoreMaximizedPanel`, `setPanelAutoHide`, `undo`, `redo`,
`resetLayout` (each returns `true` on success; `dock.lastError` has the reason otherwise), the properties
`activePanel`, `panels`, `openPanels`, `maximizedPanel`, `canUndo`, `canRedo`, and `dock.panel(id)` for
bindings such as `dock.panel("console").open`. `QmlPanelAdapter::registerLazyPanel()` loads the QML when the
panel is first shown.

## NativeWindowAdapter

```cpp
auto *adapter = QFlexDock::NativeWindowAdapter::registerPanel(&manager, "viewport", renderWindow, "Viewport");
connect(adapter, &QFlexDock::NativeWindowAdapter::surfaceAboutToBeDestroyed, renderer, &Renderer::releaseSwapchain);
connect(adapter, &QFlexDock::NativeWindowAdapter::surfaceCreated, renderer, &Renderer::createSwapchain);
```

Embeds a `QWindow` with `QWidget::createWindowContainer()` and reports when its surface is created,
destroyed or exposed and when its screen changes. Devices, swapchains and render loops are the
application's business. The limits are in [platform-notes.md](platform-notes.md).
