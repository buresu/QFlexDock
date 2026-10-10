// SPDX-License-Identifier: MIT
#include <QFlexDock/DockManager.h>
#include <QFlexDockQuick/QmlDockController.h>
#include <QFlexDockQuick/QmlPanelAdapter.h>

#include <QtQml/QQmlEngine>
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QQmlEngine engine;
    QFlexDock::DockManager manager;
    QFlexDock::QmlDockController controller(&manager);
    controller.installInto(&engine);
    return controller.panelIds().isEmpty() ? 0 : 1;
}
