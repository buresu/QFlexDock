// SPDX-License-Identifier: MIT
#include "core/DockDragController.h"

#include "widgets/DockAreaWidget.h"
#include "widgets/DockFloatingWindow.h"
#include "widgets/DockTabBar.h"
#include "widgets/DockTabGroup.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QCursor>
#include <QtGui/QDrag>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLayout>

#ifdef Q_OS_WIN
#  include <QtCore/qt_windows.h>
#endif
#ifdef Q_OS_MACOS
#  include <CoreGraphics/CoreGraphics.h>
#endif

namespace QFlexDock {

namespace {

// The formats by which QtWidgets tells Qt's Wayland platform plugin to move a
// window along with a drag: the QWindow's address as a qintptr, and the point
// of the window the pointer holds it at, each written with QDataStream.
const QLatin1String CarriedWindowFormat("application/x-qt-mainwindowdrag-window");
const QLatin1String CarriedWindowGripFormat("application/x-qt-mainwindowdrag-position");

// Where the pointer holds a ghost when its position on the dragged group is
// not known: just inside the top left corner.
constexpr QPoint GhostGrip(18, 12);

// How far the pointer may appear to move on a window that is being carried
// before that window is taken not to be carried at all.
constexpr int CarriedPointerDrift = 48;

// How often a window that is moved along with a drag from here is put where
// the pointer is, in milliseconds.
constexpr int FollowInterval = 8;

bool onWayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

bool onWindows()
{
    return QGuiApplication::platformName() == QLatin1String("windows");
}

bool onMac()
{
    return QGuiApplication::platformName() == QLatin1String("cocoa");
}

// Whether the button a drag was held by is still down now that the drag is
// over: it is after a drag cancelled from the keyboard, not after a drop.
bool dragButtonStillDown()
{
#ifdef Q_OS_WIN
    // The drag loop of Windows keeps the mouse to itself, and Qt hears of the
    // release only once QDrag::exec() has returned: what Qt knows is the
    // button as it was before the drag. The system knows better.
    if (onWindows())
        return GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) < 0;
#endif
#ifdef Q_OS_MACOS
    // So does the drag loop of macOS, and the release Qt makes up for it
    // afterwards is still in its event queue then.
    if (onMac()) {
        return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState,
                                        kCGMouseButtonLeft);
    }
#endif
    return QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
}

// Whether the Escape key is down, where only the system can tell during a
// drag and is asked (macOS).
bool escapeKeyDown()
{
#ifdef Q_OS_MACOS
    constexpr CGKeyCode EscapeKey = 53; // kVK_Escape, which only Carbon names
    if (onMac())
        return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, EscapeKey);
#endif
    return false;
}

} // namespace

DockDragController::DockDragController(DockManagerPrivate *manager)
    : QObject(manager->q)
    , m_manager(manager)
{
}

QString DockDragController::mimeType()
{
    return QStringLiteral("application/x-qflexdock-drag-session");
}

void DockDragController::requestPanelDrag(const PanelId &panel, const QPixmap &pixmap)
{
    m_dragStart = QCursor::pos();
    QMetaObject::invokeMethod(this, [this, panel, pixmap] {
        if (begin(panel, false))
            run(pixmap);
        else
            m_dragStart.reset();
    }, Qt::QueuedConnection);
}

void DockDragController::requestGroupDrag(const PanelId &anyPanelOfGroup, const QPixmap &pixmap)
{
    m_dragStart = QCursor::pos();
    QMetaObject::invokeMethod(this, [this, anyPanelOfGroup, pixmap] {
        if (begin(anyPanelOfGroup, true))
            run(pixmap);
        else
            m_dragStart.reset();
    }, Qt::QueuedConnection);
}

const DragSession *DockDragController::begin(const PanelId &panel, bool wholeGroup)
{
    if (m_session)
        return nullptr;
    const std::optional<PanelLocation> location = m_manager->state.locate(panel);
    if (!location || location->isAutoHidden())
        return nullptr;

    DragSession session;
    session.token = QUuid::createUuid().toRfc4122();
    session.wholeGroup = wholeGroup;
    session.sourceContainer = location->container;
    session.sourceNode = location->node;
    if (wholeGroup) {
        const LayoutNode *group =
            m_manager->state.find(location->container)->tree.findNode(location->node);
        session.panels = group->panels;
        session.primary = group->active;
    } else {
        session.panels = QStringList{panel};
        session.primary = panel;
    }
    m_session = std::move(session);
    m_pendingDrop.reset();
    qApp->installEventFilter(this);
    m_manager->setDragInProgress(true);
    return &*m_session;
}

bool DockDragController::requestWindowDrag(const QString &containerId, const QPoint &grip)
{
    DockFloatingWindow *window = m_manager->floatingWindows.value(containerId);
    // (A window that would be moved from here is better moved by the window
    // system, which also snaps it to the edges of the screen.)
    if (!carriesWindows() || movesCarriedWindows() || !window || m_session)
        return false;
    const QPointer<DockFloatingWindow> guard(window);
    QMetaObject::invokeMethod(this, [this, containerId, guard, grip] {
        if (guard && beginContainer(containerId))
            run(QPixmap(), guard, grip);
    }, Qt::QueuedConnection);
    return true;
}

const DragSession *DockDragController::beginContainer(const QString &containerId)
{
    const ContainerState *container = m_manager->state.find(containerId);
    if (m_session || !container || container->kind != ContainerKind::Floating
        || !container->tree.root()) {
        return nullptr;
    }
    DragSession session;
    session.token = QUuid::createUuid().toRfc4122();
    session.wholeGroup = true;
    session.sourceContainer = containerId;
    session.sourceNode = container->tree.root()->id;
    session.sourceIsTabs = container->tree.root()->isTabs();
    session.panels = container->tree.panels();
    session.primary = container->tree.tabNodes().front()->active;
    m_session = std::move(session);
    m_pendingDrop.reset();
    qApp->installEventFilter(this);
    m_manager->setDragInProgress(true);
    return &*m_session;
}

QMimeData *DockDragController::createMimeData(QWindow *carried, const QPoint &grip) const
{
    auto *mimeData = new QMimeData;
    if (m_session)
        mimeData->setData(mimeType(), m_session->token);
    if (carried) {
        QByteArray window;
        QDataStream windowStream(&window, QIODevice::WriteOnly);
        windowStream << reinterpret_cast<qintptr>(carried);
        QByteArray position;
        QDataStream positionStream(&position, QIODevice::WriteOnly);
        positionStream << grip;
        mimeData->setData(CarriedWindowFormat, window);
        mimeData->setData(CarriedWindowGripFormat, position);
    }
    return mimeData;
}

bool DockDragController::carriesWindows() const
{
    return m_manager->dragGhostEnabled
        && ((!m_carryingUnsupported && onWayland()) || onWindows() || onMac());
}

bool DockDragController::movesCarriedWindows() const
{
    return m_manager->dragGhostEnabled && (onWindows() || onMac());
}

bool DockDragController::buttonStillDown() const
{
    return !m_letGoAt && dragButtonStillDown();
}

bool DockDragController::dockWindowAt(const QPoint &globalPos, const QWidget *except) const
{
    const auto isAt = [&](const QWidget *window) {
        return window && window != except && window->isVisible() && !window->isMinimized()
            && window->frameGeometry().contains(globalPos);
    };
    for (const DockWorkspace *workspace : std::as_const(m_manager->workspaces)) {
        if (isAt(workspace->window()))
            return true;
    }
    for (const DockFloatingWindow *window : std::as_const(m_manager->floatingWindows)) {
        if (!window->isGhost() && isAt(window))
            return true;
    }
    return false;
}

DockFloatingWindow *DockDragController::windowDraggedWhole() const
{
    if (!m_session)
        return nullptr;
    const ContainerState *container = m_manager->state.find(m_session->sourceContainer);
    if (!container || container->kind != ContainerKind::Floating
        || container->tree.panels().size() != m_session->panels.size()) {
        return nullptr;
    }
    return m_manager->floatingWindows.value(container->id);
}

DockFloatingWindow *DockDragController::createGhost()
{
    if (!m_session)
        return nullptr;
    for (const PanelId &panel : std::as_const(m_session->panels)) {
        if (!m_manager->userMay(panel, DockFeature::Floatable))
            return nullptr;
    }

    // The ghost looks like what is dragged would as a floating window, at the
    // size it has now, and is held where the pointer is on it: the tab group
    // appears to come off in one piece.
    QPixmap picture;
    QSize size(480, 360);
    m_ghostGrip = GhostGrip;
    // The window this may become belongs to what any floating window of the
    // same origin belongs to: the window of the owner workspace, if there is
    // one. Not to the window the tab is dragged out of: a child of that
    // could never get behind it, and would not count as a window of its own.
    const DockWorkspace *workspace = m_manager->workspaceFor(m_session->sourceContainer);
    QWidget *owner = workspace ? workspace->window() : nullptr;
    if (DockAreaWidget *area = m_manager->areaFor(m_session->sourceContainer)) {
        if (DockTabGroup *group = area->group(m_session->sourceNode)) {
            size = group->size();
            picture = pictureOf(group, *m_session);
            const QPoint pointer = group->mapFromGlobal(QCursor::pos());
            if (group->rect().contains(pointer))
                m_ghostGrip = pointer;
        }
    }
    const DockPanel *primary = m_manager->panels.value(m_session->primary);
    auto *ghost = new DockFloatingWindow(m_manager, QString(), m_manager->floatingFrame);
    ghost->beginGhost(picture, primary ? primary->title() : QString());
    // A custom frame comes around the picture, and shifts where it is held.
    const QMargins frame = ghost->layout()->contentsMargins();
    const int titleHeight = ghost->titleBar() ? ghost->titleBar()->sizeHint().height() : 0;
    size += QSize(frame.left() + frame.right(), frame.top() + frame.bottom() + titleHeight);
    m_ghostGrip += QPoint(frame.left(), frame.top() + titleHeight);
    // Where to is the compositor's business, or else known here.
    QPoint position(0, 0);
    if (movesCarriedWindows()) {
        ghost->setCarriedAlong(true);
        position = QCursor::pos() - m_ghostGrip;
    }
    ghost->present(QRect(position, size), owner);
    return ghost;
}

// A picture of the group as it would look holding only what is dragged.
QPixmap DockDragController::pictureOf(DockTabGroup *group, const DragSession &session)
{
    QPixmap picture = group->grab();
    DockTabBar *bar = group->tabBar();
    const int index = bar->indexOfPanel(session.primary);
    if (session.wholeGroup || bar->count() < 2 || index < 0
        || bar->parentWidget() != group->titleBar()) {
        return picture;
    }

    // One tab out of several: the other tabs stay behind. Repaint the tab bar
    // with just that tab. It stays where it is, under the pointer that holds
    // it; in the window this becomes, it will be the first.
    const QRect barRect(bar->mapTo(group, QPoint(0, 0)), bar->size());
    const QRect tabRect = bar->tabRect(index).intersected(bar->rect());
    const QPixmap tab = bar->grab(tabRect);
    QPainter painter(&picture);
    painter.setClipRect(barRect);
    painter.fillRect(barRect, group->palette().color(QPalette::Window));
    QWidget *title = group->titleBar();
    title->render(&painter, title->mapTo(group, QPoint(0, 0)), QRegion(),
                  QWidget::DrawWindowBackground);
    painter.drawPixmap(barRect.topLeft() + tabRect.topLeft(), tab);

    // What stands right behind the tabs stands behind the one that is left.
    QWidget *behind = group->actionBar(DockTitlePlace::AfterTabs);
    if (behind->isVisible() && behind->parentWidget() == title) {
        const QRect was(behind->mapTo(group, QPoint(0, 0)), behind->size());
        QPixmap buttons(behind->size() * picture.devicePixelRatio());
        buttons.setDevicePixelRatio(picture.devicePixelRatio());
        buttons.fill(Qt::transparent);
        behind->render(&buttons, QPoint(), QRegion(), QWidget::DrawChildren);
        painter.setClipRect(was);
        painter.fillRect(was, group->palette().color(QPalette::Window));
        title->render(&painter, title->mapTo(group, QPoint(0, 0)), QRegion(),
                      QWidget::DrawWindowBackground);
        painter.setClipRect(QRect(title->mapTo(group, QPoint(0, 0)), title->size()));
        painter.drawPixmap(QPoint(barRect.left() + tabRect.right() + 1, was.top()), buttons);
    }
    return picture;
}

void DockDragController::showSourcePreview()
{
    m_manager->showDraggedOut(session());
}

void DockDragController::setCarriedWindow(QWidget *window)
{
    m_carried = window;
    m_carriedPointer.reset();
}

bool DockDragController::noteDragOver(QWidget *receiver, const QPoint &pos)
{
    if (!m_carried || !receiver || receiver->window() != m_carried)
        return false;
    // A window moved from here is known to follow, if a moment behind.
    if (movesCarriedWindows())
        return true;
    const QPoint onWindow = receiver->mapTo(m_carried.data(), pos);
    if (!m_carriedPointer) {
        m_carriedPointer = onWindow;
    } else if ((onWindow - *m_carriedPointer).manhattanLength() > CarriedPointerDrift) {
        // The pointer wanders over the window: the compositor is not moving
        // it (no xdg-toplevel-drag). Do not try again; drags fall back to
        // showing a picture of the tab.
        m_carryingUnsupported = true;
    }
    return true;
}

const DragSession *DockDragController::session() const
{
    return m_session ? &*m_session : nullptr;
}

const DragSession *DockDragController::sessionFor(const QMimeData *mimeData) const
{
    if (!m_session || !mimeData || !mimeData->hasFormat(mimeType()))
        return nullptr;
    return mimeData->data(mimeType()) == m_session->token ? &*m_session : nullptr;
}

DockResult DockDragController::drop(const DropTarget &target)
{
    if (!m_session)
        return DockResult::failure(DockError::InvalidArgument, QStringLiteral("no drag in progress"));
    if (!m_manager->dropAllowed(*m_session, target)) {
        return DockResult::failure(DockError::PolicyViolation,
                                   QStringLiteral("this drop is not allowed"));
    }
    m_pendingDrop = target;
    if (m_running)
        return DockResult::success(); // committed by run() once the drag is over
    return end();
}

DockResult DockDragController::end()
{
    DockResult result;
    // The views show what they hold again before that changes.
    m_manager->hideAllOverlays();
    m_manager->showDraggedOut(nullptr);
    if (m_session && m_pendingDrop) {
        const DragSession session = *m_session;
        result = m_manager->commitDrop(session, *m_pendingDrop);
        if (result)
            (void)m_manager->activate(session.primary, true);
    }
    cancel();
    return result;
}

void DockDragController::cancel()
{
    m_session.reset();
    m_pendingDrop.reset();
    m_droppedOnGhost = false;
    qApp->removeEventFilter(this);
    m_manager->hideAllOverlays();
    m_manager->showDraggedOut(nullptr);
    m_manager->setDragInProgress(false);
}

void DockDragController::run(const QPixmap &pixmap, DockFloatingWindow *carriedWindow,
                             const QPoint &grip)
{
    m_running = true;
    m_escapePressed = false;
    m_letGoAt.reset();
    // Installed once more from inside the drag's event loop, so that it comes
    // after (and therefore runs before) the filter the platform's drag loop
    // installs, which swallows the Escape key.
    QTimer::singleShot(0, this, [this] {
        if (m_running)
            qApp->installEventFilter(this);
    });

    // What travels with the pointer: an existing floating window, a ghost, or
    // (wherever windows cannot be carried) a picture of the tab.
    QPointer<DockFloatingWindow> ghost;
    QPoint hold = grip;
    const bool moved = movesCarriedWindows();
    if (!carriedWindow && carriesWindows()) {
        DockFloatingWindow *whole = windowDraggedWhole();
        // Moved from here, it has to be a window that can be moved.
        if (whole && moved && (whole->isMaximized() || whole->isFullScreen()))
            whole = nullptr;
        if (whole) {
            // Everything in a floating window is dragged by its tab: there is
            // nothing to picture, the window itself comes along.
            carriedWindow = whole;
            const QPoint pointer = whole->mapFromGlobal(QCursor::pos());
            hold = QPoint(qBound(0, pointer.x(), whole->width() - 1),
                          qBound(0, pointer.y(), whole->height() - 1));
        } else {
            ghost = createGhost();
            hold = m_ghostGrip;
        }
    }
    DockFloatingWindow *carried = carriedWindow ? carriedWindow : ghost.data();
    setCarriedWindow(carried);
    showSourcePreview();

    // Where nobody carries the window, it is kept at the pointer from here:
    // the platform's drag loop lets timers through.
    const QPointer<DockFloatingWindow> followed(moved ? carried : nullptr);
    const QPoint origin = followed ? followed->pos() : QPoint();
    // On macOS a drag that nobody took is over for the user well before
    // exec() returns: the system first has its picture slide back to where
    // it came from. How the drag ended has to be seen when it does, and by
    // asking the system: Qt hears of neither the button nor the Escape key.
    const bool endsUnseen = onMac();
    // That wait is not there when somebody takes the drop. So the ghost
    // does, wherever no window with a dock area is under the pointer: the
    // drag returns at once, and no other application gets to see it.
    const bool ghostTakesDrops = onMac();
    QTimer follow;
    const auto keepAtPointer = [this, followed, hold, origin, endsUnseen, ghostTakesDrops,
                                isGhost = !carriedWindow] {
        if (m_letGoAt || (endsUnseen && m_escapePressed))
            return; // over already: nothing follows the pointer any more
        const QPoint pointer = QCursor::pos();
        if (endsUnseen) {
            if (escapeKeyDown()) {
                m_escapePressed = true;
                // A ghost hidden is done with (see finish()); a window that
                // was itself moved goes back where it was.
                if (followed && isGhost) {
                    followed->hide();
                } else if (followed) {
                    followed->setCarriedAlong(false);
                    followed->move(origin);
                }
                return;
            }
            if (!dragButtonStillDown())
                m_letGoAt = pointer;
        }
        if (!followed)
            return;
        const QPoint frame = followed->geometry().topLeft() - followed->pos();
        followed->move(pointer - hold - frame);
        // A ghost lets the pointer through all the time, unless it is to
        // take the drop where nothing else of ours would. A window that is
        // itself moved only does over where it could be docked: anywhere
        // else it is a window being moved, which no other application is to
        // take for something dragged over it.
        if (!isGhost)
            followed->setCarriedAlong(dockWindowAt(pointer, followed));
        else if (ghostTakesDrops)
            followed->setTakesDrops(!dockWindowAt(pointer, followed));
    };
    if (followed || endsUnseen) {
        follow.setTimerType(Qt::PreciseTimer);
        connect(&follow, &QTimer::timeout, this, keepAtPointer);
        follow.start(FollowInterval);
    }

    // The drag belongs to the manager, not to the tab bar it started on: that
    // widget may be gone by the time the drag ends.
    QPointer<DockDragController> self(this);
    auto *drag = new QDrag(m_manager->q);
    drag->setMimeData(createMimeData(carried && !moved ? carried->windowHandle() : nullptr, hold));
    if (!carried && !pixmap.isNull()) {
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(qMin(pixmap.width() / 2, 40), qMin(pixmap.height() / 2, 12)));
    } else if (carried && onMac()) {
        // A drag without a picture is given one by Qt there. The window is
        // all there is to see.
        QPixmap nothing(1, 1);
        nothing.fill(Qt::transparent);
        drag->setPixmap(nothing);
    }
    const Qt::DropAction action = drag->exec(Qt::MoveAction);
    if (!self)
        return;

    m_running = false;
    setCarriedWindow(nullptr);
    follow.stop();
    keepAtPointer(); // where it was let go of, to the pixel
    if (followed && carriedWindow) {
        carriedWindow->setCarriedAlong(false);
        if (!m_pendingDrop) {
            // Put down, it stays there; a cancelled drag takes it back.
            // Either way it is the window the user has been holding.
            if (m_escapePressed || buttonStillDown())
                carriedWindow->move(origin);
            carriedWindow->raise();
            carriedWindow->activateWindow();
        }
    }
    finish(action, ghost, carriedWindow != nullptr, moved && ghost);
    m_dragStart.reset();
    m_letGoAt.reset();
}

void DockDragController::finish(Qt::DropAction action, DockFloatingWindow *ghost, bool windowDrag,
                                bool ghostMoved)
{
    const auto discardGhost = [&ghost] {
        if (ghost) {
            ghost->hide();
            ghost->deleteLater();
            ghost = nullptr;
        }
    };
    // A ghost that hid itself was found not to be carried (see noteDragOver).
    if (ghost && !ghost->isVisible())
        discardGhost();

    if (!m_session || m_pendingDrop) {
        discardGhost();
        (void)end(); // commits the drop, if there is one
        return;
    }
    if (windowDrag) {
        // An existing floating window was carried around and put down
        // somewhere that is not a dock area: it was moved, nothing more.
        cancel();
        return;
    }

    // "Dropped outside every dock area", as opposed to cancelled:
    //  - with a carried ghost, Qt reports the unclaimed drop as accepted and
    //    only a cancelled drag as ignored;
    //  - a ghost that was moved from here may have taken the drop itself;
    //  - on Wayland without one, the two cannot be told apart;
    //  - elsewhere, also with a ghost that was moved from here: nobody took
    //    it, no Escape, and the mouse button is up (a drag cancelled from the
    //    keyboard ends with the button still down).
    bool droppedOutside = false;
    if (ghost && !ghostMoved) {
        droppedOutside = action != Qt::IgnoreAction;
    } else if (m_droppedOnGhost) {
        droppedOutside = true;
    } else if (!onWayland()) {
        droppedOutside = action == Qt::IgnoreAction && !m_escapePressed && !buttonStillDown();
    }

    const DragSession session = *m_session;
    // Where the pointer was when the drag ended, which may be a while ago.
    const QPoint pointer = m_letGoAt.value_or(QCursor::pos());
    bool floatable = m_manager->floatOnOutsideDrop;
    for (const PanelId &panel : session.panels)
        floatable = floatable && m_manager->userMay(panel, DockFeature::Floatable);
    if (!droppedOutside || !floatable) {
        discardGhost();
        cancel();
        return;
    }

    // What was dragged is a floating window's whole content already: there is
    // nothing to float. A ghost must not be left behind as a second window.
    if (ghost && windowDraggedWhole()) {
        discardGhost();
        cancel();
        return;
    }

    QRect geometry;
    // What is torn off keeps its size: the frame of its window comes on top.
    bool frameToAdd = false;
    if (ghost) {
        // The ghost is where the user dropped it (only the compositor knows
        // where that is) and already has the right size: it simply stops
        // being a picture.
        geometry = ghost->geometry();
    } else if (const DockFloatingWindow *whole = windowDraggedWhole(); whole && m_dragStart) {
        // A floating window dragged by all it holds, where nothing carries
        // it along: it goes as far as the pointer went.
        geometry = whole->geometry().translated(pointer - *m_dragStart);
    } else {
        QSize size(480, 360);
        if (DockAreaWidget *area = m_manager->areaFor(session.sourceContainer)) {
            if (DockTabGroup *group = area->group(session.sourceNode))
                size = group->size();
        }
        geometry = QRect(pointer - QPoint(40, 12), size);
        frameToAdd = true;
    }
    cancel();
    // A ghost that was moved from here is not a window to keep: the pointer
    // goes through it. The window proper comes to be where it is, first.
    DockFloatingWindow *adopted = ghostMoved ? nullptr : ghost;
    const DockResult result =
        m_manager->floatPanels(session.primary, session.wholeGroup, geometry, adopted);
    if (!result || ghostMoved)
        discardGhost();
    if (!result)
        return;
    if (frameToAdd) {
        const std::optional<PanelLocation> location = m_manager->state.locate(session.primary);
        DockFloatingWindow *window =
            location ? m_manager->floatingWindows.value(location->container) : nullptr;
        if (window && window->hasCustomFrame()) {
            const QMargins frame = window->layout()->contentsMargins();
            const int title = window->titleBar() ? window->titleBar()->sizeHint().height() : 0;
            window->resize(geometry.size() + QSize(frame.left() + frame.right(),
                                                   frame.top() + frame.bottom() + title));
        }
    }
    (void)m_manager->activate(session.primary, true);
}

// Watches the application for as long as there is a session.
bool DockDragController::eventFilter(QObject *watched, QEvent *event)
{
    // An Escape key press during the drag, on platforms whose drag loop runs
    // on Qt's event queue (on Windows the key never gets here, and on macOS
    // the system is asked instead: see run()). Together with
    // the mouse button state this tells a cancelled drag from a drop outside
    // every window.
    if (m_running
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_escapePressed = true;
    }
    // A dock drag is for dock areas. Content that takes whatever is dragged
    // over it (a QQuickWidget does, whatever its items say) would keep the
    // drag from the area it is in, and the drop guide with it. It is passed
    // over: Qt then offers the drag to the widgets around it, up to the area.
    if (event->type() == QEvent::DragEnter && watched->isWidgetType()
        && !qobject_cast<DockAreaWidget *>(watched) && !qobject_cast<DockFloatingWindow *>(watched)
        && sessionFor(static_cast<QDragEnterEvent *>(event)->mimeData())) {
        event->ignore();
        return true;
    }
    return false;
}

} // namespace QFlexDock
