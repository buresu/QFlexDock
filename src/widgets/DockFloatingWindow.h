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
/// The title row of the Custom frame is only there for what no header in the
/// window stands for. A window holding one tab group leaves it to the header
/// of that group, which then is the title: it names the panel, if there is
/// one, or holds the tabs, and has the buttons of the window.
///
/// How wide that frame is and how round its corners are is up to the theme
/// (DockTheme::floatingBorderWidth, floatingCornerRadius).
///
/// Style sheets: class selector `QFlexDock--DockFloatingWindow`, with the
/// `customFrame`, `maximized` and `owner` properties; with the custom frame also
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
    /// The id of the workspace the window belongs to, for style sheets that
    /// tell the windows of one workspace from those of another.
    Q_PROPERTY(QString owner READ owner)

public:
    DockFloatingWindow(DockManagerPrivate *manager, const QString &containerId,
                       DockManager::FloatingFrame frame);

    [[nodiscard]] QString containerId() const { return m_containerId; }
    [[nodiscard]] DockAreaWidget *area() const { return m_area; }
    [[nodiscard]] bool hasCustomFrame() const { return m_customFrame; }
    [[nodiscard]] QString owner() const { return m_owner; }
    /// A custom frame without a title row (FloatingFrame::Minimal).
    [[nodiscard]] bool hasMinimalFrame() const { return m_customFrame && !m_titleBar; }
    [[nodiscard]] QWidget *titleBar() const { return m_titleBar; }
    /// Whether the title row of the Custom frame is there at the moment.
    [[nodiscard]] bool hasTitleRow() const { return m_titleRow; }
    /// The header of the window's one tab group is its title (Custom frame).
    [[nodiscard]] bool headerIsTitle() const { return m_titleBar && !m_titleRow && !m_ghost; }
    /// No title row to move the window by: the headers of its groups do
    /// that (Minimal frame, and Custom while a header is the title).
    [[nodiscard]] bool isMovedByHeaders() const { return m_customFrame && !m_titleRow; }
    /// What the frame drawn here takes around the dock area: its border and
    /// the title row, if that is there.
    [[nodiscard]] QMargins customFrameMargins() const;
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
    /// Gives a window that has come to hold an iconified column and nothing
    /// else the size of that column's strip of buttons (and of the tab group
    /// that is out beside it), and one that no longer does the size it had
    /// before. Called once the layout is applied, and when a group comes out.
    void fitIconified();

    // --- Drag ghost ------------------------------------------------------------
    /// Turns the (not yet presented) window into the ghost of a drag: the
    /// picture and nothing around it, so that the pointer holds what is
    /// dragged where it took it. The frame comes when the ghost becomes the
    /// real window. `bare`: that goes for the window system's frame too.
    void beginGhost(const QPixmap &picture, const QString &title, bool bare = false);
    [[nodiscard]] bool isGhost() const { return m_ghost; }
    /// The ghost becomes the real window of container `containerId`, of
    /// `size`: what it pictured, and the frame that now comes around it.
    void adoptAs(const QString &containerId, const QSize &size);
    /// For a window that is kept at the pointer during a drag by moving it
    /// (DockDragController::movesCarriedWindows()): the pointer goes through
    /// it, so that the drag finds what is underneath, and so does a little
    /// of the drop guide there. A window not shown yet stays that way, and
    /// above other windows, for good (a ghost); one that is shown can be
    /// turned back, and is whenever nothing it could be docked in is
    /// underneath.
    void setCarriedAlong(bool carried);
    /// For a ghost that is kept at the pointer that way: whether it takes
    /// the drop itself, the pointer not going through it for the time. It
    /// then stands for everywhere that is not a dock area, and tells the
    /// drag controller of a drop there (DockDragController::noteDropOnGhost()).
    void setTakesDrops(bool takes);
    [[nodiscard]] bool takesDrops() const { return m_takesDrops; }

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
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reportGeometry();
    void settlePlainGeometry();
    [[nodiscard]] bool isPlain() const;
    void ghostDragMoved();
    void updateFrameMargins();
    [[nodiscard]] bool wantsTitleRow(const LayoutTree &tree) const;
    bool showTitleRow(bool shown);

    DockManagerPrivate *m_manager;
    QString m_containerId;
    QString m_owner;
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
    bool m_bareGhost = false;
    /// The size a ghost is to have as the window it became, for as long as
    /// the window system may still say otherwise, and how often it did.
    QSize m_sizeToKeep;
    int m_sizeKept = 0;
    bool m_carriedAlong = false;
    bool m_takesDrops = false;
    /// The window holds an iconified column and nothing else, and what size
    /// it had before it did.
    bool m_iconified = false;
    QSize m_expandedSize;
    /// The size last asked for as such a window.
    QSize m_fitted;

    // Custom frame only.
    QWidget *m_titleBar = nullptr;
    bool m_titleRow = false;
    QLabel *m_titleIcon = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QPoint m_titlePress;
    bool m_titlePressed = false;

    QLabel *m_ghostPicture = nullptr;
};

} // namespace QFlexDock
