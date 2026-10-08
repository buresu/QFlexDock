// SPDX-License-Identifier: MIT
#pragma once

#include "core/DockManager_p.h"

#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
QT_END_NAMESPACE

namespace QFlexDock {

class DockAreaWidget;

/// A top-level window holding a layout tree of its own, so panels torn off
/// together can still be split and tabbed.
///
/// Created and destroyed by the manager to mirror the floating containers of
/// the layout state; closing it closes its panels (they stay registered).
///
/// Its frame is either the platform's (DockManager::FloatingFrame::Native) or
/// drawn here (Custom): a frameless window with a title row of its own and
/// resizable edges. Moving and resizing are still done by the window system
/// (QWindow::startSystemMove() / startSystemResize()).
///
/// Style sheets: class selector `QFlexDock--DockFloatingWindow`; with the
/// custom frame also `#dockFloatingTitleBar`, `#dockFloatingTitle`,
/// `#dockFloatingMaximizeButton` and `#dockFloatingCloseButton`.
///
/// A window can start out as the "ghost" of a drag: it shows a picture of what
/// is being dragged and follows the pointer. If the drag ends outside every
/// dock area, that very window becomes the floating window (so it is already
/// where the user dropped it); otherwise it is discarded.
class QFLEXDOCK_EXPORT DockFloatingWindow : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool customFrame READ hasCustomFrame CONSTANT)

public:
    DockFloatingWindow(DockManagerPrivate *manager, const QString &containerId,
                       DockManager::FloatingFrame frame);

    [[nodiscard]] QString containerId() const { return m_containerId; }
    [[nodiscard]] DockAreaWidget *area() const { return m_area; }
    [[nodiscard]] bool hasCustomFrame() const { return m_customFrame; }
    [[nodiscard]] QWidget *titleBar() const { return m_titleBar; }
    [[nodiscard]] QToolButton *closeButton() const { return m_closeButton; }
    [[nodiscard]] QToolButton *maximizeButton() const { return m_maximizeButton; }
    void detachFromManager();

    void setLayoutState(const ContainerState &container);
    /// Shows the window for the first time, at `geometry` if that is valid,
    /// stacked above the window of its owner workspace.
    void present(const QRect &geometry, QWidget *ownerWindow);
    void updateTitle();
    void refreshAppearance();

    // --- Drag ghost ------------------------------------------------------------
    /// Turns the (not yet presented) window into the ghost of a drag.
    void beginGhost(const QPixmap &picture, const QString &title);
    [[nodiscard]] bool isGhost() const { return m_ghost; }
    /// The ghost becomes the real window of container `containerId`.
    void adoptAs(const QString &containerId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reportGeometry();
    void ghostDragMoved();
    void updateFrameMargins();
    void toggleMaximized();
    [[nodiscard]] Qt::Edges edgesAt(const QPoint &pos) const;

    DockManagerPrivate *m_manager;
    QString m_containerId;
    DockAreaWidget *m_area;
    bool m_customFrame;
    bool m_presented = false;
    bool m_ghost = false;

    // Custom frame only.
    QWidget *m_titleBar = nullptr;
    QLabel *m_titleIcon = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QPoint m_titlePress;
    bool m_titlePressed = false;

    QLabel *m_ghostPicture = nullptr;
};

} // namespace QFlexDock
