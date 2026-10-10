// SPDX-License-Identifier: MIT
#pragma once

#include "core/DockManager_p.h"
#include "core/LayoutSolver.h"

#include <QtWidgets/QFrame>
#include <QtWidgets/QToolButton>

QT_BEGIN_NAMESPACE
class QBoxLayout;
QT_END_NAMESPACE

namespace QFlexDock {

class DockAreaWidget;

/// The bar above a column of tab groups, where a workspace docks in columns
/// (DockManager::setColumnDocking()). Dragged, it moves the whole column; its
/// button shrinks the column to a strip of buttons and brings it back, and so
/// does a double click. In a floating window that holds nothing else it is
/// the window's title bar, and has a button to close it.
///
/// Style sheets: class selector `QFlexDock--DockColumnBar`, with the
/// `iconified` property; its buttons are `#dockIconifyButton` and
/// `#dockColumnCloseButton`.
class QFLEXDOCK_EXPORT DockColumnBar : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool iconified READ isIconified)

public:
    DockColumnBar(DockManagerPrivate *manager, DockAreaWidget *area);

    /// `towardsRight`: the mark on the button points right. `closes`: the
    /// bar stands for the window, which it can close.
    void configure(NodeId column, bool iconified, bool towardsRight, bool closes);
    void detachFromManager() { m_manager = nullptr; }
    void refreshAppearance();

    [[nodiscard]] NodeId column() const { return m_column; }
    [[nodiscard]] bool isIconified() const { return m_iconified; }
    [[nodiscard]] QToolButton *iconifyButton() const { return m_iconify; }
    [[nodiscard]] QToolButton *closeButton() const { return m_close; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    DockManagerPrivate *m_manager;
    DockAreaWidget *m_area;
    NodeId m_column;
    bool m_iconified = false;
    bool m_towardsRight = false;
    QToolButton *m_iconify;
    QToolButton *m_close;
    QPoint m_press;
    bool m_pressed = false;
};

/// The button of one panel in an iconified column: its icon, and its title
/// where the strip is wide enough. A click brings the panel's tab group out
/// beside the strip or puts it away; dragged, the panel comes along. Class
/// selector `QFlexDock--DockIconButton` (a QToolButton, checked while its
/// panel is out).
class QFLEXDOCK_EXPORT DockIconButton : public QToolButton
{
    Q_OBJECT

public:
    DockIconButton(const PanelId &panel, QWidget *parent);

    [[nodiscard]] PanelId panelId() const { return m_panel; }

Q_SIGNALS:
    void dragStarted();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    /// Checked by the strip, for the panel that is out: a click only asks.
    void nextCheckState() override {}

private:
    PanelId m_panel;
    QPoint m_press;
};

/// The grip above the buttons of one tab group in an iconified column: the
/// group is dragged by it. Class selector `QFlexDock--DockIconGrip`.
class QFLEXDOCK_EXPORT DockIconGrip : public QWidget
{
    Q_OBJECT

public:
    explicit DockIconGrip(QWidget *parent);

    QSize sizeHint() const override { return QSize(16, 8); }
    QSize minimumSizeHint() const override { return QSize(8, 8); }

Q_SIGNALS:
    void dragStarted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QPoint m_press;
    bool m_pressed = false;
};

/// View of an iconified column: for each of its tab groups a grip and the
/// buttons of its panels, one below the other. A panel that brings a small
/// form of itself (DockPanel::setCompactWidget()) has that in place of its
/// button.
///
/// Like a tab group, it decides nothing: a click asks the dock area to bring
/// a group out, a drag goes to the drag controller.
///
/// Style sheets: class selector `QFlexDock--DockIconStrip`, with the
/// `labelled` property (wide enough for the titles).
class QFLEXDOCK_EXPORT DockIconStrip : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(bool labelled READ isLabelled)

public:
    DockIconStrip(DockManagerPrivate *manager, DockAreaWidget *area);
    ~DockIconStrip() override;

    /// Brings grips and buttons in line with `column`.
    void setColumn(const LayoutNode &column);
    /// The tab group that is out beside the strip (null: none): the button
    /// of its current panel is checked.
    void setShown(NodeId group);
    void detachFromManager() { m_manager = nullptr; }
    /// For a strip that is no longer needed and about to be deleted: what
    /// belongs to the manager leaves it now, and it forgets the manager.
    void retire();

    [[nodiscard]] NodeId nodeId() const { return m_nodeId; }
    [[nodiscard]] bool isLabelled() const { return m_labelled; }
    [[nodiscard]] QList<DockIconButton *> buttons() const;
    [[nodiscard]] DockIconButton *button(const PanelId &panel) const;
    [[nodiscard]] QList<DockIconGrip *> grips() const;
    /// The tab groups shown, from the top.
    [[nodiscard]] QList<NodeId> groups() const;
    /// Grip and buttons of one tab group; strip coordinates. Null if it is
    /// not in the strip.
    [[nodiscard]] QRect blockRect(NodeId group) const;
    [[nodiscard]] QWidget *block(NodeId group) const;
    /// The tab group whose block `pos` is on or nearest to, and whether
    /// `pos` is below the last one.
    [[nodiscard]] NodeId groupAt(const QPoint &pos, bool *below = nullptr) const;

    /// As narrow as its icons, as wide as its titles, as high as it takes.
    [[nodiscard]] SizeLimits sizeLimits() const;
    /// The size to give a window that holds nothing but the strip.
    [[nodiscard]] QSize preferredSize() const;

    void refreshPanel(const PanelId &panel);
    void refreshAppearance();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct Block
    {
        NodeId node;
        QStringList panels;
        PanelId active;
        QWidget *widget = nullptr;
    };

    void rebuild();
    void releaseCompactWidgets();
    void measure();
    void updateLabels();
    void updateChecked();
    [[nodiscard]] QIcon iconFor(const DockPanel *panel) const;

    DockManagerPrivate *m_manager;
    DockAreaWidget *m_area;
    NodeId m_nodeId;
    NodeId m_shown;
    QList<Block> m_blocks;
    QBoxLayout *m_layout;
    bool m_labelled = false;
    // Widths the strip can have: with icons alone, and with the titles.
    int m_iconWidth = 0;
    int m_labelWidth = 0;
};

} // namespace QFlexDock
