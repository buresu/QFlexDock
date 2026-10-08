// SPDX-License-Identifier: MIT
//
// A QML panel next to widget panels. The QML drives the dock through
// QmlDockController and can be docked, floated and moved like any other panel.

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockWorkspace.h>
#include <QFlexDockQuick/QmlDockController.h>
#include <QFlexDockQuick/QmlPanelAdapter.h>

#include "../common/ExamplePanels.h"

#include <QtQml/QQmlEngine>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>

using namespace QFlexDock;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // The engine outlives the manager, which owns the QQuickWidgets using it.
    QQmlEngine engine;
    DockManager manager;
    QmlDockController controller(&manager);
    controller.installInto(&engine); // QML sees it as `dock`

    QMainWindow window;
    DockWorkspace *workspace = manager.createWorkspace(QStringLiteral("main"));
    window.setCentralWidget(workspace);

    manager.registerPanel(QStringLiteral("scene"), ExamplePanels::makeViewport(QStringLiteral("Scene")),
                          QStringLiteral("Scene"));
    manager.registerPanel(QStringLiteral("console"), ExamplePanels::makeLog(),
                          QStringLiteral("Console"));
    QmlPanelAdapter::registerPanel(&manager, QStringLiteral("qml"), &engine,
                                   QUrl(QStringLiteral("qrc:/Panel.qml")), QStringLiteral("QML Panel"));

    workspace->addPanel(QStringLiteral("scene"));
    workspace->addPanel(QStringLiteral("qml"), DockArea::Right, 0.4);
    workspace->addPanel(QStringLiteral("console"), DockArea::Bottom, 0.25);

    window.resize(1000, 650);
    window.show();
    return app.exec();
}
