// SPDX-License-Identifier: MIT
//
// Several main windows sharing one DockManager: any panel can be dragged from
// one window into another, and it is the same widget that moves.

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include "../common/ExamplePanels.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>

using namespace QFlexDock;

namespace {

// A main window with a workspace of its own. Closing it destroys the window
// and its workspace; the panels in it are closed, not destroyed, and can be
// shown again from any other window's View menu.
QMainWindow *createWindow(DockManager &manager, const QString &workspaceId)
{
    auto *window = new QMainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QStringLiteral("QFlexDock - ") + workspaceId);
    DockWorkspace *workspace = manager.createWorkspace(workspaceId);
    window->setCentralWidget(workspace);

    QMenu *viewMenu = window->menuBar()->addMenu(QStringLiteral("&View"));
    for (DockPanel *panel : manager.panels()) {
        QAction *action = viewMenu->addAction(panel->title());
        action->setCheckable(true);
        action->setChecked(panel->isOpen());
        // Show it here if it is closed, close it if it is open anywhere.
        QObject::connect(action, &QAction::triggered, workspace, [&manager, panel, workspace] {
            if (panel->isOpen())
                manager.hidePanel(panel->id());
            else
                manager.movePanel(panel->id(), workspace, DockArea::Center);
        });
        QObject::connect(panel, &DockPanel::openChanged, action, &QAction::setChecked);
    }

    QMenu *windowMenu = window->menuBar()->addMenu(QStringLiteral("&Window"));
    windowMenu->addAction(QStringLiteral("New Window"), [&manager] {
        static int counter = 2;
        QMainWindow *another = createWindow(manager, QStringLiteral("window-%1").arg(++counter));
        another->resize(700, 500);
        another->show();
    });

    QObject::connect(&manager, &DockManager::panelWindowChanged, window,
                     [window](DockPanel *panel, QWidget *topLevel) {
                         if (topLevel == window)
                             window->statusBar()->showMessage(
                                 panel->title() + QStringLiteral(" moved into this window"), 3000);
                     });
    return window;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    DockManager manager;

    const char *names[] = {"Scene", "Hierarchy", "Inspector", "Assets", "Console", "Timeline"};
    for (const char *name : names) {
        const QString title = QString::fromLatin1(name);
        QWidget *content = title == QLatin1String("Scene") ? ExamplePanels::makeViewport(title)
                         : title == QLatin1String("Console") ? ExamplePanels::makeLog()
                         : title == QLatin1String("Hierarchy") ? ExamplePanels::makeTree()
                         : ExamplePanels::makeList(12);
        manager.registerPanel(title.toLower(), content, title);
    }

    QMainWindow *first = createWindow(manager, QStringLiteral("window-1"));
    QMainWindow *second = createWindow(manager, QStringLiteral("window-2"));

    DockWorkspace *a = manager.workspace(QStringLiteral("window-1"));
    DockWorkspace *b = manager.workspace(QStringLiteral("window-2"));
    a->addPanel(QStringLiteral("scene"));
    a->addPanel(QStringLiteral("hierarchy"), DockArea::Left, 0.25);
    a->addPanel(QStringLiteral("console"), DockArea::Bottom, 0.25);
    b->addPanel(QStringLiteral("inspector"));
    b->addPanel(QStringLiteral("assets"), DockArea::Bottom);
    manager.movePanel(QStringLiteral("timeline"), QStringLiteral("assets"), DockArea::Center);

    first->resize(900, 600);
    second->resize(500, 600);
    first->show();
    second->show();
    return app.exec();
}
