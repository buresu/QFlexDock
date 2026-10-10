// SPDX-License-Identifier: MIT
//
// A browser in the manner of Chrome, as far as its windows go: every window is
// a row of tabs, a tab is dragged from one window into the row of another, and
// a tab let go of anywhere else becomes a window of its own. There is no main
// window. The application is its floating windows, each gone with its last tab.
//
// The pages are mock-ups: what matters here is how the windows behave and look.
#include "Page.h"
#include "Style.h"

#include <QFlexDock/DockManager.h>

#include <QtGui/QScreen>
#include <QtGui/QShortcut>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>

using namespace QFlexDock;
using namespace ChromeStyle;

namespace {

/// A menu with the round corners the style sheet gives it.
void roundCorners(QMenu *menu)
{
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setWindowFlag(Qt::FramelessWindowHint);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint);
}

QWidget *windowOf(const DockPanel *panel)
{
    return panel && panel->isOpen() && panel->widget() ? panel->widget()->window() : nullptr;
}

} // namespace

class Browser : public QObject
{
public:
    Browser()
    {
        // Windows without a title row: the tabs are at the very top, and the
        // row they are in is what a window is moved and maximized by.
        m_manager.setFloatingWindowFrame(DockManager::FloatingWindowFrame::Minimal);
        m_manager.setFloatsOnOutsideDrop(true);
        m_manager.setUndoLimit(0); // a closed tab is gone for good

        DockTheme theme;
        theme.titleButtons = {};
        theme.iconSize = 16;
        theme.tabWidth = TabWidth;
        theme.tabOverflow = DockTabOverflow::Shrink;
        theme.floatingBorderWidth = 1;
        theme.floatingCornerRadius = WindowRadius;
        m_manager.setTheme(theme);
        m_manager.setOverlayPainter(std::make_shared<OverlayPainter>());
        // A tab goes among the tabs of a window and nowhere else: only the
        // row of tabs takes one, and each tab allows DockArea::Center only,
        // so that a window is never split. Let go of anywhere else, a tab
        // becomes a window.
        m_manager.setCenterDropEnabled(false);
        // A dragged tab is out of its row at once, and a row it is held over
        // makes room for it.
        m_manager.setTabDragPreviewEnabled(true);

        connect(&m_manager, &DockManager::panelOpenChanged, this, [this](DockPanel *panel, bool open) {
            if (!open)
                discardLater(panel->id());
        });
        connect(&m_manager, &DockManager::panelAboutToBeUnregistered, this,
                [this](DockPanel *panel) { m_maximize.remove(panel->id()); });
        connect(&m_manager, &DockManager::panelWindowChanged, this, &Browser::updateWindowButtons);
        connect(&m_manager, &DockManager::panelContextMenuRequested, this, &Browser::fillTabMenu);
        qApp->installEventFilter(this);

        const auto shortcut = [this](const QKeySequence &keys, auto &&run) {
            auto *s = new QShortcut(keys, this);
            s->setContext(Qt::ApplicationShortcut);
            connect(s, &QShortcut::activated, this, std::forward<decltype(run)>(run));
        };
        shortcut(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { newTab(currentTab()); });
        shortcut(QKeySequence(Qt::CTRL | Qt::Key_N), [this] { newWindow(); });
        shortcut(QKeySequence(Qt::CTRL | Qt::Key_W), [this] { closeTab(currentTab()); });
        shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W), [this] { closeWindow(currentTab()); });
        shortcut(QKeySequence(Qt::CTRL | Qt::Key_L), [this] {
            if (BrowserTab *tab = contentOf(currentTab()))
                tab->focusAddress();
        });
    }

    void start()
    {
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        const QSize size(qMin(1240, screen.width() - 120), qMin(820, screen.height() - 100));
        // Like the browser when it is started: one window with one new tab.
        (void)newWindow(QRect(screen.center() - QPoint(size.width() / 2, size.height() / 2), size));
    }

private:
    // --- Tabs and windows -----------------------------------------------------

    PanelId createTab()
    {
        const PanelId id = u"tab-%1"_s.arg(++m_count);
        auto *content = new BrowserTab;
        DockPanel *panel = m_manager.registerPanel(id, content, u"New Tab"_s);
        panel->setIcon(icon(Glyph::Page, TextMuted));
        DockPolicy policy;
        policy.features = DockFeature::Movable | DockFeature::Closable | DockFeature::Floatable
            | DockFeature::Tabbable;
        policy.allowedAreas = DockArea::Center;
        panel->setPolicy(policy);
        content->pageChanged = [panel](const QString &title, const QIcon &mark) {
            panel->setTitle(title);
            panel->setIcon(mark);
        };
        content->menuButton()->setMenu(mainMenu(id, content));

        // What the row of tabs holds besides the tabs. The actions are the
        // tab's own: whichever tab is in front brings them to its window.
        const auto action = [panel](Glyph glyph, const QString &text, const char *name) {
            auto *a = new QAction(icon(glyph), text, panel);
            a->setObjectName(QLatin1String(name));
            return a;
        };
        QAction *search = action(Glyph::ChevronDown, u"Search tabs"_s, "tabSearch");
        search->setMenu(tabListMenu(id, content));
        panel->setTitleActions({search}, DockTitlePlace::Start);

        QAction *add = action(Glyph::Plus, u"New tab"_s, "newTab");
        connect(add, &QAction::triggered, this, [this, id] { newTab(id); });
        panel->setTitleActions({add}, DockTitlePlace::AfterTabs);

        QAction *minimize = action(Glyph::Minimize, u"Minimize"_s, "windowMinimize");
        QAction *maximize = action(Glyph::Maximize, u"Maximize"_s, "windowMaximize");
        QAction *close = action(Glyph::Close, u"Close"_s, "windowClose");
        connect(minimize, &QAction::triggered, this, [panel] {
            if (QWidget *window = windowOf(panel))
                window->showMinimized();
        });
        connect(maximize, &QAction::triggered, this, [panel] {
            if (QWidget *window = windowOf(panel))
                window->isMaximized() ? window->showNormal() : window->showMaximized();
        });
        connect(close, &QAction::triggered, this, [this, id] { closeWindow(id); });
        panel->setTitleActions({minimize, maximize, close});
        m_maximize.insert(id, maximize);
        return id;
    }

    /// A new tab in the window of `beside`, at `index` (-1: last), or in a
    /// window of its own when there is none.
    PanelId newTab(const PanelId &beside, int index = -1)
    {
        if (!m_manager.hasPanel(beside) || !m_manager.panel(beside)->isOpen())
            return newWindow();
        const PanelId id = createTab();
        (void)m_manager.movePanel(id, beside, DockArea::Center, index);
        (void)m_manager.activatePanel(id);
        contentOf(id)->focusAddress(); // a new tab is for typing where to go
        return id;
    }

    PanelId newWindow(QRect geometry = {})
    {
        if (!geometry.isValid())
            geometry = besideCurrentWindow();
        const PanelId id = createTab();
        (void)m_manager.floatPanel(id, geometry);
        (void)m_manager.activatePanel(id);
        contentOf(id)->focusAddress();
        return id;
    }

    void closeTab(const PanelId &id) { (void)m_manager.closePanel(id); }

    void closeWindow(const PanelId &anyTab)
    {
        if (QWidget *window = windowOf(m_manager.panel(anyTab)))
            window->close();
    }

    /// Closed tabs are not kept. With the last of them the application ends.
    void discardLater(const PanelId &id)
    {
        QMetaObject::invokeMethod(this, [this, id] {
            const DockPanel *panel = m_manager.panel(id);
            if (panel && !panel->isOpen())
                (void)m_manager.unregisterPanel(id, DockManager::PlacementMemory::Forget);
            if (m_manager.panels().isEmpty())
                QCoreApplication::quit();
        }, Qt::QueuedConnection);
    }

    /// The tab the user is at: the active one, or the one in front in the
    /// active window.
    [[nodiscard]] PanelId currentTab() const
    {
        const DockPanel *active = m_manager.activePanel();
        if (active && active->isOpen())
            return active->id();
        const auto panels = m_manager.panels();
        for (const DockPanel *panel : panels) {
            if (panel->isOpen() && !panel->widget()->isHidden())
                return panel->id();
        }
        return {};
    }

    [[nodiscard]] BrowserTab *contentOf(const PanelId &id) const
    {
        const DockPanel *panel = m_manager.panel(id);
        return panel ? static_cast<BrowserTab *>(panel->widget()) : nullptr;
    }

    /// Where a new window goes: a little down and to the right of the one in use.
    [[nodiscard]] QRect besideCurrentWindow() const
    {
        if (const QWidget *window = windowOf(m_manager.panel(currentTab())))
            return window->normalGeometry().translated(36, 36);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        return QRect(screen.topLeft() + QPoint(80, 60), QSize(1100, 760).boundedTo(screen.size()));
    }

    // --- The buttons of the window ----------------------------------------------

    void updateWindowButtons()
    {
        for (auto it = m_maximize.cbegin(); it != m_maximize.cend(); ++it) {
            const QWidget *window = windowOf(m_manager.panel(it.key()));
            const bool maximized = window && window->isMaximized();
            it.value()->setIcon(icon(maximized ? Glyph::Restore : Glyph::Maximize));
            it.value()->setText(maximized ? u"Restore"_s : u"Maximize"_s);
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::WindowStateChange && watched->isWidgetType()
            && static_cast<QWidget *>(watched)->isWindow()) {
            QMetaObject::invokeMethod(this, &Browser::updateWindowButtons, Qt::QueuedConnection);
        }
        return QObject::eventFilter(watched, event);
    }

    // --- Menus --------------------------------------------------------------------

    /// The tabs of the window, behind the button at the start of the row.
    QMenu *tabListMenu(const PanelId &id, QWidget *parent)
    {
        auto *menu = new QMenu(parent);
        roundCorners(menu);
        menu->addAction(u"Open tabs"_s)->setEnabled(false);
        connect(menu, &QMenu::aboutToShow, this, [this, menu, id] {
            menu->clear();
            menu->addAction(u"Open tabs"_s)->setEnabled(false);
            const QStringList tabs = m_manager.tabGroupPanels(id);
            for (const PanelId &tab : tabs) {
                const DockPanel *p = m_manager.panel(tab);
                QAction *entry = menu->addAction(p->icon(), p->title());
                entry->setCheckable(true);
                entry->setChecked(tab == id);
                connect(entry, &QAction::triggered, this,
                        [this, tab] { (void)m_manager.activatePanel(tab); });
            }
        });
        return menu;
    }

    /// The menu behind the three dots. Only what opens tabs and windows does
    /// something.
    QMenu *mainMenu(const PanelId &id, QWidget *parent)
    {
        auto *menu = new QMenu(parent);
        roundCorners(menu);
        const auto add = [this, menu](Glyph glyph, const QString &text, const QKeySequence &keys = {},
                                      std::function<void()> run = {}) {
            // The keys are shown only; those that work are set up in the constructor.
            QAction *a = menu->addAction(
                icon(glyph, TextMuted),
                keys.isEmpty() ? text : text + u'\t' + keys.toString(QKeySequence::NativeText));
            if (run)
                connect(a, &QAction::triggered, this, run);
            return a;
        };
        add(Glyph::Tab, u"New tab"_s, QKeySequence(Qt::CTRL | Qt::Key_T), [this, id] { newTab(id); });
        add(Glyph::Window, u"New window"_s, QKeySequence(Qt::CTRL | Qt::Key_N), [this] { newWindow(); });
        add(Glyph::Hat, u"New Incognito window"_s, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
        menu->addSeparator();
        add(Glyph::Key, u"Passwords and autofill"_s);
        add(Glyph::Clock, u"History"_s);
        add(Glyph::Download, u"Downloads"_s, QKeySequence(Qt::CTRL | Qt::Key_J));
        add(Glyph::Star, u"Bookmarks and lists"_s);
        add(Glyph::Apps, u"Tab groups"_s);
        add(Glyph::Extension, u"Extensions"_s);
        add(Glyph::Trash, u"Delete browsing data..."_s,
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete));
        menu->addSeparator();
        add(Glyph::Zoom, u"Zoom    100%"_s);
        menu->addSeparator();
        add(Glyph::Print, u"Print..."_s, QKeySequence(Qt::CTRL | Qt::Key_P));
        add(Glyph::Search, u"Find and edit"_s);
        add(Glyph::Gear, u"More tools"_s);
        menu->addSeparator();
        add(Glyph::Help, u"Help"_s);
        add(Glyph::Gear, u"Settings"_s);
        add(Glyph::Close, u"Exit"_s, {}, [] { QApplication::closeAllWindows(); });
        return menu;
    }

    /// The menu of a tab. The library's own entries (float, dock, maximize)
    /// are about panels in general; a browser has others.
    void fillTabMenu(DockPanel *panel, QMenu *menu)
    {
        const PanelId id = panel->id();
        const QStringList tabs = m_manager.tabGroupPanels(id);
        const int index = int(tabs.indexOf(id));
        menu->clear();
        roundCorners(menu);
        const auto add = [this, menu](const QString &text, bool enabled, auto &&run) {
            QAction *a = menu->addAction(text);
            a->setEnabled(enabled);
            connect(a, &QAction::triggered, this, std::forward<decltype(run)>(run));
        };
        add(u"New tab to the right"_s, true, [this, id, index] { newTab(id, index + 1); });
        add(u"Move tab to new window"_s, tabs.size() > 1, [this, id] {
            (void)m_manager.floatPanel(id, besideCurrentWindow());
            (void)m_manager.activatePanel(id);
        });
        menu->addSeparator();
        add(u"Reload"_s, true, [] {});
        add(u"Duplicate"_s, true, [this, id, index] {
            const BrowserTab *original = contentOf(id);
            const bool site = original->showsSite();
            const Site shown = original->site();
            const PanelId copy = newTab(id, index + 1);
            if (site)
                contentOf(copy)->open(shown);
        });
        menu->addSeparator();
        add(u"Close"_s, true, [this, id] { closeTab(id); });
        add(u"Close other tabs"_s, tabs.size() > 1, [this, id, tabs] {
            QStringList others = tabs;
            others.removeAll(id);
            (void)m_manager.closePanels(others);
        });
        add(u"Close tabs to the right"_s, index + 1 < tabs.size(),
            [this, tabs, index] { (void)m_manager.closePanels(tabs.mid(index + 1)); });
    }

    DockManager m_manager;
    QHash<PanelId, QAction *> m_maximize;
    int m_count = 0;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"qflexdock-chrome-style"_s);
    QApplication::setApplicationDisplayName(u"QFlexDock Chrome style"_s);
    QApplication::setStyle(new Style);
    QApplication::setPalette(palette());
    QFont font = QApplication::font();
    font.setPixelSize(13);
    QApplication::setFont(font);
    app.setStyleSheet(styleSheet());

    Browser browser;
    browser.start();
    return QApplication::exec();
}
