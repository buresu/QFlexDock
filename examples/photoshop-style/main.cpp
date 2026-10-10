// SPDX-License-Identifier: MIT
//
// The window layout and look of Photoshop, rebuilt with QFlexDock: documents
// under their tabs in the middle, and around them panels that stand in
// columns, each column under a bar that puts it away as a strip of icons.
// Only the layout and the style are reproduced. None of the application's
// code or artwork is used, and nothing here edits a picture.
//
// How the pieces map onto the library:
//  - The documents live in a workspace of their own, which is the content of
//    a fixed, headerless panel in the middle of the outer workspace. A
//    document only ever becomes a tab among documents, or a window.
//  - The outer workspace docks in columns (setColumnDocking()): what is
//    dropped on the side of a panel group goes beside the column that group
//    is in, on its top or bottom into the column, on the group into its tabs.
//    The bar above a column moves all of it, and its button turns the column
//    into buttons (setColumnIconified()); a button brings its group out
//    beside the strip. A floating column is moved and closed by its bar.
//  - The tools are a panel like the others, with a small form of its own
//    (DockPanel::setCompactWidget()): one column of them in place of a
//    button, so that its column is the one-wide tool bar when iconified and
//    the two-wide one when not.
//  - The drop guide is DockGuide::Preview with a painter of this example's
//    own, which draws an outline around what would be joined and a bar where
//    something would be put in between. Tabs show a drag as it would turn
//    out (setTabDragPreviewEnabled()), so the outline can follow the tab.
//  - A document becomes a tab by a row of tabs or a title only, and a window
//    when it is let go of anywhere else: the middle of the document groups
//    takes no drop (setCenterDropEnabled() for their workspace).
//  - A split handle pushes on through what cannot get smaller
//    (setSplitterPushEnabled()): dragging the boundary of the column at the
//    right moves the strip of icons beside it along, at the documents' cost.
//  - A drop filter keeps the rest in shape: columns only at the sides, and
//    nothing above or below the documents or the tools.
//  - The frameless window with its round corners, its menus, the row of
//    options and the bar that hangs below the document (a window of its
//    own) are ordinary widgets around the workspace.

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include "Panels.h"
#include "Style.h"
#include "Window.h"

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>

#include <algorithm>
#include <functional>
#include <memory>

using namespace QFlexDock;
using namespace Photoshop;

namespace {

const QString PanelWorkspace = u"panels"_s;
const QString DocumentWorkspace = u"documents"_s;
const QString DocumentWell = u"documentWell"_s;
const QString DocumentPrefix = u"document:"_s;
const QString Tools = u"tools"_s;

struct Panel
{
    PanelId id;
    QString title;
};

/// Tells when something that the bar below the document goes by has moved:
/// a window, or the picture of a document.
class Watcher : public QObject
{
public:
    explicit Watcher(std::function<void()> changed)
        : m_changed(std::move(changed))
    {
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::WindowStateChange: {
            const auto *widget = qobject_cast<const QWidget *>(watched);
            if (widget && (widget->isWindow() || dynamic_cast<const Canvas *>(widget))
                && !dynamic_cast<const TaskBar *>(widget) && !m_pending) {
                // Once for all that moves in one go.
                m_pending = true;
                QTimer::singleShot(0, this, [this] {
                    m_pending = false;
                    m_changed();
                });
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

private:
    std::function<void()> m_changed;
    bool m_pending = false;
};

class Workbench
{
public:
    Workbench();
    // The window goes first, and the manager reports the layout changes that
    // brings. By then there is no chrome left to bring in line with them.
    ~Workbench() { QObject::disconnect(&m_manager, nullptr, &m_window, nullptr); }
    void show() { m_window.show(); }

private:
    void setUpDocking();
    void addPanels();
    void buildLayout();
    void addMenus();

    DockPanel *addDocument();
    void newDocument();
    [[nodiscard]] DockPanel *currentDocument() const;
    void togglePanel(const PanelId &id);
    void placeTaskBar();

    DockManager m_manager;
    Window m_window;
    TitleBar *m_titleBar = nullptr;
    OptionsBar *m_optionsBar = nullptr;
    TaskBar *m_taskBar = nullptr;
    std::unique_ptr<Watcher> m_watcher;
    DockWorkspace *m_panels = nullptr;
    DockWorkspace *m_documents = nullptr;
    QList<Panel> m_panelList;
    PanelId m_lastDocument;
    int m_documentCount = 0;
};

Workbench::Workbench()
{
    setUpDocking();

    m_panels = m_manager.createWorkspace(PanelWorkspace);
    // The documents: a workspace that is the content of a panel in the middle.
    auto *well = new QWidget;
    auto *wellLayout = new QVBoxLayout(well);
    wellLayout->setContentsMargins(0, 0, 0, 0);
    m_documents = m_manager.createWorkspace(DocumentWorkspace, well);
    m_documents->setObjectName(DocumentWorkspace);
    wellLayout->addWidget(m_documents);
    well->setMinimumSize(280, 200);
    DockPanel *center = m_manager.registerPanel(DocumentWell, well);
    center->setFeatures({});
    center->setHeaderVisible(false);

    // Panels in columns, each under its bar; the documents just under tabs,
    // which are also the one place where a document becomes a tab.
    m_panels->setColumnDocking(true);
    m_documents->setTitleButtons({});
    m_documents->setCenterDropEnabled(false);

    m_titleBar = new TitleBar(&m_window);
    m_optionsBar = new OptionsBar;
    m_window.addPart(m_titleBar);
    m_window.addPart(m_optionsBar);
    m_window.addPart(m_panels, 1);
    m_window.setWindowTitle(u"QFlexDock - Photoshop-style layout"_s);

    addPanels();
    buildLayout();
    addMenus();

    QObject::connect(&m_manager, &DockManager::activePanelChanged, &m_window, [this](DockPanel *panel) {
        if (panel && panel->id().startsWith(DocumentPrefix))
            m_lastDocument = panel->id();
        placeTaskBar();
    });
    // The bar below the document is a window, so it is put there from here,
    // whenever the document may be elsewhere.
    m_taskBar = new TaskBar(&m_window);
    QObject::connect(&m_manager, &DockManager::layoutChanged, &m_window, [this] { placeTaskBar(); });
    m_watcher = std::make_unique<Watcher>([this] { placeTaskBar(); });
    // What the menu of a panel has to offer here.
    QObject::connect(&m_manager, &DockManager::panelContextMenuRequested, &m_window,
                     [this](DockPanel *panel, QMenu *menu) {
        for (const QString &name : {u"dockActionAutoHide"_s, u"dockActionMaximize"_s}) {
            if (QAction *action = menu->findChild<QAction *>(name))
                menu->removeAction(action);
        }
        if (panel->workspace() != m_panels || panel->id() == Tools)
            return;
        const PanelId id = panel->id();
        menu->addSeparator();
        menu->addAction(u"Close Tab Group"_s, &m_window, [this, id] {
            (void)m_manager.closePanels(m_manager.tabGroupPanels(id));
        });
        const bool iconified = m_manager.isColumnIconified(id);
        menu->addAction(iconified ? u"Expand Panels"_s : u"Collapse to Icons"_s, &m_window,
                        [this, id, iconified] { (void)m_manager.setColumnIconified(id, !iconified); });
    });
    m_window.resize(1400, 900);
}

void Workbench::setUpDocking()
{
    DockTheme theme;
    theme.titleButtons = DockTitleButton::Menu;
    theme.iconSize = 18;
    theme.tabIcons = false; // tabs are names; the icons are for the strips
    theme.splitHandleWidth = 2;
    theme.floatingBorderWidth = 1;
    theme.floatingCornerRadius = 8;
    theme.columnBarHeight = 12;
    theme.icons.insert(DockIcon::Menu, icon(Glyph::Menu, Text, 14));
    theme.icons.insert(DockIcon::Close, icon(Glyph::Close, Text, 12));
    theme.icons.insert(DockIcon::Maximize, icon(Glyph::Maximize, Text, 12));
    theme.icons.insert(DockIcon::Restore, icon(Glyph::Restore, Text, 12));
    theme.icons.insert(DockIcon::IconifyLeft, chevrons(false));
    theme.icons.insert(DockIcon::IconifyRight, chevrons(true));

    // Nothing is shown but the place of the drop; the sides of a group are
    // a few pixels deep, and so is the border of the window.
    DockOverlayStyle &guide = theme.overlay;
    guide.guide = DockGuide::Preview;
    guide.edgeExtent = 10;
    guide.zoneMargin = 0;
    guide.outerBandWidth = 12;
    m_manager.setTheme(theme);
    m_manager.setOverlayPainter(std::make_shared<OverlayPainter>());

    m_manager.setLinkedSplittersEnabled(false);
    m_manager.setCornerResizeEnabled(false);
    m_manager.setSplitterPushEnabled(true);
    m_manager.setFloatsOnOutsideDrop(true);
    m_manager.setTabDragPreviewEnabled(true);
    m_manager.setFloatingWindowType(DockManager::FloatingWindowType::Tool);

    m_manager.setDropFilter([](const DockDropRequest &request) {
        // (Among the documents, their policies say it all.)
        if (request.workspaceId != PanelWorkspace)
            return true;
        const bool beside = request.area == DockArea::Left || request.area == DockArea::Right;
        // At the border of the window: one more column.
        if (request.targetPanel.isEmpty())
            return beside;
        // A floating window is one column.
        if (request.intoFloatingWindow && beside)
            return false;
        // The documents and the tools have nothing above or below them.
        if (request.targetPanel == DocumentWell || request.targetPanel == Tools)
            return beside;
        return true;
    });
}

// --- Panels ----------------------------------------------------------------------------

void Workbench::addPanels()
{
    const auto add = [this](const QString &id, const QString &title, Glyph glyph, QWidget *content) {
        DockPanel *panel = m_manager.registerPanel(id, content, title);
        panel->setIcon(icon(glyph, Text, 18));
        DockPolicy policy;
        policy.features = DockFeature::Movable | DockFeature::Closable | DockFeature::Floatable
            | DockFeature::Tabbable;
        policy.allowedWorkspaces = {PanelWorkspace};
        panel->setPolicy(policy);
        // Closed from its menu, not by a button on its tab.
        panel->setTabCloseButton(false);
        m_panelList.append({id, title});
    };
    add(u"color"_s, u"Color"_s, Glyph::Color, makeColor());
    add(u"swatches"_s, u"Swatches"_s, Glyph::Swatches, makeSwatches());
    add(u"gradients"_s, u"Gradients"_s, Glyph::Gradients, makeGradients());
    add(u"patterns"_s, u"Patterns"_s, Glyph::Patterns, makePatterns());
    add(u"properties"_s, u"Properties"_s, Glyph::Properties, makeProperties());
    add(u"adjustments"_s, u"Adjustments"_s, Glyph::Adjustments,
        makeList({{Glyph::Adjustments, u"Brightness/Contrast"_s}, {Glyph::Sliders, u"Levels"_s},
                  {Glyph::Paths, u"Curves"_s}, {Glyph::Gradients, u"Hue/Saturation"_s},
                  {Glyph::Channels, u"Color Balance"_s}}, -1, {}));
    add(u"libraries"_s, u"Libraries"_s, Glyph::Libraries,
        makeNote(u"Libraries are not part of this example."_s));
    add(u"layers"_s, u"Layers"_s, Glyph::Layers, makeLayers());
    add(u"channels"_s, u"Channels"_s, Glyph::Channels,
        makeList({{Glyph::Eye, u"RGB"_s}, {Glyph::Eye, u"Red"_s}, {Glyph::Eye, u"Green"_s},
                  {Glyph::Eye, u"Blue"_s}}, 0,
                 {{Glyph::Marquee, u"Load channel as selection"_s},
                  {Glyph::Mask, u"Save selection as channel"_s},
                  {Glyph::NewItem, u"Create new channel"_s}, {Glyph::Trash, u"Delete channel"_s}}));
    add(u"paths"_s, u"Paths"_s, Glyph::Paths,
        makeList({}, -1, {{Glyph::Marquee, u"Load path as a selection"_s},
                          {Glyph::NewItem, u"Create new path"_s}, {Glyph::Trash, u"Delete path"_s}}));
    add(u"history"_s, u"History"_s, Glyph::History,
        makeList({{Glyph::Snapshot, u"Untitled-1"_s}, {Glyph::Document, u"New"_s}}, 1,
                 {{Glyph::NewItem, u"Create new document from current state"_s},
                  {Glyph::Camera, u"Create new snapshot"_s}, {Glyph::Trash, u"Delete current state"_s}}));
    add(u"actions"_s, u"Actions"_s, Glyph::Actions,
        makeList({{Glyph::Adjustments, u"Auto Color Balance"_s}, {Glyph::Adjustments, u"Auto Tone"_s},
                  {Glyph::Adjustments, u"Enhance Contrast"_s}, {Glyph::Image, u"Select Subject"_s},
                  {Glyph::Image, u"Remove Background"_s}, {Glyph::Drop, u"Blur Background"_s},
                  {Glyph::Sparkle, u"Sharpen"_s}, {Glyph::Pixels, u"Black & White"_s}}, -1,
                 {{Glyph::Stop, u"Stop"_s}, {Glyph::Record, u"Record"_s}, {Glyph::Play, u"Play"_s},
                  {Glyph::Folder, u"Create new set"_s}, {Glyph::NewItem, u"Create new action"_s},
                  {Glyph::Trash, u"Delete"_s}}));

    // The tools: two columns of them as a panel, one as what stands for the
    // panel where its column is iconified. They never share a column.
    DockPanel *tools = m_manager.registerPanel(Tools, new ToolBox(2), u"Tools"_s);
    tools->setCompactWidget(new ToolBox(1));
    tools->setHeaderVisible(false);
    DockPolicy policy;
    policy.features = DockFeature::Movable | DockFeature::Closable | DockFeature::Floatable;
    policy.allowedAreas = DockArea::Left | DockArea::Right;
    policy.allowedWorkspaces = {PanelWorkspace};
    tools->setPolicy(policy);
    m_panelList.append({Tools, u"Tools"_s});
}

DockPanel *Workbench::addDocument()
{
    const QString name = u"Untitled-%1"_s.arg(++m_documentCount);
    const PanelId id = DocumentPrefix + name;
    DockPanel *panel = m_manager.registerPanel(id, makeDocument(), name + u" @ 66.7% (RGB/8)"_s);
    DockPolicy policy;
    policy.features = DockFeature::Movable | DockFeature::Closable | DockFeature::Floatable
        | DockFeature::Tabbable;
    policy.allowedAreas = DockArea::Center; // a tab among documents, or a window
    policy.allowedWorkspaces = {DocumentWorkspace};
    panel->setPolicy(policy);
    return panel;
}

void Workbench::newDocument()
{
    DockPanel *panel = addDocument();
    const DockPanel *beside = currentDocument();
    if (beside && beside->isOpen() && !beside->isFloating())
        (void)m_manager.movePanel(panel->id(), beside->id(), DockArea::Center);
    else
        (void)m_documents->addPanel(panel->id());
    (void)m_manager.activatePanel(panel->id());
}

DockPanel *Workbench::currentDocument() const
{
    return m_manager.panel(m_lastDocument);
}

void Workbench::togglePanel(const PanelId &id)
{
    const DockPanel *panel = m_manager.panel(id);
    if (!panel)
        return;
    if (panel->isOpen())
        (void)m_manager.closePanel(id);
    else if (m_manager.openPanel(id))
        (void)m_manager.activatePanel(id);
}

// Below the sheet of the current document, until the user puts it elsewhere.
void Workbench::placeTaskBar()
{
    const DockPanel *document = currentDocument();
    const QWidget *content = document && document->isOpen() ? document->widget() : nullptr;
    const Canvas *canvas = nullptr;
    const QList<QWidget *> parts = content ? content->findChildren<QWidget *>() : QList<QWidget *>();
    for (const QWidget *part : parts) {
        if (!canvas)
            canvas = dynamic_cast<const Canvas *>(part);
    }
    if (!canvas || !canvas->isVisible() || canvas->window()->isMinimized()) {
        m_taskBar->hide();
        return;
    }
    const QSize size = m_taskBar->sizeHint();
    if (!m_taskBar->wasMoved()) {
        const QPoint top = canvas->mapToGlobal(canvas->below(size.height()));
        m_taskBar->setGeometry(top.x() - size.width() / 2, top.y(), size.width(), size.height());
    }
    m_taskBar->show();
}

// --- Layout ----------------------------------------------------------------------------

void Workbench::buildLayout()
{
    (void)m_panels->addPanel(DocumentWell);
    DockPanel *document = addDocument();
    (void)m_documents->addPanel(document->id());

    // The column at the right: three groups of panels.
    (void)m_manager.movePanel(u"color"_s, DocumentWell, DockArea::Right, -1, 0.24);
    for (const QString &id : {u"swatches"_s, u"gradients"_s, u"patterns"_s})
        (void)m_manager.movePanel(id, u"color"_s, DockArea::Center);
    (void)m_manager.movePanel(u"properties"_s, u"color"_s, DockArea::Bottom, -1, 0.72);
    for (const QString &id : {u"adjustments"_s, u"libraries"_s})
        (void)m_manager.movePanel(id, u"properties"_s, DockArea::Center);
    (void)m_manager.movePanel(u"layers"_s, u"properties"_s, DockArea::Bottom, -1, 0.5);
    for (const QString &id : {u"channels"_s, u"paths"_s})
        (void)m_manager.movePanel(id, u"layers"_s, DockArea::Center);
    for (const QString &id : {u"color"_s, u"properties"_s, u"layers"_s})
        (void)m_manager.activatePanel(id);

    // Beside it, a column that is put away: two icons.
    (void)m_manager.movePanel(u"history"_s, DocumentWell, DockArea::Right, -1, 0.02);
    (void)m_manager.movePanel(u"actions"_s, u"history"_s, DockArea::Center);
    (void)m_manager.setColumnIconified(u"history"_s, true);

    // At the left, the tools: put away as well, which is one column of them.
    (void)m_manager.movePanel(Tools, DocumentWell, DockArea::Left, -1, 0.05);
    (void)m_manager.setColumnIconified(Tools, true);

    m_lastDocument = document->id();
    (void)m_manager.activatePanel(m_lastDocument);
    m_manager.saveDefaultLayout();
    m_manager.clearUndoHistory();
}

// --- Menus -----------------------------------------------------------------------------

void Workbench::addMenus()
{
    QMenuBar *bar = m_titleBar->menuBar();

    QMenu *file = bar->addMenu(u"&File"_s);
    file->addAction(u"&New..."_s, QKeySequence::New, &m_window, [this] { newDocument(); });
    file->addAction(u"&Close"_s, QKeySequence(Qt::CTRL | Qt::Key_W), &m_window, [this] {
        if (const DockPanel *document = currentDocument())
            (void)m_manager.closePanel(document->id());
    });
    file->addSeparator();
    file->addAction(u"E&xit"_s, &m_window, &QWidget::close);

    QMenu *edit = bar->addMenu(u"&Edit"_s);
    edit->addAction(u"&Undo Layout Change"_s, QKeySequence::Undo, &m_window,
                    [this] { (void)m_manager.undo(); });
    edit->addAction(u"&Redo Layout Change"_s, QKeySequence::Redo, &m_window,
                    [this] { (void)m_manager.redo(); });

    for (const QString &title : {u"&Image"_s, u"&Layer"_s, u"T&ype"_s, u"&Select"_s, u"Fil&ter"_s,
                                 u"&View"_s, u"&Plugins"_s}) {
        QMenu *menu = bar->addMenu(title);
        menu->addAction(u"Not part of this example"_s)->setEnabled(false);
    }

    // Where a document is: a window of its own, or back among the tabs.
    QMenu *window = bar->addMenu(u"&Window"_s);
    QMenu *arrange = window->addMenu(u"&Arrange"_s);
    QAction *floatAction = arrange->addAction(u"&Float in Window"_s, &m_window, [this] {
        if (const DockPanel *document = currentDocument())
            (void)m_manager.floatPanel(document->id());
    });
    QAction *consolidate = arrange->addAction(u"&Consolidate All to Tabs"_s, &m_window, [this] {
        const QList<DockPanel *> all = m_manager.panels();
        for (const DockPanel *panel : all) {
            if (panel->id().startsWith(DocumentPrefix) && panel->isFloating())
                (void)m_manager.movePanel(panel->id(), m_documents, DockArea::Center);
        }
    });
    QMenu *workspace = window->addMenu(u"Wor&kspace"_s);
    QAction *reset = workspace->addAction(u"&Reset Essentials"_s, &m_window,
                                          [this] { (void)m_manager.resetLayout(); });
    auto *switcher = new QMenu(&m_window);
    switcher->addAction(reset);
    m_optionsBar->workspaceButton()->setMenu(switcher);
    window->addSeparator();

    // Every panel, shown or closed.
    QList<Panel> byName = m_panelList;
    std::sort(byName.begin(), byName.end(),
              [](const Panel &a, const Panel &b) { return a.title < b.title; });
    QList<QAction *> toggles;
    for (const Panel &panel : std::as_const(byName)) {
        const PanelId id = panel.id;
        QAction *toggle = window->addAction(panel.title, &m_window, [this, id] { togglePanel(id); });
        toggle->setCheckable(true);
        toggle->setData(id);
        toggles.append(toggle);
    }
    QObject::connect(window, &QMenu::aboutToShow, &m_window, [=, this] {
        for (QAction *toggle : toggles) {
            const DockPanel *panel = m_manager.panel(toggle->data().toString());
            toggle->setChecked(panel && panel->isOpen());
        }
        const DockPanel *document = currentDocument();
        floatAction->setEnabled(document && document->isOpen() && !document->isFloating());
        const QList<DockPanel *> all = m_manager.panels();
        consolidate->setEnabled(std::any_of(all.cbegin(), all.cend(), [](const DockPanel *panel) {
            return panel->id().startsWith(DocumentPrefix) && panel->isFloating();
        }));
    });

    bar->addMenu(u"&Help"_s)->addAction(u"About Qt"_s, qApp, &QApplication::aboutQt);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(new Style);
    app.setPalette(palette());
    app.setStyleSheet(styleSheet());

    Workbench workbench;
    workbench.show();
    return app.exec();
}
