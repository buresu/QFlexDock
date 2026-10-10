// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#include <QtWidgets/QWidget>

#include <memory>

namespace QFlexDock {

class DockManager;
class DockManagerPrivate;

/// The dock area of one window: shows one layout tree of tab groups and split
/// handles, plus the auto-hide bars along its borders.
///
/// Created by DockManager::createWorkspace() and then owned like any widget
/// (typically by the QMainWindow it is the central widget of). Destroying a
/// workspace closes its panels; the panels and their content stay registered
/// with the manager and can be shown elsewhere.
class QFLEXDOCK_EXPORT DockWorkspace : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString workspaceId READ workspaceId CONSTANT)
    Q_PROPERTY(bool columnDocking READ isColumnDocking WRITE setColumnDocking)
    Q_PROPERTY(QFlexDock::DockGroupHeader groupHeader READ groupHeader WRITE setGroupHeader
               RESET unsetGroupHeader)
    Q_PROPERTY(QFlexDock::DockTitleButtons titleButtons READ titleButtons WRITE setTitleButtons
               RESET unsetTitleButtons)
    Q_PROPERTY(bool centerDropEnabled READ isCenterDropEnabled WRITE setCenterDropEnabled
               RESET unsetCenterDropEnabled)

public:
    ~DockWorkspace() override;

    [[nodiscard]] QString workspaceId() const;
    /// Null once the manager has been destroyed.
    [[nodiscard]] DockManager *manager() const;

    /// Same as DockManager::movePanel(id, this, area, fraction).
    DockResult addPanel(const PanelId &id, DockArea area = DockArea::Center,
                        double fraction = -1.0);

    /// Panels docked in this workspace, in layout order. Floating and
    /// auto-hidden panels are not included.
    [[nodiscard]] QStringList panels() const;
    /// The panel whose tab group fills this workspace (see
    /// DockManager::maximizePanel()), or an empty id.
    [[nodiscard]] PanelId maximizedPanel() const;

    /// Whether the tab groups of this workspace, and of the floating windows
    /// it owns, are docked in columns (default false). A column is what
    /// stands above one another: a tab group, or several. Then
    ///  - a drop on the left or right side of a group docks beside the
    ///    column that group is in, never into it;
    ///  - every column of panels that may be moved has a bar above it. The
    ///    bar is dragged to move the column as it is, and its button shrinks
    ///    the column to buttons (DockManager::setColumnIconified()). In a
    ///    floating window the bar is what the window is moved by and closed
    ///    with, so such a window has no title row
    ///    (DockManager::FloatingWindowFrame::Custom is Minimal there).
    [[nodiscard]] bool isColumnDocking() const;
    void setColumnDocking(bool enabled);

    // What this workspace, and the floating windows it owns, have of their
    // own in place of what the manager says for all of them. Each is the
    // manager's until it is set, and again once it is unset.

    /// The header of the tab groups (DockManager::setGroupHeader()):
    /// documents under their tabs in the middle of tool panels with title
    /// bars, say. A panel takes the header of where it is put.
    [[nodiscard]] DockGroupHeader groupHeader() const;
    void setGroupHeader(DockGroupHeader header);
    void unsetGroupHeader();
    /// The built-in header buttons, in place of the theme's
    /// (DockTheme::titleButtons).
    [[nodiscard]] DockTitleButtons titleButtons() const;
    void setTitleButtons(DockTitleButtons buttons);
    void unsetTitleButtons();
    /// Whether the middle of a tab group takes a dragged panel
    /// (DockManager::setCenterDropEnabled()): documents that become tabs of
    /// each other by their tab rows only, among panels that take a drop
    /// anywhere.
    [[nodiscard]] bool isCenterDropEnabled() const;
    void setCenterDropEnabled(bool enabled);
    void unsetCenterDropEnabled();

private:
    friend class DockManager;
    friend class DockManagerPrivate;
    struct Private;
    DockWorkspace(DockManager *manager, const QString &id, QWidget *parent);
    std::unique_ptr<Private> d;
};

} // namespace QFlexDock
