// SPDX-License-Identifier: MIT
#include <QFlexDock/NativeWindowAdapter.h>

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>

#include <QtGui/QGuiApplication>
#include <QtGui/QPlatformSurfaceEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QWidget>

namespace QFlexDock {

NativeWindowAdapter::NativeWindowAdapter(QWindow *window)
    : m_window(window)
{
    Q_ASSERT(window);
    m_container = QWidget::createWindowContainer(window);
    m_container->setObjectName(QStringLiteral("dockNativeWindowContainer"));
    // A native window has no size hint; do not let a panel collapse to nothing.
    m_container->setMinimumSize(32, 32);
    setParent(m_container);

    window->installEventFilter(this);
    connect(window, &QWindow::screenChanged, this, &NativeWindowAdapter::screenChanged);
    m_exposed = window->isExposed();

    // Clicks into a native window never reach the widgets around it. Follow
    // its input focus instead, so that working in it makes its panel the
    // active one like it does for widget content.
    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow *focus) {
        if (focus && focus == m_window && m_panel && !m_panel->isActive())
            m_panel->activate();
    });
}

NativeWindowAdapter::~NativeWindowAdapter() = default;

NativeWindowAdapter *NativeWindowAdapter::registerPanel(DockManager *manager, const PanelId &id,
                                                        QWindow *window, const QString &title)
{
    if (!manager || !window || manager->hasPanel(id) || id.isEmpty()) {
        // Let the manager produce the proper error without us having wrapped
        // (and thereby taken over) the window.
        if (manager)
            (void)manager->registerPanel(id, nullptr, title);
        return nullptr;
    }
    auto *adapter = new NativeWindowAdapter(window);
    DockPanel *panel = manager->registerPanel(id, adapter->container(), title);
    Q_ASSERT(panel);
    panel->setHidesContentDuringDrag(true);
    adapter->m_panel = panel;
    return adapter;
}

QWindow *NativeWindowAdapter::window() const
{
    return m_window;
}

QWidget *NativeWindowAdapter::container() const
{
    return m_container;
}

DockPanel *NativeWindowAdapter::panel() const
{
    return m_panel;
}

bool NativeWindowAdapter::hasSurface() const
{
    return m_window && m_window->handle();
}

bool NativeWindowAdapter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window) {
        if (event->type() == QEvent::PlatformSurface) {
            const auto type = static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType();
            if (type == QPlatformSurfaceEvent::SurfaceCreated)
                Q_EMIT surfaceCreated();
            else
                Q_EMIT surfaceAboutToBeDestroyed();
        } else if (event->type() == QEvent::Expose) {
            const bool exposed = m_window->isExposed();
            if (exposed != m_exposed) {
                m_exposed = exposed;
                Q_EMIT exposedChanged(exposed);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace QFlexDock
