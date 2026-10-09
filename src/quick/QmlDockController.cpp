// SPDX-License-Identifier: MIT
#include <QFlexDockQuick/QmlDockController.h>

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>

namespace QFlexDock {

namespace {

DockArea toDockArea(QmlDockController::Area area)
{
    switch (area) {
    case QmlDockController::Left:
        return DockArea::Left;
    case QmlDockController::Right:
        return DockArea::Right;
    case QmlDockController::Top:
        return DockArea::Top;
    case QmlDockController::Bottom:
        return DockArea::Bottom;
    case QmlDockController::Center:
        return DockArea::Center;
    }
    return DockArea::None;
}

} // namespace

QmlDockController::QmlDockController(DockManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    connect(manager, &DockManager::activePanelChanged, this, &QmlDockController::activePanelChanged);
    connect(manager, &DockManager::layoutChanged, this, &QmlDockController::layoutChanged);
    connect(manager, &DockManager::undoStateChanged, this, &QmlDockController::undoStateChanged);
    connect(manager, &DockManager::panelRegistered, this, &QmlDockController::panelsChanged);
    // Queued: the panel is still registered while the "about to" signal runs.
    connect(manager, &DockManager::panelAboutToBeUnregistered, this,
            &QmlDockController::panelsChanged, Qt::QueuedConnection);
    connect(manager, &DockManager::panelOpenChanged, this, [this](DockPanel *panel, bool open) {
        Q_EMIT panelOpenChanged(panel->id(), open);
    });
}

DockManager *QmlDockController::manager() const
{
    return m_manager;
}

void QmlDockController::installInto(QQmlEngine *engine, const QString &name)
{
    // For the Area enum; the controller itself is reached through the context.
    qmlRegisterUncreatableType<QmlDockController>(
        "QFlexDock", 1, 0, "Dock",
        QStringLiteral("Dock is provided by the application as a context property"));
    engine->rootContext()->setContextProperty(name, this);
}

QString QmlDockController::activePanel() const
{
    const DockPanel *panel = m_manager ? m_manager->activePanel() : nullptr;
    return panel ? panel->id() : QString();
}

QStringList QmlDockController::panels() const
{
    QStringList ids;
    if (m_manager) {
        for (const DockPanel *panel : m_manager->panels())
            ids << panel->id();
    }
    return ids;
}

QStringList QmlDockController::openPanels() const
{
    QStringList ids;
    if (m_manager) {
        for (const DockPanel *panel : m_manager->panels()) {
            if (panel->isOpen())
                ids << panel->id();
        }
    }
    return ids;
}

QString QmlDockController::maximizedPanel() const
{
    return m_manager ? m_manager->maximizedPanel() : QString();
}

bool QmlDockController::canUndo() const
{
    return m_manager && m_manager->canUndo();
}

bool QmlDockController::canRedo() const
{
    return m_manager && m_manager->canRedo();
}

bool QmlDockController::hasPanel(const QString &id) const
{
    return m_manager && m_manager->hasPanel(id);
}

bool QmlDockController::isPanelOpen(const QString &id) const
{
    const DockPanel *p = m_manager ? m_manager->panel(id) : nullptr;
    return p && p->isOpen();
}

QStringList QmlDockController::tabGroupPanels(const QString &id) const
{
    return m_manager ? m_manager->tabGroupPanels(id) : QStringList();
}

QObject *QmlDockController::panel(const QString &id) const
{
    DockPanel *p = m_manager ? m_manager->panel(id) : nullptr;
    if (p)
        QQmlEngine::setObjectOwnership(p, QQmlEngine::CppOwnership);
    return p;
}

bool QmlDockController::report(const DockResult &result)
{
    const QString message = result ? QString() : result.message();
    if (message != m_lastError) {
        m_lastError = message;
        Q_EMIT lastErrorChanged();
    }
    return result.ok();
}

#define QFLEXDOCK_FORWARD(call)                                                                    \
    if (!m_manager)                                                                                \
        return report(DockResult::failure(DockError::InvalidArgument,                              \
                                          QStringLiteral("the dock manager is gone")));            \
    return report(m_manager->call)

bool QmlDockController::showPanel(const QString &id)
{
    QFLEXDOCK_FORWARD(showPanel(id));
}

bool QmlDockController::hidePanel(const QString &id)
{
    QFLEXDOCK_FORWARD(hidePanel(id));
}

bool QmlDockController::showPanels(const QStringList &ids)
{
    QFLEXDOCK_FORWARD(showPanels(ids));
}

bool QmlDockController::hidePanels(const QStringList &ids)
{
    QFLEXDOCK_FORWARD(hidePanels(ids));
}

bool QmlDockController::togglePanel(const QString &id)
{
    QFLEXDOCK_FORWARD(togglePanel(id));
}

bool QmlDockController::activatePanel(const QString &id)
{
    QFLEXDOCK_FORWARD(activatePanel(id));
}

bool QmlDockController::movePanel(const QString &id, const QString &relativeTo, Area area)
{
    QFLEXDOCK_FORWARD(movePanel(id, relativeTo, toDockArea(area)));
}

bool QmlDockController::movePanelToWorkspace(const QString &id, const QString &workspaceId, Area area)
{
    QFLEXDOCK_FORWARD(movePanel(id, m_manager->workspace(workspaceId), toDockArea(area)));
}

bool QmlDockController::floatPanel(const QString &id)
{
    QFLEXDOCK_FORWARD(floatPanel(id));
}

bool QmlDockController::dockPanel(const QString &id)
{
    QFLEXDOCK_FORWARD(dockPanel(id));
}

bool QmlDockController::maximizePanel(const QString &id)
{
    QFLEXDOCK_FORWARD(maximizePanel(id));
}

bool QmlDockController::restoreMaximizedPanel()
{
    QFLEXDOCK_FORWARD(restoreMaximizedPanel());
}

bool QmlDockController::setPanelAutoHide(const QString &id, bool autoHide)
{
    QFLEXDOCK_FORWARD(setPanelAutoHide(id, autoHide));
}

bool QmlDockController::undo()
{
    QFLEXDOCK_FORWARD(undo());
}

bool QmlDockController::redo()
{
    QFLEXDOCK_FORWARD(redo());
}

bool QmlDockController::resetLayout()
{
    QFLEXDOCK_FORWARD(resetLayout());
}

#undef QFLEXDOCK_FORWARD

} // namespace QFlexDock
