# QFlexDock

A docking library for Qt 6 Widgets. C++20, CMake, MIT.
Made for creative tools, IDEs, 3D editors and node editors.

- **Large five-zone drop guide** — top, bottom, left, right and center zones that cover the whole target panel, plus a separate band for docking against the whole workspace.
- **Linked splitters** — boundaries that form one line move together. Grab the point where a vertical and a horizontal boundary meet to move both at once.
- **Panels move between `QMainWindow`s** — one `DockManager` is shared, and the same widget instance moves to the other window.
- **Layouts as JSON** — versioned, validated and repaired on load; unknown panels and broken data do not break the layout.
- **QML and GPU content** — `QQuickWidget`, `QOpenGLWidget` and native `QWindow`s as panels.
- **Follows the host application** — drawn with its `QStyle`, `QPalette`, `QFont` and style sheet, and follows changes at run time.
- **Tabs or title bars** — groups headed by their tabs, or by a title bar with tabs only where panels are stacked; fixed content that panels dock around.
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

Examples are in [examples/](examples/): `basic`, `multi-window`, `quick-panel`, `gpu-panel`, and
`obs-style`, which rebuilds the window layout and look of OBS Studio (layout and style only).

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
within a group), title bars made of the application's own widgets, animations. Replacing the dock UI itself with QML is out of scope.

**Tested** — all 14 test suites pass, with warnings as errors, in [CI](.github/workflows/ci.yml) and locally:

| Environment | Notes |
|---|---|
| Linux, Qt 6.8, 6.10 and 6.12 | Static and shared; also with ASan + UBSan |
| Windows, Qt 6.8 and 6.10 | Build and tests on the `offscreen` platform only |
| macOS, Qt 6.8 and 6.10 | Build and tests on the `offscreen` platform only |
| `offscreen` | `QOpenGLWidget` and real-drag tests are skipped |
| X11 (Xvfb) | Includes real `QDrag` between windows, Esc, outside drops, moving a `QOpenGLWidget` |
| Wayland (headless KWin and Weston) | Real-drag and window-position tests are skipped |

**Not verified:**

- **Windows and macOS on screen** — the library builds and its tests pass there, but nothing was run with the
  native platform plugin: no real windows, no real drags.
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
