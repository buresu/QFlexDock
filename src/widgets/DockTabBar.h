// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockTheme.h>
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
    /// The tab at `index` is no panel's: an empty place kept open among the
    /// tabs for what is being dragged over them. It has no tab data, is not
    /// painted, and is as wide as setGapWidth() says.
    [[nodiscard]] bool isGap(int index) const { return index >= 0 && panelAt(index).isEmpty(); }
    [[nodiscard]] int gapIndex() const;
    void setGapWidth(int width);
    [[nodiscard]] int indexOfPanel(const PanelId &panel) const;

    /// Panels whose title is drawn in italics (preview tabs).
    void setItalicPanels(const QSet<PanelId> &panels);
    [[nodiscard]] bool isActiveGroup() const { return m_activeGroup; }
    void setActiveGroup(bool active);
    [[nodiscard]] QColor activeIndicatorColor() const { return m_indicatorColor; }
    void setActiveIndicatorColor(const QColor &color);

    /// How wide tabs are and what becomes of them in a bar too narrow for
    /// them (DockTheme::tabWidth and tabOverflow).
    void setTabSizing(int tabWidth, DockTabOverflow overflow);
    [[nodiscard]] int tabWidth() const { return m_tabWidth; }
    [[nodiscard]] DockTabOverflow tabOverflow() const
    {
        return m_shrink ? DockTabOverflow::Shrink : DockTabOverflow::Scroll;
    }
    /// Whether the bar ends with its last tab, for something else to stand
    /// right behind the tabs. Otherwise it takes the whole header.
    [[nodiscard]] bool hugsTabs() const { return m_hugsTabs; }
    void setHugsTabs(bool hugs);

    /// The part of the bar where a drop inserts between tabs: the tabs
    /// themselves plus a little room behind the last one for appending
    /// (which may lie beyond a bar that ends with its last tab). The empty
    /// rest of the bar is not part of it.
    [[nodiscard]] QRect tabDropRegion() const;
    /// Position among the tabs a drop at `pos` would insert at (0..count).
    /// A gap is not counted: it is where the drop goes, not a tab.
    [[nodiscard]] int insertIndexAt(const QPoint &pos) const;
    /// Marker to draw for an insertion at `index`, in tab bar coordinates.
    [[nodiscard]] QRect insertIndicatorRect(int index) const;

Q_SIGNALS:
    /// The user pressed a tab and dragged it beyond the drag threshold.
    void panelDragStarted(const QFlexDock::PanelId &panel);
    /// Same, but on the part of the bar without tabs: drags the whole group.
    void groupDragStarted();
    /// Middle click on a tab (pressed and let go of on the same one).
    void panelCloseRequested(const QFlexDock::PanelId &panel);
    void panelMenuRequested(const QFlexDock::PanelId &panel, const QPoint &globalPos);
    /// Double click on a tab or on the empty part of the bar.
    void barDoubleClicked(bool onTab);

public:
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    [[nodiscard]] QSize tabSizeHint(int index) const override;
    [[nodiscard]] QSize minimumTabSizeHint(int index) const override;
    void tabLayoutChange() override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    [[nodiscard]] QSize unsqueezedMinimum(int index) const;
    void updateTabButtons();

    QSet<PanelId> m_italic;
    int m_tabWidth = -1;
    int m_gapWidth = 0;
    bool m_shrink = false;
    bool m_hugsTabs = false;
    /// Tab buttons were hidden for want of room, and may have to come back.
    bool m_buttonsHidden = false;
    mutable bool m_measuringMinimum = false;
    QColor m_indicatorColor;
    bool m_activeGroup = false;
    bool m_pressed = false;
    int m_pressIndex = -1;
    /// The tab the middle button went down on.
    PanelId m_middlePanel;
    QPoint m_pressPos;
};

} // namespace QFlexDock
