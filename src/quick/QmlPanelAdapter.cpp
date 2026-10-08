// SPDX-License-Identifier: MIT
#include <QFlexDockQuick/QmlPanelAdapter.h>

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>

#include <QtCore/QLoggingCategory>
#include <QtCore/QPointer>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlError>
#include <QtQuickWidgets/QQuickWidget>

namespace QFlexDock {

QQuickWidget *QmlPanelAdapter::createWidget(QQmlEngine *engine, const QUrl &source, QWidget *parent)
{
    auto *widget = new QQuickWidget(engine, parent);
    widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // Clicking into the panel makes it the active one, as with widget content.
    widget->setFocusPolicy(Qt::StrongFocus);
    widget->setSource(source);
    if (widget->status() == QQuickWidget::Error) {
        const QList<QQmlError> errors = widget->errors();
        for (const QQmlError &error : errors)
            qWarning("QFlexDock: %s", qPrintable(error.toString()));
    }
    return widget;
}

DockPanel *QmlPanelAdapter::registerPanel(DockManager *manager, const PanelId &id,
                                          QQmlEngine *engine, const QUrl &source,
                                          const QString &title)
{
    if (!manager || !engine || id.isEmpty() || manager->hasPanel(id)) {
        // Fail through the manager, before any QML is loaded.
        if (manager)
            (void)manager->registerPanel(id, nullptr, title);
        return nullptr;
    }
    return manager->registerPanel(id, createWidget(engine, source), title);
}

DockPanel *QmlPanelAdapter::registerLazyPanel(DockManager *manager, const PanelId &id,
                                              QQmlEngine *engine, const QUrl &source,
                                              const QString &title)
{
    if (!manager || !engine)
        return nullptr;
    const QPointer<QQmlEngine> guard(engine);
    return manager->registerPanelFactory(id, [guard, source](const PanelId &) -> QWidget * {
        return guard ? createWidget(guard, source) : nullptr;
    }, title);
}

QQuickWidget *QmlPanelAdapter::quickWidget(const DockPanel *panel)
{
    return panel ? qobject_cast<QQuickWidget *>(panel->widget()) : nullptr;
}

} // namespace QFlexDock
