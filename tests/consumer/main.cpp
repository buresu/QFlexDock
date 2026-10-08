// SPDX-License-Identifier: MIT
#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>
#include <QFlexDock/NativeWindowAdapter.h>

#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QFlexDock::DockManager manager;
    QMainWindow window;
    QFlexDock::DockWorkspace *workspace = manager.createWorkspace();
    window.setCentralWidget(workspace);
    manager.registerPanel(QStringLiteral("a"), new QLabel(QStringLiteral("A")));
    manager.registerPanel(QStringLiteral("b"), new QLabel(QStringLiteral("B")));
    const bool ok = workspace->addPanel(QStringLiteral("a"))
        && workspace->addPanel(QStringLiteral("b"), QFlexDock::DockArea::Right)
        && manager.restoreLayout(manager.saveLayout());
    return ok && QFlexDock::versionString() == QStringLiteral("0.1.0") ? 0 : 1;
}
