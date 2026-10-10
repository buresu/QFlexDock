// SPDX-License-Identifier: MIT
//
// The window layout and look of Visual Studio, rebuilt with QFlexDock: tool
// windows with a title bar around a place for documents under their tabs,
// panes with round corners and a gap between them, a guide of small buttons
// while something is dragged, and tool windows that are put away at a border
// of the window and slide out from there. Only the layout and the style are
// reproduced. None of the application's code or artwork is used, and nothing
// here edits a project.
//
// How the pieces map onto the library:
//  - The documents live in a workspace of their own, which is the content of
//    a fixed, headerless panel in the middle of the outer workspace. A
//    document stays in there (or floats); a tool window may be docked in
//    either, and takes the header of where it is: a title bar with its tabs
//    below among the tools, a tab above among the documents.
//  - The guide is DockGuide::Buttons. Over a document it is a cross for that
//    document, a ring around the cross for docking beside the documents as a
//    whole, and a button at each border of the window. Let go of anywhere
//    else, what is dragged becomes a window of its own.
//  - The pin in a title bar puts the tab group away at the nearest border
//    (auto-hide). Its name there brings it out beside the other panes, which
//    make room for it (AutoHideReveal::Beside); a click on anything else
//    sends it back, and its pin docks it again.
//  - The outline of the pane the user works in is the `active` property of
//    a tab group in the style sheet.
//  - Every boundary is moved on its own: no linked splitters, no corners.
//  - The frameless window, its title row, the tool bar and the status bar
//    are ordinary widgets around the workspace.

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include "Panels.h"
#include "Style.h"
#include "Window.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>

using namespace QFlexDock;
using namespace VisualStudio;

namespace {

const QString ToolWorkspace = u"main"_s;
const QString DocumentWorkspace = u"documents"_s;
const QString DocumentWell = u"documentWell"_s;
const QString DocumentPrefix = u"document:"_s;

struct ToolWindow
{
    PanelId id;
    QString title;
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
    void addToolWindows();
    void buildLayout();
    void addChrome();
    void addMenus();

    DockPanel *addDocument(const QString &name, const QString &text);
    void newDocument();
    [[nodiscard]] DockPanel *currentDocument() const;
    [[nodiscard]] DockPanel *currentPanel() const;
    void splitDocuments(DockArea side);
    void showToolWindow(const PanelId &id);

    DockManager m_manager;
    Window m_window;
    TitleBar *m_titleBar = nullptr;
    StatusBar *m_statusBar = nullptr;
    DockWorkspace *m_tools = nullptr;
    DockWorkspace *m_documents = nullptr;
    QList<ToolWindow> m_toolWindows;
    PanelId m_lastDocument;
    int m_documentCount = 0;
};

Workbench::Workbench()
{
    setUpDocking();

    m_tools = m_manager.createWorkspace(ToolWorkspace);
    // The documents: a workspace that is the content of a panel in the middle.
    auto *well = new QWidget;
    auto *wellLayout = new QVBoxLayout(well);
    wellLayout->setContentsMargins(0, 0, 0, 0);
    m_documents = m_manager.createWorkspace(DocumentWorkspace, well);
    m_documents->setObjectName(DocumentWorkspace);
    wellLayout->addWidget(m_documents);
    well->setMinimumSize(240, 160);
    DockPanel *center = m_manager.registerPanel(DocumentWell, well);
    center->setFeatures({});
    center->setHeaderVisible(false);

    // Title bars among the tools, tabs among the documents, where a header
    // has only the buttons the documents bring themselves.
    m_manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
    m_manager.setGroupHeader(m_documents, DockManager::GroupHeader::Tabs);
    m_manager.setTitleButtons(m_documents, {});

    addChrome();
    addToolWindows();
    buildLayout();
    addMenus();

    QObject::connect(&m_manager, &DockManager::activePanelChanged, &m_window, [this](DockPanel *panel) {
        if (panel && panel->id().startsWith(DocumentPrefix))
            m_lastDocument = panel->id();
    });
    // Among the documents nothing is put away at a border.
    QObject::connect(&m_manager, &DockManager::panelContextMenuRequested, &m_window,
                     [this](DockPanel *panel, QMenu *menu) {
        if (panel->workspace() != m_documents)
            return;
        if (QAction *autoHide = menu->findChild<QAction *>(u"dockActionAutoHide"_s))
            menu->removeAction(autoHide);
    });
    m_window.resize(1400, 860);
}

void Workbench::setUpDocking()
{
    DockTheme theme;
    theme.titleButtons = DockTitleButton::Menu | DockTitleButton::AutoHide | DockTitleButton::Close;
    theme.iconSize = 16;
    theme.splitHandleWidth = 6; // the gap between two panes
    theme.floatingBorderWidth = 1;
    // A pin standing up in the title bar of what is docked, lying down in
    // what has slid out.
    theme.icons.insert(DockIcon::Unpin, icon(Glyph::Pin));
    theme.icons.insert(DockIcon::Pin, icon(Glyph::PinSide));
    theme.icons.insert(DockIcon::Close, icon(Glyph::Close));
    theme.icons.insert(DockIcon::Menu, icon(Glyph::ChevronDown));
    theme.icons.insert(DockIcon::Maximize, icon(Glyph::Maximize));
    theme.icons.insert(DockIcon::Restore, icon(Glyph::Restore));

    DockOverlayStyle &guide = theme.overlay;
    guide.guide = DockGuide::Buttons;
    guide.buttonSize = 38;
    guide.zoneGap = 4;
    guide.zoneMargin = 8;
    guide.cornerRadius = 4;
    guide.borderWidth = 1;
    guide.buttonColor = QColor(44, 44, 44, 238);
    guide.zoneBorderColor = QColor(0xff5a5a5a);
    guide.hoverColor = QColor(147, 138, 191, 90);
    guide.hoverBorderColor = QColor(Accent);
    guide.glyphColor = QColor(0xffd0d0d0);
    guide.previewColor = QColor(64, 98, 128, 110);
    guide.previewBorderColor = QColor(100, 140, 175, 160);
    m_manager.setTheme(theme);

    m_manager.setLinkedSplittersEnabled(false);
    m_manager.setCornerResizeEnabled(false);
    m_manager.setFloatsOnOutsideDrop(true);
    // A title bar takes all the tool windows stacked under it along; one of
    // them is taken out by its tab.
    m_manager.setTitleBarMovesGroup(true);
    m_manager.setFloatingWindowType(DockManager::FloatingWindowType::Tool);
    // What is put away comes out beside the panes, not over them.
    m_manager.setAutoHideReveal(DockManager::AutoHideReveal::Beside);
}

// --- Panels ----------------------------------------------------------------------------

void Workbench::addToolWindows()
{
    const QStringList files{u"Program.cs"_s, u"Workspace.cs"_s, u"TextFile1.txt"_s};
    const auto add = [this](const QString &id, const QString &title, QWidget *content) {
        m_manager.registerPanel(id, content, title);
        m_toolWindows.append({id, title});
    };
    add(u"chat"_s, u"GitHub Copilot Chat"_s, makeChat());
    add(u"solution"_s, u"Solution Explorer"_s, makeSolutionExplorer(files));
    add(u"git"_s, u"Git Changes"_s, makeGitChanges());
    add(u"output"_s, u"Output"_s, makeOutput());
    add(u"errors"_s, u"Error List"_s, makeErrorList());
}

DockPanel *Workbench::addDocument(const QString &name, const QString &text)
{
    const PanelId id = DocumentPrefix + name;
    DockPanel *panel = m_manager.registerPanel(id, new TextEditor(text), name);
    DockPolicy policy;
    policy.features = AllDockFeatures & ~DockFeatures(DockFeature::AutoHideable);
    policy.allowedWorkspaces = {DocumentWorkspace};
    panel->setPolicy(policy);

    // At the end of the tab row: what a row of documents has there.
    auto *more = new QAction(icon(Glyph::Ellipsis), u"Document Groups"_s, panel);
    auto *menu = new QMenu(&m_window);
    menu->addAction(u"New Vertical Document Group"_s, panel,
                    [this] { splitDocuments(DockArea::Right); });
    menu->addAction(u"New Horizontal Document Group"_s, panel,
                    [this] { splitDocuments(DockArea::Bottom); });
    menu->addSeparator();
    menu->addAction(u"Close All Tabs"_s, panel, [this, id] {
        (void)m_manager.hidePanels(m_manager.tabGroupPanels(id));
    });
    more->setMenu(menu);
    auto *settings = new QAction(icon(Glyph::Gear), u"Tab Settings"_s, panel);
    panel->setTitleActions({more, settings});
    return panel;
}

void Workbench::newDocument()
{
    const QString name = u"TextFile%1.txt"_s.arg(++m_documentCount + 1);
    DockPanel *panel = addDocument(name, QString());
    const DockPanel *beside = currentDocument();
    if (beside && beside->isOpen() && beside->workspace() == m_documents && !beside->isFloating())
        (void)m_manager.movePanel(panel->id(), beside->id(), DockArea::Center);
    else
        (void)m_documents->addPanel(panel->id());
    (void)m_manager.activatePanel(panel->id());
}

DockPanel *Workbench::currentDocument() const
{
    return m_manager.panel(m_lastDocument);
}

DockPanel *Workbench::currentPanel() const
{
    DockPanel *panel = m_manager.activePanel();
    return panel && panel->id() != DocumentWell ? panel : nullptr;
}

// The current document into a group of its own beside the others.
void Workbench::splitDocuments(DockArea side)
{
    const DockPanel *document = currentDocument();
    if (!document || m_manager.tabGroupPanels(document->id()).size() < 2)
        return;
    (void)m_manager.movePanel(document->id(), document->id(), side);
}

void Workbench::showToolWindow(const PanelId &id)
{
    const DockPanel *panel = m_manager.panel(id);
    if (panel && !panel->isOpen())
        (void)m_manager.showPanel(id);
    (void)m_manager.activatePanel(id);
}

// --- Layout ----------------------------------------------------------------------------

void Workbench::buildLayout()
{
    (void)m_tools->addPanel(DocumentWell);

    const QString program = u"using System;\n\nnamespace Docking;\n\ninternal static class Program\n{\n"
                            "    private static void Main()\n    {\n"
                            "        var workspace = new Workspace(\"main\");\n"
                            "        workspace.Dock(\"Solution Explorer\", Side.Right);\n"
                            "        workspace.Dock(\"Output\", Side.Bottom);\n"
                            "        Console.WriteLine(workspace);\n    }\n}\n"_s;
    const QString workspace = u"namespace Docking;\n\ninternal enum Side { Left, Right, Top, Bottom }\n\n"
                              "internal sealed class Workspace(string name)\n{\n"
                              "    private readonly List<(string Panel, Side Side)> _docked = [];\n\n"
                              "    public void Dock(string panel, Side side) => _docked.Add((panel, side));\n\n"
                              "    public override string ToString() => $\"{name}: {_docked.Count} docked\";\n}\n"_s;
    (void)m_documents->addPanel(addDocument(u"Program.cs"_s, program)->id());
    (void)m_documents->addPanel(addDocument(u"Workspace.cs"_s, workspace)->id());
    (void)m_documents->addPanel(addDocument(u"TextFile1.txt"_s, QString())->id());

    // One group of tool windows beside the documents.
    (void)m_tools->addPanel(u"chat"_s, DockArea::Right, 0.22);
    (void)m_manager.movePanel(u"solution"_s, u"chat"_s, DockArea::Center);
    (void)m_manager.movePanel(u"git"_s, u"chat"_s, DockArea::Center);
    (void)m_manager.activatePanel(u"chat"_s);

    // Two more that belong below the documents, and are put away there.
    (void)m_manager.movePanel(u"output"_s, DocumentWell, DockArea::Bottom, -1, 0.3);
    (void)m_manager.movePanel(u"errors"_s, u"output"_s, DockArea::Center);
    (void)m_manager.setPanelAutoHide(u"output"_s, true, DockArea::Bottom);
    (void)m_manager.setPanelAutoHide(u"errors"_s, true, DockArea::Bottom);

    m_lastDocument = DocumentPrefix + u"TextFile1.txt"_s;
    (void)m_manager.activatePanel(m_lastDocument);
    m_manager.saveDefaultLayout();
    m_manager.clearUndoHistory();
}

// --- Chrome ----------------------------------------------------------------------------

void Workbench::addChrome()
{
    m_titleBar = new TitleBar(&m_window);
    auto *toolBar = new ToolBar;
    toolBar->addGroup();
    toolBar->add(Glyph::Back, u"Navigate Backward"_s, IconMuted);
    toolBar->add(Glyph::Forward, u"Navigate Forward"_s, IconMuted);
    toolBar->addSeparator();
    QObject::connect(toolBar->add(Glyph::NewFile, u"New File"_s), &QToolButton::clicked, &m_window,
                     [this] { newDocument(); });
    toolBar->add(Glyph::Open, u"Open File"_s);
    toolBar->add(Glyph::Save, u"Save"_s, Blue);
    toolBar->add(Glyph::SaveAll, u"Save All"_s, Blue);
    toolBar->addSeparator();
    QObject::connect(toolBar->add(Glyph::Undo, u"Undo the Last Change to the Layout"_s),
                     &QToolButton::clicked, &m_window, [this] { (void)m_manager.undo(); });
    QObject::connect(toolBar->add(Glyph::Redo, u"Redo"_s), &QToolButton::clicked, &m_window,
                     [this] { (void)m_manager.redo(); });
    toolBar->addSeparator();
    toolBar->add(Glyph::Play, u"Attach to Process"_s, Green, u"Attach..."_s);
    toolBar->addSeparator();
    toolBar->add(Glyph::Search, u"Find in Files"_s);
    toolBar->add(Glyph::Ellipsis, u"More"_s);
    toolBar->addGroup();
    toolBar->add(Glyph::Comment, u"Comment Out"_s);
    toolBar->add(Glyph::Indent, u"Increase Indent"_s, IconMuted);
    toolBar->addSeparator();
    toolBar->add(Glyph::Bookmark, u"Toggle Bookmark"_s);
    toolBar->add(Glyph::Ellipsis, u"More"_s);
    toolBar->addGroup(true);
    toolBar->add(Glyph::Feedback, u"Send Feedback"_s);
    toolBar->add(Glyph::Account, u"Live Share"_s);

    // Some room between the border of the window and the panes.
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(6, 0, 6, 4);
    bodyLayout->addWidget(m_tools);

    m_statusBar = new StatusBar;
    m_window.addPart(m_titleBar);
    m_window.addPart(toolBar);
    m_window.addPart(body, 1);
    m_window.addPart(m_statusBar);
    m_window.setWindowTitle(u"QFlexDock - Visual Studio-style layout"_s);
}

void Workbench::addMenus()
{
    QMenuBar *bar = m_titleBar->menuBar();

    QMenu *file = bar->addMenu(u"&File"_s);
    file->addAction(u"&New File"_s, QKeySequence::New, &m_window, [this] { newDocument(); });
    file->addAction(u"&Close"_s, QKeySequence(Qt::CTRL | Qt::Key_F4), &m_window, [this] {
        if (const DockPanel *document = currentDocument())
            (void)m_manager.hidePanel(document->id());
    });
    file->addSeparator();
    file->addAction(u"E&xit"_s, &m_window, &QWidget::close);

    QMenu *edit = bar->addMenu(u"&Edit"_s);
    edit->addAction(u"&Undo Layout Change"_s, &m_window, [this] { (void)m_manager.undo(); });
    edit->addAction(u"&Redo Layout Change"_s, &m_window, [this] { (void)m_manager.redo(); });

    // Every tool window, wherever it is: shown if it was closed, slid out if
    // it is put away, brought to the front otherwise.
    QMenu *view = bar->addMenu(u"&View"_s);
    for (const ToolWindow &tool : std::as_const(m_toolWindows)) {
        const PanelId id = tool.id;
        view->addAction(tool.title, &m_window, [this, id] { showToolWindow(id); });
    }

    for (const QString &title : {u"&Git"_s, u"&Debug"_s, u"Te&st"_s, u"&Tools"_s, u"E&xtensions"_s}) {
        QMenu *menu = bar->addMenu(title);
        menu->addAction(u"Not part of this example"_s)->setEnabled(false);
    }

    QMenu *window = bar->addMenu(u"&Window"_s);
    QAction *floatAction = window->addAction(u"&Float"_s, &m_window, [this] {
        if (const DockPanel *panel = currentPanel())
            (void)m_manager.floatPanel(panel->id());
    });
    QAction *dockAction = window->addAction(u"&Dock"_s, &m_window, [this] {
        if (const DockPanel *panel = currentPanel())
            (void)m_manager.dockPanel(panel->id());
    });
    QAction *autoHide = window->addAction(u"&Auto Hide"_s, &m_window, [this] {
        if (const DockPanel *panel = currentPanel())
            (void)m_manager.setPanelAutoHide(panel->id(), true);
    });
    window->addSeparator();
    window->addAction(u"New &Vertical Document Group"_s, &m_window,
                      [this] { splitDocuments(DockArea::Right); });
    window->addAction(u"New Hori&zontal Document Group"_s, &m_window,
                      [this] { splitDocuments(DockArea::Bottom); });
    window->addSeparator();
    window->addAction(u"&Reset Window Layout"_s, &m_window, [this] { (void)m_manager.resetLayout(); });
    QObject::connect(window, &QMenu::aboutToShow, &m_window, [=, this] {
        const DockPanel *panel = currentPanel();
        const bool tool = panel && !panel->id().startsWith(DocumentPrefix);
        floatAction->setEnabled(panel && !panel->isFloating());
        dockAction->setEnabled(panel && (panel->isFloating() || panel->isAutoHidden()));
        autoHide->setEnabled(tool && !panel->isAutoHidden() && !panel->isFloating()
                             && panel->workspace() == m_tools);
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
