// SPDX-License-Identifier: MIT
#pragma once

#include "core/DockManager_p.h"

#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QFrame>

#include <array>

QT_BEGIN_NAMESPACE
class QBoxLayout;
class QGridLayout;
class QLabel;
class QStyleOptionTab;
class QToolButton;
QT_END_NAMESPACE

namespace QFlexDock {

/// One collapsed panel in an auto-hide bar. Painted as a tab of the host style
/// (QStyle::CE_TabBarTab, rotated on the left and right borders); class
/// selector `QFlexDock--DockAutoHideTab`.
class QFLEXDOCK_EXPORT DockAutoHideTab : public QAbstractButton
{
    Q_OBJECT

public:
    DockAutoHideTab(const PanelId &panel, DockArea edge, QWidget *parent);

    [[nodiscard]] PanelId panelId() const { return m_panel; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void initOption(QStyleOptionTab *option) const;

    PanelId m_panel;
    DockArea m_edge;
};

/// The strip along one border of a workspace holding its auto-hidden panels.
/// Class selector `QFlexDock--DockAutoHideBar`; the `edge` property is "left",
/// "right", "top" or "bottom".
class QFLEXDOCK_EXPORT DockAutoHideBar : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString edge READ edgeName CONSTANT)

public:
    DockAutoHideBar(DockArea edge, QWidget *parent);

    [[nodiscard]] DockArea edge() const { return m_edge; }
    [[nodiscard]] QString edgeName() const;
    [[nodiscard]] QList<DockAutoHideTab *> tabs() const;
    [[nodiscard]] DockAutoHideTab *tab(const PanelId &panel) const;

    void setPanels(DockManagerPrivate *manager, const QStringList &panels);
    void setExpanded(const PanelId &panel);

Q_SIGNALS:
    void tabClicked(const QFlexDock::PanelId &panel);

private:
    DockArea m_edge;
    QBoxLayout *m_layout;
    QStringList m_panels;
};

/// The panel that slides out of an auto-hide bar over the dock area. Class
/// selector `QFlexDock--DockAutoHidePopup`; its buttons are `#dockPinButton`
/// and `#dockCloseButton`.
class QFLEXDOCK_EXPORT DockAutoHidePopup : public QFrame
{
    Q_OBJECT

public:
    explicit DockAutoHidePopup(QWidget *parent);

    [[nodiscard]] QWidget *contentHost() const { return m_host; }
    [[nodiscard]] QToolButton *pinButton() const { return m_pin; }
    [[nodiscard]] QToolButton *closeButton() const { return m_close; }
    void setTitle(const QString &title);
    void setEdge(DockArea edge);
    /// Extent across the border it slid out of, as chosen by dragging its grip.
    [[nodiscard]] int extent() const { return m_extent; }

Q_SIGNALS:
    void extentChanged();
    void escapePressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QLabel *m_title;
    QToolButton *m_pin;
    QToolButton *m_close;
    QWidget *m_host;
    QWidget *m_grip;
    QGridLayout *m_layout;
    DockArea m_edge = DockArea::Left;
    int m_extent = -1;
    QPoint m_gripPress;
    int m_gripStartExtent = 0;
};

/// Everything auto-hide for one workspace: the four bars and the popup.
/// Which panel is slid out is view state, not part of the layout.
class QFLEXDOCK_EXPORT DockAutoHideContainer : public QObject
{
    Q_OBJECT

public:
    DockAutoHideContainer(DockManagerPrivate *manager, DockWorkspace *workspace,
                          DockAreaWidget *area);

    void detachFromManager();
    [[nodiscard]] DockAutoHideBar *bar(DockArea edge) const;
    [[nodiscard]] DockAutoHidePopup *popup() const { return m_popup; }
    [[nodiscard]] PanelId expandedPanel() const { return m_expanded; }

    void setLayoutState(const ContainerState &container);
    void expand(const PanelId &panel);
    void collapse();
    void refreshPanel(const PanelId &panel);
    void refreshAppearance();
    /// A mouse press somewhere in the application: collapse unless it was on
    /// the popup or on a bar.
    void pressedElsewhere(QWidget *pressed);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updatePopupGeometry();
    [[nodiscard]] DockArea edgeOf(const PanelId &panel) const;

    DockManagerPrivate *m_manager;
    DockWorkspace *m_workspace;
    DockAreaWidget *m_area;
    std::array<DockAutoHideBar *, 4> m_bars{};
    std::array<QStringList, 4> m_panels;
    DockAutoHidePopup *m_popup;
    PanelId m_expanded;
};

} // namespace QFlexDock
