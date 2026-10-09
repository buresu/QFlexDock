// SPDX-License-Identifier: MIT
#pragma once

#include "core/DockManager_p.h"

#include <QtCore/QElapsedTimer>
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
/// drawn here: a frameless window with resizable edges and (Custom) a title
/// row of its own, or (Minimal) none, in which case the headers of the tab
/// groups inside are what moves it. Moving and resizing are still done by the
/// window system (QWindow::startSystemMove() / startSystemResize()).
///
/// How wide that frame is and how round its corners are is up to the theme
/// (DockTheme::floatingBorderWidth, floatingCornerRadius).
///
/// Style sheets: class selector `QFlexDock--DockFloatingWindow`, with the
/// `customFrame` and `maximized` properties; with the custom frame also
/// `#dockFloatingTitleBar`, `#dockFloatingTitle`,
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
    /// A custom frame without a title row (FloatingFrame::Minimal).
    [[nodiscard]] bool hasMinimalFrame() const { return m_customFrame && !m_titleBar; }
    [[nodiscard]] QWidget *titleBar() const { return m_titleBar; }
    [[nodiscard]] QToolButton *closeButton() const { return m_closeButton; }
    [[nodiscard]] QToolButton *maximizeButton() const { return m_maximizeButton; }
    /// Width of the border the custom frame puts around the content.
    [[nodiscard]] int borderWidth() const { return m_borderWidth; }
    [[nodiscard]] int cornerRadius() const { return m_cornerRadius; }
    /// The edges the window would be resized by from `pos`; none inside.
    [[nodiscard]] Qt::Edges resizeEdgesAt(const QPoint &pos) const;
    /// Has the window system resize the window by `edges`.
    bool startResize(Qt::Edges edges);
    [[nodiscard]] static Qt::CursorShape resizeCursor(Qt::Edges edges);
    /// The strips over the content by which a window with a thin border is
    /// resized; none if the border is wide enough itself.
    [[nodiscard]] QList<QWidget *> resizeGrips() const { return m_grips; }
    void detachFromManager();

    void setLayoutState(const ContainerState &container);
    /// Shows the window for the first time, at `geometry` if that is valid,
    /// stacked above the window of its owner workspace.
    void present(const QRect &geometry, QWidget *ownerWindow);
    void updateTitle();
    void refreshAppearance();
    void toggleMaximized();

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
    void settlePlainGeometry();
    [[nodiscard]] bool isPlain() const;
    void ghostDragMoved();
    void updateFrameMargins();

    DockManagerPrivate *m_manager;
    QString m_containerId;
    DockAreaWidget *m_area;
    bool m_customFrame;
    int m_borderWidth = 0;
    int m_cornerRadius = 0;
    QList<QWidget *> m_grips;
    /// The geometries last reported to the manager, oldest first, with when.
    struct Reported
    {
        qint64 at = 0;
        QRect geometry;
    };
    QList<Reported> m_reported;
    QElapsedTimer m_clock;
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
