# Platform notes

What is stated here as working was observed in a test or by hand. "Not verified" means it is expected to
work from Qt's documentation, but nobody has checked.

| | Automated tests | Real `QDrag` |
|---|---|---|
| Linux X11 (Xvfb, Qt 6.12) | All suites pass | **Automated** (`tst_realdrag`) |
| Linux Wayland (headless KWin, Qt 6.12) | All suites pass | Tab drags tried by hand on KWin; cannot be automated |
| `offscreen` | All suites pass (no `QOpenGLWidget`) | — |
| Windows, macOS | **Never run** | Not verified |

## Drag and drop

Dock drags use `QDrag`. The layout never changes during a drag; the drop is committed as one transaction afterwards.

| | Follows the pointer | Dropped outside every dock area |
|---|---|---|
| Wayland, compositor with `xdg-toplevel-drag` | A **ghost**: a window showing the dragged panel at its real size | The ghost becomes the floating window |
| Wayland, other compositors | A picture of the tab | Nothing |
| X11 | A picture of the tab | The panel floats at the pointer |
| Windows, macOS | A picture of the tab | Nothing (by default) |

### Windows carried by a drag (Wayland)

A Wayland client can neither move a window to the pointer nor see the pointer over other windows. The
compositor can, however, move a window as part of a drag (`xdg-toplevel-drag-v1`). QFlexDock uses that:

- **Dragging a tab** carries a ghost, held where it was grabbed. Dropped on a dock area it docks and the
  ghost disappears; dropped elsewhere the ghost itself becomes the floating window; Esc just removes it.
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
- `DockManager::setDragGhostEnabled(false)` turns all of this off. Outside drops then do nothing on
  Wayland, because they cannot be told apart from a cancelled drag.

### Dropping outside = floating

With `DockManager::setFloatsOnOutsideDrop(true)` a drag that no dock area took floats the panel.
**It is on by default on X11 and Wayland**, the two platforms where a drop outside can be told from Esc:
on Wayland by the drop action Qt reports for a window-carrying drag, on X11 by whether Esc was pressed
and whether the mouse button is still down when the drag ends (covered by `tst_realdrag`). On Windows and
macOS this is not verified; if you turn it on, check that Esc does not float the panel.

A content widget that accepts every MIME format hides the drop guide over itself; one that refuses
QFlexDock's format lets the drop through to the dock area.

## Floating window frame

`DockManager::setFloatingWindowFrame()` chooses the frame of floating windows created from then on.

| | `FloatingFrame::Native` (default) | `FloatingFrame::Custom` |
|---|---|---|
| Title bar and border | The window system's | Drawn by QFlexDock, styleable with a style sheet |
| Move and resize | The window system | Requested with `QWindow::startSystemMove()` / `startSystemResize()` |
| Snapping, tiling, window menu | Yes | Up to the window system |
| Re-dock by dragging the title | No (drag a tab instead) | On Wayland with `xdg-toplevel-drag` |

## Wayland

- **Window positions can be neither read nor set.** Where a window floated from the API or a menu appears
  is up to the compositor, and only the size of a saved geometry is restored.
- Floating windows are transient for the window of the workspace that owns them, and stay above it.
- To run the tests, configure with `-DQFLEXDOCK_TEST_PLATFORM=` (empty) and use
  `tests/run-on-wayland.sh <build-dir>`. It starts a headless compositor (`kwin_wayland --virtual`, or
  `weston --backend=headless`, which is not verified) with its own D-Bus session, so nothing shows up on your desktop.

## QML (`QQuickWidget`)

- The scene is rendered off-screen and composed into the widget, which is why the drop guide can be drawn
  over it and why it behaves like any other widget. The price is an extra copy per frame: not the right
  choice for video or heavy 3D views.
- Moving to another top-level window keeps the `QQuickWidget` and its QML objects; Qt only recreates the
  scene graph's GPU resources (checked with the software renderer in `tst_quick`).
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

## High DPI and multiple monitors

Sizes and saved geometries are device-independent pixels; scaling is left to Qt. On restore, windows that
are not sufficiently on any screen are moved onto one. Moving between monitors with different DPI,
unplugging monitors and fractional scaling were **not verified**.

## Manual checks

What the tests cannot drive, to be tried with the examples:

- **Window-carrying drags (Wayland)**, `qflexdock-basic` — a dragged tab comes off as a ghost of the same
  size; the guide still shows on the window below; dropping on a dock area docks; dropping elsewhere leaves
  a floating window of the same size; Esc changes nothing; dragging the only tab of a floating window moves
  that window without creating another; re-docking and floating again works repeatedly; with the custom
  frame, the title row moves the window and re-docks it, and the border resizes it.
- **Drags (Windows, macOS)**, `qflexdock-multi-window` — the five zones and the outer band appear and
  highlight; every zone docks as shown; dropping on a tab inserts there; drops work across windows; Esc
  changes nothing; the empty part of a tab bar drags the whole group.
- **Splitters**, `qflexdock-basic` — boundaries in a line highlight and move together, Alt moves one; the
  point where two boundaries meet shows the four-way cursor and moves both.
- **Style** — the Theme menu and the system's light/dark switch are followed at once.
- **GPU**, `qflexdock-gpu-panel` — rendering continues through tabbing, splitting, floating and moving to the
  other window; the native-window panel hides during a drag.
- **Monitors** — a floating window saved on a second monitor is restored on a remaining one; panels moved
  between monitors of different DPI keep their proportions.
