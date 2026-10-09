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

bool onWayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
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
    QMetaObject::invokeMethod(this, [this, panel, pixmap] {
        if (begin(panel, false))
            run(pixmap);
    }, Qt::QueuedConnection);
}

void DockDragController::requestGroupDrag(const PanelId &anyPanelOfGroup, const QPixmap &pixmap)
{
    QMetaObject::invokeMethod(this, [this, anyPanelOfGroup, pixmap] {
        if (begin(anyPanelOfGroup, true))
            run(pixmap);
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
    m_manager->setDragInProgress(true);
    return &*m_session;
}

bool DockDragController::requestWindowDrag(const QString &containerId, const QPoint &grip)
{
    DockFloatingWindow *window = m_manager->floatingWindows.value(containerId);
    if (!carriesWindows() || !window || m_session)
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
    return m_manager->dragGhostEnabled && !m_carryingUnsupported && onWayland();
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
    QWidget *owner = nullptr;
    if (DockAreaWidget *area = m_manager->areaFor(m_session->sourceContainer)) {
        owner = area->window();
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
    ghost->present(QRect(QPoint(0, 0), size), owner);
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
    // with just that tab, moved to the front.
    const QRect barRect(bar->mapTo(group, QPoint(0, 0)), bar->size());
    const QPixmap tab = bar->grab(bar->tabRect(index));
    QPainter painter(&picture);
    painter.setClipRect(barRect);
    painter.fillRect(barRect, group->palette().color(QPalette::Window));
    QWidget *title = group->titleBar();
    title->render(&painter, title->mapTo(group, QPoint(0, 0)), QRegion(),
                  QWidget::DrawWindowBackground);
    painter.drawPixmap(barRect.topLeft() + QPoint(0, bar->tabRect(index).top()), tab);
    return picture;
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
    m_manager->hideAllOverlays();
    m_manager->setDragInProgress(false);
}

void DockDragController::run(const QPixmap &pixmap, DockFloatingWindow *carriedWindow,
                             const QPoint &grip)
{
    m_running = true;
    m_escapePressed = false;
    // Installed from inside the drag's event loop, so that it comes after (and
    // therefore runs before) the filter the platform's drag loop installs,
    // which swallows the Escape key.
    QTimer::singleShot(0, this, [this] {
        if (m_running)
            qApp->installEventFilter(this);
    });

    // What travels with the pointer: an existing floating window, a ghost, or
    // (wherever windows cannot be carried) a picture of the tab.
    QPointer<DockFloatingWindow> ghost;
    QPoint hold = grip;
    if (!carriedWindow && carriesWindows()) {
        if (DockFloatingWindow *whole = windowDraggedWhole()) {
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

    // The drag belongs to the manager, not to the tab bar it started on: that
    // widget may be gone by the time the drag ends.
    QPointer<DockDragController> self(this);
    auto *drag = new QDrag(m_manager->q);
    drag->setMimeData(createMimeData(carried ? carried->windowHandle() : nullptr, hold));
    if (!carried && !pixmap.isNull()) {
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(qMin(pixmap.width() / 2, 40), qMin(pixmap.height() / 2, 12)));
    }
    const Qt::DropAction action = drag->exec(Qt::MoveAction);
    if (!self)
        return;

    qApp->removeEventFilter(this);
    m_running = false;
    setCarriedWindow(nullptr);
    finish(action, ghost, carriedWindow != nullptr);
}

void DockDragController::finish(Qt::DropAction action, DockFloatingWindow *ghost, bool windowDrag)
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
    //  - on Wayland without one, the two cannot be told apart;
    //  - elsewhere: nobody took it, no Escape, and the mouse button is up (a
    //    drag cancelled from the keyboard ends with the button still down).
    bool droppedOutside = false;
    if (ghost) {
        droppedOutside = action != Qt::IgnoreAction;
    } else if (!onWayland()) {
        droppedOutside = action == Qt::IgnoreAction && !m_escapePressed
            && !QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    }

    const DragSession session = *m_session;
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
    if (ghost) {
        // The ghost is where the user dropped it (only the compositor knows
        // where that is) and already has the right size: it simply stops
        // being a picture.
        geometry = ghost->geometry();
    } else {
        QSize size(480, 360);
        if (DockAreaWidget *area = m_manager->areaFor(session.sourceContainer)) {
            if (DockTabGroup *group = area->group(session.sourceNode))
                size = group->size();
        }
        geometry = QRect(QCursor::pos() - QPoint(40, 12), size);
    }
    cancel();
    const DockResult result = m_manager->floatPanels(session.primary, session.wholeGroup, geometry, ghost);
    if (result)
        (void)m_manager->activate(session.primary, true);
    else
        discardGhost();
}

// Notes an Escape key press during the drag, on platforms whose drag loop runs
// on Qt's event queue. Together with the mouse button state this tells a
// cancelled drag from a drop outside every window.
bool DockDragController::eventFilter(QObject *, QEvent *event)
{
    if (m_running
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_escapePressed = true;
    }
    return false;
}

} // namespace QFlexDock
