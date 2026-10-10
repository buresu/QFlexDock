# Saving and restoring layouts

```cpp
QByteArray json = manager.saveLayout();              // or manager.saveLayout("layout.json")

QFlexDock::DockRestoreReport report;
if (QFlexDock::DockResult r = manager.restoreLayout(json, &report); !r)
    qWarning() << "cannot restore:" << r.message();  // the layout has not changed
```

A saved layout covers the whole application: every workspace and every floating window.
Restoring is one transaction and can be undone.

## Format

```json
{
  "format": "QFlexDock.Layout",
  "schemaVersion": 1,
  "windows": [
    {
      "id": "main",
      "kind": "workspace",
      "geometry": { "x": 100, "y": 80, "width": 1200, "height": 800 },
      "maximized": false,
      "fullScreen": false,
      "layout": {
        "type": "split", "orientation": "horizontal", "weight": 1,
        "children": [
          { "type": "tabs", "weight": 0.7, "panels": ["scene", "game"], "active": "scene" },
          { "type": "tabs", "weight": 0.3, "panels": ["inspector"], "active": "inspector" }
        ]
      },
      "maximizedPanel": "",
      "autoHide": { "left": [], "right": [], "top": [], "bottom": ["console"] }
    },
    {
      "id": "floating-1",
      "kind": "floating",
      "owner": "main",
      "geometry": { "x": 400, "y": 300, "width": 480, "height": 360 },
      "layout": { "type": "tabs", "weight": 1, "panels": ["assets"], "active": "assets" }
    }
  ],
  "panelMemory": {
    "timeline": { "container": "main", "tabSiblings": ["console"], "tabIndex": 1, "...": "..." }
  }
}
```

- `windows[].id` is the workspace id given to `createWorkspace()`, or a floating window's id.
- `geometry`, `maximized`, `fullScreen` describe the top-level window a workspace sits in, or the floating window.
- A node of `layout` that is a column shrunk to a strip of buttons has `"iconified": true`.
- `panelMemory` records where panels that are not placed right now (closed, or not registered) go back to.

Only **stable panel ids** are stored: no pointers, no node ids. What is inside a panel is the application's to save.

## On restore

**Refused** — nothing changes and an error is returned:

| | `DockError` |
|---|---|
| Not JSON, wrong `format`, missing `schemaVersion` or `windows` | `ParseError` |
| `schemaVersion` newer than this build, or older with no migration | `UnsupportedVersion` |
| The file cannot be opened (`loadLayout()`) | `IoError` |

**Repaired** — the layout is restored and `DockRestoreReport::warnings` says what was done: a panel id that
appears twice (the first one wins), unknown node types (dropped), empty tab groups and single-child splits
(normalized), invalid weights (made equal), an active tab that is not in its group, nesting deeper than 128
levels, windows without an id or with a duplicate one, empty floating windows, a floating window whose
owner does not exist.

**Differences from the running application:**

- **Panels that are not registered** (`report.missingPanels`) are left out, but their place is kept. When the
  same id is registered later, the panel **appears where it was**. This is how late-loading plugins work.
- **Registered panels the layout knows nothing of** (`report.unknownPanels`) end up closed, like every panel
  the layout does not place. A panel that was closed when the layout was saved is not among them: its place
  is in `panelMemory`. These are the ones that did not exist then, for the application to show where it likes.
- **Panels registered with a factory** — content is only created for tabs that are actually shown.
- **Workspaces that do not exist** (`report.unknownWorkspaces`) — their panels end up closed. QFlexDock
  never creates a `QMainWindow`; create the workspaces before restoring.
- **Windows that are off-screen** — floating windows, and workspace windows if `restoresWindowGeometry()` is
  on, are moved onto an available screen unless enough of them, title bar included, is already visible.
- **On Wayland** window positions can be neither saved nor restored; sizes are.

If the application manages the geometry of its main windows itself, call `manager.setRestoresWindowGeometry(false)`.

## Schema versions

`schemaVersion` is `1`. When the format changes, the number goes up and a step "from N to N+1" is added to
`LayoutMigration::builtIn()`; older documents are converted step by step on load, newer ones are refused.
There is no older version yet, so the list is empty.

## Presets

```cpp
manager.savePreset("Animation");          // name the current layout
manager.applyPreset("Animation");
QByteArray all = manager.savePresets();   // every preset as one JSON document; where it goes is up to you
manager.restorePresets(all);              // all or nothing

manager.saveDefaultLayout();              // remember the current layout as the default
manager.resetLayout();                    // and go back to it
```

Presets use the same format and go through the same checks when applied.
