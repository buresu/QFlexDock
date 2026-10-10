// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockPolicy.h>
#include <QFlexDock/DockTheme.h>
#include <QFlexDock/DockWorkspace.h>
#include <QFlexDock/Global.h>

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QRect>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QMenu;
class QWidget;
QT_END_NAMESPACE

namespace QFlexDock {

class DockManagerPrivate;

/// Creates the content of a factory-backed panel the first time it is shown.
/// The returned widget is owned by the manager. May return nullptr, in which
/// case the panel stays empty and the factory is asked again next time.
using DockPanelFactory = std::function<QWidget *(const PanelId &)>;

/// What restoreLayout() had to work around. A restore that produces warnings
/// still succeeds.
struct QFLEXDOCK_EXPORT DockRestoreReport
{
    /// Panels in the layout that are not registered. Their position is kept
    /// and they appear there once registered.
    QStringList missingPanels;
    /// Registered panels the layout knows nothing of: it neither places them
    /// nor remembers a place for them, as it does for a panel that was closed
    /// when it was saved. These are the ones that are new to it (a panel
    /// added to the application since). One that was open and has a default
    /// placement (DockPanel::setDefaultPlacement()) is shown there; any other
    /// ends up closed, like every panel the layout does not place.
    QStringList unknownPanels;
    /// Workspaces in the layout that do not exist; their panels are closed.
    QStringList unknownWorkspaces;
    /// Repairs made to the data (duplicates dropped, unknown nodes skipped...).
    QStringList warnings;
};

/// The single coordinator of a docking setup: it owns the panels, knows every
/// workspace and floating window, and is the only place the layout changes.
///
/// Every change is a transaction on a copy of the layout: it is validated and
/// then applied to the widgets in one go, or rejected leaving everything as it
/// was. Functions returning DockResult report which.
///
/// Ownership: the manager owns the DockPanel objects and their content
/// widgets. Workspaces are ordinary widgets owned by their Qt parent; when one
/// is destroyed its panels are closed, not destroyed.
///
/// All members must be used from the GUI thread.
class QFLEXDOCK_EXPORT DockManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QFlexDock::DockPanel *activePanel READ activePanel NOTIFY activePanelChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(int undoLimit READ undoLimit WRITE setUndoLimit NOTIFY undoLimitChanged)
    Q_PROPERTY(bool restoresWindowGeometry READ restoresWindowGeometry
               WRITE setRestoresWindowGeometry NOTIFY restoresWindowGeometryChanged)
    Q_PROPERTY(bool linkedSplittersEnabled READ linkedSplittersEnabled
               WRITE setLinkedSplittersEnabled NOTIFY linkedSplittersEnabledChanged)
    Q_PROPERTY(bool cornerResizeEnabled READ isCornerResizeEnabled WRITE setCornerResizeEnabled
               NOTIFY cornerResizeEnabledChanged)
    Q_PROPERTY(bool splitterPushEnabled READ isSplitterPushEnabled WRITE setSplitterPushEnabled
               NOTIFY splitterPushEnabledChanged)
    Q_PROPERTY(bool centerDropEnabled READ isCenterDropEnabled WRITE setCenterDropEnabled
               NOTIFY centerDropEnabledChanged)
    Q_PROPERTY(bool tabDragPreviewEnabled READ isTabDragPreviewEnabled
               WRITE setTabDragPreviewEnabled NOTIFY tabDragPreviewEnabledChanged)
    Q_PROPERTY(bool floatsOnOutsideDrop READ floatsOnOutsideDrop WRITE setFloatsOnOutsideDrop
               NOTIFY floatsOnOutsideDropChanged)
    Q_PROPERTY(bool dragGhostEnabled READ isDragGhostEnabled WRITE setDragGhostEnabled
               NOTIFY dragGhostEnabledChanged)
    Q_PROPERTY(FloatingWindowFrame floatingWindowFrame READ floatingWindowFrame
               WRITE setFloatingWindowFrame NOTIFY floatingWindowFrameChanged)
    Q_PROPERTY(FloatingWindowType floatingWindowType READ floatingWindowType
               WRITE setFloatingWindowType NOTIFY floatingWindowTypeChanged)
    Q_PROPERTY(QFlexDock::DockGroupHeader groupHeader READ groupHeader WRITE setGroupHeader
               NOTIFY groupHeaderChanged)
    Q_PROPERTY(bool titleBarMovesGroup READ titleBarMovesGroup WRITE setTitleBarMovesGroup
               NOTIFY titleBarMovesGroupChanged)
    Q_PROPERTY(AutoHideReveal autoHideReveal READ autoHideReveal WRITE setAutoHideReveal
               NOTIFY autoHideRevealChanged)

public:
    /// What happens to a panel's remembered position when it is unregistered.
    enum class PlacementMemory {
        /// Remember it: registering the same id again puts the panel back.
        Keep,
        Forget,
    };
    Q_ENUM(PlacementMemory)

    /// Who draws the frame of floating windows.
    enum class FloatingWindowFrame {
        /// The window system: its title bar, buttons, borders and behaviour.
        Native,
        /// QFlexDock (the default): a frameless window with a title row and
        /// resizable edges of its own, styled like the rest of the dock UI.
        /// A window holding one tab group has no title row beside the header
        /// of that group, which is its title: one panel is named there
        /// without a tab, and the tabs of several are in it. Where the
        /// platform allows it (see docs/platform-notes.md) such a window can
        /// be docked by dragging its title onto a dock area.
        Custom,
        /// QFlexDock, without a title row: just a resizable border around
        /// the content. The headers of the tab groups inside are what the
        /// user takes hold of, which suits DockGroupHeader::TitleBar.
        Minimal,
    };
    Q_ENUM(FloatingWindowFrame)

    /// What kind of window a floating window is to the window system.
    enum class FloatingWindowType {
        /// A window like any other (Qt::Window). It belongs to the window of
        /// the workspace that owns it; whether it also stays above that
        /// window is up to the platform (macOS does not keep it there).
        Window,
        /// A tool window (Qt::Tool), where a workspace owns it: above the
        /// application's other windows on every platform, and whatever else
        /// the platform makes of tool windows (see docs/platform-notes.md).
        /// A floating window that no workspace owns stays a Window.
        Tool,
    };
    Q_ENUM(FloatingWindowType)

    /// How a panel that was put away at a border (auto-hide) is shown while
    /// it is out.
    enum class AutoHideReveal {
        /// Over the dock area (the default), covering what is under it.
        Over,
        /// Beside the dock area, which makes do with the room that is left:
        /// everything stays in view, laid out anew, as if the panel were
        /// docked along that border for as long as it is out.
        Beside,
    };
    Q_ENUM(AutoHideReveal)

    explicit DockManager(QObject *parent = nullptr);
    ~DockManager() override;

    // --- Workspaces ----------------------------------------------------------
    /// Creates a workspace to put into a window, typically as the central
    /// widget of a QMainWindow. `id` identifies it in saved layouts and must be
    /// unique; an empty id gets a generated one ("workspace-1", ...). Returns
    /// nullptr if the id is taken.
    DockWorkspace *createWorkspace(const QString &id = {}, QWidget *parent = nullptr);
    [[nodiscard]] QList<DockWorkspace *> workspaces() const;
    [[nodiscard]] DockWorkspace *workspace(const QString &id) const;

    // --- Panel registration --------------------------------------------------
    /// Registers `content` as panel `id`. The manager takes ownership of the
    /// widget. Returns nullptr (see lastError()) if the id is empty or taken,
    /// or the widget is null or already registered. A registered panel is not
    /// shown until it is placed (movePanel(), openPanel(), a restored layout).
    DockPanel *registerPanel(const PanelId &id, QWidget *content, const QString &title = {});
    /// Registers a panel whose content is created on first use.
    DockPanel *registerPanelFactory(const PanelId &id, DockPanelFactory factory,
                                    const QString &title = {});
    /// Removes the panel from the layout and destroys it and its content.
    DockResult unregisterPanel(const PanelId &id, PlacementMemory memory = PlacementMemory::Keep);
    /// Like unregisterPanel(), but hands the content widget back: the caller
    /// owns it again (it is hidden and has no parent). Null if there was none.
    QWidget *releasePanel(const PanelId &id, PlacementMemory memory = PlacementMemory::Keep);
    [[nodiscard]] DockPanel *panel(const PanelId &id) const;
    [[nodiscard]] QList<DockPanel *> panels() const;
    [[nodiscard]] bool hasPanel(const PanelId &id) const;
    /// Why the last call that returns a pointer returned nullptr.
    [[nodiscard]] DockResult lastError() const;

    // --- Placement -----------------------------------------------------------
    /// Docks a panel onto `workspace` as a whole: an edge area along the
    /// outside of everything already there, Center as a tab of its current tab
    /// group. `fraction` is the share of the space taken (edge areas only).
    /// A panel that is already placed is moved.
    DockResult movePanel(const PanelId &id, DockWorkspace *workspace, DockArea area,
                         double fraction = -1.0);
    /// Docks a panel relative to the tab group `relativeTo` is in: an edge area
    /// splits that group, Center joins it at `tabIndex` (-1 appends).
    DockResult movePanel(const PanelId &id, const PanelId &relativeTo, DockArea area,
                         int tabIndex = -1, double fraction = -1.0);
    /// Same, for the whole tab group `anyPanelOfGroup` is in.
    DockResult moveTabGroup(const PanelId &anyPanelOfGroup, DockWorkspace *workspace, DockArea area,
                            double fraction = -1.0);
    DockResult moveTabGroup(const PanelId &anyPanelOfGroup, const PanelId &relativeTo,
                            DockArea area, int tabIndex = -1, double fraction = -1.0);

    /// Moves the panel (or its whole tab group) into a new floating window; a
    /// closed panel is shown in one. A null geometry picks a default near the
    /// panel's current position. Note that Wayland compositors ignore the
    /// position.
    ///
    /// Floating windows need no workspace: an application can consist of
    /// them alone, every window a set of tabs that are dragged from one to
    /// the other, and each gone with its last panel.
    DockResult floatPanel(const PanelId &id, const QRect &geometry = {});
    DockResult floatTabGroup(const PanelId &anyPanelOfGroup, const QRect &geometry = {});
    /// Puts a floating or auto-hidden panel back where it was last docked.
    DockResult dockPanel(const PanelId &id);

    /// Shows a closed panel where it last was (or, with no place remembered,
    /// at its default placement) and activates it.
    DockResult openPanel(const PanelId &id);
    /// Closes the panel: it leaves the layout but stays registered, and its
    /// position is remembered for openPanel().
    DockResult closePanel(const PanelId &id);
    /// The same for several panels as one change (and one undo step): the
    /// way to put a whole area away and bring it back. Panels closed together
    /// return together: in their old order, with the same tab in front, and
    /// as one when the user pulls them back out of the edge they went to
    /// (see DockPanel::setCollapsible()). openPanels() activates none of them.
    DockResult openPanels(const QStringList &ids);
    DockResult closePanels(const QStringList &ids);
    DockResult togglePanel(const PanelId &id);
    /// Makes the panel the current tab of its group, raises its window and
    /// gives it keyboard focus.
    DockResult activatePanel(const PanelId &id);
    /// Makes the panel the current tab of its group and nothing else: the
    /// active panel and keyboard focus stay where they are. To choose which
    /// tab of a group is in front while the user works elsewhere.
    DockResult raisePanel(const PanelId &id);
    [[nodiscard]] DockPanel *activePanel() const;
    /// The panels that share a tab group with `id`, itself included, in the
    /// order of their tabs. Empty if the panel is not in a tab group.
    [[nodiscard]] QStringList tabGroupPanels(const PanelId &id) const;
    /// The panel in front of the tab group `anyPanelOfGroup` is in (see
    /// DockPanel::isCurrent()). Empty if the panel is not in a tab group.
    [[nodiscard]] PanelId currentPanel(const PanelId &anyPanelOfGroup) const;

    /// Lets the panel's tab group fill its workspace (or floating window). The
    /// layout tree is left untouched, so restoring brings everything back.
    DockResult maximizePanel(const PanelId &id);
    DockResult restoreMaximizedPanel();
    /// The maximized panel of any workspace or floating window, or an empty
    /// id. DockWorkspace::maximizedPanel() tells about one workspace.
    [[nodiscard]] PanelId maximizedPanel() const;

    // --- Columns ---------------------------------------------------------------
    /// Shrinks the column `anyPanelOfColumn` is in to a strip of buttons, one
    /// for each of its panels, or shows the panels again. A button brings its
    /// tab group out beside the strip, over what is there, and puts it away
    /// again. A floating window that is nothing but the strip grows by that
    /// group for as long as it is out. The strip is as narrow as its icons,
    /// and can be dragged wider for the titles to show.
    ///
    /// Works in any workspace; with DockWorkspace::setColumnDocking() the
    /// user has a button
    /// for it. What is dropped into an iconified column becomes part of it,
    /// what is taken out of one and floats stays iconified.
    DockResult setColumnIconified(const PanelId &anyPanelOfColumn, bool iconified);
    [[nodiscard]] bool isColumnIconified(const PanelId &anyPanelOfColumn) const;
    /// The panels of the column `id` is in, from the top, each tab group in
    /// the order of its tabs. Empty if the panel is not in a tab group.
    [[nodiscard]] QStringList columnPanels(const PanelId &id) const;

    /// Collapses the panel into an auto-hide bar of its workspace (`edge` None
    /// picks the nearest border), or pins it back into the layout.
    DockResult setPanelAutoHide(const PanelId &id, bool autoHide, DockArea edge = DockArea::None);

    // --- Policies ------------------------------------------------------------
    void setDropFilter(DockDropFilter filter);

    // --- Persistence ---------------------------------------------------------
    /// The whole layout (all workspaces and floating windows) as JSON.
    [[nodiscard]] QByteArray saveLayout() const;
    DockResult saveLayout(const QString &filePath) const;
    /// Replaces the layout by a saved one. Fails without changing anything if
    /// the data is not a usable layout; repairs what can be repaired otherwise
    /// and lists the repairs in `report`.
    DockResult restoreLayout(const QByteArray &json, DockRestoreReport *report = nullptr);
    DockResult loadLayout(const QString &filePath, DockRestoreReport *report = nullptr);
    /// Whether restoreLayout() also restores the geometry of the windows the
    /// workspaces are in (default true). Floating windows always are.
    [[nodiscard]] bool restoresWindowGeometry() const;
    void setRestoresWindowGeometry(bool enabled);

    // --- Presets and reset ---------------------------------------------------
    /// Stores the current layout under `name`, replacing a preset of that name.
    DockResult savePreset(const QString &name);
    DockResult applyPreset(const QString &name);
    DockResult removePreset(const QString &name);
    [[nodiscard]] QStringList presetNames() const;
    /// All presets as JSON, to be stored by the application.
    [[nodiscard]] QByteArray savePresets() const;
    DockResult restorePresets(const QByteArray &json);
    /// Remembers the current layout as the one resetLayout() returns to.
    void saveDefaultLayout();
    DockResult resetLayout();

    // --- Undo / redo ---------------------------------------------------------
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    DockResult undo();
    DockResult redo();
    void clearUndoHistory();
    [[nodiscard]] int undoLimit() const;
    /// Maximum number of undo steps kept (default 50; 0 disables undo).
    void setUndoLimit(int limit);

    // --- Behaviour and look --------------------------------------------------
    [[nodiscard]] bool linkedSplittersEnabled() const;
    /// Whether aligned split handles are dragged together (default true).
    /// Holding Alt while dragging moves a single handle either way.
    void setLinkedSplittersEnabled(bool enabled);
    [[nodiscard]] bool isCornerResizeEnabled() const;
    /// Whether the point where a vertical and a horizontal split handle meet
    /// can be dragged to move both at once (default true).
    void setCornerResizeEnabled(bool enabled);
    [[nodiscard]] bool isSplitterPushEnabled() const;
    /// Whether a split handle that is dragged against a neighbour that can
    /// get no smaller goes on, and takes the room from what lies behind that
    /// neighbour, which is moved along as it is (default false: the handle
    /// stops there). A column that is a strip of buttons, or a palette of
    /// fixed width, is then no obstacle between two areas that can change.
    void setSplitterPushEnabled(bool enabled);
    [[nodiscard]] bool isCenterDropEnabled() const;
    /// Whether the middle of a tab group takes a dragged panel (default
    /// true): as a new tab of that group, or, on the group the panel comes
    /// from, to leave it where it is. Turned off, a panel becomes a tab by
    /// the header of a group only: between two tabs, anywhere on the title
    /// row when the sides of the group are not open to it either
    /// (DockPolicy::allowedAreas), or on a title bar that names its panel.
    /// A workspace with nothing in it takes a panel where its tabs will be:
    /// along its top, as high as a row of tabs. The rest of the group is then no drop
    /// target, and what is let go of there floats like anything dropped
    /// outside every dock area: tabs are torn off by dragging them away.
    ///
    /// DockWorkspace::setCenterDropEnabled() says so for one workspace.
    void setCenterDropEnabled(bool enabled);
    [[nodiscard]] bool isTabDragPreviewEnabled() const;
    /// Whether tab bars show a tab drag as it would turn out (default
    /// false). The tab that is dragged leaves its bar at once, the tabs
    /// behind it closing up, and the tabs it is held over make room for it
    /// where it would go, instead of a mark between them. This is shown
    /// only: the layout changes when the tab is dropped, and a cancelled
    /// drag puts everything back as it was. The last tab of a group stays.
    void setTabDragPreviewEnabled(bool enabled);
    [[nodiscard]] bool floatsOnOutsideDrop() const;
    /// Whether dropping a dragged panel outside every dock area floats it.
    /// Defaults to true where that can be told apart from a cancelled drag
    /// (X11, Windows, macOS, and Wayland with the drag ghost); see
    /// docs/platform-notes.md.
    void setFloatsOnOutsideDrop(bool enabled);

    [[nodiscard]] FloatingWindowFrame floatingWindowFrame() const;
    /// Frame of floating windows created from now on (default Custom).
    /// Existing floating windows keep theirs.
    void setFloatingWindowFrame(FloatingWindowFrame frame);
    [[nodiscard]] FloatingWindowType floatingWindowType() const;
    /// Kind of floating windows created from now on (default Window).
    /// Existing floating windows keep theirs.
    void setFloatingWindowType(FloatingWindowType type);
    [[nodiscard]] DockGroupHeader groupHeader() const;
    /// Header of every tab group (default Tabs). Can be changed at any time.
    /// DockWorkspace::setGroupHeader() says so for one workspace.
    void setGroupHeader(DockGroupHeader header);
    [[nodiscard]] bool titleBarMovesGroup() const;
    /// What the title bar of DockGroupHeader::TitleBar stands for (default
    /// false): the current panel, or with true all the panels stacked under
    /// it. Dragging it then moves the whole tab group, and a double click
    /// floats the group or docks all of it again. One panel is still moved
    /// by its tab.
    void setTitleBarMovesGroup(bool enabled);
    [[nodiscard]] AutoHideReveal autoHideReveal() const;
    /// Where a panel that slides out of an auto-hide bar goes (default Over).
    /// Either way a click elsewhere sends it back.
    void setAutoHideReveal(AutoHideReveal reveal);
    [[nodiscard]] bool isDragGhostEnabled() const;
    /// Whether a dragged panel is shown as a window that follows the pointer
    /// and, dropped outside every dock area, becomes the floating window right
    /// there (default true). Only platforms where a window can come along
    /// with a drag do this: Wayland compositors with xdg-toplevel-drag,
    /// which carry it, and Windows and macOS, where QFlexDock moves it.
    /// Everywhere else the drag shows a picture of the tab, as it does with
    /// this turned off.
    void setDragGhostEnabled(bool enabled);

    [[nodiscard]] DockTheme theme() const;
    void setTheme(const DockTheme &theme);
    /// Replaces how the drop overlay is painted; null restores the default.
    void setOverlayPainter(std::shared_ptr<DockOverlayPainter> painter);

Q_SIGNALS:
    void workspaceAdded(QFlexDock::DockWorkspace *workspace);
    void workspaceRemoved(const QString &workspaceId);
    void panelRegistered(QFlexDock::DockPanel *panel);
    void panelAboutToBeUnregistered(QFlexDock::DockPanel *panel);

    /// Around every committed change. No intermediate state is ever visible
    /// between the two.
    void layoutAboutToChange();
    void layoutChanged();
    /// The panel is about to change / has changed where it is placed (tab
    /// group, workspace, floating window, auto-hide bar, or closed).
    void panelAboutToMove(QFlexDock::DockPanel *panel);
    void panelMoved(QFlexDock::DockPanel *panel);
    void panelOpenChanged(QFlexDock::DockPanel *panel, bool open);
    /// The user asks for the panel to be closed (DockPanel::closeRequested()).
    void panelCloseRequested(QFlexDock::DockPanel *panel);
    /// The panel's content ended up in a different top-level window.
    void panelWindowChanged(QFlexDock::DockPanel *panel, QWidget *topLevel);
    void activePanelChanged(QFlexDock::DockPanel *panel);

    /// A panel's context menu is about to be shown; add or remove actions.
    void panelContextMenuRequested(QFlexDock::DockPanel *panel, QMenu *menu);

    void undoStateChanged();
    void presetsChanged();
    void themeChanged();

    void undoLimitChanged(int limit);
    void restoresWindowGeometryChanged(bool enabled);
    void linkedSplittersEnabledChanged(bool enabled);
    void cornerResizeEnabledChanged(bool enabled);
    void splitterPushEnabledChanged(bool enabled);
    void centerDropEnabledChanged(bool enabled);
    void tabDragPreviewEnabledChanged(bool enabled);
    void floatsOnOutsideDropChanged(bool enabled);
    void dragGhostEnabledChanged(bool enabled);
    void floatingWindowFrameChanged(QFlexDock::DockManager::FloatingWindowFrame frame);
    void floatingWindowTypeChanged(QFlexDock::DockManager::FloatingWindowType type);
    void groupHeaderChanged(QFlexDock::DockGroupHeader header);
    void titleBarMovesGroupChanged(bool enabled);
    void autoHideRevealChanged(QFlexDock::DockManager::AutoHideReveal reveal);

private:
    friend class DockManagerPrivate;
    bool eventFilter(QObject *watched, QEvent *event) override;
    std::unique_ptr<DockManagerPrivate> d;
};

} // namespace QFlexDock
