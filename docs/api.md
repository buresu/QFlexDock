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
| `<QFlexDock/NativeWindowAdapter.h>` | `NativeWindowAdapter` |
| `<QFlexDock/Global.h>` | `PanelId`, `DockArea`, `DockFeature`, `DockGuide`, `DockGroupHeader`, `DockTitleButton`, `DockError`, `DockResult` |
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
| The small form of a panel (`DockPanel::setCompactWidget()`) | `DockManager` | Destroyed with the panel, or when another is set |
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
| `movePanel(id, workspace, area, fraction)`, `DockWorkspace::addPanel(id, area, fraction)` | Dock against the whole workspace: an edge, or `Center` to join the tab group used last |
| `movePanel(id, relativeTo, area, tabIndex, fraction)` | Dock against the tab group of `relativeTo`: split it on an edge, or `Center` to become a tab |
| `moveTabGroup(anyPanel, …)` | The same for a whole tab group |
| `floatPanel(id, geometry)`, `floatTabGroup(anyPanel, geometry)` | Into a new floating window; a closed panel is shown in one |
| `dockPanel(id)` | From floating or auto-hide back to where it was docked last |
| `openPanel(id)`, `closePanel(id)`, `togglePanel(id)` | Reopen a closed panel in its old place, or with none remembered at its default placement / close it (it stays registered) |
| `openPanels(ids)`, `closePanels(ids)` | The same for several panels as one change and one undo step |
| `activatePanel(id)`, `activePanel()` | Bring the tab to the front, raise the window, give focus |
| `raisePanel(id)` | Bring the tab to the front and nothing else: the active panel and keyboard focus stay where they are |
| `tabGroupPanels(id)`, `currentPanel(id)` | The panels sharing a tab group with `id`, in the order of their tabs, and the one of them in front |
| `maximizePanel(id)`, `restoreMaximizedPanel()` | Let the tab group fill its container; the layout tree is not changed |
| `setPanelAutoHide(id, on, edge)` | Collapse into an auto-hide bar / pin back |

`fraction` is the share the new side takes: 0.25 by default against a workspace, 0.5 when splitting a group.

A panel that is closed leaves its room to the neighbour it was split off from; nothing else changes size.
`openPanel()` puts it back at the size it had. Side areas that can be put away are just that:
`closePanels()` on what is in them, `openPanels()` to bring them back, in any order. Panels closed together
return in their old tab order, with the same tab in front. With `DockPanel::setCollapsible(true)` the user
can put them away as well, by pushing a split handle against them, and pull them out again.

A panel that leaves a split leaves the room it shows as: where a size limit held it narrower than its
share (a palette of fixed width docked at a quarter of the window), what goes to its neighbour is that width.

**Where a panel goes unasked.** `DockPanel::setDefaultPlacement(placement)` says where a panel is put when
it is to be shown and nothing says where: `openPanel()` of a panel no place is remembered for (without a
default placement: a tab in the first workspace), and a layout put in place as a whole that knows nothing
of the panel (see [persistence.md](persistence.md)). A `DockPlacement` is what `movePanel()` takes:
`relativeTo` and an area to dock against that panel's tab group, or `workspace` (an id) and an area to dock
against a workspace; with both, the workspace is used while the panel named is not in a tab group.

```cpp
QFlexDock::DockPlacement placement;
placement.relativeTo = "console";                 // among the tabs of the console...
placement.workspace = "main";                     // ...or, while that is closed, in this workspace
placement.area = QFlexDock::DockArea::Center;
manager.registerPanel("problems", view, "Problems")->setDefaultPlacement(placement);
manager.openPanels({"console", "problems"});      // in this order: one may be placed beside another
```

**Policies** — `DockPanel::setPolicy(policy)`, `setDropFilter(filter)` (called for every drop; return `false` to
refuse). The request names the panels, the target and its area, and for a drop on a header the position
among the tabs there (`tabIndex`, -1 elsewhere).

```cpp
QFlexDock::DockPolicy policy;
policy.features = QFlexDock::AllDockFeatures & ~QFlexDock::DockFeatures(QFlexDock::DockFeature::Closable);
policy.allowedAreas = QFlexDock::DockArea::Left | QFlexDock::DockArea::Right;
policy.allowedWorkspaces = {"main"};      // empty: no restriction
manager.panel("scene")->setPolicy(policy);
```

Features: `Movable`, `Closable`, `Floatable`, `Tabbable` (both the dragged and the receiving panel must allow it),
`AutoHideable`, `Maximizable`. A zone that is not allowed is neither shown nor accepted.

**Layouts** — `saveLayout()`, `restoreLayout()`, `loadLayout()`, presets, `resetLayout()`: see [persistence.md](persistence.md).
`undo()` / `redo()` cover layout changes; dragging a splitter is one step, switching tabs and moving a
floating window are none.

**Behaviour and looks** — `setLinkedSplittersEnabled()`, `setCornerResizeEnabled()`, `setSplitterPushEnabled()`, `setFloatsOnOutsideDrop()`,
`setCenterDropEnabled()`, `setTabDragPreviewEnabled()`, `setFloatingWindowFrame(FloatingWindowFrame::Custom | Minimal | Native)`,
`setFloatingWindowType(FloatingWindowType::Window | Tool)`, `setGroupHeader()`, `setTitleBarMovesGroup()`,
`setAutoHideReveal(AutoHideReveal::Over | Beside)`,
`setDragGhostEnabled()`, `setTheme()`, `setOverlayPainter()`. See [styling.md](styling.md) and
[platform-notes.md](platform-notes.md). Each setting with a getter is a property of the manager and has a
signal for its changes (`groupHeaderChanged()`, …).

**One workspace.** What differs from one workspace to the next is set on the `DockWorkspace`, for it and
the floating windows it owns: `setColumnDocking()`, and three that take the place of what the manager says
for all of them, `setGroupHeader()`, `setTitleButtons()` and `setCenterDropEnabled()`. Each of the three is
the manager's again after `unsetGroupHeader()`, `unsetTitleButtons()`, `unsetCenterDropEnabled()`.
`maximizedPanel()` and `panels()` tell what the workspace holds.

`currentPanel()` is the panel of a workspace that is being worked in, or was last: the tab in front of the
group in which a panel was active last, in the workspace or a floating window it owns (before any was: of
its first group). Where the documents have a workspace to themselves it is the document the rest of the
application is about, and it stays the same while the user works in a panel elsewhere.
`currentPanelChanged(id)` tells of another one. `setCurrentPanel(id)` makes a panel of the workspace the
current one without activating it: its tab comes to the front, and keyboard focus stays where it is.

`setPlaceholderWidget(widget)` gives a workspace something to show while no panel is docked in it: a start
page, a hint. The widget fills the workspace, which owns it, and the workspace takes dragged panels as an
empty one does (as long as the widget does not accept drops itself).

A panel that is put away at a border (`setPanelAutoHide()`) comes out when its name in the bar is chosen,
and goes back with a click elsewhere. `AutoHideReveal::Over` (the default) shows it over the dock area.
With `Beside` it takes its room from the dock area, which lays out all it holds in what is left: as if
the panel were docked along that border for as long as it is out. The layout itself does not change.

**The drop guide.** What is shown while something is dragged, and with that what is aimed at, is a theme
token (`DockTheme::overlay.guide`):

| `DockGuide` | |
|---|---|
| `Zones` (default) | Five large areas that cover the tab group under the pointer, and a band along the border of the workspace |
| `Preview` | The same areas, not drawn: only the rectangle the drop would take is shown |
| `Buttons` | A cross of small buttons in the middle of the tab group under the pointer, and a button at each border of the workspace. Only a button takes the drop, or a header: the tabs of a group, and a title bar that names its panel |

With `Buttons`, what is let go of beside the buttons is dropped nowhere, and floats where
`setFloatsOnOutsideDrop(true)` says so. A workspace inside a panel (below) covers little of itself with
its buttons, so the workspace around it offers its own along with them, to what it takes as well: a ring
around the inner cross docks beside the inner workspace, and the buttons at the outer border are there
as always. The inner workspace then has no buttons at its own border.

The edge areas of `Zones` and `Preview` are a share of the group they lie over (`edgeFraction`), or with
`DockTheme::overlay.edgeExtent` as many pixels deep on every group, however large.

With `setSplitterPushEnabled(true)` a boundary that is dragged against a neighbour that can get no smaller
goes on: the neighbour is moved along as it is, and what lies behind it gives the room (nearest first;
likewise what the nearest cannot take goes to the next). A strip of buttons or a palette of fixed width
between two areas is then no obstacle to resizing them. By default a boundary stops at such a neighbour.

Linked splitters are boundaries in one line: they are dragged as one, and they stay in one line when the
minimum or maximum size of a panel holds one of them back (the others go where it can go). Turned off,
every boundary is on its own in both respects.

**Windows of tabs.** Floating windows need no workspace, so an application can consist of them alone.
With `setCenterDropEnabled(false)` the middle of a tab group takes no drop: a panel becomes a tab by the
header only, and let go of anywhere else it floats. Panels that allow `DockArea::Center` only never split a
window, and for them the whole title row takes a tab, not just the tabs. A title bar that names its panel
(`DockGroupHeader::TitleBar`, or the title of a floating window) takes one as well, and a workspace with
nothing in it where its tabs will be: along its top, as high as a row of tabs. `examples/chrome-style` is
built this way. `DockWorkspace::setCenterDropEnabled(false)` does it for one workspace and the floating
windows it owns: the documents of `examples/photoshop-style`, among panels that take a drop anywhere.

```cpp
manager.setFloatingWindowFrame(QFlexDock::DockManager::FloatingWindowFrame::Minimal);  // the tabs are the title
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

**Columns.** `DockWorkspace::setColumnDocking(true)` has a workspace, and the floating windows it owns, dock in
columns. A column is what stands above one another: a tab group, or several.

- A drop on the left or right side of a group goes beside the column that group is in, never into it; on
  its top or bottom into the column; on the group into its tabs.
- Every column of panels that may be moved has a bar above it. Dragged, it moves the column as it is. Its
  button, or a double click, iconifies the column (below).
- A floating window that is one column is moved and closed by that bar and has no title row:
  `FloatingWindowFrame::Custom` is `Minimal` there.

`setColumnIconified(anyPanelOfColumn, true)` shrinks a column to a strip of buttons, one for each panel,
those of a tab group under a grip; `false` shows the panels again, at their size. It works in any
workspace, with or without the bars. A button brings its tab group out beside the strip, over what is
there, and puts it away again; so does `activatePanel()`. A floating window that is nothing but the strip
has no room beside it and makes it: the window grows by the group for as long as that is out. The strip is as narrow as its icons and can be
dragged wider, up to the width of its titles. What is dropped on the buttons of a group becomes one of
them, above or below them a group of the column, at its side a column beside it. What is dragged out of
the strip by a button or a grip is docked like anything else, and stays iconified if it becomes a window.
`isColumnIconified()` and `columnPanels()` tell about the column a panel is in.

Which group is out is not part of the layout, like the panel that slid out of an auto-hide bar: it is
neither saved nor undone. That a column is iconified is both.

`examples/photoshop-style` is built this way:

```cpp
panels->setColumnDocking(true);                              // bars, and sides that mean the column
manager.movePanel("layers", "documents", QFlexDock::DockArea::Right);
manager.movePanel("channels", "layers", QFlexDock::DockArea::Center);
manager.movePanel("history", "layers", QFlexDock::DockArea::Bottom);   // one column, two groups
manager.setColumnIconified("layers", true);                  // three buttons, under two grips

QFlexDock::DockPanel *tools = manager.registerPanel("tools", toolsInTwoColumns);
tools->setCompactWidget(toolsInOneColumn);                   // what it is in a strip, in place of a button
```

`setGroupHeader()` chooses what tab groups have at their top:

| `DockGroupHeader` | |
|---|---|
| `Tabs` (default) | The tabs, always. Drag a tab to move a panel, the empty part of the bar to move the group |
| `TitleBar` | A title bar naming the current panel; drag it to move that panel, double click to float it or dock it again. Tabs appear below the content once a group holds more than one panel |

With `setTitleBarMovesGroup(true)` the title bar stands for all the panels stacked under it: dragging it
moves the whole tab group, and a double click floats the group or docks all of it again, as one change.
A single panel is then moved by its tab.

`DockWorkspace::setGroupHeader(header)` gives one workspace, and the floating windows it owns, a header of its
own: documents under their tabs in the middle of tool panels with title bars. A panel takes the header of
where it is put.

Which built-in buttons the header has is a theme token (`DockTheme::titleButtons`: `Menu`, `Maximize`,
`Float`, `AutoHide`, `Close`), and `DockWorkspace::setTitleButtons(buttons)` sets them for one workspace; a
panel adds its own with `DockPanel::setTitleActions()`. `AutoHide` puts every panel of the group that
allows it into the auto-hide bar of the nearest border, as one change. A header that is the title of a floating window
([platform-notes.md](platform-notes.md)) always has maximize and close, which there act on the window.

**Signals** — `layoutAboutToChange()` / `layoutChanged()`, `panelAboutToMove()` / `panelMoved()`,
`panelOpenChanged()`, `panelCloseRequested()`, `panelWindowChanged()`, `activePanelChanged()`, `panelRegistered()` /
`panelAboutToBeUnregistered()`, `workspaceAdded()` / `workspaceRemoved()`, `undoStateChanged()`,
`presetsChanged()`, `themeChanged()`, and `panelContextMenuRequested(panel, menu)` to add items to a tab's menu.

## DockPanel

Title, icon, tool tip, policy, and state (`isOpen()`, `isActive()`, `isCurrent()`, `isFloating()`,
`isAutoHidden()`, `workspace()`), all as properties that QML can bind to. `isCurrent()` is true for the tab
in front of its group, one in every group; `isActive()` for the one panel the user works in. `setDirty()`, `setPinnedTab()` and `setPreviewTab()`
only change how the tab is drawn; the application supplies the state. `setTabCloseButton(false)` leaves
the tab of a panel that may be closed without a button for it: it is closed from its menu, with the
middle button, or with the window it floats in. (`DockTheme::tabCloseButtons` does so for every tab.)

**Closing.** When the user asks for a panel to be closed (its tab button, the header's, the middle mouse
button, its menu, the floating window it is in), `closeRequested()` tells of it, and the panel is closed.
With `setClosesOnRequest(false)` the signal is all that happens, and the application decides: a document
with unsaved changes asks first, one that is gone once closed is unregistered. `closePanel()` and a layout
that is restored close a panel without a request. A floating window stays open while a panel in it does.

```cpp
panel->setClosesOnRequest(false);
QObject::connect(panel, &QFlexDock::DockPanel::closeRequested, &window, [&manager, panel] {
    if (!panel->isDirty() || confirmDiscard(panel))
        manager.unregisterPanel(panel->id());     // or panel->close(), to keep it for later
});
```

**A menu of panels.** `toggleViewAction()` is a checkable action that shows the panel and closes it: its
text is the title, and it is checked while the panel is open. The panel owns it.

```cpp
for (QFlexDock::DockPanel *panel : manager.panels())
    viewMenu->addAction(panel->toggleViewAction());
```

**A small form.** `setCompactWidget(widget)` gives a panel a small form of its own for where its column is
iconified (see Columns): it stands there in place of the panel's button, and nothing is brought out for
it. A palette of tools one button wide that is two wide as a panel, say. The manager owns the widget.

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
pointer returns. Let go there, its panels are closed (`panelOpenChanged()` tells), and `openPanel()` brings
them back at the size the group had before the drag. Every panel of a group has to be collapsible for the
group to go. The whole drag is one undo step.

Closed collapsible panels, however they were closed, leave an edge to pull at: dragging inwards from the
side they went to shows them again, as wide as the pointer is far from the edge. What comes out is what was
closed together (one `closePanels()` call, or one group pushed away), not a tab that was closed on its own
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
`examples/vscode-style` is built this way, and `examples/vs-style`, where the tool panels may go in
with the documents as well (no `allowedWorkspaces` for them).

```cpp
auto *holder = new QWidget;
auto *documents = manager.createWorkspace("documents", holder);   // lay it out in `holder`
QFlexDock::DockPanel *center = manager.registerPanel("center", holder);
center->setFeatures({});
center->setHeaderVisible(false);
tools->addPanel("center", QFlexDock::DockArea::Center);

QFlexDock::DockPolicy policy;
policy.allowedWorkspaces = {"documents"};
manager.panel("readme")->setPolicy(policy);
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
auto *controller = new QFlexDock::QmlDockController(&manager, &engine);
controller->installInto(&engine);         // `dock` in QML; enums via `import QFlexDock`, e.g. `Dock.Left`
QFlexDock::QmlPanelAdapter::registerPanel(&manager, "qml", &engine, QUrl("qrc:/Panel.qml"), "QML");
```

The controller has to outlive the QML that reads `dock`, and the manager owns the panels' scenes: as a child
of the engine it goes last. One that is destroyed before the manager leaves the scenes with a `dock` that is
null, and every binding on it reports a `TypeError`.

`QmlDockController` mirrors `DockManager` and has no state of its own. From QML: `dock.openPanel(id)`,
`closePanel`, `openPanels(ids)`, `closePanels(ids)`, `togglePanel`, `activatePanel`, `raisePanel`,
`tabGroupPanels(id)`, `currentPanel(id)`, `movePanel(id, relativeTo, Dock.Bottom)`, `movePanelToWorkspace`,
`floatPanel`, `dockPanel`, `maximizePanel`, `restoreMaximizedPanel`, `setPanelAutoHide`, `undo`, `redo`,
`resetLayout` (each returns `true` on success; `dock.lastError` has the reason otherwise), the properties
`activePanel`, `panelIds`, `openPanelIds`, `maximizedPanel`, `canUndo`, `canRedo`, and `dock.panel(id)` for
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
