// SPDX-License-Identifier: MIT
//
// One window, five panels: docking by drag and drop, linked splitters, floating,
// auto-hide, maximize, undo/redo, presets and saving the layout to a file.

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include "../common/ExamplePanels.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtGui/QStyleHints>
#include <QtWidgets/QApplication>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>

using namespace QFlexDock;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QFlexDock Basic Example"));

    // Declared before the window so that it outlives the workspace. (The other
    // order works too: the manager then simply takes its panels with it.)
    DockManager manager;

    QMainWindow window;
    DockWorkspace *workspace = manager.createWorkspace(QStringLiteral("main"));
    window.setCentralWidget(workspace);

    manager.registerPanel(QStringLiteral("scene"), ExamplePanels::makeViewport(QStringLiteral("Scene")),
                          QStringLiteral("Scene"));
    manager.registerPanel(QStringLiteral("game"), ExamplePanels::makeViewport(QStringLiteral("Game")),
                          QStringLiteral("Game"));
    manager.registerPanel(QStringLiteral("hierarchy"), ExamplePanels::makeTree(),
                          QStringLiteral("Hierarchy"));
    manager.registerPanel(QStringLiteral("inspector"), ExamplePanels::makeList(8),
                          QStringLiteral("Inspector"));
    manager.registerPanel(QStringLiteral("assets"), ExamplePanels::makeList(30),
                          QStringLiteral("Assets"));
    manager.registerPanel(QStringLiteral("console"), ExamplePanels::makeLog(),
                          QStringLiteral("Console"));

    // The scene view is the heart of this application: it cannot be closed.
    manager.panel(QStringLiteral("scene"))
        ->setFeatures(AllDockFeatures & ~DockFeatures(DockFeature::Closable));

    workspace->addPanel(QStringLiteral("scene"), DockArea::Center);
    workspace->addPanel(QStringLiteral("hierarchy"), DockArea::Left, 0.2);
    workspace->addPanel(QStringLiteral("inspector"), DockArea::Right, 0.25);
    manager.movePanel(QStringLiteral("game"), QStringLiteral("scene"), DockArea::Center);
    manager.movePanel(QStringLiteral("console"), QStringLiteral("scene"), DockArea::Bottom, -1, 0.3);
    manager.movePanel(QStringLiteral("assets"), QStringLiteral("hierarchy"), DockArea::Bottom);
    manager.activatePanel(QStringLiteral("scene"));
    manager.saveDefaultLayout();
    manager.clearUndoHistory();

    const QString layoutFile =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/basic-layout.json");
    const auto report = [&window](const DockResult &result, const QString &what) {
        window.statusBar()->showMessage(result ? what : what + QStringLiteral(" failed: ")
                                                     + result.message(), 4000);
    };

    QMenu *viewMenu = window.menuBar()->addMenu(QStringLiteral("&View"));
    for (DockPanel *panel : manager.panels()) {
        QAction *action = viewMenu->addAction(panel->title());
        action->setCheckable(true);
        action->setChecked(panel->isOpen());
        QObject::connect(action, &QAction::triggered, panel, &DockPanel::toggle);
        QObject::connect(panel, &DockPanel::openChanged, action, &QAction::setChecked);
    }

    QMenu *layoutMenu = window.menuBar()->addMenu(QStringLiteral("&Layout"));
    QAction *undo = layoutMenu->addAction(QStringLiteral("Undo Layout Change"),
                                          QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z),
                                          [&] { report(manager.undo(), QStringLiteral("Undo")); });
    QAction *redo = layoutMenu->addAction(QStringLiteral("Redo Layout Change"),
                                          QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Y),
                                          [&] { report(manager.redo(), QStringLiteral("Redo")); });
    const auto updateUndo = [&] {
        undo->setEnabled(manager.canUndo());
        redo->setEnabled(manager.canRedo());
    };
    QObject::connect(&manager, &DockManager::undoStateChanged, &window, updateUndo);
    updateUndo();
    layoutMenu->addSeparator();
    layoutMenu->addAction(QStringLiteral("Reset Layout"),
                          [&] { report(manager.resetLayout(), QStringLiteral("Reset")); });
    layoutMenu->addAction(QStringLiteral("Save Layout"), [&] {
        QDir().mkpath(QFileInfo(layoutFile).absolutePath());
        report(manager.saveLayout(layoutFile), QStringLiteral("Saved to ") + layoutFile);
    });
    layoutMenu->addAction(QStringLiteral("Load Layout"), [&] {
        DockRestoreReport details;
        const DockResult result = manager.loadLayout(layoutFile, &details);
        report(result, QStringLiteral("Loaded (%1 warnings)").arg(details.warnings.size()));
    });
    layoutMenu->addSeparator();
    QMenu *presetMenu = layoutMenu->addMenu(QStringLiteral("Presets"));
    const auto rebuildPresets = [&] {
        presetMenu->clear();
        presetMenu->addAction(QStringLiteral("Save Current As..."), [&] {
            const QString name = QInputDialog::getText(&window, QStringLiteral("Save Preset"),
                                                       QStringLiteral("Name:"));
            if (!name.isEmpty())
                report(manager.savePreset(name), QStringLiteral("Preset saved"));
        });
        presetMenu->addSeparator();
        for (const QString &name : manager.presetNames()) {
            presetMenu->addAction(name, [&, name] {
                report(manager.applyPreset(name), QStringLiteral("Preset applied"));
            });
        }
    };
    QObject::connect(&manager, &DockManager::presetsChanged, &window, rebuildPresets);
    rebuildPresets();
    layoutMenu->addSeparator();
    QAction *nativeFrame = layoutMenu->addAction(QStringLiteral("Native Frame for Floating Windows"));
    nativeFrame->setCheckable(true);
    QObject::connect(nativeFrame, &QAction::toggled, &manager, [&manager](bool native) {
        // Applies to floating windows created from now on.
        manager.setFloatingWindowFrame(native ? DockManager::FloatingFrame::Native
                                              : DockManager::FloatingFrame::Custom);
    });
    QAction *tools = layoutMenu->addAction(QStringLiteral("Floating Windows Stay Above This One"));
    tools->setCheckable(true);
    QObject::connect(tools, &QAction::toggled, &manager, [&manager](bool above) {
        // Tool windows, that is. Also for floating windows created from now on.
        manager.setFloatingWindowType(above ? DockManager::FloatingWindowType::Tool
                                            : DockManager::FloatingWindowType::Window);
    });
    QAction *linked = layoutMenu->addAction(QStringLiteral("Linked Splitters"));
    linked->setCheckable(true);
    linked->setChecked(manager.linkedSplittersEnabled());
    QObject::connect(linked, &QAction::toggled, &manager, &DockManager::setLinkedSplittersEnabled);

    // The dock UI is drawn with the application's style and palette, so
    // switching those is all a theme change takes.
    QMenu *themeMenu = window.menuBar()->addMenu(QStringLiteral("&Theme"));
    themeMenu->addAction(QStringLiteral("Light"), [] {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    });
    themeMenu->addAction(QStringLiteral("Dark"), [] {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    });
    themeMenu->addAction(QStringLiteral("Follow System"), [] {
        QGuiApplication::styleHints()->unsetColorScheme();
    });

    QObject::connect(&manager, &DockManager::activePanelChanged, &window, [&](DockPanel *panel) {
        window.statusBar()->showMessage(panel ? QStringLiteral("Active: ") + panel->title()
                                              : QString());
    });

    window.resize(1100, 700);
    window.show();
    const int result = app.exec();
    // The window goes first, and the manager reports the layout changes that
    // brings. By then there is no status bar left to show them in.
    QObject::disconnect(&manager, nullptr, &window, nullptr);
    return result;
}
