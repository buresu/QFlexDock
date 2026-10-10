// SPDX-License-Identifier: MIT
#include "widgets/DockFloatingWindow.h"

#include "core/DockDragController.h"
#include "widgets/DockAreaWidget.h"

#include <QtGui/QCloseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleOption>
#include <QtWidgets/QToolButton>

#include <utility>

namespace QFlexDock {

namespace {

// How long before a window is known to be maximized its geometry may have
// begun to change towards that already, in milliseconds.
constexpr qint64 StateChangeTime = 250;

// Width of the border of a custom-framed window unless the theme says.
constexpr int DefaultBorderWidth = 4;
// How far in from its edge such a window can be taken hold of to resize it.
constexpr int GripReach = 4;
// How much of a window shows while it is moved along with a drag.
constexpr qreal CarriedOpacity = 0.75;
// How long after a ghost became a window the window system may still resize
// it for reasons of its own (to put its frame around it, or in answer to
// what it knew of the ghost), in milliseconds, and how often that is undone.
constexpr qint64 AdoptionTime = 500;
constexpr int AdoptionResizes = 3;

// A strip along one edge of a window whose border is too thin to be grabbed.
// It lies over the content there and paints nothing.
class WindowGrip : public QWidget
{
public:
    explicit WindowGrip(DockFloatingWindow *window)
        : QWidget(window)
        , m_window(window)
    {
        setMouseTracking(true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !m_window->startResize(edgesAt(event)))
            event->ignore();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() == Qt::NoButton)
            setCursor(DockFloatingWindow::resizeCursor(edgesAt(event)));
    }

private:
    [[nodiscard]] Qt::Edges edgesAt(const QMouseEvent *event) const
    {
        return m_window->resizeEdgesAt(mapTo(m_window, event->position().toPoint()));
    }

    DockFloatingWindow *m_window;
};

} // namespace

DockFloatingWindow::DockFloatingWindow(DockManagerPrivate *manager, const QString &containerId,
                                       DockManager::FloatingWindowFrame frame)
    : QWidget(nullptr, frame == DockManager::FloatingWindowFrame::Native
                           ? Qt::Window : Qt::Window | Qt::FramelessWindowHint)
    , m_manager(manager)
    , m_containerId(containerId)
    , m_customFrame(frame != DockManager::FloatingWindowFrame::Native)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (frame == DockManager::FloatingWindowFrame::Custom) {
        m_titleBar = new QWidget(this);
        m_titleBar->setObjectName(QStringLiteral("dockFloatingTitleBar"));
        m_titleBar->setAttribute(Qt::WA_StyledBackground);
        m_titleBar->installEventFilter(this);
        m_titleIcon = new QLabel(m_titleBar);
        m_titleLabel = new QLabel(m_titleBar);
        m_titleLabel->setObjectName(QStringLiteral("dockFloatingTitle"));
        // Presses on the labels are meant for the title bar.
        m_titleIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        const auto makeButton = [this](const char *name, const QString &tip) {
            auto *button = new QToolButton(m_titleBar);
            button->setObjectName(QLatin1String(name));
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
            button->setToolTip(tip);
            button->setAccessibleName(tip);
            return button;
        };
        m_maximizeButton = makeButton("dockFloatingMaximizeButton", tr("Maximize"));
        m_closeButton = makeButton("dockFloatingCloseButton", tr("Close"));
        connect(m_maximizeButton, &QToolButton::clicked, this, &DockFloatingWindow::toggleMaximized);
        connect(m_closeButton, &QToolButton::clicked, this, &QWidget::close);

        auto *titleLayout = new QHBoxLayout(m_titleBar);
        titleLayout->setContentsMargins(8, 3, 3, 3);
        titleLayout->setSpacing(6);
        titleLayout->addWidget(m_titleIcon);
        titleLayout->addWidget(m_titleLabel, 1);
        titleLayout->addWidget(m_maximizeButton);
        titleLayout->addWidget(m_closeButton);
        layout->addWidget(m_titleBar);
        m_titleBar->hide(); // until the window holds what needs it
    }
    m_area = new DockAreaWidget(manager, containerId, this);
    layout->addWidget(m_area, 1);

    if (m_customFrame) {
        const DockTheme &theme = manager->theme;
        m_borderWidth = theme.floatingBorderWidth >= 0 ? theme.floatingBorderWidth
                                                       : DefaultBorderWidth;
        m_cornerRadius = qMax(0, theme.floatingCornerRadius);
        // What lies outside round corners has to be see-through, and that is
        // settled before the window exists.
        if (m_cornerRadius > 0)
            setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true); // for the resize cursors along the border
        if (m_borderWidth < GripReach) {
            for (int i = 0; i < 4; ++i)
                m_grips.append(new WindowGrip(this));
        }
        updateFrameMargins();
    }
    refreshAppearance();
}

void DockFloatingWindow::detachFromManager()
{
    m_manager = nullptr;
    m_area->detachFromManager();
}

void DockFloatingWindow::setLayoutState(const ContainerState &container)
{
    if (m_owner != container.owner) {
        m_owner = container.owner;
        style()->unpolish(this);
        style()->polish(this);
    }
    // Before the groups hear of it: their headers go by the title row.
    showTitleRow(wantsTitleRow(container.tree));
    m_area->setLayoutState(container);
    updateTitle();
}

// A window that holds nothing but an iconified column is as large as the
// strip of buttons that column is, and what is out beside the strip; shown
// again, the column has the size the window had before.
void DockFloatingWindow::fitIconified()
{
    const QSize strip = m_area->iconifiedSizeHint();
    const bool iconified = strip.isValid();
    if (m_ghost || (!iconified && !m_iconified) || (iconified && strip == m_fitted))
        return;
    const bool was = std::exchange(m_iconified, iconified);
    m_fitted = strip;
    if (!m_presented || !isPlain())
        return;
    const QMargins frame = layout()->contentsMargins();
    const QSize around(frame.left() + frame.right(), frame.top() + frame.bottom());
    // The least the window may be has changed with what it holds, and has
    // to be known before the window is made that small.
    layout()->invalidate();
    layout()->activate();
    if (iconified) {
        // (Also when a tab group comes out beside the strip, or goes back.)
        if (!was)
            m_expandedSize = size();
        resize(strip + around);
    } else {
        resize(m_expandedSize.isValid() ? m_expandedSize
                                        : sizeHint().expandedTo(QSize(320, 260)));
    }
}

// The title row is for what no header in the window stands for: more than
// one tab group, or a panel that does without a header.
bool DockFloatingWindow::wantsTitleRow(const LayoutTree &tree) const
{
    if (!m_titleBar || m_ghost || !m_manager)
        return false;
    const std::vector<const LayoutNode *> groups = tree.tabNodes();
    if (groups.size() != 1)
        return groups.size() > 1;
    const QStringList &inside = groups.front()->panels;
    const DockPanel *only = inside.size() == 1 ? m_manager->panels.value(inside.constFirst())
                                               : nullptr;
    return only && !only->isHeaderVisible();
}

// True if that changed something.
bool DockFloatingWindow::showTitleRow(bool shown)
{
    if (!m_titleBar || shown == m_titleRow)
        return false;
    m_titleRow = shown;
    m_titleBar->setVisible(shown);
    return true;
}

QMargins DockFloatingWindow::customFrameMargins() const
{
    QMargins frame = layout()->contentsMargins();
    if (m_titleRow)
        frame.setTop(frame.top() + m_titleBar->sizeHint().height());
    return frame;
}

void DockFloatingWindow::updateTitle()
{
    if (!m_manager || m_ghost)
        return;
    if (showTitleRow(wantsTitleRow(m_area->tree())))
        m_area->refreshAppearance(); // the headers go by it
    // Named after the current panel of its first tab group.
    const std::vector<const LayoutNode *> groups = m_area->tree().tabNodes();
    const DockPanel *panel = groups.empty() ? nullptr : m_manager->panels.value(groups.front()->active);
    setWindowTitle(panel ? panel->title() : QString());
    if (panel)
        setWindowIcon(panel->icon());
    if (m_titleLabel) {
        m_titleLabel->setText(windowTitle());
        const int extent = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
        const QIcon icon = panel ? panel->icon() : QIcon();
        m_titleIcon->setPixmap(icon.pixmap(extent, extent));
        m_titleIcon->setVisible(!icon.isNull());
    }
}

void DockFloatingWindow::refreshAppearance()
{
    if (!m_titleBar || !m_manager)
        return;
    m_closeButton->setIcon(m_manager->icon(DockIcon::Close, this));
    m_maximizeButton->setIcon(
        m_manager->icon(isMaximized() ? DockIcon::Restore : DockIcon::Maximize, this));
    const QString tip = isMaximized() ? tr("Restore") : tr("Maximize");
    m_maximizeButton->setToolTip(tip);
    m_maximizeButton->setAccessibleName(tip);
}

void DockFloatingWindow::present(const QRect &geometry, QWidget *ownerWindow)
{
    if (geometry.isValid())
        setGeometry(geometry);
    else
        resize(sizeHint().expandedTo(QSize(360, 260)));
    // An iconified column comes as its strip of buttons, whatever the size
    // of what it was taken out of.
    if (const QSize strip = m_area->iconifiedSizeHint(); strip.isValid() && !m_ghost) {
        const QMargins frame = layout()->contentsMargins();
        m_iconified = true;
        m_fitted = strip;
        resize(strip + QSize(frame.left() + frame.right(), frame.top() + frame.bottom()));
    }
    // Keeps the window above its owner. Positioning is up to the platform:
    // Wayland compositors place top-level windows themselves.
    if (ownerWindow && ownerWindow != this) {
        // What kind of window this is has to be settled before there is one.
        if (m_manager && m_manager->floatingWindowType == DockManager::FloatingWindowType::Tool
            && !testAttribute(Qt::WA_WState_Created)) {
            setWindowFlags(windowFlags() | Qt::Tool);
        }
        winId();
        if (QWindow *owner = ownerWindow->windowHandle())
            windowHandle()->setTransientParent(owner);
    }
    m_presented = true;
    show();
    // Where the plain window is, to begin with.
    m_clock.start();
    m_reported = {Reported{0, this->geometry()}};
}

// --- Drag ghost --------------------------------------------------------------

void DockFloatingWindow::beginGhost(const QPixmap &picture, const QString &title, bool bare)
{
    m_ghost = true;
    if (bare && !m_customFrame) {
        m_bareGhost = true;
        setWindowFlag(Qt::FramelessWindowHint);
    }
    showTitleRow(false);
    setWindowTitle(title);
    if (m_titleLabel) {
        m_titleLabel->setText(title);
        m_titleIcon->hide();
    }
    m_ghostPicture = new QLabel(this);
    m_ghostPicture->setPixmap(picture);
    m_ghostPicture->setScaledContents(true);
    m_ghostPicture->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    static_cast<QVBoxLayout *>(layout())->addWidget(m_ghostPicture, 1);
    m_area->hide();
    updateFrameMargins(); // a ghost has no border, and is not resized
    // Lets the ghost notice if the compositor does not carry it along with the
    // drag after all (see dragEnterEvent()).
    setAcceptDrops(true);
}

void DockFloatingWindow::adoptAs(const QString &containerId, const QSize &size)
{
    m_containerId = containerId;
    m_area->setContainerId(containerId);
    m_ghost = false;
    setAcceptDrops(false);
    delete m_ghostPicture;
    m_ghostPicture = nullptr;
    m_area->show();
    updateFrameMargins();
    if (m_bareGhost) {
        m_bareGhost = false;
        // The frame comes to the window as it is (see setCarriedAlong()); the
        // widget is only told.
        overrideWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
        if (QWindow *handle = windowHandle())
            handle->setFlag(Qt::FramelessWindowHint, false);
    }
    // What was torn off keeps its size, also if the window system takes the
    // room for its frame from the window: see resizeEvent().
    m_sizeToKeep = size;
    m_sizeKept = 0;
    m_clock.start();
    m_reported = {Reported{0, this->geometry()}};
}

void DockFloatingWindow::setCarriedAlong(bool carried)
{
    if (carried == m_carriedAlong)
        return;
    m_carriedAlong = carried;
    if (QWindow *handle = windowHandle()) {
        // On the window as it is: new flags for the widget would have it
        // make another one, in the middle of a drag.
        handle->setFlag(Qt::WindowTransparentForInput, carried);
    } else if (carried) {
        setWindowFlags(windowFlags() | Qt::WindowTransparentForInput | Qt::WindowStaysOnTopHint
                       | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    setWindowOpacity(carried ? CarriedOpacity : 1.0);
}

void DockFloatingWindow::setTakesDrops(bool takes)
{
    if (takes == m_takesDrops || !m_ghost)
        return;
    m_takesDrops = takes;
    // On the window as it is, as in setCarriedAlong().
    if (QWindow *handle = windowHandle(); handle && m_carriedAlong)
        handle->setFlag(Qt::WindowTransparentForInput, !takes);
}

void DockFloatingWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // Only a ghost takes drops itself (its dock area is hidden). It accepts
    // the drag just to keep hearing about it: see ghostDragMoved().
    if (m_ghost && m_manager && m_manager->drag->noteDragOver(this, event->position().toPoint())) {
        event->accept();
        ghostDragMoved();
    } else {
        event->ignore();
    }
}

void DockFloatingWindow::dragMoveEvent(QDragMoveEvent *event)
{
    const bool carried =
        m_ghost && m_manager && m_manager->drag->noteDragOver(this, event->position().toPoint());
    ghostDragMoved();
    if (carried && m_takesDrops)
        event->acceptProposedAction();
    else
        event->ignore();
}

void DockFloatingWindow::dropEvent(QDropEvent *event)
{
    if (m_ghost && m_takesDrops && m_manager && m_manager->drag->sessionFor(event->mimeData())) {
        m_manager->drag->noteDropOnGhost();
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

// A ghost the pointer travels across is not being carried by the compositor;
// it is merely a window lying in the way, and gets out of it.
void DockFloatingWindow::ghostDragMoved()
{
    if (m_ghost && m_manager && m_manager->drag->carryingUnsupported())
        hide();
}

// --- Events ------------------------------------------------------------------

void DockFloatingWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const qreal radius = isMaximized() || isFullScreen() ? 0 : m_cornerRadius;
    painter.setRenderHint(QPainter::Antialiasing, radius > 0);
    if (testAttribute(Qt::WA_TranslucentBackground)) {
        // Nobody fills in behind a translucent window. Kept a pixel inside
        // the outline, so that none of it shows around what is drawn next.
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().window());
        if (radius > 0)
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius - 1, radius - 1);
        else
            painter.drawRect(rect());
    }
    // Whatever a style sheet says about this class.
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    if (m_customFrame && !isMaximized() && !testAttribute(Qt::WA_StyleSheetTarget)) {
        // No window system frame: a hairline keeps the window apart from
        // what is behind it.
        painter.setPen(palette().color(QPalette::Mid));
        painter.setBrush(Qt::NoBrush);
        if (radius > 0)
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        else
            painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void DockFloatingWindow::closeEvent(QCloseEvent *event)
{
    // Closing the window closes its panels; the manager then removes the
    // window. Refused if a panel inside may not be closed.
    if (m_manager && !m_ghost && !m_manager->closeFloatingByUser(m_containerId))
        event->ignore();
    else
        event->accept();
}

void DockFloatingWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    reportGeometry();
}

void DockFloatingWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateFrameMargins(); // the grips follow the edges
    if (m_sizeToKeep.isValid()) {
        if (m_clock.elapsed() >= AdoptionTime || !isPlain() || m_sizeKept >= AdoptionResizes) {
            m_sizeToKeep = QSize();
        } else if (event->size() != m_sizeToKeep) {
            ++m_sizeKept;
            QMetaObject::invokeMethod(this, [this, size = m_sizeToKeep] { resize(size); },
                                      Qt::QueuedConnection);
            return;
        }
    }
    reportGeometry();
}

void DockFloatingWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::WindowStateChange:
        if (!isPlain())
            settlePlainGeometry();
        updateFrameMargins();
        refreshAppearance();
        if (headerIsTitle())
            m_area->refreshAppearance(); // its maximize button is the window's
        // For style sheet rules that go by the `maximized` property.
        style()->unpolish(this);
        style()->polish(this);
        update();
        break;
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
        refreshAppearance();
        break;
    default:
        break;
    }
}

bool DockFloatingWindow::isPlain() const
{
    return !(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized));
}

// The state remembers where the window is as a plain window; maximized or
// the like, it is somewhere else and the state keeps what it has.
void DockFloatingWindow::reportGeometry()
{
    if (!m_manager || !m_presented || m_ghost || !isVisible() || !isPlain())
        return;
    m_reported.append({m_clock.elapsed(), geometry()});
    if (m_reported.size() > 8)
        m_reported.removeFirst();
    m_manager->floatingGeometryChanged(m_containerId, geometry());
}

// The window has just been maximized (or the like). Window systems differ
// in what they tell first, the new size or the new state: what was reported
// on the way here may be the geometry of the new state already. The plain
// window is where it was before all that.
void DockFloatingWindow::settlePlainGeometry()
{
    if (!m_manager || m_reported.isEmpty())
        return;
    const qint64 now = m_clock.elapsed();
    Reported plain = m_reported.constFirst();
    for (const Reported &reported : std::as_const(m_reported)) {
        if (now - reported.at >= StateChangeTime)
            plain = reported;
    }
    m_reported = {plain};
    m_manager->floatingGeometryChanged(m_containerId, plain.geometry);
}

// --- Custom frame ------------------------------------------------------------

void DockFloatingWindow::updateFrameMargins()
{
    if (!m_customFrame)
        return;
    const bool framed = !isMaximized() && !isFullScreen() && !m_ghost;
    const int margin = framed ? m_borderWidth : 0;
    if (layout()->contentsMargins() != QMargins(margin, margin, margin, margin))
        layout()->setContentsMargins(margin, margin, margin, margin);
    if (m_grips.isEmpty())
        return;
    const QRect edges[4] = {
        QRect(0, 0, GripReach, height()),
        QRect(width() - GripReach, 0, GripReach, height()),
        QRect(0, 0, width(), GripReach),
        QRect(0, height() - GripReach, width(), GripReach),
    };
    for (int i = 0; i < 4; ++i) {
        QWidget *grip = m_grips.at(i);
        grip->setGeometry(edges[i]);
        grip->setVisible(framed);
        grip->raise();
    }
}

void DockFloatingWindow::toggleMaximized()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

Qt::Edges DockFloatingWindow::resizeEdgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    if (!m_customFrame || isMaximized() || isFullScreen())
        return edges;
    const int reach = qMax(m_borderWidth, GripReach);
    // Corners reach a little further than the border itself.
    const int corner = reach * 3;
    const bool nearLeft = pos.x() < corner;
    const bool nearRight = pos.x() >= width() - corner;
    const bool nearTop = pos.y() < corner;
    const bool nearBottom = pos.y() >= height() - corner;
    if (pos.x() < reach || (nearLeft && (pos.y() < reach || pos.y() >= height() - reach)))
        edges |= Qt::LeftEdge;
    if (pos.x() >= width() - reach
        || (nearRight && (pos.y() < reach || pos.y() >= height() - reach)))
        edges |= Qt::RightEdge;
    if (pos.y() < reach || (nearTop && (pos.x() < reach || pos.x() >= width() - reach)))
        edges |= Qt::TopEdge;
    if (pos.y() >= height() - reach
        || (nearBottom && (pos.x() < reach || pos.x() >= width() - reach)))
        edges |= Qt::BottomEdge;
    return edges;
}

bool DockFloatingWindow::startResize(Qt::Edges edges)
{
    return edges && windowHandle() && windowHandle()->startSystemResize(edges);
}

Qt::CursorShape DockFloatingWindow::resizeCursor(Qt::Edges edges)
{
    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
        return Qt::SizeFDiagCursor;
    if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
        return Qt::SizeBDiagCursor;
    if (edges & (Qt::LeftEdge | Qt::RightEdge))
        return Qt::SizeHorCursor;
    if (edges & (Qt::TopEdge | Qt::BottomEdge))
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

void DockFloatingWindow::mousePressEvent(QMouseEvent *event)
{
    // On the border itself. (Over the content, the grips see to it.)
    if (event->button() == Qt::LeftButton
        && startResize(resizeEdgesAt(event->position().toPoint()))) {
        return;
    }
    QWidget::mousePressEvent(event);
}

void DockFloatingWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_customFrame && event->buttons() == Qt::NoButton) {
        const Qt::Edges edges = resizeEdgesAt(event->position().toPoint());
        if (edges)
            setCursor(resizeCursor(edges));
        else
            unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

void DockFloatingWindow::leaveEvent(QEvent *event)
{
    if (m_customFrame)
        unsetCursor();
    QWidget::leaveEvent(event);
}

// The title row of the custom frame: drag to move, double click to maximize.
bool DockFloatingWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_titleBar || m_ghost)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_titlePressed = true;
            m_titlePress = mouse->position().toPoint();
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint pos = mouse->position().toPoint();
        if (m_titlePressed && mouse->buttons().testFlag(Qt::LeftButton)
            && (pos - m_titlePress).manhattanLength() >= QApplication::startDragDistance()) {
            m_titlePressed = false;
            if (isMaximized())
                break;
            // Where the platform can carry a window along with a drag, moving
            // the window is a dock drag at the same time: drop it onto a dock
            // area to dock it. Elsewhere the window system just moves it.
            const QPoint grip = m_titleBar->mapTo(this, m_titlePress);
            if (!m_manager || !m_manager->drag->requestWindowDrag(m_containerId, grip)) {
                if (windowHandle())
                    windowHandle()->startSystemMove();
            }
        }
        break;
    }
    case QEvent::MouseButtonRelease:
        m_titlePressed = false;
        break;
    case QEvent::MouseButtonDblClick:
        if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            toggleMaximized();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace QFlexDock
