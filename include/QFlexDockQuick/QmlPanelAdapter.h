// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDockQuick/QuickGlobal.h>

#include <QtCore/QUrl>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QQuickWidget;
class QWidget;
QT_END_NAMESPACE

namespace QFlexDock {

class DockManager;
class DockPanel;

/// Shows QML content in dock panels by way of QQuickWidget.
///
/// QQuickWidget renders the scene into an offscreen surface that is then
/// composited with the surrounding widgets. That is what lets a QML panel be
/// overlapped by the drop guide, clipped and stacked like any widget, at the
/// cost of an extra copy per frame compared to a QQuickWindow. Moving a panel
/// to another top-level window keeps the QQuickWidget and its QML object tree;
/// only the scene graph's GPU resources are rebuilt by Qt for the new window.
/// See docs/platform-notes.md for the trade-offs and for when a native
/// QQuickWindow (through NativeWindowAdapter) is the better choice.
class QFLEXDOCKQUICK_EXPORT QmlPanelAdapter
{
public:
    /// A QQuickWidget showing `source`, its root item resized with the widget.
    /// All panels created with the same `engine` share its context, including
    /// a QmlDockController installed into it.
    [[nodiscard]] static QQuickWidget *createWidget(QQmlEngine *engine, const QUrl &source,
                                                    QWidget *parent = nullptr);

    /// Creates the widget and registers it as panel `id`. The manager owns the
    /// widget; the engine stays the caller's and must outlive the panel.
    /// Returns nullptr if the panel cannot be registered.
    static DockPanel *registerPanel(DockManager *manager, const PanelId &id, QQmlEngine *engine,
                                    const QUrl &source, const QString &title = {});

    /// Same, but the QML is loaded only when the panel is first shown.
    static DockPanel *registerLazyPanel(DockManager *manager, const PanelId &id,
                                        QQmlEngine *engine, const QUrl &source,
                                        const QString &title = {});

    /// The QQuickWidget of a panel made by this adapter, or nullptr (also for
    /// a lazy panel that has not been shown yet).
    [[nodiscard]] static QQuickWidget *quickWidget(const DockPanel *panel);
};

} // namespace QFlexDock
