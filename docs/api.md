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
| `floatPanel(id, geometry)`, `floatTabGroup(anyPanel, geometry)` | Into a new floating window |
| `dockPanel(id)` | From floating or auto-hide back to where it was docked last |
| `showPanel(id)`, `hidePanel(id)`, `togglePanel(id)` | Reopen a closed panel in its old place / close it (it stays registered) |
| `activatePanel(id)`, `activePanel()` | Bring the tab to the front, raise the window, give focus |
| `maximizePanel(id)`, `restoreMaximizedPanel()` | Let the tab group fill its container; the layout tree is not changed |
| `setPanelAutoHide(id, on, edge)` | Collapse into an auto-hide bar / pin back |

`fraction` is the share the new side takes: 0.25 by default against a workspace, 0.5 when splitting a group.

**Policies** — `setDockPolicy(id, policy)`, `setDropFilter(filter)` (called for every drop; return `false` to refuse).

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
`setFloatingWindowFrame(FloatingFrame::Native | Custom | Minimal)`, `setDragGhostEnabled()`, `setTheme()`,
`setOverlayPainter()`. See [styling.md](styling.md) and [platform-notes.md](platform-notes.md).

`setGroupHeader()` chooses what tab groups have at their top:

| `GroupHeader` | |
|---|---|
| `Tabs` (default) | The tabs, always. Drag a tab to move a panel, the empty part of the bar to move the group |
| `TitleBar` | A title bar naming the current panel; drag it to move that panel, double click to float it or dock it again. Tabs appear below the content once a group holds more than one panel |

Which buttons the header has is a theme token (`DockTheme::titleButtons`).

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
`hidePanel`, `togglePanel`, `activatePanel`, `movePanel(id, relativeTo, Dock.Bottom)`, `movePanelToWorkspace`,
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
