# Platform notes

What is stated here as working was observed in a test or by hand. "Not verified" means it is expected to
work from Qt's documentation, but nobody has checked.

| | Automated tests | Real `QDrag` |
|---|---|---|
| Linux X11 (Xvfb) | All suites pass | **Automated** (`tst_realdrag`) |
| Linux Wayland (headless KWin and Weston) | All suites pass | Tab drags tried by hand on KWin; cannot be automated |
| `offscreen` | All suites pass (no `QOpenGLWidget`) | — |
| Windows | All suites pass on `offscreen` and with the native platform (on a hidden desktop, see below) | Tried by hand; cannot be automated |
| macOS | All suites pass on `offscreen`; **never run with the native platform** | Tried by hand; not automated |

## Drag and drop

Dock drags use `QDrag`. The layout never changes during a drag; the drop is committed as one transaction afterwards.

| | Follows the pointer | Dropped outside every dock area |
|---|---|---|
| Wayland, compositor with `xdg-toplevel-drag` | A **ghost**: a window showing the dragged panel at its real size | The ghost becomes the floating window |
| Wayland, other compositors | A picture of the tab | Nothing |
| Windows | A ghost, moved by QFlexDock and three quarters opaque | A floating window where the ghost is |
| X11 | A picture of the tab | The panel floats at the pointer |
| macOS | A ghost, as on Windows | A floating window where the ghost is |

### Windows carried by a drag (Wayland)

A Wayland client can neither move a window to the pointer nor see the pointer over other windows. The
compositor can, however, move a window as part of a drag (`xdg-toplevel-drag-v1`). QFlexDock uses that:

- **Dragging a tab** carries a ghost, held where it was grabbed. One tab out of several keeps its place in
  the ghost's row of tabs, so that the pointer stays on it; in the window it becomes it is the first tab.
  Dropped on a dock area it docks and the ghost disappears; dropped elsewhere the ghost itself becomes the
  floating window; Esc just removes it. The ghost is what is dragged and nothing else: the frame of
  the window, whichever it is, comes when it is dropped (on Windows and macOS too, **not verified** there).
- **Dragging everything a floating window contains** (its only tab or only tab group) carries that window
  itself: no second window appears.
- **A floating window with a custom frame** can be dragged by its title row and dropped onto a dock area.

Things to know:

- Qt has no public API for this. Its Wayland plugin attaches a window to a drag when the drag's
  `QMimeData` carries two MIME formats of Qt's own (the mechanism behind `QDockWidget`, present since
  Qt 6.6, **not documented**). If a future Qt changes it, drags fall back to the picture of the tab.
- The protocol is still in staging and compositor support varies (KWin 6.7 has it). There is no way to ask
  in advance, so QFlexDock watches: a window that really is carried keeps the pointer at the same spot. If
  the pointer travels across it instead, carrying is given up for the rest of the process.
- The ghost is opaque, so it hides the part of the drop guide below and to the right of the pointer.
- **The `Native` frame on KWin.** KWin (6.7) holds a carried window by its frame, not by its content, so
  under KWin's title bar the pointer is off by the height of that bar. The ghost has no frame and is held
  where it was grabbed; dropped, it gets the frame and its content moves down by the title bar (seen with
  Qt 6.12). This needs Qt 6.11 and `xdg-decoration` version 2; with an older Qt the ghost keeps the frame
  and is off, as is a `Native` floating window dragged by its only tab with any Qt. `Custom` and `Minimal`
  windows are held exactly.
- `DockManager::setDragGhostEnabled(false)` turns all of this off. Outside drops then do nothing on
  Wayland, because they cannot be told apart from a cancelled drag.

### Windows moved along with a drag (Windows, macOS)

Nothing carries a window on Windows or macOS, but a client may move one to the pointer. QFlexDock does, on
a timer, for as long as the drag lasts:

- **Dragging a tab** shows the same ghost as on Wayland. The pointer goes through it, so the drag reaches
  the dock area underneath, and it is translucent, so the drop guide shows. Dropped outside every dock area,
  a floating window appears in its place (the ghost itself is not kept).
- **Dragging everything a floating window contains** moves that window itself. It is a window being moved,
  and in the way of the drag, until the pointer is over another window with a dock area: only there does it
  turn translucent and let the pointer through. So no other application sees the drag, or comes to the
  front when the window is put down over it. (Where a window of another application lies between the two,
  that one does.) Dropped on a dock area the window docks; Esc puts it back where it was. A maximized window
  is not moved: a ghost stands in for it.
- What stands for a title bar still moves its window through the window system, which snaps it to the
  screen edges: a custom title row, and in a `Minimal` window the header beside the tabs. Such a window is
  docked by dragging a tab (or, with `DockGroupHeader::TitleBar`, the title of its panel).
- Outside a dock area the pointer shows the system's "no drop" cursor, although letting go there floats.
- `DockManager::setDragGhostEnabled(false)` goes back to a picture of the tab.

All of this was tried by hand on Windows 11 at 200% scaling, Esc included. **Not verified:** a maximized
`Minimal` window taken by its header, drags by touch or pen (a drop is told from Esc by the mouse button),
and monitors of different scaling.

On macOS it is the same code, tried by hand on macOS 27 with Qt 6.11 (a ghost docked through, dropped
outside, Esc, also with the button let go of right after it, a window dragged by its only tab). What
differs is this.

- A drag that nobody took returns to Qt only after the system has slid its picture back to where the drag
  began, a moment after the button was let go of. QFlexDock therefore asks the system for the mouse button
  and the Escape key while the drag lasts (`CGEventSourceButtonState`, `CGEventSourceKeyState`): the window
  stops following when the button goes up and floats where it was then; on Esc a ghost disappears and a
  window that was itself moved goes back at once. The floating window appears when the drag has returned.
- To spare a drop outside that wait, the ghost takes the drop itself wherever no window with a dock area
  is under the pointer (it lets the pointer through only over one): the drag returns at once, and no other
  application sees it (tried by hand). Over a dock window but on no dock area of it, the wait remains.
- The picture that slides back is an empty one: Qt gives a drag without a picture one of its own.
- **Not verified:** what Windows leaves open above.

### Dropping outside = floating

With `DockManager::setFloatsOnOutsideDrop(true)` a drag that no dock area took floats the panel.
**It is on by default on X11, Wayland, Windows and macOS**, the platforms where a drop outside can be told from Esc:
on Wayland by the drop action Qt reports for a window-carrying drag, on X11 by whether Esc was pressed
and whether the mouse button is still down when the drag ends (covered by `tst_realdrag`). On Windows
the drag loop is the system's: Qt sees neither the key nor, until the drag is over, the release, so the
system is asked whether the button is still down (tried by hand). On macOS the system is asked for both
the button and the key, during the drag (tried by hand).

Content is never offered a dock drag: it goes to the dock area the content is in, also over a widget that
accepts whatever is dragged onto it (`QQuickWidget` does). Other drags reach the content as usual.

## Floating window frame

`DockManager::setFloatingWindowFrame()` chooses the frame of floating windows created from then on.

| | `Custom` (default) | `Minimal` | `Native` |
|---|---|---|---|
| Title bar and border | A border and one title drawn by QFlexDock (below) | Only a border; the headers of the groups inside serve as the title | The window system's |
| Move and resize | Requested with `QWindow::startSystemMove()` / `startSystemResize()` | Resize as `Custom`; moved by dragging a header (below) | The window system |
| Snapping, tiling, window menu | Up to the window system | Up to the window system | Yes |
| Re-dock by dragging the title | A header that is the title: as `Minimal`. The title row: on Wayland with `xdg-toplevel-drag` | Yes, it is a dock drag (on Windows and macOS: a tab only) | No (drag a tab instead) |

**The title of a `Custom` window** is the header of its tab group, as long as it holds one: a single panel
is named there without a tab, several have their tabs there, and the maximize and close buttons in it are
the window's. A window that holds more than one tab group, or a panel without a header, has a title row of
its own above them.

A window without a title row (`Minimal`, and `Custom` with one tab group) is moved by a dock drag of what
it contains: on Wayland, Windows and macOS the window follows the pointer, on X11 a picture does and the
window then moves by as much as the pointer did. (On Windows and macOS that is so for its only tab or its
title; the header beside the tabs moves the window and nothing else.) Where outside drops do not float
(they were turned off, and nothing carries a window), the drag just moves the window and docking is left to
the float button. A double click on the header of its one tab group, beside the tabs, maximizes such a
window. A `Minimal` window that holds more than one tab group has nothing to move it by as a whole.

Frames drawn by QFlexDock can have round corners (`DockTheme::floatingCornerRadius`). The window is then
translucent, which needs a compositor: without one (a bare X server) the corners are black. QFlexDock
draws no shadow around such a window.

## Floating windows and the main window

A floating window belongs to the window of the workspace that owns it (it is transient for it). Windows,
X11 and Wayland keep it above that window. **macOS does not**: a click on the main window puts the
floating windows behind it (seen by hand).

`DockManager::setFloatingWindowType(FloatingWindowType::Tool)` makes the floating windows created from
then on tool windows (`Qt::Tool`), which are above the application's other windows everywhere. What else
a tool window is, is the platform's: on macOS a panel with the small title bar of one (with the `Native`
frame), which cannot be minimized and is hidden while another application is active; on Windows a thin
title bar and no button in the taskbar. A floating window that no workspace owns stays an ordinary window.
Tried by hand on macOS; elsewhere **not verified** beyond the window being created as one (`tst_floating`).

## Wayland

- **Window positions can be neither read nor set.** Where a window floated from the API or a menu appears
  is up to the compositor, and only the size of a saved geometry is restored.
- Floating windows are transient for the window of the workspace that owns them, and stay above it. Without
  a workspace they are windows in their own right.
- To run the tests, configure with `-DQFLEXDOCK_TEST_PLATFORM=` (empty) and use
  `tests/run-on-wayland.sh <build-dir>`. It starts a headless compositor (`kwin_wayland --virtual` or
  `weston --backend=headless`; `QFLEXDOCK_WAYLAND_COMPOSITOR` picks one when both are installed) with its
  own D-Bus session, so nothing shows up on your desktop.

## Windows

- To run the tests with the native platform plugin, configure with `-DQFLEXDOCK_TEST_PLATFORM=` (empty)
  and use `tests\run-on-hidden-desktop.ps1 <build-dir>`. It runs `ctest` on a desktop of its own that is
  never shown: real windows, the Windows 11 style, the screen's scaling and the GPU, and nothing on the
  desktop you work on. `QFLEXDOCK_TEST_RHI=1` makes `tst_quick` render with Direct3D.
- A real drag cannot be driven there (it needs the pointer of the desktop that has the input), so the
  drags themselves are checked by hand: see "Manual checks".
- A window with the native frame is never narrower than Windows allows; a narrow panel that is torn off
  gets a window a little wider than it was.

## QML (`QQuickWidget`)

- The scene is rendered off-screen and composed into the widget, which is why the drop guide can be drawn
  over it and why it behaves like any other widget. The price is an extra copy per frame: not the right
  choice for video or heavy 3D views.
- Moving to another top-level window keeps the `QQuickWidget` and its QML objects; Qt only recreates the
  scene graph's GPU resources (checked with the software renderer in `tst_quick`).
- **A panel may float, dock or close itself from its own QML** (a button in it). Rendering with the GPU, a
  `QQuickWidget` replaces its scene window when it enters another top-level window, and doing so while a
  click is still being delivered in that scene crashes. The widgets `QmlPanelAdapter` creates wait until the
  scene has returned from the event. A `QQuickWidget` of your own, registered with `registerPanel()`, does
  not: make such calls from a queued connection or `Qt.callLater()`. `tst_quick` runs on the software
  renderer, where none of this happens; `QFLEXDOCK_TEST_RHI=1` with a native platform plugin runs it on the
  GPU (tried on Windows with Direct3D).
- For performance, embed a `QQuickWindow` through `NativeWindowAdapter` instead (not verified); the limits
  of native windows below then apply.

## GPU content

QFlexDock owns no device, swapchain or draw call. It moves content and tells you when that may affect a surface.

**`QOpenGLWidget`** registers like any widget. When it enters **another top-level window, Qt recreates its GL
context** and calls `initializeGL()` again: create GL resources there and release them on
`QOpenGLContext::aboutToBeDestroyed`, as Qt recommends. Moves within one window keep the context.

**Native `QWindow`** (Vulkan, Direct3D, Metal, raw OpenGL) through `NativeWindowAdapter`, with the limits of
`QWidget::createWindowContainer()`:

- A native window is drawn **in front of every widget** in its top-level window. Nothing can be drawn over
  it, the drop guide included, so such panels are hidden for the duration of a dock drag
  (`DockPanel::setHidesContentDuringDrag()`). An auto-hide panel sliding over one is expected to end up
  underneath it (not handled).
- `surfaceAboutToBeDestroyed()` and `surfaceCreated()` report when the surface goes and comes. Release
  what depends on it before the slot returns. How often this happens depends on Qt and the platform: on
  X11 and Wayland with Qt 6.12, a surface was created once and survived every move; elsewhere, assume it can be recreated.
- Do not keep `QWindow::winId()` across moves.

Verified: a plain `QWindow` moved between tabs, windows and floating windows (X11, headless Wayland), and
an OpenGL window rendering on X11. Vulkan, Direct3D and Metal were not tried.

## macOS

Qt 6.8.3, the last open-source release of 6.8, cannot be linked against the macOS 26 SDK: it asks for the
AGL framework, which that SDK no longer has (QTBUG-137687, fixed in Qt 6.8.4 and 6.9.2). With Qt 6.8,
build with Xcode 16; otherwise use Qt 6.9.2 or later.

## High DPI and multiple monitors

Sizes and saved geometries are device-independent pixels; scaling is left to Qt. On restore, windows that
are not sufficiently on any screen are moved onto one. Moving between monitors with different DPI,
unplugging monitors and fractional scaling were **not verified**.

## Manual checks

What the tests cannot drive, to be tried with the examples:

- **Window-carrying drags (Wayland)**, `qflexdock-basic` — a dragged tab comes off as a ghost of the same
  size; the guide still shows on the window below; dropping on a dock area docks; dropping elsewhere leaves
  a floating window of the same size; Esc changes nothing; dragging the only tab of a floating window moves
  that window without creating another; re-docking and floating again works repeatedly; the title of a
  window (the header of its one group, or its title row once it is split) moves and re-docks it, and the
  border resizes it.
- **Drags (Windows, macOS)**, `qflexdock-multi-window` — the five zones and the outer band appear and
  highlight; every zone docks as shown; dropping on a tab inserts there; drops work across windows; Esc
  changes nothing; the empty part of a tab bar drags the whole group. On both also: a dragged tab comes
  off as a translucent ghost of the same size that stays at the pointer; the guide shows through it and the
  zones underneath still take the drop; let go of outside every dock area (over the desktop, another
  application, a title bar) a floating window appears exactly where the ghost was; Esc while it is held
  there floats nothing; the only tab of a floating window moves that window, opaque, which turns
  translucent over another dock window, docks when dropped on a zone there, goes back where it was on Esc,
  and put down over another application stays in front of it.
- **Splitters**, `qflexdock-basic` — boundaries in a line highlight and move together, Alt moves one; the
  point where two boundaries meet shows the four-way cursor and moves both.
- **Workspace inside a panel**, `qflexdock-vscode-style` — a document tab splits and re-tabs only within
  the middle area, is dragged as a picture of the tab, and dropped anywhere else does nothing; a view tab
  (Terminal, …) is offered the side bars, the panel and the edges around the documents, never a place among
  them; the three buttons in the title bar and the activity bar put the areas away and bring them back at
  their sizes; so does pushing the edge of an area far enough against it, and dragging inwards from the
  edge it went to (also at the right edge of the window, for the secondary side bar); with the documents split, the
  point where their boundary meets the panel's or a side bar's moves both.
- **Windows of tabs**, `qflexdock-chrome-style` — a tab that is dragged is out of its row at once, the
  others closing up, and a row it is held over opens a place for it (on Wayland the ghost lies over that
  place, its tab where the pointer is); Esc puts it back; a tab dragged along its row changes places; dragged onto
  the row of another window (its tabs, or the row beside them) it becomes a tab there; let go of anywhere
  else, over a page or the desktop, it becomes a window of the same size; the last tab takes its window with
  it, in either direction; the only tab of a window drags the window, and can still be dropped into another
  row; the row beside the tabs moves the window (on Windows and macOS nothing more: it stays opaque, snaps to the
  screen edges and goes into no other row) and a double click there maximizes it, as does the button,
  whose icon follows; the edges resize the window although the page reaches them; the corners at the top
  are round, and square when maximized; with many tabs they all get narrower and the + stays behind the
  last; closing the last window ends the application.
- **Columns**, `qflexdock-photoshop-style` — a panel tab held over a group outlines that group and tints
  its tabs, held over its own group the outline goes around the pane and the tab; near the top or bottom
  of a group a bar shows where a group would go, near the side of a column or the border of the window
  where a column would; the bar above a column drags all of it, and its button turns the column into
  icons and back, at the size it had; an icon brings its group out beside the strip and puts it away; a
  strip dragged wider by its boundary shows the titles; the tools are one column of buttons or two, by
  the same button, docked at either side or floating; a floating column is moved and closed by its bar,
  and an icon of a floating strip brings its group out beside the strip, the window growing for it; the
  boundary of the column at the right, dragged left, moves the strip of icons beside it along; a document
  becomes a tab by a row of tabs or the title of a document window only, and a window when let go of
  anywhere else; the bar below the document is a window of its own, moved by its grip; the main window
  and the floating ones have round corners.
- **Style** — the Theme menu and the system's light/dark switch are followed at once.
- **GPU**, `qflexdock-gpu-panel` — rendering continues through tabbing, splitting, floating and moving to the
  other window; the native-window panel hides during a drag.
- **Monitors** — a floating window saved on a second monitor is restored on a remaining one; panels moved
  between monitors of different DPI keep their proportions.
