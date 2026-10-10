# QFlexDock

A docking library for Qt 6 Widgets. C++20, CMake, MIT.
Made for creative tools, IDEs, 3D editors and node editors.

- **Large five-zone drop guide** — top, bottom, left, right and center zones that cover the whole target panel, plus a separate band for docking against the whole workspace. Or, by a theme token, only the place of the drop, or a cross of small buttons.
- **Linked splitters** — boundaries that form one line move together, and stay one line when a panel beside one of them cannot get any smaller. Grab the point where a vertical and a horizontal boundary meet to move both at once. Side areas can be pushed out of the way with their boundary and pulled back out of the edge they went to, and come back at their size.
- **Panels move between `QMainWindow`s** — one `DockManager` is shared, and the same widget instance moves to the other window.
- **Layouts as JSON** — versioned, validated and repaired on load; unknown panels and broken data do not break the layout.
- **QML and GPU content** — `QQuickWidget`, `QOpenGLWidget` and native `QWindow`s as panels.
- **Follows the host application** — drawn with its `QStyle`, `QPalette`, `QFont` and style sheet, and follows changes at run time.
- **Tabs or title bars** — groups headed by their tabs, or by a title bar with tabs only where panels are stacked, for all workspaces or each its own; fixed content that panels dock around; your own buttons and widgets in the headers.
- **Workspaces within workspaces** — a document area in the middle of the tool panels, each kind of panel staying in its own, with boundaries that are resized together across the two.
- **Columns that shrink to icons** — a workspace can dock in columns, each under a bar that moves it as a whole and shrinks it to a strip of buttons; a button brings its tab group out beside the strip. Drops go beside a column, into it, or into the tabs of a group.
- **Windows of tabs** — an application can do without a main window: floating windows that are rows of tabs, where a tab is dragged into the row of another window or let go of to become a window. The rows can show a drag as it will turn out: the tab gone from one, a place opening for it in the other.
- Floating windows, auto-hide, maximize, undo/redo, named presets, per-panel dock policies, lazily created panels.

> **Status: 0.1.0, in development.** The public API may still change.

## Usage

```cpp
#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockWorkspace.h>

QApplication app(argc, argv);
QFlexDock::DockManager manager;          // create it after the QApplication

QMainWindow windowA, windowB;
auto *workspaceA = manager.createWorkspace("A");
auto *workspaceB = manager.createWorkspace("B");
windowA.setCentralWidget(workspaceA);
windowB.setCentralWidget(workspaceB);

// The manager takes ownership of the content widgets.
manager.registerPanel("scene", sceneWidget, "Scene");
manager.registerPanel("inspector", inspectorWidget, "Inspector");

workspaceA->addPanel("scene", QFlexDock::DockArea::Center);
workspaceB->addPanel("inspector", QFlexDock::DockArea::Center);

// To the other window: inspectorWidget stays the same instance.
manager.movePanel("inspector", workspaceA, QFlexDock::DockArea::Right);

manager.saveLayout("layout.json");
```

Operations that can fail return a `DockResult`. A failed operation leaves the layout untouched.

```cpp
if (QFlexDock::DockResult r = manager.loadLayout("layout.json"); !r)
    qWarning() << r.message();
```

Examples are in [examples/](examples/): `basic`, `multi-window`, `quick-panel`, `gpu-panel`, and five that
rebuild the window layout and look of an existing application (layout and style only): `obs-style` after
OBS Studio, `vscode-style` after Visual Studio Code, `vs-style` after Visual Studio, `chrome-style`
after Chrome, and `photoshop-style` after Photoshop.

## Build

Requires CMake 3.23+, a C++20 compiler and Qt 6.8+ (Core, Gui, Widgets; Quick and QuickWidgets for the Quick module).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

| CMake option | Default | |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library |
| `QFLEXDOCK_BUILD_QUICK` | `ON` if Qt Quick is found | Build `QFlexDock::Quick` |
| `QFLEXDOCK_BUILD_EXAMPLES` | `ON` when top-level | Build the examples |
| `QFLEXDOCK_BUILD_TESTS` | `ON` when top-level | Build the tests (also needs `BUILD_TESTING`) |
| `QFLEXDOCK_TEST_PLATFORM` | `offscreen` | `QT_QPA_PLATFORM` for GUI tests |
| `QFLEXDOCK_SANITIZERS` | empty | e.g. `address,undefined` |
| `QFLEXDOCK_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors |

In your own project:

```cmake
find_package(QFlexDock 0.1 REQUIRED)            # add COMPONENTS Quick for the QML module
# or: add_subdirectory(third_party/QFlexDock)
target_link_libraries(app PRIVATE QFlexDock::QFlexDock)
```

`QFlexDock::QFlexDock` does not link Qt Quick; everything QML lives in `QFlexDock::Quick`.

## Status

**Not implemented:** keyboard-only docking and accessibility (beyond accessible button names and Ctrl+Tab
within a group), replacing a group's header by a widget of the application's own (it can add buttons and
widgets to it), side areas that keep their pixel size when the window is resized, animations. Replacing the dock UI itself with QML is out of scope.

**Tested** — all 15 test suites pass, with warnings as errors, in [CI](.github/workflows/ci.yml) and locally:

| Environment | Notes |
|---|---|
| Linux, Qt 6.8, 6.10 and 6.12 | Static and shared; also with ASan + UBSan |
| Windows, Qt 6.8 and 6.10 | Build and tests on the `offscreen` platform only |
| Windows 11, Qt 6.12, native platform | Locally, on a desktop that is not shown (`tests/run-on-hidden-desktop.ps1`), at 200% scaling; `tst_quick` also rendering with Direct3D. Real-drag tests are skipped |
| macOS, Qt 6.8 and 6.10 | Build and tests on the `offscreen` platform only |
| `offscreen` | `QOpenGLWidget` and real-drag tests are skipped |
| X11 (Xvfb) | Includes real `QDrag` between windows, Esc, outside drops, tabs torn off and joined between floating windows, moving a `QOpenGLWidget` |
| Wayland (headless KWin and Weston) | Real-drag and window-position tests are skipped |

**Not verified:**

- **macOS** — the tests were never run with the native platform plugin. The examples were tried by hand
  on macOS 27 with Qt 6.11 (drags, floating, the windows that follow a drag).
- **Windows** — the examples were tried by hand on Windows 11 (drags, floating, the windows that follow a
  drag, QML panels). Not tried: touch and pen, monitors of different scaling, Direct3D content in a native
  window.
- **Automated pointer drags on Wayland**, high-DPI and mixed-DPI multi-monitor setups.
- **Vulkan / Direct3D / Metal** content. Only OpenGL and a plain `QWindow` were tried.

Platform limits and a manual test checklist are in [docs/platform-notes.md](docs/platform-notes.md).

## Documentation

- [docs/api.md](docs/api.md) — the public API, ownership and conventions
- [docs/persistence.md](docs/persistence.md) — the JSON format and what happens on restore
- [docs/styling.md](docs/styling.md) — style integration, style sheet selectors, theme tokens
- [docs/platform-notes.md](docs/platform-notes.md) — platform limits, QML and GPU content, manual tests
- [docs/architecture.md](docs/architecture.md) — how it is built, and why

## License

MIT, see [LICENSE](LICENSE). The only runtime dependency is Qt 6, under whichever of its licenses you use.
QFlexDock contains no code, images or icons from other docking libraries; its icons are drawn with `QPainter`.
