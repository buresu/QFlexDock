# Architecture

> **The docking state of the whole application is one value, `LayoutState`, and only
> `DockManagerPrivate::apply()` may replace it. The widgets merely show that value.**

```
  API / drop / menu ─▶ edit a copy of LayoutState ─▶ reconcile ─▶ normalize ─▶ validate
                              │ failure: nothing changed, a DockResult is returned
                              ▼ success
                       swap the state ─▶ syncViews() ─▶ signals
                              ▼
   DockWorkspace / DockFloatingWindow ─ DockAreaWidget ─ DockTabGroup / DockSplitHandle / DockDropOverlay
```

| Layer | Main parts | Needs widgets |
|---|---|---|
| Model | `LayoutNode`, `LayoutTree`, `LayoutState` | No |
| Geometry | `LayoutSolver`, `SplitterCoordinator`, `DropZoneLayout` | No |
| Controller | `DockManager`, `DockPanel`, `DockDragController` | Yes |
| View | `DockWorkspace`, `DockAreaWidget`, `DockTabGroup`, `DockTabBar`, `DockSplitHandle`, `DockFloatingWindow`, `DockDropOverlay`, `DockAutoHide*`, `DockColumnBar`, `DockIconStrip` | Yes |
| Persistence | `LayoutSerializer`, `LayoutMigration` | No |
| Optional | `QFlexDock::Quick`, `NativeWindowAdapter` | Yes |

## Layout model

A `LayoutTree` is the layout of one container (a workspace or a floating window). It has two kinds of node:
**tabs** (panel ids and the active one) and **splits** (an orientation and two or more children — the tree
is n-ary). Each node has a `weight`, its share among its siblings. Nodes hold no `QWidget*`, so trees are
plain values that can be copied, edited and thrown away. `NodeId`s are unique within the process, tie a tab
node to its widget across changes, and are not saved.

Every mutating function leaves the tree in normal form, or unchanged if it fails:

- no empty tab group, no split with fewer than two children;
- no split directly inside a split of the same orientation, unless it is an iconified column;
- no iconified node inside an iconified node;
- sibling weights are positive and sum to 1;
- a tab group's active panel is one of its panels;
- node ids are unique, and a panel id appears once;
- depth is at most `LayoutTree::MaxDepth` (128).

A node can be **iconified**: a column that is shown as a strip of buttons instead of its panels. It is one
thing to everything that lays the tree out, whatever is in it. `LayoutTree::columnOf()` says what the
column of a node is: the iconified node it is or lies in; else, for a tab group among tab groups that are
stacked above one another and nothing else, the vertical split holding them; else the node itself.
Docking above or below a node joins its column, iconified if that is; only beside a column does a node
keep being iconified itself.

`LayoutState` adds: a panel is in **one place across all containers** (a tree or an auto-hide bar), floating
containers are not empty, a maximized panel is in its container. It also remembers, for every panel that is
not placed (closed, floated, auto-hidden, unregistered, or not yet registered), where it goes back to: its
former tab neighbours, else the node it was next to (at the share of the split it had), else its floating
window, else a default workspace.

## Applying a change

Everything goes through `apply(next, recordUndo)`:

1. **reconcile** — drop containers of workspaces that are gone (their panels are remembered as closed), add
   the ones that are missing, take unregistered panels out of the trees.
2. **normalize, validate** — if this fails, nothing has happened.
3. Emit `layoutAboutToChange()` and `panelAboutToMove()`.
4. Push the old state for undo, swap, and bring the widgets in line with `syncViews()`.
5. Emit `panelMoved()`, `layoutChanged()`, …

Between 3 and 4 the manager is busy and refuses changes (`DockError::Busy`), so what the outside sees is
always the state before or the state after. Undo, redo, presets, reset and JSON restore all hand a stored
`LayoutState` to `apply()`; reconcile deals with whatever has disappeared since.

`syncViews()` matches tab nodes to tab group widgets by `NodeId` and leaves unchanged groups alone. All dock
areas are synced first, and only then is content that is shown nowhere moved to a hidden parking widget, so
a move between groups — even between windows — is a single reparent. That matters for content with a
native surface. Tab groups that are no longer needed are deleted with `deleteLater()`, since one of them
may be where the user's action came from.

## Geometry

`DockAreaWidget` has a `QLayout` subclass so that the tree's minimum size reaches the window; the actual
arithmetic is in `LayoutSolver`. It hands out pixels by weight, pins children that hit their minimum or
maximum, and redistributes the rest. Rounding gives back one pixel at a time by largest remainder, so no
pixel is gained or lost.

**Linked splitters.** Two handles move together when they have the same orientation, belong to different
splits, lie on one line (their positions differ by less than a handle width) and are contiguous along it
(the gap is at most one handle width, i.e. a crossing handle). The relation is transitive. Aligned handles
with a panel between them are not linked. A drag is always computed from the layout at its start, so
rounding does not accumulate, and the range is the intersection of what every handle in the run allows.
Alt moves a single handle. One drag is one undo step.

**Keeping lines.** Every split is solved for itself, so a minimum or maximum in one row would take its bar
out of the line it shares with the row below. `LayoutSolver::solve()` therefore looks at the layout a
second time, without any limits, to see which handles the weights put in one line (the same rule as
above), and where such a line has parted it holds all of its bars at one place: the one nearest to their
weights that every split involved has room for. Splits with a held handle are placed again, and the lines
are gone through until nothing moves. Where no place suits them all (two minimums that fit side by side
in no row, though each row fits), each split stays as it was solved. A drag starts from where a line is
held and turns that into weights. None of this happens with `setLinkedSplittersEnabled(false)`, in a
layout no limit has touched, or in less room than the layout's minimum size.

**Corners.** Where a vertical and a horizontal boundary meet (always a T or a cross), a `DockSplitCorner`
sits on top and drags both: the run through the corner along x and the one along y, computed separately,
since they change different splits and neither axis limits the other.

A dock area inside a panel of another one (a workspace as content) reports its layout to the area around
it, which adds the inner bars to its own when it looks for corners. An inner bar that ends on an outer one,
give or take the frame of the tab group in between, makes a corner of the outer area, and dragging it moves
handles in both areas. Corners among inner bars alone stay the inner area's.

**Squeezing a group out.** A handle dragged so far that a tab group of collapsible panels would be left less
than half of its minimum size makes that group give way (`SplitterCoordinator::squeezed()`). During the drag
this is display state, like maximizing: the area lays out a copy of its tree without the group, and the
handle keeps its widget, since hiding it would lose the mouse. When the drag ends, the manager closes the
group's panels in one transaction, remembering each at the size it had when the drag began.

**Pulling panels out of an edge.** For closed collapsible panels the area puts a `DockEdgeHandle` along the
edge they would return to (`LayoutState::returnPlace()`). The first move inwards shows them for real, in a
transaction that is not recorded for undo, and from there on the drag is an ordinary drag of the handle
beside them, set to the pointer's distance from the edge. So the squeezing above applies as it is: they
stay out of view until half of their minimum fits, and go again if pushed back. If that is how the drag
ends, or with Escape, the state from before is put back; otherwise that state becomes the one undo step.

**Columns.** Where a container docks in columns (`DockManager::setColumnDocking()`), the area puts a
`DockColumnBar` above every column its user may move. The solver knows nothing of it: the node at the top
of a column is asked for that much more height, and its widget is placed below the bar. An iconified node
gets a `DockIconStrip` in place of the tab groups in it, whose content waits in the parking widget. The
one group that may be out beside its strip is an ordinary `DockTabGroup` that the area places itself, over
its other widgets: view state, like the popup of an auto-hide bar, so opening and closing it is no
transaction. An area that is nothing but one strip has no room beside it: the strip keeps its width at
the side and the group has the rest, and a floating window is made as large as the two
(`DockFloatingWindow::fitIconified()`, once a change is applied and when a group comes out or goes).

**Grab margins.** A handle follows the style's `PM_SplitterWidth`, which may be a single pixel — too thin to
aim at, and not hit-tested at all by Qt 6.12. Handles are therefore at least 7px wide to the mouse, with the
extra margin masked out of painting. The mask is also what lets a hovered handle be drawn wider than its
bar (`DockTheme::splitHandleHoverWidth`) without anything moving.

## Drag and drop

1. Dragging a tab (or the empty part of a tab bar, for the whole group) creates a `DragSession` and starts a
   `QDrag` whose `QMimeData` carries only a random token.
2. Dock areas check that the token belongs to **the session in progress**. Mime data that merely names a
   panel id, or a token from a finished session, moves nothing.
3. `DockAreaWidget::candidateAt()` turns the pointer position into a `DropCandidate` for the overlay.
   Nothing changes yet.
4. A drop only registers its target. **After `QDrag::exec()` has returned**, the policies are checked again
   and the change is committed as one transaction.

Priority at a position: the band along the border (docking against the whole workspace), then tabs
(insertion and reordering — only over the tabs themselves), then the five zones of the panel. The five zones
are a center rectangle and four trapezoids that together cover the target, so there is no small icon to aim
for. Over the dragged group itself the center zone means "leave it here" and changes nothing.

`DockGuide::Preview` has the same areas and shows only the place of the drop. `DockGuide::Buttons` has a
button per area instead (`DropButtonLayout`): a cross in the middle of the group, kept inside the dock
area, and a button at each border. Nothing but a button, tabs or a title bar is a target then. The cross
stays with its group while the pointer is on one of its buttons, which may lie over a neighbouring group.
Since buttons leave a dock area nearly free, an area inside a panel of another one asks the area around it
for its candidate as well (`resolveDrag()`), if that takes what is dragged: the outer area lays the
buttons for the group that holds the inner one around the inner cross (a second ring) and shows its own
guide. A button of the inner area wins, then one of the outer area, then a header.

In a container that docks in columns, a hit on the left or right area of a group is aimed at
`columnOf()` that group: a split, where the column is several groups. Such a target may be gone once what
is dragged has left it (a column of two groups, one of them dragged beside it), so it is found again by a
panel that stays (`retarget()` in DockManager.cpp). A strip of buttons is hit like the groups it stands
for, each by its buttons. A drag of a whole column (`DockDragController::beginColumn()`) has a split for
its source node, as a drag of a whole floating window has.

Two things change this for windows that are rows of tabs (or for one workspace of them, by
`setCenterDropEnabled(workspace, false)`). Where a group has no edge zone to offer (the
dragged panels allow `Center` only), its whole title row counts as its tabs. And with
`DockManager::setCenterDropEnabled(false)` there is no center zone, not even the "leave it here" one: what
is not dropped on a header is dropped nowhere, which `finish()` treats like a drop outside every window.

**Previewing a tab drag** (`DockManager::setTabDragPreviewEnabled()`) is a matter of the views alone. A tab
group can show its node with one panel left out (`setDraggedOut()`, set by the drag once it has its
pictures of the group) and with an empty tab kept open among the others (`setDropGap()`, set by the dock
area from the candidate under the pointer). The state is not touched. Positions are counted among the tabs
shown, the empty one excepted, and translated back to a position among the group's panels; a drop is worked
out before the preview is taken down, so that it goes where the tabs showed it would.

Only the coordinates of Qt's drag events are used, never global positions or `QApplication::widgetAt()`.

On Wayland a drag can carry a window ([platform-notes.md](platform-notes.md)). A tab drag carries a ghost
rather than really detaching the panel, which keeps "nothing changes during a drag" true; a ghost that
nobody took is adopted as the view of a new floating container. On Windows and macOS nothing carries a
window, so the controller moves it to the pointer on a timer for as long as the drag lasts; such a window lets the
pointer through (`Qt::WindowTransparentForInput`), or the drag would find nothing but it: a ghost always,
a floating window that is itself moved only while another dock window is at the pointer, so that other
applications are not offered the drag. A ghost made that way is replaced by a window proper where it was
dropped. (On macOS it also takes the drop itself where no dock window is underneath, because a drag that
nobody took returns late there.) What happens after a drag is all in
`DockDragController::finish()`, which tests drive without a real drag.

## Choices worth knowing

- **Content widgets belong to the manager**, as with `QTabWidget::addTab()`. Destroying a workspace parks
  its content instead of destroying it.
- **Closing a floating window** closes its panels and remembers the window, so showing a panel again brings
  the same arrangement back. If one of its panels is not closable, the window does not close.
- **Policies restrict user actions only**, so the application can always build the layout it wants.
- **Native windows are hidden during a dock drag** because they would cover the drop guide. A separate
  translucent top-level window for the guide was rejected: its position and stacking cannot be guaranteed on Wayland.
- **Floating windows use the native frame by default**: moving, resizing and snapping then work as the
  window system intends, with the least custom code to get wrong.
- **The window-carrying drag relies on an undocumented Qt behaviour.** It is the one exception to "public
  Qt API only", accepted because everything falls back cleanly when it is absent.
- **A handle does not push further neighbours** when the one next to it reaches its minimum size, unless
  asked to (`DockManager::setSplitterPushEnabled()`): `SplitterCoordinator` then hands what the nearest
  child cannot give or take to the ones behind it, in order.
- **A node that leaves a split gives its share to one neighbour** (the one before it; after it for the
  first), instead of spreading it over all of them. Where a size limit held the node at another size
  than its share, the share is first made what it showed as (`DockManagerPrivate::settleShare()`): the
  neighbour gets the room there was, not the room there would have been. The memory of a closed panel holds the share it had, and
  putting it back takes that share out of the neighbour again. So areas that are closed and reopened, in any
  order, come back at their sizes, as long as the window has not been resized in between: sizes are shares,
  not pixels.
- **A floating container needs no workspace.** Its owner may be empty, and `floatPanel()` shows a closed
  panel in a new one, so an application can be floating windows only. Nothing else is special about that
  case: the same containers, the same drags.
- **The header's own actions are per panel, not per group or window.** The current panel brings them, in
  three places (`DockTitlePlace`). What looks like a window's buttons in `examples/chrome-style` are the
  actions of whichever tab is in front.
- **A torn-off panel keeps its size.** A window with a frame of its own is made larger by that frame, and a
  floating window dragged by all it holds (where nothing carries it: X11) moves by as much as the pointer
  did, not to the pointer.
- **A dock area ignores a drag it may not take anything from** (the dragged panels are not allowed in its
  workspace). Qt then offers the drag to the widgets above it, which is what makes a workspace inside a
  panel of another workspace work: the inner area takes its own panels, the outer one everything else.
