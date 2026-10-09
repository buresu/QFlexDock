// SPDX-License-Identifier: MIT
#pragma once

#include "core/DockManager_p.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtGui/QPixmap>

#include <optional>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

namespace QFlexDock {

class DockTabGroup;

/// Runs dock drags: owns the one DragSession, puts its token into the
/// QMimeData, and commits or abandons the drop once the drag is over.
///
/// The layout is never touched while a drag is in flight. A dock area that
/// accepts the drop only registers the target here; the change is committed as
/// one transaction after QDrag::exec() has returned, so no window or tab bar
/// involved in the drag is rebuilt underneath the platform's drag loop.
///
/// The session exists only in this process and is looked up by token, so a
/// foreign or stale QMimeData naming a panel id cannot move anything.
///
/// Carrying a window along with the drag. On Wayland a client can neither
/// move a window to the pointer nor learn where the pointer is over other
/// windows, but the compositor can move a window as part of a drag and drop
/// operation (xdg-toplevel-drag-v1). Qt's Wayland platform does that when the
/// drag's mime data names the window in two formats of its own, the mechanism
/// behind QDockWidget (present since Qt 6.6, but not a documented API). It is
/// used here in two ways:
///  - a tab drag carries a "ghost", a window picturing what is dragged at its
///    actual size, so the tab group seems to come off in one piece; dropped
///    outside every dock area, the ghost becomes the floating window;
///  - a custom-framed floating window dragged by its title row is carried
///    itself, and can thereby be dropped onto a dock area.
/// With such a drag Qt reports a drop that nobody took as accepted, and only a
/// cancelled drag as ignored, which is what tells the two apart on Wayland.
///
/// On Windows nothing carries a window, but a client can move one to the
/// pointer, and does (movesCarriedWindows()): the ghost of a tab drag, or the
/// floating window whose whole content is dragged, is kept at the pointer
/// from here for as long as the drag lasts. The pointer goes through it, so
/// the drag still finds the dock area underneath: through a ghost always,
/// through a window that is itself moved only while another window with a
/// dock area is at the pointer. Over anything else that window is in the
/// drag's way on purpose, and no other application gets to see the drag.
class QFLEXDOCK_EXPORT DockDragController : public QObject
{
    Q_OBJECT

public:
    explicit DockDragController(DockManagerPrivate *manager);

    [[nodiscard]] static QString mimeType();

    // --- Entry points for the UI ---------------------------------------------
    // Both start the drag from the event loop rather than from the caller, so
    // the requesting widget is not on the call stack during the drag.
    void requestPanelDrag(const PanelId &panel, const QPixmap &pixmap);
    void requestGroupDrag(const PanelId &anyPanelOfGroup, const QPixmap &pixmap);
    /// A floating window dragged by its (custom) title row, held at `grip`
    /// (window coordinates). False if the platform cannot carry a window with
    /// a drag, or would have to move the window itself where the window
    /// system does that better; the caller then lets it.
    bool requestWindowDrag(const QString &containerId, const QPoint &grip);

    // --- Session lifecycle (driven directly by tests) ------------------------
    /// Starts a session for one panel or for its whole tab group. Null if the
    /// panel is not in a tab group or a session is already active.
    const DragSession *begin(const PanelId &panel, bool wholeGroup);
    /// Starts a session for everything in a floating window.
    const DragSession *beginContainer(const QString &containerId);
    /// Mime data naming the active session; the caller owns it. With
    /// `carried`, also the formats that make Qt's Wayland platform move that
    /// window along with the drag, held at `grip`.
    [[nodiscard]] QMimeData *createMimeData(QWindow *carried = nullptr,
                                            const QPoint &grip = {}) const;
    /// Whether drags carry a window along (see the class description).
    [[nodiscard]] bool carriesWindows() const;
    /// Whether the window a drag carries is moved from here, the window
    /// system not doing it (Windows).
    [[nodiscard]] bool movesCarriedWindows() const;
    /// Whether a window with a dock area of this manager's, other than
    /// `except`, is at `globalPos`: a window of a workspace or a floating
    /// window. Whatever else may lie on top of it there is not looked at.
    [[nodiscard]] bool dockWindowAt(const QPoint &globalPos, const QWidget *except) const;
    /// The floating window whose whole content the active session drags, if
    /// that is what it does (the only tab of a floating window, or its only
    /// tab group). Such a drag needs no ghost: the window is what moves.
    [[nodiscard]] DockFloatingWindow *windowDraggedWhole() const;
    /// The ghost for the active session, shown and ready to be carried. Null
    /// if the dragged panels may not float.
    [[nodiscard]] DockFloatingWindow *createGhost();
    /// Where on the ghost the pointer holds it (window coordinates).
    [[nodiscard]] QPoint ghostGrip() const { return m_ghostGrip; }
    /// What happens once QDrag::exec() has returned `action`: commit the drop,
    /// float what was dropped outside (into `ghost`, if there is one), or
    /// leave everything as it was. `windowDrag`: an existing floating window
    /// was carried. `ghostMoved`: the ghost was kept at the pointer from here
    /// (movesCarriedWindows()); the action then says no more than it does
    /// without a ghost, and the ghost, which the pointer goes through, makes
    /// way for a window proper where it is. Separate from the drag itself so
    /// tests can drive it.
    void finish(Qt::DropAction action, DockFloatingWindow *ghost, bool windowDrag,
                bool ghostMoved = false);
    /// Has the dragged tab shown as gone from its group, where tab drags are
    /// previewed (done by the drag itself, once it has its pictures of the
    /// group; tests may).
    void showSourcePreview();
    /// Names the window the drag carries (done by the drag itself; tests may).
    void setCarriedWindow(QWidget *window);
    /// A drag event at `pos` reached `receiver`. Returns true if the receiver
    /// is part of the carried window, which must then not act as a drop
    /// target, and takes note of where the pointer is on that window.
    ///
    /// A window that really is carried moves with the pointer, so the pointer
    /// stays at the same spot on it. (It may well be offered the drag: a
    /// compositor announces a drag to the window it starts on, and that is
    /// the carried window when a floating window is dragged by its own tab.)
    /// Only when the pointer travels across the window is it evidently not
    /// being carried; carrying is then given up for good.
    bool noteDragOver(QWidget *receiver, const QPoint &pos);
    /// Carrying windows was found not to work with this compositor.
    [[nodiscard]] bool carryingUnsupported() const { return m_carryingUnsupported; }
    [[nodiscard]] const DragSession *session() const;
    /// The active session if `mimeData` names it, else null.
    [[nodiscard]] const DragSession *sessionFor(const QMimeData *mimeData) const;
    [[nodiscard]] bool isActive() const { return m_session.has_value(); }

    /// A dock area accepted a drop. Commits at once, or when the running
    /// QDrag has finished.
    DockResult drop(const DropTarget &target);
    /// Ends the session, committing a deferred drop. Returns its outcome
    /// (success when there was nothing to commit).
    DockResult end();
    /// Ends the session without changing anything.
    void cancel();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void run(const QPixmap &pixmap, DockFloatingWindow *carriedWindow = nullptr,
             const QPoint &grip = {});
    [[nodiscard]] static QPixmap pictureOf(DockTabGroup *group, const DragSession &session);

    DockManagerPrivate *m_manager;
    std::optional<DragSession> m_session;
    std::optional<DropTarget> m_pendingDrop;
    bool m_running = false;
    bool m_escapePressed = false;
    bool m_carryingUnsupported = false;
    /// Where the pointer was when the drag was asked for.
    std::optional<QPoint> m_dragStart;
    QPointer<QWidget> m_carried;
    std::optional<QPoint> m_carriedPointer;
    QPoint m_ghostGrip;
};

} // namespace QFlexDock
