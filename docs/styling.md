# Styling

QFlexDock has no theme of its own. Looks are decided in three layers, each overriding the one before:

1. **The host's style** — the application's `QStyle`, `QPalette`, `QFont` and style sheet.
2. **Theme tokens** (`DockTheme`) — values specific to docking.
3. **Style sheet properties** (`qproperty-*`), and a **custom painter** (`DockOverlayPainter`) for the drop guide.

## What the host's style draws

| Part | Drawn with |
|---|---|
| Tabs | A real `QTabBar` (`QStyle::CE_TabBarTab`), close buttons included |
| Split handles | `QStyle::CE_Splitter`, as thick as `QStyle::PM_SplitterWidth` |
| Tab group frame | `QFrame::StyledPanel` |
| Title row buttons | `QToolButton`s with line icons in the palette's `WindowText` color; a panel's own actions as `QToolButton`s with the icons they bring |
| Drop guide | Translucent colors derived from the palette's `Highlight` |
| Floating windows | The platform's window frame, or with `FloatingFrame::Custom` a title row and a thin border (`Minimal`: the border alone) |

Changes to the style, palette, font or style sheet, and the system's light/dark switch, are picked up
through Qt's change events; there is nothing to call. All sizes are device-independent pixels.

## Style sheets

Class selectors join the namespace with `--`. **Every selector below is exercised in `tests/tst_style.cpp`.**
There are no sub-controls or pseudo-states beyond these.

| Selector | Target | What works |
|---|---|---|
| `QFlexDock--DockTabGroup` | A tab group (`QFrame`) | `border`, `background`, …; properties `active`, `maximized`, `headerVisible` |
| `QFlexDock--DockTabGroup #dockTitleBar` | Its title row | `background`, … |
| `#dockTitle` | The title in it (`QLabel`, with `GroupHeader::TitleBar`) | `color`, `font`, `background`, … |
| `#dockMenuButton`, `#dockMaximizeButton`, `#dockFloatButton`, `QFlexDock--DockTabGroup #dockCloseButton` | Title row buttons | As `QToolButton` |
| `#dockTitleActions`, `#dockTitleStartActions`, `#dockTabActions` | What holds a panel's own actions (`DockPanel::setTitleActions()`): at the end of the header, at its start, behind the tabs | `background`, … |
| `#dockActionButton`, `#dockActionSeparator` | Their buttons, and the lines between them | As `QToolButton` / `background`, `margin`, `min-width`; a button has the object name of its action as property `action` |
| `QFlexDock--DockTabBar` | The tab bar (`QTabBar`) | `qproperty-activeIndicatorColor`; property `activeGroup` |
| `QFlexDock--DockTabBar::tab`, `::tab:selected`, … | Tabs, above or below the content | The same as `QTabBar::tab` |
| `QFlexDock--DockSplitHandle` | Split handles | `background`, `border`; properties `orientation` (`1`: vertical bar, `2`: horizontal bar), `hovered`, `pressed` |
| `QFlexDock--DockEdgeHandle` | The edge closed collapsible panels are pulled out of | `background`, drawn only while it is hovered or held; properties `edge` (`left`, `right`, `top`, `bottom`), `hovered`, `pressed` |
| `QFlexDock--DockAutoHideBar` | Auto-hide bars | `background`, …; property `edge` (`left`, `right`, `top`, `bottom`) |
| `QFlexDock--DockAutoHidePopup` | The panel that slides out (`QFrame`) | `background`, `border`; buttons `#dockPinButton`, `#dockCloseButton` |
| `QFlexDock--DockDropOverlay` | The drop guide | Only the `qproperty-*` below |
| `QFlexDock--DockFloatingWindow` | Floating windows | `background`, with a custom frame also `border` and `border-radius`; properties `customFrame`, `maximized` |
| `#dockFloatingTitleBar`, `#dockFloatingTitle` | Title row and title text of a custom frame | `background`, `color`, … |
| `#dockFloatingMaximizeButton`, `#dockFloatingCloseButton` | Its buttons | As `QToolButton` |

```css
QFlexDock--DockTabGroup[active="true"] { border: 1px solid palette(highlight); }
QFlexDock--DockTabBar::tab:selected { background: palette(base); }
QFlexDock--DockSplitHandle { background: palette(mid); }
QFlexDock--DockSplitHandle[hovered="true"] { background: palette(highlight); }
QFlexDock--DockDropOverlay {
    qproperty-hoverColor: rgba(80, 160, 255, 120);
    qproperty-cornerRadius: 0;
}
```

Notes:

- A split handle that is styled by a style sheet does not get the style's own grip drawn on top.
- A handle can be grabbed over about 7px even when it is drawn thinner; the extra margin lies over the
  neighbouring tab groups and is never painted, so a `background` only shows in the drawn width.
- The point where a vertical and a horizontal boundary meet has no visual of its own. Pointing at it sets
  `hovered` on every handle it would move.
- With the theme token `splitHandleHoverWidth`, a handle is drawn wider while it is hovered or dragged: a
  one pixel line that lights up four pixels wide, say. The layout does not change.
- A workspace is a widget: give it an object name, and `#documents QFlexDock--DockTabBar::tab { … }` styles
  its tabs differently from those of other workspaces. (Floating windows are not inside a workspace;
  `QFlexDock--DockFloatingWindow QFlexDock--DockTabBar::tab` reaches theirs.)
- `#dockTabActions #dockActionButton[action="newTab"] { … }` styles the button of the action whose object
  name is `newTab`, and only behind the tabs.
- Round corners on a floating window need the theme token `floatingCornerRadius` (the window has to be
  translucent, which a style sheet cannot ask for). A style sheet that draws the frame gives it a
  `border-radius` to match, and may round fewer corners; `[maximized="true"]` is where to take border and
  radius away again.
- `qproperty-*` is applied once, when the widget is first polished (a Qt rule), and stays after the style
  sheet is removed.
- The 2px mark on the current tab of the active group takes its color from `qproperty-activeIndicatorColor`;
  a fully transparent color removes it.

### Drop guide properties

| Property | Type | |
|---|---|---|
| `zoneColor`, `zoneBorderColor` | color | Fill and border of a zone |
| `hoverColor`, `hoverBorderColor` | color | The zone under the pointer |
| `previewColor`, `previewBorderColor` | color | The area the panel would take (only with `showPreview`) |
| `glyphColor` | color | The direction marks |
| `borderWidth` | real | 0 for no border |
| `cornerRadius` | real | Sharp corners are rounded less so that their tips survive |
| `zoneGap` | int | Space between zones |
| `zoneMargin` | int | Space between the zones and the edge of the target (default 6) |
| `showPreview` | bool | `false` (default): highlight the zone under the pointer. `true`: show the area the panel would take instead |

## Theme tokens

For applications without a style sheet, or to set values from code:

```cpp
QFlexDock::DockTheme theme;
theme.splitHandleWidth = 6;                              // -1: the style's PM_SplitterWidth
theme.splitHandleHoverWidth = 8;                         // drawn this wide while hovered; -1: no wider
theme.iconSize = 18;                                     // -1: the style's size
theme.titleButtons = QFlexDock::DockTitleButton::Float   // buttons in a group's header;
                   | QFlexDock::DockTitleButton::Close;  // the default is Menu | Maximize
theme.tabWidth = 200;                                    // every tab this wide; -1: as wide as its title
theme.tabOverflow = QFlexDock::DockTabOverflow::Shrink;  // tabs share a crowded bar; default: Scroll
theme.floatingBorderWidth = 1;                           // frames drawn by QFlexDock; -1: 4 pixels
theme.floatingCornerRadius = 10;                         // their corners; 0: square
theme.overlay.hoverColor = QColor(255, 128, 0, 120);     // an invalid color is derived from the palette
theme.overlay.edgeFraction = 0.25;                       // depth of the edge zones, relative to the target
theme.overlay.outerBandWidth = 32;                       // band along the workspace border; 0 disables it
theme.icons.insert(QFlexDock::DockIcon::Maximize, QIcon(":/icons/maximize.svg"));
manager.setTheme(theme);                                 // at any time
```

Icons that can be replaced: `Close`, `Maximize`, `Restore`, `Float`, `Dock`, `Pin`, `Unpin`, `Menu`.

With `DockTabOverflow::Shrink` the tabs of a crowded bar get narrower together instead of scrolling. A tab
too narrow for its close button loses it, except the current one. A border thinner than four pixels is
still grabbed over four, across the edge of the content. `floatingBorderWidth` and `floatingCornerRadius`
apply to the floating windows created after `setTheme()`.

A tab of any shape can be drawn by the application's `QStyle` (a `QProxyStyle` handling
`QStyle::CE_TabBarTab`), as `examples/chrome-style` does; a style sheet rule for `::tab` takes the tabs
away from the style.

## Painting the drop guide yourself

Only the looks change; which area reacts is independent of the painting.

```cpp
class MyOverlayPainter : public QFlexDock::DockOverlayPainter
{
public:
    void paint(QPainter *painter, const QFlexDock::DockOverlayScene &scene,
               const QFlexDock::DockOverlayStyle &style) override
    {
        for (const auto &zone : scene.zones) {             // only the zones that may be shown
            painter->setBrush(zone.hovered ? style.hoverColor : style.zoneColor);
            painter->drawPolygon(zone.shape);              // also: zone.area, zone.outer
        }
    }
};
manager.setOverlayPainter(std::make_shared<MyOverlayPainter>());
```

`style` has all three layers resolved, so every color is valid. `scene.preview` (the area the panel would
take) is always filled in, whatever `showPreview` says, and `scene.tabIndicator` marks an insertion between tabs.
