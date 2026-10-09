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

namespace QFlexDock {

namespace {

// Width of the resizable border of a custom-framed window.
constexpr int FrameWidth = 4;

} // namespace

DockFloatingWindow::DockFloatingWindow(DockManagerPrivate *manager, const QString &containerId,
                                       DockManager::FloatingFrame frame)
    : QWidget(nullptr, frame == DockManager::FloatingFrame::Native
                           ? Qt::Window : Qt::Window | Qt::FramelessWindowHint)
    , m_manager(manager)
    , m_containerId(containerId)
    , m_customFrame(frame != DockManager::FloatingFrame::Native)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (frame == DockManager::FloatingFrame::Custom) {
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
    }
    if (m_customFrame) {
        setMouseTracking(true); // for the resize cursors along the border
        updateFrameMargins();
    }

    m_area = new DockAreaWidget(manager, containerId, this);
    layout->addWidget(m_area, 1);
    refreshAppearance();
}

void DockFloatingWindow::detachFromManager()
{
    m_manager = nullptr;
    m_area->detachFromManager();
}

void DockFloatingWindow::setLayoutState(const ContainerState &container)
{
    m_area->setLayoutState(container);
    updateTitle();
}

void DockFloatingWindow::updateTitle()
{
    if (!m_manager || m_ghost)
        return;
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
    // Keeps the window above its owner. Positioning is up to the platform:
    // Wayland compositors place top-level windows themselves.
    if (ownerWindow && ownerWindow != this) {
        winId();
        if (QWindow *owner = ownerWindow->windowHandle())
            windowHandle()->setTransientParent(owner);
    }
    m_presented = true;
    show();
}

// --- Drag ghost --------------------------------------------------------------

void DockFloatingWindow::beginGhost(const QPixmap &picture, const QString &title)
{
    m_ghost = true;
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
    // Lets the ghost notice if the compositor does not carry it along with the
    // drag after all (see dragEnterEvent()).
    setAcceptDrops(true);
}

void DockFloatingWindow::adoptAs(const QString &containerId)
{
    m_containerId = containerId;
    m_area->setContainerId(containerId);
    m_ghost = false;
    setAcceptDrops(false);
    delete m_ghostPicture;
    m_ghostPicture = nullptr;
    m_area->show();
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
    if (m_ghost && m_manager)
        (void)m_manager->drag->noteDragOver(this, event->position().toPoint());
    ghostDragMoved();
    event->ignore();
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
    // Background (and whatever a style sheet says about this class).
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    if (m_customFrame && !isMaximized() && !testAttribute(Qt::WA_StyleSheetTarget)) {
        // No window system frame: a hairline keeps the window apart from
        // what is behind it.
        painter.setPen(palette().color(QPalette::Mid));
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
    reportGeometry();
}

void DockFloatingWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::WindowStateChange:
        updateFrameMargins();
        refreshAppearance();
        break;
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
        refreshAppearance();
        break;
    default:
        break;
    }
}

void DockFloatingWindow::reportGeometry()
{
    if (m_manager && m_presented && !m_ghost && isVisible() && !isMaximized())
        m_manager->floatingGeometryChanged(m_containerId, geometry());
}

// --- Custom frame ------------------------------------------------------------

void DockFloatingWindow::updateFrameMargins()
{
    if (!m_customFrame)
        return;
    const int margin = isMaximized() ? 0 : FrameWidth;
    layout()->setContentsMargins(margin, margin, margin, margin);
}

void DockFloatingWindow::toggleMaximized()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

Qt::Edges DockFloatingWindow::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    if (!m_customFrame || isMaximized())
        return edges;
    // Corners reach a little further than the border itself.
    const int corner = FrameWidth * 3;
    const bool nearLeft = pos.x() < corner;
    const bool nearRight = pos.x() >= width() - corner;
    const bool nearTop = pos.y() < corner;
    const bool nearBottom = pos.y() >= height() - corner;
    if (pos.x() < FrameWidth || (nearLeft && (pos.y() < FrameWidth || pos.y() >= height() - FrameWidth)))
        edges |= Qt::LeftEdge;
    if (pos.x() >= width() - FrameWidth
        || (nearRight && (pos.y() < FrameWidth || pos.y() >= height() - FrameWidth)))
        edges |= Qt::RightEdge;
    if (pos.y() < FrameWidth || (nearTop && (pos.x() < FrameWidth || pos.x() >= width() - FrameWidth)))
        edges |= Qt::TopEdge;
    if (pos.y() >= height() - FrameWidth
        || (nearBottom && (pos.x() < FrameWidth || pos.x() >= width() - FrameWidth)))
        edges |= Qt::BottomEdge;
    return edges;
}

void DockFloatingWindow::mousePressEvent(QMouseEvent *event)
{
    const Qt::Edges edges = edgesAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && edges && windowHandle()) {
        windowHandle()->startSystemResize(edges);
        return;
    }
    QWidget::mousePressEvent(event);
}

void DockFloatingWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_customFrame && event->buttons() == Qt::NoButton) {
        const Qt::Edges edges = edgesAt(event->position().toPoint());
        if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
            setCursor(Qt::SizeFDiagCursor);
        else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
            setCursor(Qt::SizeBDiagCursor);
        else if (edges & (Qt::LeftEdge | Qt::RightEdge))
            setCursor(Qt::SizeHorCursor);
        else if (edges & (Qt::TopEdge | Qt::BottomEdge))
            setCursor(Qt::SizeVerCursor);
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
