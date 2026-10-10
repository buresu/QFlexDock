// SPDX-License-Identifier: MIT
//
// The window layout and look of OBS Studio, rebuilt with QFlexDock: a fixed
// preview in the middle, docks with title bars around it, tabs only where
// docks are stacked, frameless floating docks. Only the layout and the style
// are reproduced. None of the application's code or artwork is used, and
// nothing here records or streams.

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include "Panels.h"
#include "Style.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>

using namespace QFlexDock;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QFlexDock OBS-Style Example"));
    // One base style everywhere, so the style sheet has the same starting point.
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setPalette(ObsStyle::palette());
    app.setStyleSheet(ObsStyle::styleSheet());

    DockManager manager;

    // Docks have a title bar; tabs show up only where docks are stacked.
    manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
    // A floating dock is just the dock: no window title above its own.
    manager.setFloatingWindowFrame(DockManager::FloatingFrame::Minimal);
    // Every boundary is dragged on its own.
    manager.setLinkedSplittersEnabled(false);
    manager.setCornerResizeEnabled(false);

    DockTheme theme;
    theme.titleButtons = DockTitleButton::Float;
    theme.splitHandleWidth = 6; // the gap between docks
    theme.iconSize = 16;
    theme.icons.insert(DockIcon::Float, ObsStyle::icon(ObsStyle::Glyph::Float));
    theme.icons.insert(DockIcon::Dock, ObsStyle::icon(ObsStyle::Glyph::Float));
    theme.icons.insert(DockIcon::Close, ObsStyle::icon(ObsStyle::Glyph::Close));
    manager.setTheme(theme);
    manager.setOverlayPainter(std::make_shared<ObsStyle::OverlayPainter>());

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("QFlexDock - OBS-style layout"));
    DockWorkspace *workspace = manager.createWorkspace(QStringLiteral("main"));
    workspace->setContentsMargins(4, 2, 4, 4);
    window.setCentralWidget(workspace);

    // The preview is not a dock. It has no header to take hold of and none of
    // the dock features, so it stays where it is; docks go around it.
    DockPanel *preview = manager.registerPanel(QStringLiteral("preview"), ObsStyle::makePreview(),
                                               QStringLiteral("Preview"));
    preview->setFeatures({});
    preview->setHeaderVisible(false);

    struct Dock
    {
        const char *id;
        const char *title;
        QWidget *content;
    };
    const QList<Dock> docks{
        {"scenes", "Scenes", ObsStyle::makeScenes()},
        {"sources", "Sources", ObsStyle::makeSources()},
        {"mixer", "Audio Mixer", ObsStyle::makeMixer()},
        {"transitions", "Scene Transitions", ObsStyle::makeTransitions()},
        {"controls", "Controls", ObsStyle::makeControls()},
    };
    const DockFeatures dockFeatures = DockFeature::Movable | DockFeature::Floatable
        | DockFeature::Tabbable | DockFeature::Closable;
    for (const Dock &dock : docks) {
        DockPanel *panel = manager.registerPanel(QString::fromLatin1(dock.id), dock.content,
                                                 QString::fromLatin1(dock.title));
        panel->setFeatures(dockFeatures);
    }

    // Scenes above Sources down the left side, the other three along the
    // bottom, the preview in what is left.
    workspace->addPanel(QStringLiteral("preview"), DockArea::Center);
    workspace->addPanel(QStringLiteral("scenes"), DockArea::Left, 0.16);
    manager.movePanel(QStringLiteral("sources"), QStringLiteral("scenes"), DockArea::Bottom, -1, 0.5);
    manager.movePanel(QStringLiteral("mixer"), QStringLiteral("preview"), DockArea::Bottom, -1, 0.3);
    manager.movePanel(QStringLiteral("transitions"), QStringLiteral("mixer"), DockArea::Right, -1, 0.4);
    manager.movePanel(QStringLiteral("controls"), QStringLiteral("transitions"), DockArea::Right, -1,
                      0.5);
    manager.saveDefaultLayout();
    manager.clearUndoHistory();

    // --- Menus -----------------------------------------------------------------
    QMenuBar *menus = window.menuBar();
    QMenu *fileMenu = menus->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("E&xit"), &app, &QApplication::quit);
    menus->addMenu(QStringLiteral("&Edit"))->addAction(QStringLiteral("Undo Layout Change"),
                                                       [&manager] { (void)manager.undo(); });
    menus->addMenu(QStringLiteral("&View"));

    QMenu *dockMenu = menus->addMenu(QStringLiteral("&Docks"));
    QAction *lock = dockMenu->addAction(QStringLiteral("Lock Docks"));
    lock->setCheckable(true);
    QObject::connect(lock, &QAction::toggled, &manager, [&manager, dockFeatures](bool locked) {
        // A locked dock can be neither moved nor floated; it loses its button, too.
        for (DockPanel *panel : manager.panels()) {
            if (panel->id() != QLatin1String("preview"))
                panel->setFeatures(locked ? DockFeatures(DockFeature::Closable) : dockFeatures);
        }
    });
    dockMenu->addAction(QStringLiteral("Reset Docks"), [&manager] { (void)manager.resetLayout(); });
    dockMenu->addSeparator();
    for (const Dock &dock : docks)
        dockMenu->addAction(manager.panel(QString::fromLatin1(dock.id))->toggleViewAction());
    for (const char *title : {"&Profile", "&Scene Collection", "&Tools", "&Help"})
        menus->addMenu(QString::fromLatin1(title));

    // --- Status bar --------------------------------------------------------------
    QStatusBar *status = window.statusBar();
    status->setSizeGripEnabled(false);
    const auto addStatus = [status](const QString &text, ObsStyle::Glyph glyph, bool withIcon) {
        auto *label = new QLabel(status);
        if (withIcon)
            label->setPixmap(ObsStyle::icon(glyph, ObsStyle::TextMuted).pixmap(16, 16));
        else
            label->setText(text);
        status->addPermanentWidget(label);
    };
    addStatus({}, ObsStyle::Glyph::Bars, true);
    addStatus(QStringLiteral("00:00:00"), {}, false);
    addStatus(QStringLiteral("00:00:00"), {}, false);
    addStatus(QStringLiteral("CPU: 4.2%"), {}, false);
    addStatus(QStringLiteral("60.00 / 60.00 FPS"), {}, false);

    window.resize(1280, 820);
    window.show();
    return app.exec();
}
