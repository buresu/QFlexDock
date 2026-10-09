// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#include <QtWidgets/QWidget>

namespace QFlexDock {

class DockAreaWidget;

/// The draggable bar between two children of a split. It carries no layout
/// logic: it reports drags to its DockAreaWidget, which moves this handle
/// together with every handle linked to it.
///
/// Painted with the host style's splitter handle (QStyle::CE_Splitter); a
/// style sheet can restyle it through the class selector
/// `QFlexDock--DockSplitHandle` and the `orientation`, `hovered` and `pressed`
/// properties.
///
/// The bar may be drawn as thin as one pixel (several styles ask for that),
/// which nobody can aim at, and which Qt (6.12 at least) does not even
/// deliver mouse events to. The widget is therefore never narrower than
/// MinimumGrabExtent: a thinner bar gets a margin on both sides that reaches
/// over the neighbouring tab groups, takes the mouse, and paints nothing.
class QFLEXDOCK_EXPORT DockSplitHandle : public QWidget
{
    Q_OBJECT
    /// Orientation of the split: Qt::Horizontal is a vertical bar moving along x.
    Q_PROPERTY(Qt::Orientation orientation READ orientation)
    Q_PROPERTY(bool hovered READ isHovered)
    Q_PROPERTY(bool pressed READ isPressed)

public:
    explicit DockSplitHandle(DockAreaWidget *area);

    /// How wide the handle is to the pointer at least, across its bar.
    static constexpr int MinimumGrabExtent = 7;

    /// `index` is this handle's position in the area's solved layout, `bar`
    /// the space the layout gave it (in the area's coordinates).
    void configure(int index, Qt::Orientation orientation, const QRect &bar);
    /// Moves the handle to another bar without making it another handle.
    void place(const QRect &bar);
    /// The drawn bar, in the area's coordinates. geometry() may be larger.
    [[nodiscard]] QRect barGeometry() const { return m_bar; }
    /// What is drawn right now: the bar, or the wider strip it lights up as
    /// while hovered or dragged (DockTheme::splitHandleHoverWidth).
    [[nodiscard]] QRect drawnGeometry() const;
    [[nodiscard]] int index() const { return m_index; }
    [[nodiscard]] Qt::Orientation orientation() const { return m_orientation; }
    [[nodiscard]] bool isHovered() const { return m_hovered; }
    [[nodiscard]] bool isPressed() const { return m_pressed; }
    /// Hover state is set by the area so that linked handles light up together.
    void setHovered(bool hovered);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void finish(bool cancel);
    void restyle();
    void updateMask();

    DockAreaWidget *m_area;
    int m_index = -1;
    Qt::Orientation m_orientation = Qt::Horizontal;
    bool m_hovered = false;
    bool m_pressed = false;
    QPoint m_pressPos;
    QRect m_bar;
    QRect m_litBar;
};

/// The edge that collapsible panels went to when they were closed (see
/// DockPanel::setCollapsible()). Dragging inwards from it pulls them out
/// again; from there on it is a drag of the handle beside them, which pushed
/// far enough back puts them away once more.
///
/// It shows nothing until it is pointed at. Style sheets: class selector
/// `QFlexDock--DockEdgeHandle` with the `edge` ("left", "right", "top",
/// "bottom"), `hovered` and `pressed` properties; a `background` is drawn
/// only while it is hovered or held, as wide as a hovered split handle.
class QFLEXDOCK_EXPORT DockEdgeHandle : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString edge READ edgeName)
    Q_PROPERTY(bool hovered READ isHovered)
    Q_PROPERTY(bool pressed READ isPressed)

public:
    /// How far into the dock area it reaches for the pointer.
    static constexpr int GrabExtent = 8;

    explicit DockEdgeHandle(DockAreaWidget *area);

    /// `bar` is the strip along the edge, in the area's coordinates; `side`
    /// tells which edge of what is there it lies along.
    void configure(const QStringList &panels, DockArea side, const QRect &bar);
    [[nodiscard]] QStringList panels() const { return m_panels; }
    [[nodiscard]] DockArea side() const { return m_side; }
    [[nodiscard]] QString edgeName() const;
    [[nodiscard]] QRect barGeometry() const { return m_bar; }
    [[nodiscard]] bool isHovered() const { return m_hovered; }
    [[nodiscard]] bool isPressed() const { return m_pressed; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void finish(bool cancel);
    void restyle();
    [[nodiscard]] QRect inwards(const QRect &bar, int thickness) const;

    DockAreaWidget *m_area;
    QStringList m_panels;
    DockArea m_side = DockArea::Left;
    QRect m_bar;
    bool m_hovered = false;
    bool m_pressed = false;
    /// The panels are out, and the drag has become one of their handle.
    bool m_pulled = false;
    QPoint m_pressPos;
};

/// The spot where a boundary between columns meets one between rows. Taking
/// hold of it moves both at once, each along its own axis. Like the handles
/// it carries no layout logic and reports to its DockAreaWidget.
///
/// It paints nothing: the bars meeting under it light up instead (their
/// `hovered` property), and the pointer turns into the four-way resize cursor.
class QFLEXDOCK_EXPORT DockSplitCorner : public QWidget
{
    Q_OBJECT

public:
    /// How wide and high the corner is to the pointer at least. A little more
    /// than a handle, so that it can be found along the bars.
    static constexpr int MinimumGrabExtent = 11;

    explicit DockSplitCorner(DockAreaWidget *area);

    /// `index` is this corner's position in the area's list of corners,
    /// `patch` the place where the bars meet (in the area's coordinates).
    void configure(int index, const QRect &patch);
    [[nodiscard]] int index() const { return m_index; }
    /// Where the bars meet, in the area's coordinates. geometry() is larger.
    [[nodiscard]] QRect patchGeometry() const { return m_patch; }
    [[nodiscard]] bool isPressed() const { return m_pressed; }

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void finish(bool cancel);

    DockAreaWidget *m_area;
    int m_index = -1;
    bool m_pressed = false;
    QPoint m_pressPos;
    QRect m_patch;
};

} // namespace QFlexDock
