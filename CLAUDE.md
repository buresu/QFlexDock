# QFlexDock — working notes

A docking library for Qt 6 Widgets. C++20, CMake, MIT. Structure and design choices: `docs/architecture.md`.

## Commands

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure            # runs on offscreen

# X11: the only place where real QDrag and QOpenGLWidget tests run
QT_QPA_PLATFORM=xcb xvfb-run -a -s "-screen 0 2000x1200x24" build/tests/tst_realdrag

# Wayland: an isolated headless compositor (needs kwin_wayland or weston), nothing appears on the desktop
cmake -S . -B build-wl -G Ninja -DQFLEXDOCK_TEST_PLATFORM=
cmake --build build-wl && tests/run-on-wayland.sh build-wl

# One test / saving screenshots along the way
QT_QPA_PLATFORM=offscreen build/tests/tst_manager maximizeAndRestore
QFLEXDOCK_TEST_GRABS=/tmp/grabs QT_QPA_PLATFORM=offscreen build/tests/tst_dragdrop

# An example on a private X server, where xdotool can drive it and `import -window root` can look at it
xvfb-run -a -s "-screen 0 1600x900x24" build/examples/qflexdock-vscode-style

# Sanitizers (a report ends the test), shared library, install
CXX=clang++ cmake -S . -B build-asan -G Ninja -DQFLEXDOCK_SANITIZERS=address,undefined
cmake -S . -B build-shared -G Ninja -DBUILD_SHARED_LIBS=ON -DQFLEXDOCK_WARNINGS_AS_ERRORS=ON
cmake --install build --prefix /tmp/prefix
cmake -S tests/consumer -B /tmp/consumer -DCMAKE_PREFIX_PATH=/tmp/prefix && cmake --build /tmp/consumer
```

Never run GUI tests on the user's real desktop (a bare `QT_QPA_PLATFORM=wayland`): windows pop up one
after another. Use the script above or Xvfb.

## Layout

```
include/QFlexDock/        public headers
include/QFlexDockQuick/   public headers of QFlexDock::Quick
src/core/                 model and controllers
  LayoutModel             LayoutNode / LayoutTree: values, normal form, validation
  LayoutState             every container's tree + where absent panels go back to. The one state
  LayoutSolver            weights -> pixels, with minimum and maximum sizes
  SplitterCoordinator     linked handles, corners, ranges, pixels -> weights
  DropZones               geometry and hit testing of the five zones
  DockManager(_p.h)       transactions through apply(), every operation, syncing the views
  DockDragController      drag sessions, QDrag, committing the drop
src/widgets/              views: they show the state and decide nothing
src/persistence/          JSON, validation and repair, migration, fitting windows to screens
src/quick/                QmlDockController, QmlPanelAdapter
src/gpu/                  NativeWindowAdapter
tests/                    Qt Test; shared fixtures in TestUtils.h
```

## Rules

- **There is one state, `LayoutState`, and every change goes through `DockManagerPrivate::apply()`**:
  edit a copy → `reconcile` → `normalize` → `validate` → swap → `syncViews`. Nothing else touches the state
  (the exceptions: weights while a splitter is dragged, and floating window positions).
- **No `QWidget*` in the model.** `Layout*`, `SplitterCoordinator` and `DropZones` stay testable without widgets.
- **Views do not decide.** Clicks and drags become requests to the manager.
- **Content widgets belong to the manager.** When a tab group or workspace goes away, content is moved to
  the parking widget (`reparentContent` / `parkIfHostedBy`), never with a bare `setParent`: the lifecycle
  signals are emitted from there.
- **The layout does not change during a drag.** It is committed after `QDrag::exec()` returns.
- **No Qt private API.** The build uses `QT_NO_KEYWORDS` and `QT_NO_CAST_FROM_ASCII`
  (`Q_EMIT` / `Q_SIGNALS`, `QStringLiteral`).
- **No API newer than Qt 6.8.** The local Qt is 6.12, so check when an API was introduced.
- **Nothing copied from other docking libraries or applications**: no code, no artwork, no theme files, no
  material of unclear origin, no Conan files. Other docking libraries are not named in the repository. An
  example that rebuilds the layout and look of an existing application may name it (`examples/obs-style`,
  `examples/vscode-style`, `examples/chrome-style`), and does so with its own code and drawings only.
- A public API change comes with its tests, examples and `docs/api.md`.
- A style sheet selector or property goes into `docs/styling.md` only once `tests/tst_style.cpp` shows it works.
- The README's "Status" lists only what was implemented and what was actually run.
- Documentation is in English and kept short: what a user or contributor needs, not a history of the work.

## Writing tests

- Drags use the `Drag` helper in `tests/tst_dragdrop.cpp`, which sends `QDragEnterEvent` and friends straight
  to a dock area. Qt delivers no move after a drop or leave until the next enter; the helper handles that.
- A drag enter that an area ignores goes on to the area around it (`QApplication` does that also for events
  sent directly). A workspace inside a panel is tested by sending the events to the innermost area.
- Ghost and window-carrying drags on Wayland cannot be automated. `tst_floating` calls
  `DockDragController::createGhost()` and `finish(action, ghost, windowDrag)` directly. Never start
  `requestWindowDrag()` or a real `QDrag` from a test on Wayland: without pointer input it never returns.
- Events sent to a widget directly skip hit testing. Anything about "can the user reach it" goes through
  the window: `QTest::mousePress(widget->window()->windowHandle(), …)`.
- `QTest::mouseMove()` on a widget only moves the cursor when no button is down. To test hovering, send the
  move through the window, after `QCursor::setPos()` from elsewhere inside the window on platforms with a
  real pointer (see `thinHandlesCanBeGrabbed`).
- A real `QDrag` can only be automated on X11 (`tst_realdrag`). The whole drag happens inside one
  `processEvents()`, so intermediate state is recorded in steps scheduled by timers. `QTest` does not move
  the real pointer while a button is down; where the code under test reads `QCursor::pos()`, the test sets it.
- Wayland gives no top-level window positions; tests that look at them branch on `windowPositionsWork()`.
- A lambda connected to a signal that captures locals must be declared before the fixture (`TwoWindows`) or
  disconnected before leaving scope: destroying the fixture emits layout signals too.
