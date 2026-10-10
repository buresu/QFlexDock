// SPDX-License-Identifier: MIT
//
// The window layout and look of Visual Studio Code, rebuilt with QFlexDock:
// documents in the middle, a side bar on either side and a panel below that
// can be put away and brought back, flat tabs there and boxed ones for the
// documents, buttons of the application's own in the headers. Only the
// layout and the style are reproduced. None of the application's code or
// artwork is used, and nothing here edits a project.
//
// How the pieces map onto the library:
//  - The documents live in a workspace of their own, which is the content of
//    a fixed, headerless panel in the middle of the outer workspace. Each kind
//    of panel is only allowed in its own workspace, so documents and views
//    cannot be dropped into each other's place.
//  - Putting an area away is closing its panels; bringing it back is showing
//    them again. They return to where they were, at the size they had. The
//    views are "collapsible": pushing the boundary of an area far enough
//    against it puts it away as well, and dragging inwards from the edge it
//    went to pulls it out again.
//  - Where a boundary between documents ends on the boundary of a side area,
//    the point can be dragged to move both, across the two workspaces.
//  - Documents cannot float: a dragged tab is a picture of the tab, and
//    dropping it outside the middle does nothing.
//  - The frameless window, its title bar, the activity bar, the status bar
//    and the command palette are ordinary widgets around the workspace.

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include "Style.h"
#include "Views.h"
#include "Window.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QWidgetAction>

using namespace QFlexDock;
using namespace VsStyle;

namespace {

/// Views that are put away and brought back together: the side bars and the
/// panel.
struct Area
{
    QStringList views;
    /// Those of them that were open when any last was: what comes back.
    QStringList lastOpen;
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
    void addViews();
    void buildLayout();
    void addMenus();
    void addStatusBar();

    DockPanel *addView(Area &area, const QString &id, const QString &title, QWidget *content);
    QAction *action(DockPanel *panel, Glyph glyph, const QString &text,
                    std::function<void()> run = {});
    QAction *separator(DockPanel *panel);
    QAction *menuAction(DockPanel *panel, QMenu *menu);

    [[nodiscard]] bool isOpen(const Area &area) const;
    void setOpen(Area &area, bool open);
    void toggle(Area &area) { setOpen(area, !isOpen(area)); }
    void trackAreas();
    void showSideView(const PanelId &id);
    void showView(Area &area, const PanelId &id);

    void openFile(const QString &path, bool preview);
    [[nodiscard]] PanelId currentDocument();
    void closeDocument();
    void splitEditor(const PanelId &id);
    void closeEditors(const PanelId &id, bool others);
    void syncChrome();
    void syncStatus();
    [[nodiscard]] QList<CommandPalette::Entry> paletteEntries();

    DockManager m_manager;
    Window m_window;
    TitleBar *m_titleBar = nullptr;
    ActivityBar *m_activityBar = nullptr;
    StatusBar *m_statusBar = nullptr;
    CommandPalette *m_palette = nullptr;
    DockWorkspace *m_workspace = nullptr;
    DockWorkspace *m_editors = nullptr;

    Area m_sideBar;
    Area m_panel;
    Area m_auxBar;
    QHash<PanelId, QToolButton *> m_activityButtons;
    QToolButton *m_sideBarButton = nullptr;
    QToolButton *m_panelButton = nullptr;
    QToolButton *m_auxBarButton = nullptr;
    QList<QAction *> m_maximizeActions;

    QList<File> m_files;
    QHash<PanelId, CodeEditor *> m_codeEditors;
    PanelId m_preview;
    PanelId m_document;
    QToolButton *m_position = nullptr;
    QToolButton *m_language = nullptr;
};

const QString MainWorkspace = u"main"_s;
const QString EditorWorkspace = u"editors"_s;
const QString FilePrefix = u"file:"_s;

Workbench::Workbench()
    : m_files(projectFiles())
{
    setUpDocking();

    m_titleBar = new TitleBar(&m_window);
    m_activityBar = new ActivityBar;
    m_statusBar = new StatusBar;
    m_workspace = m_manager.createWorkspace(MainWorkspace);
    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(m_activityBar);
    bodyLayout->addWidget(m_workspace, 1);
    m_window.addPart(m_titleBar);
    m_window.addPart(body, 1);
    m_window.addPart(m_statusBar);
    m_window.setWindowTitle(u"QFlexDock - VS Code-style layout"_s);

    m_palette = new CommandPalette(&m_window);
    m_palette->entries = [this] { return paletteEntries(); };
    m_titleBar->commandCenter()->setText(u"Docking"_s);
    QObject::connect(m_titleBar->commandCenter(), &QPushButton::clicked, m_palette,
                     [this] { m_palette->open(); });

    addViews();
    buildLayout();
    addMenus();
    addStatusBar();

    QObject::connect(&m_manager, &DockManager::layoutChanged, &m_window, [this] {
        trackAreas();
        syncChrome();
        syncStatus();
    });
    trackAreas();
    QObject::connect(&m_manager, &DockManager::activePanelChanged, &m_window,
                     [this] { syncStatus(); });
    syncChrome();
    syncStatus();
    m_window.resize(1280, 800);
}

void Workbench::setUpDocking()
{
    DockTheme theme;
    theme.titleButtons = {};          // the headers hold the application's own buttons only
    theme.iconSize = 16;
    theme.splitHandleWidth = 1;       // a line between the areas...
    theme.splitHandleHoverWidth = 4;  // ...that is wider while it is pointed at
    // What a drag shows: the place the dragged tab would take, and nothing else.
    theme.overlay.guide = DockGuide::Preview;
    theme.overlay.previewColor = QColor(83, 89, 93, 128);
    theme.overlay.hoverBorderColor = QColor(Accent); // the mark between two tabs
    theme.overlay.borderWidth = 0;
    theme.overlay.cornerRadius = 0;
    m_manager.setTheme(theme);
}

// --- Panels ----------------------------------------------------------------------------

QAction *Workbench::action(DockPanel *panel, Glyph glyph, const QString &text,
                           std::function<void()> run)
{
    auto *a = new QAction(icon(glyph), text, panel);
    a->setToolTip(text);
    if (run)
        QObject::connect(a, &QAction::triggered, panel, std::move(run));
    return a;
}

QAction *Workbench::separator(DockPanel *panel)
{
    auto *a = new QAction(panel);
    a->setSeparator(true);
    return a;
}

QAction *Workbench::menuAction(DockPanel *panel, QMenu *menu)
{
    QAction *a = action(panel, Glyph::Ellipsis, u"More Actions..."_s);
    a->setMenu(menu);
    return a;
}

DockPanel *Workbench::addView(Area &area, const QString &id, const QString &title, QWidget *content)
{
    DockPanel *panel = m_manager.registerPanel(id, content, title);
    // A view can be dragged to another area, and there become one of its
    // tabs. It cannot be closed on its own, floated, or go among the documents.
    DockPolicy policy;
    policy.features = DockFeature::Movable | DockFeature::Tabbable | DockFeature::Maximizable;
    policy.allowedWorkspaces = {MainWorkspace};
    panel->setPolicy(policy);
    // Pushing the edge of its area far enough against it puts it away, and
    // it can be pulled back out of the edge it went to.
    panel->setCollapsible(true);
    area.views.append(id);
    return panel;
}

void Workbench::addViews()
{
    // The place of the documents: a workspace inside a panel that has no
    // header and none of the dock features, so it stays in the middle.
    auto *editorArea = new EditorArea;
    editorArea->setObjectName(u"editorArea"_s);
    m_editors = m_manager.createWorkspace(EditorWorkspace, editorArea);
    m_editors->setObjectName(EditorWorkspace);
    auto *areaLayout = new QVBoxLayout(editorArea);
    areaLayout->setContentsMargins(0, 0, 0, 0);
    areaLayout->addWidget(m_editors);
    editorArea->setMinimumSize(220, 120);
    DockPanel *center = m_manager.registerPanel(u"editor-area"_s, editorArea, u"Editors"_s);
    center->setFeatures({});
    center->setHeaderVisible(false);

    // --- Primary side bar: one view at a time, chosen in the activity bar.
    struct SideView
    {
        const char *id;
        const char *title;
        Glyph glyph;
        QWidget *content;
    };
    const QList<SideView> sideViews{
        {"explorer", "EXPLORER", Glyph::Files,
         makeExplorer([this](const QString &path, bool preview) { openFile(path, preview); })},
        {"search", "SEARCH", Glyph::Search, makeSearch()},
        {"scm", "SOURCE CONTROL", Glyph::SourceControl, makeSourceControl()},
        {"debug", "RUN AND DEBUG", Glyph::Debug, makeRunAndDebug()},
        {"extensions", "EXTENSIONS", Glyph::Extensions, makeExtensions()},
    };
    for (const SideView &view : sideViews) {
        const QString id = QString::fromLatin1(view.id);
        const QString title = QString::fromLatin1(view.title);
        DockPanel *panel = addView(m_sideBar, id, title, view.content);
        auto *menu = new QMenu(&m_window);
        menu->addAction(u"Hide Primary Side Bar"_s, [this] { setOpen(m_sideBar, false); });
        QList<QAction *> actions;
        if (id == "explorer"_L1) {
            actions << action(panel, Glyph::NewFile, u"New File..."_s)
                    << action(panel, Glyph::NewFolder, u"New Folder..."_s)
                    << action(panel, Glyph::Refresh, u"Refresh Explorer"_s)
                    << action(panel, Glyph::CollapseAll, u"Collapse Folders"_s);
        } else if (id != "debug"_L1) {
            actions << action(panel, Glyph::Refresh, u"Refresh"_s);
        }
        panel->setTitleActions(actions << menuAction(panel, menu));

        QToolButton *button = m_activityBar->addButton(view.glyph, title.at(0) + title.mid(1).toLower(),
                                                       true);
        QObject::connect(button, &QToolButton::clicked, &m_window, [this, id] { showSideView(id); });
        m_activityButtons.insert(id, button);
    }
    m_activityBar->addButton(Glyph::Account, u"Accounts"_s, false);
    m_activityBar->addButton(Glyph::Gear, u"Manage"_s, false);

    // --- Panel: views as tabs, with buttons of their own and two for the
    // panel as a whole.
    const auto addPanelView = [this](const QString &id, const QString &title, QWidget *content,
                                     const std::function<QList<QAction *>(DockPanel *)> &own) {
        DockPanel *panel = addView(m_panel, id, title, content);
        QAction *maximize = action(panel, Glyph::ChevronUp, u"Maximize Panel Size"_s, [this, id] {
            if (m_workspace->maximizedPanel().isEmpty())
                (void)m_manager.maximizePanel(id);
            else
                (void)m_manager.restoreMaximizedPanel();
        });
        m_maximizeActions.append(maximize);
        auto *menu = new QMenu(&m_window);
        menu->addAction(u"Move Panel Right"_s, [this, id] {
            (void)m_manager.moveTabGroup(id, m_workspace, DockArea::Right);
        });
        menu->addAction(u"Move Panel to Bottom"_s, [this, id] {
            (void)m_manager.moveTabGroup(id, u"editor-area"_s, DockArea::Bottom, -1, 0.3);
        });
        panel->setTitleActions(own(panel) << menuAction(panel, menu) << separator(panel) << maximize
                                          << action(panel, Glyph::Close, u"Hide Panel (Ctrl+J)"_s,
                                                    [this] { setOpen(m_panel, false); }));
    };
    addPanelView(u"problems"_s, u"PROBLEMS"_s,
                 makeMessage(u"No problems have been detected in the workspace."_s),
                 [this](DockPanel *panel) {
                     // Not a button: a widget of the application's own.
                     auto *filter = new QWidgetAction(panel);
                     auto *edit = new QLineEdit(&m_window);
                     edit->setObjectName(u"panelFilter"_s);
                     edit->setPlaceholderText(u"Filter (e.g. text)"_s);
                     edit->setMinimumWidth(70);
                     filter->setDefaultWidget(edit);
                     return QList<QAction *>{filter, action(panel, Glyph::Filter, u"More Filters..."_s),
                                             action(panel, Glyph::CollapseAll, u"Collapse All"_s)};
                 });
    addPanelView(u"output"_s, u"OUTPUT"_s,
                 makeConsole(u"[cmake] Configuring done\n[cmake] Generating done\n"
                             "[build] ninja: no work to do.\n[build] Build finished with exit code 0\n"_s),
                 [this](DockPanel *panel) {
                     return QList<QAction *>{action(panel, Glyph::Trash, u"Clear Output"_s)};
                 });
    addPanelView(u"debug-console"_s, u"DEBUG CONSOLE"_s,
                 makeMessage(u"Start a debug session to evaluate expressions."_s),
                 [](DockPanel *) { return QList<QAction *>(); });
    addPanelView(u"terminal"_s, u"TERMINAL"_s,
                 makeConsole(u"~/Documents/Docking $ cmake --build build\n"
                             "ninja: no work to do.\n~/Documents/Docking $ "_s),
                 [this](DockPanel *panel) {
                     return QList<QAction *>{action(panel, Glyph::Plus, u"New Terminal"_s),
                                             action(panel, Glyph::Split, u"Split Terminal"_s),
                                             action(panel, Glyph::Trash, u"Kill Terminal"_s)};
                 });
    addPanelView(u"ports"_s, u"PORTS"_s,
                 makeMessage(u"No forwarded ports. Forward a port to access your locally running "
                             "services over the internet."_s),
                 [](DockPanel *) { return QList<QAction *>(); });

    // --- Secondary side bar.
    const auto addAuxView = [this](const QString &id, const QString &title, QWidget *content,
                                   Glyph first, const QString &firstText) {
        DockPanel *panel = addView(m_auxBar, id, title, content);
        auto *menu = new QMenu(&m_window);
        menu->addAction(u"Move Secondary Side Bar Left"_s, [this, id] {
            (void)m_manager.moveTabGroup(id, m_workspace, DockArea::Left);
        });
        menu->addAction(u"Move Secondary Side Bar Right"_s, [this, id] {
            (void)m_manager.moveTabGroup(id, m_workspace, DockArea::Right);
        });
        panel->setTitleActions({action(panel, first, firstText), menuAction(panel, menu),
                                separator(panel),
                                action(panel, Glyph::Close, u"Hide Secondary Side Bar (Ctrl+Alt+B)"_s,
                                       [this] { setOpen(m_auxBar, false); })});
    };
    addAuxView(u"chat"_s, u"CHAT"_s, makeChat(), Glyph::Plus, u"New Chat"_s);
    auto *outline = new QTreeWidget;
    outline->setHeaderHidden(true);
    outline->setIndentation(12);
    for (const char *name : {"Node", "Layout"}) {
        auto *item = new QTreeWidgetItem(outline, {QString::fromLatin1(name)});
        item->setSizeHint(0, QSize(0, 22));
        for (const char *member : {"find", "insert", "remove", "normalize"}) {
            auto *child = new QTreeWidgetItem(item, {QString::fromLatin1(member)});
            child->setSizeHint(0, QSize(0, 22));
        }
    }
    outline->expandAll();
    addAuxView(u"outline"_s, u"OUTLINE"_s, outline, Glyph::CollapseAll, u"Collapse All"_s);
    addAuxView(u"timeline"_s, u"TIMELINE"_s,
               plainList({u"File Saved    now"_s, u"File Saved    2 min"_s, u"File Created    1 hr"_s}),
               Glyph::Refresh, u"Refresh"_s);
}

void Workbench::buildLayout()
{
    (void)m_workspace->addPanel(u"editor-area"_s, DockArea::Center);
    (void)m_workspace->addPanel(u"explorer"_s, DockArea::Left, 0.2);
    (void)m_workspace->addPanel(m_auxBar.views.constFirst(), DockArea::Right, 0.24);
    for (const PanelId &id : m_auxBar.views.mid(1))
        (void)m_manager.movePanel(id, m_auxBar.views.constFirst(), DockArea::Center);
    (void)m_manager.movePanel(m_panel.views.constFirst(), u"editor-area"_s, DockArea::Bottom, -1, 0.3);
    for (const PanelId &id : m_panel.views.mid(1))
        (void)m_manager.movePanel(id, m_panel.views.constFirst(), DockArea::Center);
    (void)m_manager.raisePanel(m_panel.views.constFirst());
    (void)m_manager.raisePanel(m_auxBar.views.constFirst());

    // As the application starts: the secondary side bar is put away.
    setOpen(m_auxBar, false);
    openFile(u"src/core/Layout.h"_s, false);
    openFile(u"src/CMakeLists.txt"_s, false);
    m_manager.saveDefaultLayout();
    m_manager.clearUndoHistory();
}

// --- Areas -------------------------------------------------------------------------------

bool Workbench::isOpen(const Area &area) const
{
    return std::any_of(area.views.begin(), area.views.end(),
                       [this](const PanelId &id) { return m_manager.panel(id)->isOpen(); });
}

void Workbench::setOpen(Area &area, bool open)
{
    if (open == isOpen(area))
        return;
    // As one change each way. Panels that are closed together come back
    // together: where they were, as wide as they were, in the same order.
    // The same goes for the user pulling them out of the edge they went to.
    if (!open) {
        (void)m_manager.closePanels(area.views);
        return;
    }
    const QStringList back = area.lastOpen.isEmpty() ? area.views : area.lastOpen;
    (void)m_manager.openPanels(back);
    (void)m_manager.activatePanel(m_manager.currentPanel(back.constFirst()));
}

// An area is also put away by the user pushing its boundary against it, so
// what was in it is noted as the layout changes, not when it is put away.
void Workbench::trackAreas()
{
    for (Area *area : {&m_sideBar, &m_panel, &m_auxBar}) {
        QStringList open;
        for (const PanelId &id : std::as_const(area->views)) {
            if (m_manager.panel(id)->isOpen())
                open.append(id);
        }
        if (!open.isEmpty())
            area->lastOpen = open;
    }
}

// The side bar shows one view at a time. Another one takes the place of the
// one that is there; the one that is there, asked for again, is put away.
void Workbench::showSideView(const PanelId &id)
{
    const auto shown = [this]() -> PanelId {
        for (const PanelId &view : std::as_const(m_sideBar.views)) {
            if (m_manager.panel(view)->isOpen())
                return view;
        }
        return {};
    };
    if (shown() == id) {
        setOpen(m_sideBar, false);
        return;
    }
    if (shown().isEmpty())
        setOpen(m_sideBar, true);
    const PanelId current = shown();
    if (current != id) {
        (void)m_manager.movePanel(id, current, DockArea::Center);
        (void)m_manager.closePanel(current);
    }
    (void)m_manager.activatePanel(id);
}

void Workbench::showView(Area &area, const PanelId &id)
{
    setOpen(area, true);
    (void)m_manager.openPanel(id);
}

// --- Documents ---------------------------------------------------------------------------

void Workbench::openFile(const QString &path, bool preview)
{
    const PanelId id = FilePrefix + path;
    DockPanel *panel = m_manager.panel(id);
    if (!panel) {
        const auto file = std::find_if(m_files.cbegin(), m_files.cend(),
                                       [&path](const File &f) { return f.path == path; });
        if (file == m_files.cend())
            return;
        CodeEditor *editor = nullptr;
        panel = m_manager.registerPanel(id, makeEditorPane(*file, &editor),
                                        path.mid(path.lastIndexOf(u'/') + 1));
        panel->setIcon(fileIcon(path));
        panel->setToolTip(path);
        // Documents stay among documents, in the middle. They cannot float
        // either, so a dragged tab is a picture of the tab and nothing more.
        DockPolicy policy;
        policy.features = DockFeature::Movable | DockFeature::Closable | DockFeature::Tabbable
            | DockFeature::Maximizable;
        policy.allowedWorkspaces = {EditorWorkspace};
        panel->setPolicy(policy);

        auto *menu = new QMenu(&m_window);
        menu->addAction(u"Close All"_s, [this, id] { closeEditors(id, false); });
        menu->addAction(u"Close Others"_s, [this, id] { closeEditors(id, true); });
        menu->addSeparator();
        menu->addAction(u"Keep Open"_s, [panel] { panel->setPreviewTab(false); });
        panel->setTitleActions({action(panel, Glyph::Split, u"Split Editor Right"_s,
                                       [this, id] { splitEditor(id); }),
                                menuAction(panel, menu)});

        m_codeEditors.insert(id, editor);
        // Typing marks the tab, and makes a preview stay.
        QObject::connect(editor->document(), &QTextDocument::modificationChanged, panel,
                         [panel](bool modified) {
                             panel->setDirty(modified);
                             if (modified)
                                 panel->setPreviewTab(false);
                         });
        QObject::connect(editor, &QPlainTextEdit::cursorPositionChanged, panel,
                         [this] { syncStatus(); });
    }

    if (!panel->isOpen()) {
        // Into the group the user worked in last.
        (void)m_editors->addPanel(id);
        panel->setPreviewTab(preview);
        if (preview) {
            // There is one preview; the next file looked at replaces it.
            DockPanel *previous = m_manager.panel(m_preview);
            if (previous && previous != panel && previous->isPreviewTab())
                (void)m_manager.closePanel(m_preview);
            m_preview = id;
        }
    } else if (!preview) {
        panel->setPreviewTab(false);
    }
    (void)m_manager.activatePanel(id);
}

// The document worked in last, also while a view has the focus.
PanelId Workbench::currentDocument()
{
    const DockPanel *active = m_manager.activePanel();
    if (active && m_codeEditors.contains(active->id()))
        m_document = active->id();
    const DockPanel *document = m_manager.panel(m_document);
    if (!document || !document->isOpen()) {
        // It was closed: whatever is in front in the middle now.
        m_document = m_manager.currentPanel(m_editors->panels().value(0));
    }
    return m_document;
}

void Workbench::closeDocument()
{
    const PanelId id = currentDocument();
    if (id.isEmpty())
        return;
    QStringList others = m_manager.tabGroupPanels(id);
    others.removeAll(id);
    (void)m_manager.closePanel(id);
    // The tab that comes to the front in its place is the one worked in now.
    if (!others.isEmpty()) {
        (void)m_manager.activatePanel(m_manager.currentPanel(others.constFirst()));
    } else if (const PanelId next = currentDocument(); !next.isEmpty()) {
        (void)m_manager.activatePanel(next);
    }
}

// One tab of several moves into a group of its own, right of the others.
void Workbench::splitEditor(const PanelId &id)
{
    const QStringList group = m_manager.tabGroupPanels(id);
    if (group.size() < 2)
        return;
    const PanelId other = group.constFirst() == id ? group.at(1) : group.constFirst();
    (void)m_manager.movePanel(id, other, DockArea::Right);
    (void)m_manager.activatePanel(id);
}

void Workbench::closeEditors(const PanelId &id, bool others)
{
    const QStringList group = m_manager.tabGroupPanels(id);
    for (const PanelId &panel : group) {
        if (!others || panel != id)
            (void)m_manager.closePanel(panel);
    }
}

// --- The chrome follows the layout ------------------------------------------------------

void Workbench::syncChrome()
{
    for (auto it = m_activityButtons.cbegin(); it != m_activityButtons.cend(); ++it)
        it.value()->setChecked(m_manager.panel(it.key())->isOpen());
    m_sideBarButton->setChecked(isOpen(m_sideBar));
    m_panelButton->setChecked(isOpen(m_panel));
    m_auxBarButton->setChecked(isOpen(m_auxBar));
    const bool maximized = !m_workspace->maximizedPanel().isEmpty();
    for (QAction *maximize : std::as_const(m_maximizeActions)) {
        maximize->setIcon(icon(maximized ? Glyph::ChevronDown : Glyph::ChevronUp));
        maximize->setToolTip(maximized ? u"Restore Panel Size"_s : u"Maximize Panel Size"_s);
    }
}

void Workbench::syncStatus()
{
    if (!m_position)
        return;
    const CodeEditor *editor = m_codeEditors.value(currentDocument());
    m_position->setVisible(editor != nullptr);
    m_language->setVisible(editor != nullptr);
    if (!editor)
        return;
    const QTextCursor cursor = editor->textCursor();
    m_position->setText(u"Ln %1, Col %2"_s.arg(cursor.blockNumber() + 1)
                            .arg(cursor.positionInBlock() + 1));
    const QString path = m_document;
    m_language->setText(path.endsWith(".txt"_L1)  ? u"CMake"_s
                        : path.endsWith(".md"_L1) ? u"Markdown"_s
                        : path.endsWith(".h"_L1) || path.endsWith(".cpp"_L1) ? u"C++"_s
                                                                             : u"Plain Text"_s);
}

void Workbench::addStatusBar()
{
    m_statusBar->add({}, false, icon(Glyph::Remote, TextBright))->setObjectName(u"remote"_s);
    m_statusBar->add(u"main"_s, false, icon(Glyph::Branch));
    m_statusBar->add({}, false, icon(Glyph::Sync));
    m_statusBar->add(u"0"_s, false, icon(Glyph::Error));
    m_statusBar->add(u"0"_s, false, icon(Glyph::Warning));
    m_position = m_statusBar->add(u"Ln 1, Col 1"_s, true);
    m_statusBar->add(u"Spaces: 4"_s, true);
    m_statusBar->add(u"UTF-8"_s, true);
    m_statusBar->add(u"LF"_s, true);
    m_language = m_statusBar->add(u"C++"_s, true);
    m_statusBar->add({}, true, icon(Glyph::Bell));
}

// --- Menus and commands ------------------------------------------------------------------

void Workbench::addMenus()
{
    m_sideBarButton = m_titleBar->addLayoutButton(Glyph::SideBarLeftOff, Glyph::SideBarLeft,
                                                  u"Toggle Primary Side Bar (Ctrl+B)"_s);
    m_panelButton = m_titleBar->addLayoutButton(Glyph::PanelBottomOff, Glyph::PanelBottom,
                                                u"Toggle Panel (Ctrl+J)"_s);
    m_auxBarButton = m_titleBar->addLayoutButton(Glyph::SideBarRightOff, Glyph::SideBarRight,
                                                 u"Toggle Secondary Side Bar (Ctrl+Alt+B)"_s);
    QObject::connect(m_sideBarButton, &QToolButton::clicked, &m_window, [this] { toggle(m_sideBar); });
    QObject::connect(m_panelButton, &QToolButton::clicked, &m_window, [this] { toggle(m_panel); });
    QObject::connect(m_auxBarButton, &QToolButton::clicked, &m_window, [this] { toggle(m_auxBar); });

    QMenuBar *menus = m_titleBar->menuBar();
    const auto add = [](QMenu *menu, const QString &text, const QKeySequence &shortcut,
                        std::function<void()> run) {
        QAction *a = menu->addAction(text);
        a->setShortcut(shortcut);
        if (run)
            QObject::connect(a, &QAction::triggered, menu, std::move(run));
        else
            a->setEnabled(false);
        return a;
    };

    QMenu *file = menus->addMenu(u"&File"_s);
    add(file, u"New Text File"_s, QKeySequence::New, {});
    add(file, u"Open File..."_s, QKeySequence::Open, [this] { m_palette->open(); });
    file->addSeparator();
    add(file, u"Save"_s, QKeySequence::Save, [this] {
        if (CodeEditor *editor = m_codeEditors.value(currentDocument()))
            editor->document()->setModified(false);
    });
    add(file, u"Close Editor"_s, QKeySequence(Qt::CTRL | Qt::Key_W), [this] { closeDocument(); });
    file->addSeparator();
    add(file, u"Exit"_s, QKeySequence::Quit, [this] { m_window.close(); });

    QMenu *edit = menus->addMenu(u"&Edit"_s);
    add(edit, u"Undo Layout Change"_s, {}, [this] { (void)m_manager.undo(); });
    add(edit, u"Redo Layout Change"_s, {}, [this] { (void)m_manager.redo(); });

    QMenu *selection = menus->addMenu(u"&Selection"_s);
    add(selection, u"Select All"_s, {}, {});
    add(selection, u"Expand Selection"_s, {}, {});

    QMenu *view = menus->addMenu(u"&View"_s);
    add(view, u"Command Palette..."_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),
        [this] { m_palette->open(u"> "_s); });
    view->addSeparator();
    QMenu *appearance = view->addMenu(u"Appearance"_s);
    add(appearance, u"Primary Side Bar"_s, QKeySequence(Qt::CTRL | Qt::Key_B),
        [this] { toggle(m_sideBar); });
    add(appearance, u"Secondary Side Bar"_s, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B),
        [this] { toggle(m_auxBar); });
    add(appearance, u"Panel"_s, QKeySequence(Qt::CTRL | Qt::Key_J), [this] { toggle(m_panel); });
    appearance->addSeparator();
    add(appearance, u"Reset Layout"_s, {}, [this] { (void)m_manager.resetLayout(); });
    view->addSeparator();
    const QList<Qt::Key> keys{Qt::Key_E, Qt::Key_F, Qt::Key_G, Qt::Key_D, Qt::Key_X};
    for (int i = 0; i < m_sideBar.views.size(); ++i) {
        const PanelId id = m_sideBar.views.at(i);
        const QString title = m_manager.panel(id)->title();
        add(view, title.at(0) + title.mid(1).toLower(),
            QKeySequence(Qt::CTRL | Qt::SHIFT | keys.at(i)), [this, id] {
                if (!m_manager.panel(id)->isOpen())
                    showSideView(id);
            });
    }
    view->addSeparator();
    add(view, u"Problems"_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M),
        [this] { showView(m_panel, u"problems"_s); });
    add(view, u"Output"_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U),
        [this] { showView(m_panel, u"output"_s); });
    add(view, u"Terminal"_s, QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft),
        [this] { showView(m_panel, u"terminal"_s); });

    QMenu *go = menus->addMenu(u"&Go"_s);
    add(go, u"Go to File..."_s, QKeySequence(Qt::CTRL | Qt::Key_P), [this] { m_palette->open(); });
    add(go, u"Back"_s, {}, {});
    add(go, u"Forward"_s, {}, {});

    QMenu *help = menus->addMenu(u"&Help"_s);
    add(help, u"About Qt"_s, {}, [] { QApplication::aboutQt(); });
}

QList<CommandPalette::Entry> Workbench::paletteEntries()
{
    QList<CommandPalette::Entry> entries;
    for (const File &file : std::as_const(m_files)) {
        const qsizetype slash = file.path.lastIndexOf(u'/');
        const QString path = file.path;
        entries.append({path.mid(slash + 1), slash < 0 ? QString() : path.left(slash),
                        fileIcon(path), [this, path] { openFile(path, false); }, false});
    }
    const auto command = [&entries](const QString &text, std::function<void()> run) {
        entries.append({text, {}, {}, std::move(run), true});
    };
    command(u"View: Toggle Primary Side Bar Visibility"_s, [this] { toggle(m_sideBar); });
    command(u"View: Toggle Panel Visibility"_s, [this] { toggle(m_panel); });
    command(u"View: Toggle Secondary Side Bar Visibility"_s, [this] { toggle(m_auxBar); });
    command(u"View: Reset Layout"_s, [this] { (void)m_manager.resetLayout(); });
    for (const PanelId &id : std::as_const(m_sideBar.views)) {
        const QString title = m_manager.panel(id)->title();
        command(u"View: Show "_s + title.at(0) + title.mid(1).toLower(), [this, id] {
            if (!m_manager.panel(id)->isOpen())
                showSideView(id);
        });
    }
    return entries;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"QFlexDock VS Code-Style Example"_s);
    // One base style everywhere, so the style sheet has the same starting point.
    QApplication::setStyle(new Style);
    QApplication::setPalette(palette());
    QFont font = QApplication::font();
    font.setPixelSize(13);
    QApplication::setFont(font);
    app.setStyleSheet(styleSheet());

    Workbench workbench;
    workbench.show();
    return app.exec();
}
