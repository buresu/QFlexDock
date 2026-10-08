// SPDX-License-Identifier: MIT
#pragma once

#include "core/LayoutSolver.h"

#include <QtWidgets/QFrame>

QT_BEGIN_NAMESPACE
class QToolButton;
QT_END_NAMESPACE

namespace QFlexDock {

class DockAreaWidget;
class DockManagerPrivate;
class DockPanel;
class DockTabBar;

/// View of one tab node: a title row (tab bar and group buttons) above the
/// content of the current panel.
///
/// A group mirrors its LayoutNode and never decides anything itself: clicks
/// and drags are turned into requests to the manager, and what is shown
/// changes only when the manager hands the group a new node.
///
/// Style sheets: class selector `QFlexDock--DockTabGroup`, with the `active`
/// and `maximized` properties; the title row is `#dockTitleBar`, its buttons
/// `#dockMenuButton` and `#dockMaximizeButton`.
class QFLEXDOCK_EXPORT DockTabGroup : public QFrame
{
    Q_OBJECT
    /// The group holds the manager's active panel.
    Q_PROPERTY(bool active READ isActive)
    Q_PROPERTY(bool maximized READ isMaximized)

public:
    DockTabGroup(DockManagerPrivate *manager, DockAreaWidget *area);
    ~DockTabGroup() override;

    /// Brings tabs and content in line with `node`.
    void setNode(const LayoutNode &node, bool maximized);
    void detachFromManager();

    [[nodiscard]] NodeId nodeId() const { return m_nodeId; }
    [[nodiscard]] const QStringList &panelIds() const { return m_panels; }
    [[nodiscard]] PanelId currentPanel() const { return m_current; }
    [[nodiscard]] bool isActive() const { return m_active; }
    [[nodiscard]] bool isMaximized() const { return m_maximized; }
    void setActive(bool active);

    [[nodiscard]] DockTabBar *tabBar() const { return m_tabBar; }
    [[nodiscard]] QWidget *titleBar() const { return m_titleBar; }
    [[nodiscard]] QWidget *contentHost() const { return m_host; }
    [[nodiscard]] QToolButton *menuButton() const { return m_menuButton; }
    [[nodiscard]] QToolButton *maximizeButton() const { return m_maximizeButton; }

    /// Smallest and largest size this group can take, from its panels' content.
    [[nodiscard]] SizeLimits sizeLimits() const;

    /// Re-reads title, icon and tab state of one panel / of everything.
    void refreshPanel(const PanelId &panel);
    void refreshAppearance();
    /// Lays the current content out in the host and shows it.
    void updateContentGeometry();

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildTabs();
    void updateTab(int index);
    void syncContents();
    void showGroupMenu();
    void restyle();

    DockManagerPrivate *m_manager;
    DockAreaWidget *m_area;
    NodeId m_nodeId;
    QStringList m_panels;
    PanelId m_current;
    bool m_active = false;
    bool m_maximized = false;

    QWidget *m_titleBar;
    DockTabBar *m_tabBar;
    QToolButton *m_menuButton;
    QToolButton *m_maximizeButton;
    QWidget *m_host;
};

} // namespace QFlexDock
