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
    Q_PROPERTY(bool linkedSplittersEnabled READ linkedSplittersEnabled
               WRITE setLinkedSplittersEnabled NOTIFY linkedSplittersEnabledChanged)

public:
    /// What happens to a panel's remembered position when it is unregistered.
    enum class PlacementMemory {
        /// Remember it: registering the same id again puts the panel back.
        Keep,
        Forget,
    };
    Q_ENUM(PlacementMemory)

    /// Who draws the frame of floating windows.
    enum class FloatingFrame {
        /// The window system: its title bar, buttons, borders and behaviour.
        Native,
        /// QFlexDock: a frameless window with a title row and resizable
        /// edges of its own, styled like the rest of the dock UI. Where the
        /// platform allows it (see docs/platform-notes.md) such a window can
        /// be docked by dragging its title row onto a dock area.
        Custom,
        /// QFlexDock, without a title row: just a resizable border around
        /// the content. The headers of the tab groups inside are what the
        /// user takes hold of, which suits GroupHeader::TitleBar.
        Minimal,
    };
    Q_ENUM(FloatingFrame)

    /// What a tab group has at its top.
    enum class GroupHeader {
        /// Its tabs, always.
        Tabs,
        /// A title bar naming the current panel. Tabs appear only once the
        /// group holds more than one panel, and then at its bottom.
        TitleBar,
    };
    Q_ENUM(GroupHeader)

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
    /// shown until it is placed (addPanel(), showPanel(), a restored layout).
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
    DockResult addPanel(const PanelId &id, DockWorkspace *workspace,
                        DockArea area = DockArea::Center, double fraction = -1.0);
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

    /// Moves the panel (or its whole tab group) into a new floating window. A
    /// null geometry picks a default near the panel's current position. Note
    /// that Wayland compositors ignore the position.
    DockResult floatPanel(const PanelId &id, const QRect &geometry = {});
    DockResult floatTabGroup(const PanelId &anyPanelOfGroup, const QRect &geometry = {});
    /// Puts a floating or auto-hidden panel back where it was last docked.
    DockResult dockPanel(const PanelId &id);

    /// Shows a closed panel where it last was (or in the first workspace) and
    /// activates it.
    DockResult showPanel(const PanelId &id);
    /// Closes the panel: it leaves the layout but stays registered, and its
    /// position is remembered for showPanel().
    DockResult hidePanel(const PanelId &id);
    DockResult togglePanel(const PanelId &id);
    /// Makes the panel the current tab of its group, raises its window and
    /// gives it keyboard focus.
    DockResult activatePanel(const PanelId &id);
    [[nodiscard]] DockPanel *activePanel() const;

    /// Lets the panel's tab group fill its workspace (or floating window). The
    /// layout tree is left untouched, so restoring brings everything back.
    DockResult maximizePanel(const PanelId &id);
    DockResult restoreMaximizedPanel();
    /// The maximized panel of `workspace`, or of any container when null.
    [[nodiscard]] PanelId maximizedPanel(const DockWorkspace *workspace = nullptr) const;

    /// Collapses the panel into an auto-hide bar of its workspace (`edge` None
    /// picks the nearest border), or pins it back into the layout.
    DockResult setPanelAutoHide(const PanelId &id, bool autoHide, DockArea edge = DockArea::None);

    // --- Policies ------------------------------------------------------------
    DockResult setDockPolicy(const PanelId &id, const DockPolicy &policy);
    [[nodiscard]] DockPolicy dockPolicy(const PanelId &id) const;
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
    [[nodiscard]] bool floatsOnOutsideDrop() const;
    /// Whether dropping a dragged panel outside every dock area floats it.
    /// Defaults to true where that can be told apart from a cancelled drag
    /// (X11, and Wayland with the drag ghost); see docs/platform-notes.md.
    void setFloatsOnOutsideDrop(bool enabled);

    [[nodiscard]] FloatingFrame floatingWindowFrame() const;
    /// Frame of floating windows created from now on (default Native).
    /// Existing floating windows keep theirs.
    void setFloatingWindowFrame(FloatingFrame frame);
    [[nodiscard]] GroupHeader groupHeader() const;
    /// Header of every tab group (default Tabs). Can be changed at any time.
    void setGroupHeader(GroupHeader header);
    [[nodiscard]] bool isDragGhostEnabled() const;
    /// Whether a dragged panel is shown as a window that follows the pointer
    /// and, dropped outside every dock area, becomes the floating window right
    /// there (default true). Only platforms that can carry a window along
    /// with a drag do this: Wayland compositors with xdg-toplevel-drag.
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
    /// The panel's content ended up in a different top-level window.
    void panelWindowChanged(QFlexDock::DockPanel *panel, QWidget *topLevel);
    void activePanelChanged(QFlexDock::DockPanel *panel);

    /// A panel's context menu is about to be shown; add or remove actions.
    void panelContextMenuRequested(QFlexDock::DockPanel *panel, QMenu *menu);

    void undoStateChanged();
    void presetsChanged();
    void themeChanged();
    void linkedSplittersEnabledChanged(bool enabled);

private:
    friend class DockManagerPrivate;
    bool eventFilter(QObject *watched, QEvent *event) override;
    std::unique_ptr<DockManagerPrivate> d;
};

} // namespace QFlexDock
