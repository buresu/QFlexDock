// SPDX-License-Identifier: MIT
#include <QFlexDockQuick/QmlPanelAdapter.h>

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>

#include <QtCore/QEvent>
#include <QtCore/QLoggingCategory>
#include <QtCore/QPointer>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlError>
#include <QtQuickWidgets/QQuickWidget>

namespace QFlexDock {

namespace {

// The QQuickWidget of a panel, which its own scene may send to another
// window: a button in it that floats the panel, docks it or closes it.
//
// A QQuickWidget rendering with the GPU answers a new top-level window by
// making itself a new scene window (QQuickWindow) and destroying the one it
// had. With a click still being handed around in that one, this pulls the
// ground from under the scene. So the widget hears of its new window only
// once the scene has let go of the event.
class PanelQuickWidget : public QQuickWidget
{
public:
    using QQuickWidget::QQuickWidget;

protected:
    bool event(QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::WindowAboutToChangeInternal || type == QEvent::WindowChangeInternal) {
            if (m_handling > 0) {
                m_windowChanged = true;
                return true;
            }
            return QQuickWidget::event(event);
        }

        // (The scene may also unregister the panel, and this goes with it.)
        const QPointer<PanelQuickWidget> self(this);
        ++m_handling;
        const bool result = QQuickWidget::event(event);
        if (!self)
            return result;
        if (--m_handling == 0 && m_windowChanged) {
            m_windowChanged = false;
            for (QEvent::Type later : {QEvent::WindowAboutToChangeInternal,
                                       QEvent::WindowChangeInternal}) {
                QEvent change(later);
                QQuickWidget::event(&change);
            }
        }
        return result;
    }

private:
    int m_handling = 0;
    bool m_windowChanged = false;
};

} // namespace

QQuickWidget *QmlPanelAdapter::createWidget(QQmlEngine *engine, const QUrl &source, QWidget *parent)
{
    auto *widget = new PanelQuickWidget(engine, parent);
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
