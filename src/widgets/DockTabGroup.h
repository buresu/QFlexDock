// SPDX-License-Identifier: MIT
#pragma once

#include "core/LayoutSolver.h"

#include <QtCore/QPointer>
#include <QtWidgets/QFrame>

#include <array>

QT_BEGIN_NAMESPACE
class QAction;
class QBoxLayout;
class QLabel;
class QToolButton;
QT_END_NAMESPACE

namespace QFlexDock {

class DockAreaWidget;
class DockFloatingWindow;
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
/// The only group of a floating window that has no title row for it
/// (DockFloatingWindow::headerIsTitle()) is that window's title: one panel
/// is named as by GroupHeader::TitleBar, without a tab, and the maximize and
/// close buttons are there and act on the window.
///
/// A group mirrors its LayoutNode and never decides anything itself: clicks
/// and drags are turned into requests to the manager, and what is shown
/// changes only when the manager hands the group a new node.
///
/// Style sheets: class selector `QFlexDock--DockTabGroup`, with the `active`,
/// `maximized` and `headerVisible` properties; the title row is
/// `#dockTitleBar`, the title in it `#dockTitle`, its buttons
/// `#dockMenuButton`, `#dockMaximizeButton`, `#dockFloatButton` and
/// `#dockCloseButton`. The buttons of the current panel's own actions
/// (DockPanel::setTitleActions()) are `#dockActionButton`s, each with the
/// object name of its action as its `action` property, and the lines between
/// them `#dockActionSeparator`. They are inside `#dockTitleActions` at the
/// end of the header, `#dockTitleStartActions` at its start and
/// `#dockTabActions` behind the tabs.
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
    /// Holds what the current panel's title actions for `place` are shown
    /// as, in order.
    [[nodiscard]] QWidget *actionBar(DockTitlePlace place = DockTitlePlace::End) const
    {
        return m_actionBars[size_t(place)].bar;
    }
    [[nodiscard]] QWidget *widgetForAction(const QAction *action) const;
    /// Gives the widgets of QWidgetActions back. For a group that is about
    /// to go: the group taking over may need them before this one is deleted.
    void releaseTitleActions() { clearTitleActions(); }

    // --- Preview of a tab drag (DockManager::setTabDragPreviewEnabled()) -------
    // What is shown only; the group's node is what it was.
    /// `panel` is being dragged out of this group: its tab is not shown, and
    /// if it was in front, its neighbour's content is. Empty: as it is.
    void setDraggedOut(const PanelId &panel);
    [[nodiscard]] PanelId draggedOut() const { return m_draggedOut; }
    /// Keeps a place open among the tabs shown, before the tab at `index`,
    /// for what is held over them; -1 closes it.
    void setDropGap(int index);
    [[nodiscard]] int dropGap() const { return m_dropGap; }
    /// The position among the group's panels a drop at `pos` (tab bar
    /// coordinates) takes, and in `gap` where among the tabs shown that is.
    [[nodiscard]] int dropIndexAt(const QPoint &pos, int *gap = nullptr) const;
    /// The panel whose content and header are shown: the current one, unless
    /// that is being dragged out.
    [[nodiscard]] PanelId shownPanel() const;

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
    void insertGap(int index);
    void updateTab(int index);
    void updateHeader();
    struct ActionBar;
    void updateTitleActions(const DockPanel *current);
    [[nodiscard]] bool fillActionBar(ActionBar &bar, const QList<QAction *> &actions);
    void clearActionBar(ActionBar &bar);
    void clearTitleActions();
    void headerDoubleClicked(bool onTab);
    void syncContents();
    void showGroupMenu();
    void showPanelMenu(const PanelId &panel, const QPoint &globalPos);
    void restyle();
    void startPanelDrag(const PanelId &panel, const QPixmap &pixmap);
    void startGroupDrag();
    [[nodiscard]] bool moveWindowInstead(qsizetype draggedPanels, bool byHeader);
    [[nodiscard]] DockFloatingWindow *floatingWindow() const;
    void toggleMaximized();
    void toggleFloating();
    void closeByUser(const PanelId &panel);
    [[nodiscard]] bool titleBarEvent(QEvent *event);

    DockManagerPrivate *m_manager;
    DockAreaWidget *m_area;
    NodeId m_nodeId;
    QStringList m_panels;
    PanelId m_current;
    PanelId m_draggedOut;
    int m_dropGap = -1;
    bool m_active = false;
    bool m_maximized = false;
    bool m_headerVisible = true;
    /// The header names the current panel, as with GroupHeader::TitleBar.
    bool m_titleMode = false;
    /// The header is the title of the floating window the group is alone in.
    bool m_windowTitle = false;

    // Null until the constructor gets to them: the event filter is asked
    // about the parts created first while the later ones do not exist yet.
    QWidget *m_titleBar = nullptr;
    QBoxLayout *m_titleLayout = nullptr;
    DockTabBar *m_tabBar = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_menuButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_floatButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    /// What is in an action bar: each action with the widget standing for it.
    struct ShownAction
    {
        QPointer<QAction> action;
        QPointer<QWidget> widget;
        /// The widget was asked of a QWidgetAction and goes back to it.
        bool requested = false;
    };
    struct ActionBar
    {
        QWidget *bar = nullptr;
        QBoxLayout *layout = nullptr;
        QList<ShownAction> shown;
        QList<QAction *> list;
    };
    /// One for each DockTitlePlace.
    std::array<ActionBar, 3> m_actionBars;
    /// Takes the room behind tabs that only take what they need.
    QWidget *m_filler = nullptr;
    bool m_actionRetried = false;
    QWidget *m_host = nullptr;
    QPoint m_titlePress;
    bool m_titlePressed = false;
};

} // namespace QFlexDock
