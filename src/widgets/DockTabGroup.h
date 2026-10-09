// SPDX-License-Identifier: MIT
#pragma once

#include "core/LayoutSolver.h"

#include <QtWidgets/QFrame>

QT_BEGIN_NAMESPACE
class QBoxLayout;
class QLabel;
class QToolButton;
QT_END_NAMESPACE

namespace QFlexDock {

class DockAreaWidget;
class DockManagerPrivate;
class DockPanel;
class DockTabBar;

/// View of one tab node: a header above the content of the current panel.
///
/// The header is a title row holding either the tabs (GroupHeader::Tabs) or
/// the current panel's title (GroupHeader::TitleBar; the tabs then go below
/// the content, and only show when there is more than one), followed by the
/// group's buttons. A panel can do without a header altogether
/// (DockPanel::setHeaderVisible()).
///
/// A group mirrors its LayoutNode and never decides anything itself: clicks
/// and drags are turned into requests to the manager, and what is shown
/// changes only when the manager hands the group a new node.
///
/// Style sheets: class selector `QFlexDock--DockTabGroup`, with the `active`,
/// `maximized` and `headerVisible` properties; the title row is
/// `#dockTitleBar`, the title in it `#dockTitle`, its buttons
/// `#dockMenuButton`, `#dockMaximizeButton`, `#dockFloatButton` and
/// `#dockCloseButton`.
class QFLEXDOCK_EXPORT DockTabGroup : public QFrame
{
    Q_OBJECT
    /// The group holds the manager's active panel.
    Q_PROPERTY(bool active READ isActive)
    Q_PROPERTY(bool maximized READ isMaximized)
    Q_PROPERTY(bool headerVisible READ isHeaderVisible)

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
    [[nodiscard]] bool isHeaderVisible() const { return m_headerVisible; }
    void setActive(bool active);

    [[nodiscard]] DockTabBar *tabBar() const { return m_tabBar; }
    [[nodiscard]] QWidget *titleBar() const { return m_titleBar; }
    [[nodiscard]] QWidget *contentHost() const { return m_host; }
    [[nodiscard]] QToolButton *menuButton() const { return m_menuButton; }
    [[nodiscard]] QToolButton *maximizeButton() const { return m_maximizeButton; }
    [[nodiscard]] QToolButton *floatButton() const { return m_floatButton; }
    [[nodiscard]] QToolButton *closeButton() const { return m_closeButton; }
    [[nodiscard]] QLabel *titleLabel() const { return m_titleLabel; }

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
    void updateHeader();
    void syncContents();
    void showGroupMenu();
    void showPanelMenu(const PanelId &panel, const QPoint &globalPos);
    void restyle();
    void startPanelDrag(const PanelId &panel, const QPixmap &pixmap);
    void startGroupDrag();
    [[nodiscard]] bool moveWindowInstead(qsizetype draggedPanels);
    void toggleMaximized();
    void toggleFloating();
    void closeByUser(const PanelId &panel);
    [[nodiscard]] bool titleBarEvent(QEvent *event);

    DockManagerPrivate *m_manager;
    DockAreaWidget *m_area;
    NodeId m_nodeId;
    QStringList m_panels;
    PanelId m_current;
    bool m_active = false;
    bool m_maximized = false;
    bool m_headerVisible = true;
    /// GroupHeader::TitleBar is in effect.
    bool m_titleMode = false;

    QWidget *m_titleBar;
    QBoxLayout *m_titleLayout;
    DockTabBar *m_tabBar;
    QLabel *m_titleLabel;
    QToolButton *m_menuButton;
    QToolButton *m_maximizeButton;
    QToolButton *m_floatButton;
    QToolButton *m_closeButton;
    QWidget *m_host;
    QPoint m_titlePress;
    bool m_titlePressed = false;
};

} // namespace QFlexDock
