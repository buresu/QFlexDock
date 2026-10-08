// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#include <QtCore/QSet>
#include <QtWidgets/QTabBar>

namespace QFlexDock {

/// The tab strip of a tab group. A QTabBar, so the host style, palette, font
/// and `QTabBar::tab` style-sheet rules apply as they do everywhere else in
/// the application (class selector: `QFlexDock--DockTabBar`).
///
/// Tabs are not rearranged by QTabBar's own "movable" mode. Dragging a tab
/// starts a dock drag instead, and reordering is a drop on the tab bar, so one
/// mechanism covers reordering, moving between groups and tearing off.
class QFLEXDOCK_EXPORT DockTabBar : public QTabBar
{
    Q_OBJECT
    /// Marks the current tab of the group that holds the active panel.
    Q_PROPERTY(bool activeGroup READ isActiveGroup)
    /// Colour of that mark. Invalid uses the palette's Highlight; a fully
    /// transparent colour turns the mark off.
    Q_PROPERTY(QColor activeIndicatorColor READ activeIndicatorColor WRITE setActiveIndicatorColor)

public:
    explicit DockTabBar(QWidget *parent = nullptr);

    [[nodiscard]] PanelId panelAt(int index) const { return tabData(index).toString(); }
    [[nodiscard]] int indexOfPanel(const PanelId &panel) const;

    /// Panels whose title is drawn in italics (preview tabs).
    void setItalicPanels(const QSet<PanelId> &panels);
    [[nodiscard]] bool isActiveGroup() const { return m_activeGroup; }
    void setActiveGroup(bool active);
    [[nodiscard]] QColor activeIndicatorColor() const { return m_indicatorColor; }
    void setActiveIndicatorColor(const QColor &color);

    /// The part of the bar where a drop inserts between tabs: the tabs
    /// themselves plus a little room behind the last one for appending. The
    /// empty rest of the bar is not part of it.
    [[nodiscard]] QRect tabDropRegion() const;
    /// Tab index a drop at `pos` would insert at (0..count).
    [[nodiscard]] int insertIndexAt(const QPoint &pos) const;
    /// Marker to draw for an insertion at `index`, in tab bar coordinates.
    [[nodiscard]] QRect insertIndicatorRect(int index) const;

Q_SIGNALS:
    /// The user pressed a tab and dragged it beyond the drag threshold.
    void panelDragStarted(const QFlexDock::PanelId &panel);
    /// Same, but on the part of the bar without tabs: drags the whole group.
    void groupDragStarted();
    /// Middle click on a tab.
    void panelCloseRequested(const QFlexDock::PanelId &panel);
    void panelMenuRequested(const QFlexDock::PanelId &panel, const QPoint &globalPos);
    /// Double click on a tab or on the empty part of the bar.
    void barDoubleClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QSet<PanelId> m_italic;
    QColor m_indicatorColor;
    bool m_activeGroup = false;
    bool m_pressed = false;
    int m_pressIndex = -1;
    QPoint m_pressPos;
};

} // namespace QFlexDock
