// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include "widgets/DockFloatingWindow.h"

#include "core/DockDragController.h"
#include "widgets/DockAutoHide.h"
#include "widgets/DockColumn.h"
#include "widgets/DockDropOverlay.h"
#include "widgets/DockSplitHandle.h"
#include "widgets/DockTabBar.h"

#include <QtCore/QMimeData>
#include <QtGui/QPainter>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QApplication>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QToolButton>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

/// A host style with its own idea of how wide a splitter handle is.
class WideSplitterStyle : public QProxyStyle
{
public:
    explicit WideSplitterStyle(int width)
        : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
        , m_width(width)
    {
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        return metric == PM_SplitterWidth ? m_width : QProxyStyle::pixelMetric(metric, option, widget);
    }

private:
    int m_width;
};

class RecordingPainter : public DockOverlayPainter
{
public:
    void paint(QPainter *painter, const DockOverlayScene &scene, const DockOverlayStyle &style) override
    {
        ++calls;
        lastScene = scene;
        lastStyle = style;
        painter->fillRect(scene.bounds, Qt::magenta);
    }
    int calls = 0;
    DockOverlayScene lastScene;
    DockOverlayStyle lastStyle;
};

QColor pixel(QWidget *widget, const QPoint &pos)
{
    return picture(widget).pixelColor(pos);
}

/// The colour a widget's pixels are mostly drawn in, ignoring transparency.
bool containsColor(const QImage &image, const QColor &color, int tolerance = 12)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.alpha() > 200 && qAbs(c.red() - color.red()) <= tolerance
                && qAbs(c.green() - color.green()) <= tolerance
                && qAbs(c.blue() - color.blue()) <= tolerance) {
                return true;
            }
        }
    }
    return false;
}

void buildLayout(TwoWindows &f)
{
    QVERIFY(f.a->addPanel(p("a")));
    QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
    QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
    QVERIFY(f.manager.movePanel(p("d"), p("a"), DockArea::Center));
}

/// Shows the drop guide over the group of `panel`, as a hovering drag would.
void showGuide(TwoWindows &f, DockAreaWidget *area, const char *dragged, const char *over)
{
    DockDragController *controller = priv(f.manager)->drag;
    controller->cancel();
    QVERIFY(controller->begin(p(dragged), false));
    const QPoint pos = area->groupOfPanel(p(over))->geometry().center();
    area->showOverlay(area->candidateAt(pos, *controller->session()));
    QVERIFY(area->overlay()->isVisible());
}

} // namespace

class tst_Style : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_style = QApplication::style()->objectName();
        m_palette = QApplication::palette();
        m_font = QApplication::font();
    }

    // Every test leaves the application looking as it found it.
    void cleanup()
    {
        qApp->setStyleSheet(QString());
        QApplication::setStyle(m_style);
        QApplication::setPalette(m_palette);
        QApplication::setFont(m_font);
    }

    void usesTheHostStyleByDefault()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        DockAreaWidget *area = areaOf(f.a);
        // Metrics come from the application style, not from constants of ours.
        QCOMPARE(area->handleWidth(),
                 QApplication::style()->pixelMetric(QStyle::PM_SplitterWidth, nullptr, area));
        DockTabGroup *group = area->groupOfPanel(p("a"));
        QCOMPARE(group->style(), QApplication::style());
        QCOMPARE(group->tabBar()->font(), QApplication::font("QTabBar"));
        QCOMPARE(group->palette(), QApplication::palette(group));
        // The widgets are ordinary Qt classes a style or style sheet recognises.
        QVERIFY(qobject_cast<QTabBar *>(group->tabBar()));
        QVERIFY(qobject_cast<QFrame *>(group));
        QCOMPARE(QLatin1String(group->metaObject()->className()),
                 QLatin1String("QFlexDock::DockTabGroup"));
    }

    void followsAStyleChangeAtRuntime()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        DockAreaWidget *area = areaOf(f.a);
        const int before = area->handleWidth();
        const int widthA = area->groupOfPanel(p("a"))->width();

        QApplication::setStyle(new WideSplitterStyle(before + 9));
        QCoreApplication::processEvents();
        QCOMPARE(area->handleWidth(), before + 9);
        QTRY_COMPARE(area->visibleHandles().constFirst()->barGeometry().width(), before + 9);
        // The layout was recomputed around the wider handles.
        QVERIFY(area->groupOfPanel(p("a"))->width() < widthA);
        QRegion covered;
        for (const DockTabGroup *group : area->groups())
            covered += group->geometry();
        for (const SolvedHandle &handle : area->solved().handles)
            covered += handle.rect;
        QCOMPARE(covered, QRegion(area->contentsRect()));
        grab(&f.windowA, p("style-wide-handles"));

        QApplication::setStyle(QStringLiteral("Windows"));
        QCoreApplication::processEvents();
        QCOMPARE(area->handleWidth(),
                 QApplication::style()->pixelMetric(QStyle::PM_SplitterWidth, nullptr, area));
        QCOMPARE(area->groupOfPanel(p("a"))->style(), QApplication::style());
        grab(&f.windowA, p("style-windows"));
    }

    void followsAPaletteChangeAtRuntime()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        DockAreaWidget *area = areaOf(f.a);
        QVERIFY(f.manager.activatePanel(p("a")));

        // A dark palette with a distinctive accent.
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(40, 42, 46));
        dark.setColor(QPalette::WindowText, QColor(230, 231, 233));
        dark.setColor(QPalette::Base, QColor(30, 31, 34));
        dark.setColor(QPalette::Text, QColor(230, 231, 233));
        dark.setColor(QPalette::Button, QColor(52, 54, 59));
        dark.setColor(QPalette::ButtonText, QColor(230, 231, 233));
        dark.setColor(QPalette::Highlight, QColor(255, 128, 0));
        dark.setColor(QPalette::HighlightedText, Qt::black);
        QApplication::setPalette(dark);
        QCoreApplication::processEvents();

        DockTabGroup *group = area->groupOfPanel(p("a"));
        QCOMPARE(group->palette().color(QPalette::Window), QColor(40, 42, 46));
        // The overlay derives its colours from the palette's accent...
        const DockOverlayStyle style = area->overlay()->effectiveStyle();
        QCOMPARE(style.hoverBorderColor, QColor(255, 128, 0));
        QCOMPARE(style.zoneColor.red(), 255);
        QCOMPARE(style.zoneColor.green(), 128);
        QVERIFY(style.zoneColor.alpha() < 255);
        // ...the built-in glyph icons are redrawn in the new text colour...
        const QImage menuIcon = group->menuButton()->icon().pixmap(32, 32).toImage();
        QVERIFY(containsColor(menuIcon, QColor(230, 231, 233)));
        // ...and so is the mark on the active tab.
        const QRect tab = group->tabBar()->tabRect(group->tabBar()->currentIndex());
        QCOMPARE(pixel(group->tabBar(), QPoint(tab.center().x(), tab.top())), QColor(255, 128, 0));
        showGuide(f, area, "c", "a");
        grab(&f.windowA, p("style-dark-palette"));
        priv(f.manager)->drag->cancel();
    }

    void followsAFontChangeAtRuntime()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        DockAreaWidget *area = areaOf(f.a);
        DockTabGroup *group = area->groupOfPanel(p("a"));
        const int titleHeight = group->titleBar()->height();
        const int contentHeight = f.widgets[p("d")]->height();
        const QSize minimum = area->layoutMinimumSize();

        QFont big = QApplication::font();
        big.setPointSizeF(big.pointSizeF() * 2.2);
        QApplication::setFont(big);
        QCoreApplication::processEvents();

        QCOMPARE(group->tabBar()->font().pointSizeF(), big.pointSizeF());
        QTRY_VERIFY(group->titleBar()->height() > titleHeight);
        // The taller title row took its space from the content below it.
        QCOMPARE(group->titleBar()->height() + f.widgets[p("d")]->height(),
                 titleHeight + contentHeight);
        QVERIFY(area->layoutMinimumSize().height() > minimum.height());
        grab(&f.windowA, p("style-big-font"));
    }

    void styleSheetsStyleTheDockWidgets()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        DockAreaWidget *area = areaOf(f.a);
        QVERIFY(f.manager.setPanelAutoHide(p("c"), true, DockArea::Left));
        QVERIFY(f.manager.activatePanel(p("c")));
        QVERIFY(f.manager.activatePanel(p("a")));
        const QRect contentBefore = f.widgets[p("a")]->geometry();

        // Every selector documented in docs/styling.md is exercised here.
        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockSplitHandle { background: #ff0000; }
            QFlexDock--DockSplitHandle[orientation="2"] { background: #00ff00; }
            QFlexDock--DockTabGroup { border: 3px solid #808080; }
            QFlexDock--DockTabGroup[active="true"] { border: 3px solid #0000ff; }
            QFlexDock--DockTabGroup[maximized="true"] { border: 3px solid #00ffff; }
            QFlexDock--DockTabGroup #dockTitleBar { background: #303030; }
            #dockMenuButton { background: #abcdef; border: none; }
            #dockMaximizeButton { background: #fedcba; border: none; }
            QFlexDock--DockSplitHandle[hovered="true"] { background: #ffff00; }
            QFlexDock--DockTabBar { qproperty-activeIndicatorColor: #ff00ff; }
            QFlexDock--DockTabBar[activeGroup="true"] { qproperty-toolTip: "active group"; }
            QFlexDock--DockAutoHideBar[edge="right"] { background: #ff0000; }
            #dockPinButton { background: #00ff00; border: none; }
            #dockCloseButton { background: #0000ff; border: none; }
            QFlexDock--DockFloatingWindow { background: #112233; }
            QFlexDock--DockTabBar::tab { padding: 9px 14px; color: #ffffff; background: #505050; }
            QFlexDock--DockTabBar::tab:selected { background: #707070; }
            QFlexDock--DockAutoHideBar { background: #123456; }
            QFlexDock--DockAutoHidePopup { background: #654321; }
            QFlexDock--DockDropOverlay {
                qproperty-zoneColor: rgba(10, 20, 30, 40);
                qproperty-hoverColor: #00ffff;
                qproperty-previewColor: rgba(1, 2, 3, 4);
                qproperty-zoneBorderColor: #010101;
                qproperty-hoverBorderColor: #020202;
                qproperty-previewBorderColor: #030303;
                qproperty-glyphColor: #040404;
                qproperty-borderWidth: 4;
                qproperty-cornerRadius: 0;
                qproperty-zoneGap: 12;
                qproperty-zoneMargin: 30;
                qproperty-showPreview: true;
            }
        )"));
        QCoreApplication::processEvents();

        // Split handles: background, by orientation.
        const auto handles = area->visibleHandles();
        QCOMPARE(handles.size(), 1);
        QCOMPARE(handles.at(0)->orientation(), Qt::Horizontal);
        QCOMPARE(pixel(handles.at(0), handles.at(0)->rect().center()), QColor(0xff, 0, 0));
        area->setHandleHover(handles.at(0)->index(), true);
        QCOMPARE(pixel(handles.at(0), handles.at(0)->rect().center()), QColor(0xff, 0xff, 0));
        area->setHandleHover(handles.at(0)->index(), false);
        QCOMPARE(pixel(handles.at(0), handles.at(0)->rect().center()), QColor(0xff, 0, 0));

        // Tab groups: border, and the active one told apart.
        DockTabGroup *active = area->groupOfPanel(p("a"));
        DockTabGroup *inactive = area->groupOfPanel(p("b"));
        QVERIFY(active->isActive());
        QCOMPARE(pixel(active, QPoint(1, active->height() / 2)), QColor(0, 0, 0xff));
        QCOMPARE(pixel(inactive, QPoint(1, inactive->height() / 2)), QColor(0x80, 0x80, 0x80));
        // The border takes room: the content moved in by its width.
        QTRY_VERIFY(f.widgets[p("a")]->width() < contentBefore.width());
        QCOMPARE(active->contentsMargins(), QMargins(3, 3, 3, 3));

        // Title row, its buttons and tabs.
        DockTabBar *bar = active->tabBar();
        QCOMPARE(bar->activeIndicatorColor(), QColor(0xff, 0, 0xff));
        QCOMPARE(bar->toolTip(), p("active group"));
        QCOMPARE(inactive->tabBar()->toolTip(), QString());
        QCOMPARE(pixel(active->menuButton(), QPoint(1, 1)), QColor(0xab, 0xcd, 0xef));
        QCOMPARE(pixel(active->maximizeButton(), QPoint(1, 1)), QColor(0xfe, 0xdc, 0xba));
        const QRect selected = bar->tabRect(bar->currentIndex());
        const QRect other = bar->tabRect(bar->currentIndex() == 0 ? 1 : 0);
        QCOMPARE(pixel(bar, QPoint(selected.left() + 3, selected.bottom() - 3)), QColor(0x70, 0x70, 0x70));
        QCOMPARE(pixel(bar, QPoint(other.left() + 3, other.bottom() - 3)), QColor(0x50, 0x50, 0x50));
        QCOMPARE(pixel(bar, QPoint(selected.center().x(), selected.top())), QColor(0xff, 0, 0xff));
        QCOMPARE(pixel(active->titleBar(), QPoint(bar->tabRect(bar->count() - 1).right() + 30, 4)),
                 QColor(0x30, 0x30, 0x30));

        // Auto-hide bar and popup.
        DockAutoHideContainer *autoHide = DockManagerPrivate::get(f.a)->autoHide;
        DockAutoHideBar *left = autoHide->bar(DockArea::Left);
        QCOMPARE(left->edgeName(), p("left"));
        QCOMPARE(pixel(left, QPoint(left->width() / 2, left->height() - 4)), QColor(0x12, 0x34, 0x56));
        // The same class, told apart by edge.
        QVERIFY(f.manager.setPanelAutoHide(p("b"), true, DockArea::Right));
        DockAutoHideBar *right = autoHide->bar(DockArea::Right);
        QTRY_VERIFY(right->isVisible());
        QCOMPARE(pixel(right, QPoint(right->width() / 2, right->height() - 4)), QColor(0xff, 0, 0));
        QCOMPARE(pixel(left, QPoint(left->width() / 2, left->height() - 4)), QColor(0x12, 0x34, 0x56));
        QVERIFY(f.manager.setPanelAutoHide(p("b"), false));
        QVERIFY(f.manager.activatePanel(p("c")));
        QCOMPARE(pixel(autoHide->popup(), QPoint(40, 6)), QColor(0x65, 0x43, 0x21));
        QCOMPARE(pixel(autoHide->popup()->pinButton(), QPoint(1, 1)), QColor(0, 0xff, 0));
        QCOMPARE(pixel(autoHide->popup()->closeButton(), QPoint(1, 1)), QColor(0, 0, 0xff));
        autoHide->collapse();

        // A maximized group, and a floating window.
        QVERIFY(f.manager.activatePanel(p("a")));
        QVERIFY(f.manager.maximizePanel(p("a")));
        active = area->groupOfPanel(p("a"));
        QCOMPARE(pixel(active, QPoint(1, active->height() / 2)), QColor(0, 0xff, 0xff));
        QVERIFY(f.manager.restoreMaximizedPanel());
        QCOMPARE(pixel(active, QPoint(1, active->height() / 2)), QColor(0, 0, 0xff));
        QVERIFY(f.a->addPanel(p("e"), DockArea::Bottom));
        QVERIFY(f.manager.floatPanel(p("e"), QRect(30, 30, 300, 200)));
        QWidget *floating = f.widgets[p("e")]->window();
        QVERIFY(floating != &f.windowA);
        QVERIFY(QTest::qWaitForWindowExposed(floating));
        QCOMPARE(pixel(floating, QPoint(floating->width() / 2, floating->height() - 12)),
                 QColor(0x11, 0x22, 0x33));
        QVERIFY(f.manager.closePanel(p("e")));
        QVERIFY(f.manager.activatePanel(p("a")));

        // Drop overlay: set through qproperty-.
        const DockOverlayStyle style = area->overlay()->effectiveStyle();
        QCOMPARE(style.zoneColor, QColor(10, 20, 30, 40));
        QCOMPARE(style.hoverColor, QColor(0, 255, 255));
        QCOMPARE(style.previewColor, QColor(1, 2, 3, 4));
        QCOMPARE(style.zoneBorderColor, QColor(1, 1, 1));
        QCOMPARE(style.hoverBorderColor, QColor(2, 2, 2));
        QCOMPARE(style.previewBorderColor, QColor(3, 3, 3));
        QCOMPARE(style.glyphColor, QColor(4, 4, 4));
        QCOMPARE(style.borderWidth, 4.0);
        QCOMPARE(style.cornerRadius, 0.0);
        QCOMPARE(style.zoneGap, 12);
        QCOMPARE(style.zoneMargin, 30);
        QCOMPARE(style.showPreview, true);
        QCOMPARE(DockOverlayStyle{}.showPreview, false); // off unless asked for
        // What the sheet does not mention keeps its default.
        QCOMPARE(style.outerBandWidth, DockOverlayStyle{}.outerBandWidth);
        showGuide(f, area, "b", "a");
        grab(&f.windowA, p("style-qss"));
        priv(f.manager)->drag->cancel();

        // Taking the sheet away again restores the native look.
        qApp->setStyleSheet(QString());
        QCoreApplication::processEvents();
        QVERIFY(pixel(handles.at(0), handles.at(0)->rect().center()) != QColor(0xff, 0, 0));
        QTRY_COMPARE(f.widgets[p("a")]->size(), contentBefore.size());
        QCOMPARE(area->overlay()->effectiveStyle().borderWidth, 4.0); // qproperty values persist
    }

    // The parts that come with DockGroupHeader::TitleBar, with the title buttons
    // and with a panel that has no header.
    void styleSheetsStyleTheTitleBarHeader()
    {
        TwoWindows f;
        f.manager.setGroupHeader(DockGroupHeader::TitleBar);
        DockTheme theme;
        theme.titleButtons = DockTitleButton::Float | DockTitleButton::Close;
        f.manager.setTheme(theme);
        f.manager.panel(p("c"))->setFeatures({});
        f.manager.panel(p("c"))->setHeaderVisible(false);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Right));
        DockAreaWidget *area = areaOf(f.a);

        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockTabGroup { background: #101010; border: 2px solid #808080; }
            QFlexDock--DockTabGroup[headerVisible="false"] { border: 2px solid #00ff00; }
            QFlexDock--DockTabGroup #dockTitleBar { background: #303030; }
            #dockTitle { background: #0000ff; color: #ffffff; }
            #dockFloatButton { background: #abcdef; border: none; }
            QFlexDock--DockTabGroup #dockCloseButton { background: #fedcba; border: none; }
            QFlexDock--DockTabBar::tab { background: #505050; padding: 6px 10px; }
            QFlexDock--DockTabBar::tab:selected { background: #ff0000; }
        )"));
        QCoreApplication::processEvents();

        DockTabGroup *group = area->groupOfPanel(p("a"));
        QVERIFY(containsColor(group->titleLabel()->grab().toImage(), QColor(0, 0, 0xff)));
        QVERIFY(containsColor(group->floatButton()->grab().toImage(), QColor(0xab, 0xcd, 0xef)));
        QVERIFY(containsColor(group->closeButton()->grab().toImage(), QColor(0xfe, 0xdc, 0xba)));
        QCOMPARE(pixel(group, QPoint(0, group->height() / 2)), QColor(0x80, 0x80, 0x80));
        // The tabs below the content are QTabBar tabs like the ones on top.
        QTRY_VERIFY(group->tabBar()->isVisible());
        const DockTabBar *bar = group->tabBar();
        QCOMPARE(pixel(group->tabBar(), bar->tabRect(bar->currentIndex()).center() + QPoint(0, 9)),
                 QColor(0xff, 0, 0));

        DockTabGroup *bare = area->groupOfPanel(p("c"));
        QVERIFY(!bare->isHeaderVisible());
        QCOMPARE(pixel(bare, QPoint(0, bare->height() / 2)), QColor(0, 0xff, 0));
        grab(&f.windowA, p("style-qss-titlebar"));

        // The property follows the panel.
        f.manager.panel(p("c"))->setHeaderVisible(true);
        QCoreApplication::processEvents();
        QCOMPARE(pixel(bare, QPoint(0, bare->height() / 2)), QColor(0x80, 0x80, 0x80));
        qApp->setStyleSheet(QString());
    }

    void styleSheetsStyleTheTitleActions()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Center));
        QVERIFY(f.b->addPanel(p("d")));
        f.a->setObjectName(p("documents"));
        QAction one(p("One"));
        QAction line;
        line.setSeparator(true);
        QAction two(p("Two"));
        f.manager.panel(p("b"))->setTitleActions({&one, &line, &two});

        qApp->setStyleSheet(QStringLiteral(R"(
            #dockTitleActions { background: #102030; }
            #dockActionButton { background: #abcdef; border: none; margin: 2px; }
            #dockActionSeparator { background: #ff00ff; border: none; min-width: 3px; }
            QFlexDock--DockTabBar::tab { background: #0000ff; padding: 6px 10px; }
            #documents QFlexDock--DockTabBar::tab { background: #00ff00; }
            #documents QFlexDock--DockTabBar::tab:selected { background: #ff0000; }
        )"));
        QCoreApplication::processEvents();

        DockTabGroup *group = areaOf(f.a)->groupOfPanel(p("b"));
        QVERIFY(containsColor(group->actionBar()->grab().toImage(), QColor(0x10, 0x20, 0x30)));
        QVERIFY(containsColor(group->widgetForAction(&one)->grab().toImage(),
                              QColor(0xab, 0xcd, 0xef)));
        QVERIFY(containsColor(group->widgetForAction(&line)->grab().toImage(),
                              QColor(0xff, 0, 0xff)));
        grab(&f.windowA, p("style-qss-title-actions"));

        // A workspace's object name tells its tabs from those of the others.
        const DockTabBar *bar = group->tabBar();
        QCOMPARE(pixel(group->tabBar(), bar->tabRect(bar->currentIndex()).center() + QPoint(0, 9)),
                 QColor(0xff, 0, 0));
        QCOMPARE(pixel(group->tabBar(), bar->tabRect(0).center() + QPoint(0, 9)),
                 QColor(0, 0xff, 0));
        DockTabGroup *other = areaOf(f.b)->groupOfPanel(p("d"));
        QCOMPARE(pixel(other->tabBar(), other->tabBar()->tabRect(0).center() + QPoint(0, 9)),
                 QColor(0, 0, 0xff));
        qApp->setStyleSheet(QString());
    }

    void styleSheetsStyleTheTitleActionPlaces()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QAction list(p("List"));
        QAction add(p("Add"));
        add.setObjectName(p("newTab"));
        QAction other(p("Other"));
        QAction more(p("More"));
        DockPanel *a = f.manager.panel(p("a"));
        a->setTitleActions({&list}, DockTitlePlace::Start);
        a->setTitleActions({&add, &other}, DockTitlePlace::AfterTabs);
        a->setTitleActions({&more});

        qApp->setStyleSheet(QStringLiteral(R"(
            #dockTitleStartActions { background: #102030; }
            #dockTabActions { background: #203040; }
            #dockActionButton { background: #abcdef; border: none; margin: 2px; }
            #dockTabActions #dockActionButton { background: #00ff00; }
            #dockTabActions #dockActionButton[action="newTab"] { background: #ff0000; }
        )"));
        QCoreApplication::processEvents();

        const DockTabGroup *group = areaOf(f.a)->groupOfPanel(p("a"));
        QVERIFY(containsColor(group->actionBar(DockTitlePlace::Start)->grab().toImage(),
                              QColor(0x10, 0x20, 0x30)));
        QVERIFY(containsColor(group->actionBar(DockTitlePlace::AfterTabs)->grab().toImage(),
                              QColor(0x20, 0x30, 0x40)));
        const auto middle = [&](const QAction *action) {
            QWidget *button = group->widgetForAction(action);
            return pixel(button, QPoint(3, button->height() / 2));
        };
        QCOMPARE(middle(&list), QColor(0xab, 0xcd, 0xef));
        QCOMPARE(middle(&more), QColor(0xab, 0xcd, 0xef));
        QCOMPARE(middle(&other), QColor(0, 0xff, 0));
        QCOMPARE(middle(&add), QColor(0xff, 0, 0));
        grab(&f.windowA, p("style-qss-action-places"));
        qApp->setStyleSheet(QString());
    }

    // A frame of QFlexDock's own with round corners, styled by a style sheet.
    void styleSheetsStyleTheRoundFloatingFrame()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        DockTheme theme;
        theme.floatingBorderWidth = 1;
        theme.floatingCornerRadius = 12;
        f.manager.setTheme(theme);
        f.manager.setFloatingWindowFrame(DockManager::FloatingWindowFrame::Minimal);
        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockFloatingWindow {
                background: #112233; border: 1px solid #ff8800;
                border-top-left-radius: 12px; border-top-right-radius: 12px;
            }
            QFlexDock--DockFloatingWindow[maximized="true"] { border: none; border-radius: 0; }
        )"));
        QVERIFY(f.manager.floatPanel(p("b"), QRect(60, 60, 320, 240)));
        auto *window = qobject_cast<DockFloatingWindow *>(f.widgets[p("b")]->window());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(window->size(), QSize(320, 240));

        // Round where the style sheet says so, and only there.
        QCOMPARE(window->property("owner").toString(), p("A"));
        const QImage image = window->grab().toImage();
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(image.width() - 1, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(0, image.height() - 1), QColor(0xff, 0x88, 0x00));
        QCOMPARE(image.pixelColor(0, image.height() / 2), QColor(0xff, 0x88, 0x00));
        QCOMPARE(image.pixelColor(image.width() / 2, 0), QColor(0xff, 0x88, 0x00));
        grab(window, p("style-qss-round-frame"));

        // The windows of one workspace can be told from those of another.
        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockFloatingWindow { background: #112233; border: 1px solid #ff8800; }
            QFlexDock--DockFloatingWindow[owner="B"] { border: 1px solid #0088ff; }
        )"));
        QVERIFY(f.b->addPanel(p("c")));
        QVERIFY(f.manager.floatPanel(p("c"), QRect(420, 60, 320, 240)));
        auto *other = qobject_cast<DockFloatingWindow *>(f.widgets[p("c")]->window());
        QVERIFY(other && QTest::qWaitForWindowExposed(other));
        QCOMPARE(other->owner(), p("B"));
        QCOMPARE(other->grab().toImage().pixelColor(0, 120), QColor(0, 0x88, 0xff));
        QCOMPARE(window->grab().toImage().pixelColor(0, 120), QColor(0xff, 0x88, 0x00));
        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockFloatingWindow {
                background: #112233; border: 1px solid #ff8800;
                border-top-left-radius: 12px; border-top-right-radius: 12px;
            }
            QFlexDock--DockFloatingWindow[maximized="true"] { border: none; border-radius: 0; }
        )"));

        window->showMaximized();
        const QSize normal(320, 240);
        if (QTest::qWaitFor([&] { return window->isMaximized() && window->size() != normal; }, 3000)) {
            QCoreApplication::processEvents();
            const QImage maximized = window->grab().toImage();
            QCOMPARE(maximized.pixelColor(0, 0).alpha(), 255);
            QVERIFY(maximized.pixelColor(0, maximized.height() / 2) != QColor(0xff, 0x88, 0x00));
        }
        qApp->setStyleSheet(QString());
    }

    void styleSheetsStyleTheEdgeHandle()
    {
        TwoWindows f;
        DockTheme theme;
        theme.splitHandleWidth = 1;
        theme.splitHandleHoverWidth = 4;
        f.manager.setTheme(theme);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Left));
        f.manager.panel(p("b"))->setCollapsible(true);
        QVERIFY(f.manager.closePanel(p("b")));
        DockAreaWidget *area = areaOf(f.a);
        QCOMPARE(area->visibleEdgeHandles().size(), 1);
        DockEdgeHandle *edge = area->visibleEdgeHandles().constFirst();

        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockEdgeHandle { background: #00ff00; }
            QFlexDock--DockEdgeHandle[edge="left"][pressed="true"] { background: #ff0000; }
        )"));
        QCoreApplication::processEvents();
        const auto column = [&](int x) {
            return picture(&f.windowA).pixelColor(
                area->mapTo(&f.windowA, QPoint(x, area->height() / 2)));
        };
        // Nothing of it shows until it is pointed at; then as wide as a
        // hovered split handle, and no wider.
        QVERIFY(column(0) != QColor(0, 0xff, 0));
        QEnterEvent enter(QPointF(2, 2), QPointF(2, 2), QPointF(2, 2));
        QCoreApplication::sendEvent(edge, &enter);
        QVERIFY(edge->isHovered());
        QCOMPARE(column(0), QColor(0, 0xff, 0));
        QCOMPARE(column(3), QColor(0, 0xff, 0));
        QVERIFY(column(4) != QColor(0, 0xff, 0));
        QTest::mousePress(edge, Qt::LeftButton, {}, edge->rect().center());
        QCOMPARE(column(1), QColor(0xff, 0, 0));
        QTest::mouseRelease(edge, Qt::LeftButton, {}, edge->rect().center());
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(edge, &leave);
        QVERIFY(column(0) != QColor(0, 0xff, 0));
        qApp->setStyleSheet(QString());
    }

    void styleSheetsStyleColumnsAndTheirButtons()
    {
        TwoWindows f;
        f.show();
        f.a->setColumnDocking(true);
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right, -1, 0.3));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        DockAreaWidget *area = areaOf(f.a);
        const LayoutNode *column = area->tree().columnOf(area->tree().findPanel(p("b"))->id);
        DockIconStrip *strip = area->iconStrip(column->id);
        DockColumnBar *iconified = area->columnBar(column->id);
        DockColumnBar *open = area->columnBar(area->tree().findPanel(p("a"))->id);
        QVERIFY(strip && iconified && open);

        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockColumnBar { background: #102030; }
            QFlexDock--DockColumnBar[iconified="true"] { background: #302010; }
            #dockIconifyButton { background: #00c000; border: none; }
            QFlexDock--DockIconStrip { background: #204060; }
            QFlexDock--DockIconStrip[labelled="true"] { background: #206040; }
            QFlexDock--DockIconGrip { background: #c000c0; }
            QFlexDock--DockIconButton { background: #c0c000; border: none; }
            QFlexDock--DockIconButton:checked { background: #c00000; }
            QFlexDock--DockTabGroup[flyout="true"] { border: 3px solid #00c0c0; }
            #dockFlyoutButton { background: #0000c0; border: none; }
        )"));
        QCoreApplication::processEvents();
        const auto colorIn = [&](QWidget *widget, const QPoint &pos) {
            return picture(&f.windowA).pixelColor(widget->mapTo(&f.windowA, pos));
        };

        // The bars, by what their column is, and the button in them.
        QCOMPARE(colorIn(open, QPoint(open->width() / 2, 2)), QColor(0x10, 0x20, 0x30));
        QVERIFY(iconified->property("iconified").toBool());
        QVERIFY(containsColor(picture(iconified), QColor(0x30, 0x20, 0x10))
                || iconified->width() <= iconified->iconifyButton()->width());
        QCOMPARE(pixel(open->iconifyButton(), QPoint(1, 1)), QColor(0, 0xc0, 0));

        // The strip, wide enough for its titles here, with its grips and buttons.
        QVERIFY(strip->isLabelled());
        QCOMPARE(colorIn(strip, QPoint(strip->width() / 2, strip->height() - 3)),
                 QColor(0x20, 0x60, 0x40));
        const DockIconGrip *grip = strip->grips().constFirst();
        QCOMPARE(pixel(strip, grip->mapTo(strip, QPoint(1, 1))), QColor(0xc0, 0, 0xc0));
        DockIconButton *button = strip->button(p("b"));
        QCOMPARE(pixel(button, QPoint(1, 1)), QColor(0xc0, 0xc0, 0));

        // The button of what is out, and the group that is.
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(pixel(button, QPoint(1, 1)), QColor(0xc0, 0, 0));
        DockTabGroup *out = area->groupOfPanel(p("b"));
        QVERIFY(out && out->property("flyout").toBool());
        QCOMPARE(pixel(out, QPoint(1, out->height() / 2)), QColor(0, 0xc0, 0xc0));
        QCOMPARE(pixel(out->flyoutButton(), QPoint(1, 1)), QColor(0, 0, 0xc0));

        // Dragged narrow, the strip is another to the style sheet.
        area->showFlyout({});
        area->beginHandleDrag(0, false);
        area->moveHandleDrag(2000);
        area->endHandleDrag(false);
        QVERIFY(!strip->isLabelled());
        QCOMPARE(colorIn(strip, QPoint(strip->width() / 2, strip->height() - 3)),
                 QColor(0x20, 0x40, 0x60));

        // The button that closes a floating column.
        qApp->setStyleSheet(QStringLiteral("#dockColumnCloseButton { background: #c06000; border: none; }"));
        QVERIFY(f.manager.floatPanel(p("a"), QRect(40, 40, 300, 240)));
        const DockFloatingWindow *window = priv(f.manager)->floatingWindows.begin().value();
        QVERIFY(QTest::qWaitForWindowExposed(const_cast<DockFloatingWindow *>(window)));
        QCOMPARE(window->area()->columnBars().size(), 1);
        QToolButton *close = window->area()->columnBars().constFirst()->closeButton();
        QVERIFY(close->isVisible());
        QCOMPARE(pixel(close, QPoint(1, 1)), QColor(0xc0, 0x60, 0));
        qApp->setStyleSheet(QString());
    }

    void themeTokensLayerBetweenPaletteAndStyleSheet()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        DockAreaWidget *area = areaOf(f.a);
        QSignalSpy themeChanged(&f.manager, &DockManager::themeChanged);
        const QColor accent = QApplication::palette().color(QPalette::Highlight);

        // Defaults: everything from the palette.
        QCOMPARE(area->overlay()->effectiveStyle().hoverBorderColor, accent);

        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::green);
        DockTheme theme;
        theme.splitHandleWidth = 9;
        theme.iconSize = 20;
        theme.overlay.zoneColor = QColor(1, 2, 3, 100);
        theme.overlay.hoverColor = QColor(4, 5, 6, 200);
        theme.overlay.cornerRadius = 2;
        theme.overlay.edgeFraction = 0.2;
        theme.overlay.outerBandWidth = 40;
        theme.overlay.zoneMargin = 4;
        theme.overlay.showPreview = true;
        theme.icons.insert(DockIcon::Maximize, QIcon(pixmap));
        f.manager.setTheme(theme);
        QCoreApplication::processEvents();
        QCOMPARE(themeChanged.size(), 1);
        QCOMPARE(f.manager.theme().splitHandleWidth, 9);

        QCOMPARE(area->handleWidth(), 9);
        QTRY_COMPARE(area->visibleHandles().constFirst()->barGeometry().width(), 9);
        DockOverlayStyle style = area->overlay()->effectiveStyle();
        QCOMPARE(style.zoneColor, QColor(1, 2, 3, 100));
        QCOMPARE(style.hoverColor, QColor(4, 5, 6, 200));
        QCOMPARE(style.hoverBorderColor, accent); // not set by the theme
        QCOMPARE(style.cornerRadius, 2.0);
        QCOMPARE(style.outerBandWidth, 40);
        QCOMPARE(style.zoneMargin, 4);
        QCOMPARE(style.showPreview, true);
        DockTabGroup *group = area->groupOfPanel(p("a"));
        QCOMPARE(group->maximizeButton()->iconSize(), QSize(20, 20));
        QVERIFY(containsColor(group->maximizeButton()->icon().pixmap(16, 16).toImage(), Qt::green));
        // An icon the theme does not replace stays the built-in one.
        QVERIFY(!containsColor(group->menuButton()->icon().pixmap(16, 16).toImage(), Qt::green));

        // A style sheet wins over the theme where both speak.
        qApp->setStyleSheet(QStringLiteral(
            "QFlexDock--DockDropOverlay { qproperty-zoneColor: #0a0b0c; }"));
        QCoreApplication::processEvents();
        style = area->overlay()->effectiveStyle();
        QCOMPARE(style.zoneColor, QColor(0x0a, 0x0b, 0x0c));
        QCOMPARE(style.hoverColor, QColor(4, 5, 6, 200));

        // The theme can be changed again at any time.
        f.manager.setTheme(DockTheme{});
        QCoreApplication::processEvents();
        QCOMPARE(area->handleWidth(),
                 QApplication::style()->pixelMetric(QStyle::PM_SplitterWidth, nullptr, area));
        QCOMPARE(area->overlay()->effectiveStyle().hoverColor.alpha() < 255, true);
    }

    void customOverlayPainterReplacesTheLook()
    {
        TwoWindows f;
        f.show();
        buildLayout(f);
        DockAreaWidget *area = areaOf(f.a);
        auto painter = std::make_shared<RecordingPainter>();
        f.manager.setOverlayPainter(painter);

        showGuide(f, area, "c", "a");
        const QImage image = area->overlay()->grab().toImage();
        QVERIFY(painter->calls > 0);
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), QColor(Qt::magenta));
        // It is handed the same scene and an already resolved style.
        QCOMPARE(painter->lastScene.zones.size(), area->overlay()->scene().zones.size());
        QVERIFY(painter->lastScene.preview.isValid());
        QVERIFY(painter->lastStyle.zoneColor.isValid());
        QVERIFY(painter->lastStyle.glyphColor.isValid());

        // Hit testing does not depend on the painter.
        const DragSession *session = priv(f.manager)->drag->session();
        const QPoint pos = area->groupOfPanel(p("a"))->geometry().center();
        QCOMPARE(area->candidateAt(pos, *session).target.area, DockArea::Center);

        f.manager.setOverlayPainter(nullptr);
        const int calls = painter->calls;
        area->overlay()->grab();
        QCOMPARE(painter->calls, calls);
        priv(f.manager)->drag->cancel();
    }

    void defaultOverlayPainterDrawsTheScene()
    {
        // The built-in painter on its own, without any widget around it.
        DockOverlayScene scene;
        scene.bounds = QRect(0, 0, 400, 300);
        DockOverlayScene::Zone left;
        left.area = DockArea::Left;
        left.shape = QPolygonF({QPointF(0, 0), QPointF(100, 75), QPointF(100, 225), QPointF(0, 300)});
        left.hovered = true;
        DockOverlayScene::Zone center;
        center.area = DockArea::Center;
        center.shape = QPolygonF(QRectF(100, 75, 200, 150));
        scene.zones = {left, center};
        scene.preview = QRect(0, 0, 200, 300);
        scene.tabIndicator = QRect(250, 0, 3, 20);

        DockOverlayStyle style;
        style.zoneColor = QColor(0, 0, 255);
        style.hoverColor = QColor(255, 0, 0);
        style.previewColor = QColor(0, 255, 0);
        style.hoverBorderColor = QColor(255, 255, 0);
        style.glyphColor = QColor(0, 0, 0, 0);
        style.borderWidth = 0;
        style.zoneGap = 0;
        style.cornerRadius = 0;
        const auto render = [&](bool showPreview) {
            style.showPreview = showPreview;
            QImage image(400, 300, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            DockDefaultOverlayPainter().paint(&painter, scene, style.resolved(QPalette()));
            painter.end();
            return image;
        };

        // Default: the hovered area is highlighted; no preview rectangle.
        QImage image = render(false);
        QCOMPARE(image.pixelColor(30, 150), QColor(255, 0, 0));   // hovered area
        QCOMPARE(image.pixelColor(200, 100), QColor(0, 0, 255));  // plain area
        QCOMPARE(image.pixelColor(150, 20).alpha(), 0);           // where only the preview would be
        QCOMPARE(image.pixelColor(251, 10), QColor(255, 255, 0)); // tab insertion marker
        QCOMPARE(image.pixelColor(380, 150).alpha(), 0);          // nothing there

        // With the preview: it takes the place of the hovered area, and the
        // other areas are drawn only where it does not cover them. Nowhere are
        // two translucent layers stacked.
        image = render(true);
        QCOMPARE(image.pixelColor(30, 150), QColor(0, 255, 0));   // hovered area -> preview
        QCOMPARE(image.pixelColor(150, 20), QColor(0, 255, 0));   // preview
        QCOMPARE(image.pixelColor(150, 150), QColor(0, 255, 0));  // preview, not centre + preview
        QCOMPARE(image.pixelColor(250, 150), QColor(0, 0, 255));  // rest of the centre area
        QCOMPARE(image.pixelColor(380, 150).alpha(), 0);
    }

    // DockGuide::Preview and DockGuide::Buttons, as the built-in painter
    // draws them.
    void defaultOverlayPainterDrawsTheOtherGuides()
    {
        DockOverlayScene scene;
        scene.bounds = QRect(0, 0, 400, 300);
        DockOverlayScene::Zone left;
        left.area = DockArea::Left;
        left.shape = QPolygonF(QRectF(140, 130, 40, 40));
        left.hovered = true;
        DockOverlayScene::Zone center;
        center.area = DockArea::Center;
        center.shape = QPolygonF(QRectF(180, 130, 40, 40));
        DockOverlayScene::Zone border;
        border.area = DockArea::Right;
        border.outer = true;
        border.shape = QPolygonF(QRectF(350, 130, 40, 40));
        scene.zones = {left, center, border};
        scene.preview = QRect(0, 0, 100, 300);
        scene.tabIndicator = QRect(250, 0, 3, 20);

        DockOverlayStyle style;
        style.zoneColor = QColor(0, 0, 255);
        style.hoverColor = QColor(255, 0, 0);
        style.previewColor = QColor(0, 255, 0);
        style.hoverBorderColor = QColor(255, 255, 0);
        style.buttonColor = QColor(10, 20, 30);
        style.glyphColor = QColor(255, 255, 255);
        style.borderWidth = 0;
        style.cornerRadius = 0;
        const auto render = [&](DockGuide guide) {
            style.guide = guide;
            QImage image(400, 300, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            DockDefaultOverlayPainter().paint(&painter, scene, style.resolved(QPalette()));
            painter.end();
            return image;
        };

        // Preview: the place of the drop, and the mark between tabs. No areas.
        QImage image = render(DockGuide::Preview);
        QCOMPARE(image.pixelColor(50, 150), QColor(0, 255, 0));
        QCOMPARE(image.pixelColor(160, 150).alpha(), 0);
        QCOMPARE(image.pixelColor(200, 150).alpha(), 0);
        QCOMPARE(image.pixelColor(370, 150).alpha(), 0);
        QCOMPARE(image.pixelColor(251, 10), QColor(255, 255, 0));

        // Buttons: a square each, the hovered one in the hover colour, with
        // a picture in it; and the preview along with them.
        image = render(DockGuide::Buttons);
        QCOMPARE(image.pixelColor(50, 150), QColor(0, 255, 0));
        QCOMPARE(image.pixelColor(142, 132), QColor(255, 0, 0));
        QCOMPARE(image.pixelColor(182, 132), QColor(10, 20, 30));
        QCOMPARE(image.pixelColor(352, 132), QColor(10, 20, 30));
        QCOMPARE(image.pixelColor(300, 150).alpha(), 0); // between the buttons: nothing
        QCOMPARE(image.pixelColor(251, 10), QColor(255, 255, 0));
        // The picture: a window in the glyph colour, the part that is taken
        // filled in.
        QVERIFY(containsColor(image.copy(180, 130, 40, 40), QColor(255, 255, 255)));
        QVERIFY(containsColor(image.copy(180, 130, 40, 40), QColor(255, 255, 0), 70));
        QVERIFY(containsColor(image.copy(350, 130, 40, 40), QColor(255, 255, 255)));
    }

    // The button guide from a style sheet, the button that puts a group
    // away, and the panel that comes out again.
    void styleSheetsStyleTheButtonGuide()
    {
        TwoWindows f;
        DockTheme theme;
        theme.titleButtons = DockTitleButton::AutoHide;
        f.manager.setTheme(theme);
        f.show();
        buildLayout(f);
        DockAreaWidget *area = areaOf(f.a);
        QCOMPARE(area->overlay()->effectiveStyle().guide, DockGuide::Zones);

        qApp->setStyleSheet(QStringLiteral(R"(
            #dockAutoHideButton { background: #00ff00; border: none; }
            QFlexDock--DockAutoHidePopup { background: #0000ff; }
            #dockAutoHideBody { background: #ff00ff; border: 3px solid #00ffff; }
            QFlexDock--DockDropOverlay {
                qproperty-guide: Buttons;
                qproperty-buttonSize: 44;
                qproperty-buttonColor: #102030;
                qproperty-borderWidth: 0;
                qproperty-cornerRadius: 0;
            }
        )"));
        QCoreApplication::processEvents();

        const DockOverlayStyle style = area->overlay()->effectiveStyle();
        QCOMPARE(style.guide, DockGuide::Buttons);
        QCOMPARE(style.buttonSize, 44);
        QCOMPARE(style.buttonColor, QColor(0x10, 0x20, 0x30));
        // Another window's guide is as the theme says.
        QCOMPARE(DockOverlayStyle{}.guide, DockGuide::Zones);

        showGuide(f, area, "a", "b");
        const DockOverlayScene scene = area->overlay()->scene();
        QVERIFY(!scene.zones.isEmpty());
        for (const auto &zone : scene.zones)
            QCOMPARE(zone.shape.boundingRect().size().toSize(), QSize(44, 44));
        const QPoint corner = scene.zones.constFirst().shape.boundingRect().topLeft().toPoint();
        QCOMPARE(pixel(area->overlay(), corner + QPoint(1, 1)), QColor(0x10, 0x20, 0x30));
        grab(&f.windowA, p("style-qss-buttons"));
        priv(f.manager)->drag->cancel();

        DockTabGroup *group = area->groupOfPanel(p("b"));
        QVERIFY(group->autoHideButton()->isVisible());
        QCOMPARE(pixel(group->autoHideButton(), QPoint(1, 1)), QColor(0, 0xff, 0));
        QTest::mouseClick(group->autoHideButton(), Qt::LeftButton);
        const DockAutoHideBar *bar = DockManagerPrivate::get(f.a)->autoHide->bar(DockArea::Right);
        QCOMPARE(bar->tabs().size(), 1);

        // The panel that comes out: a frame of its own beside the grip, which
        // shows the background of the whole.
        QVERIFY(f.manager.activatePanel(p("b")));
        DockAutoHidePopup *popup = DockManagerPrivate::get(f.a)->autoHide->popup();
        QVERIFY(popup->isVisible());
        auto *body = popup->findChild<QFrame *>(p("dockAutoHideBody"));
        QVERIFY(body);
        QCOMPARE(body->contentsMargins(), QMargins(3, 3, 3, 3));
        QCOMPARE(pixel(popup, QPoint(2, popup->height() / 2)), QColor(0, 0, 0xff)); // the grip
        QCOMPARE(pixel(body, QPoint(1, body->height() / 2)), QColor(0, 0xff, 0xff));
        QCOMPARE(pixel(body, QPoint(body->width() - 20, 10)).blue(), 0xff);
        grab(&f.windowA, p("style-qss-autohide-body"));
        DockManagerPrivate::get(f.a)->autoHide->collapse();
        qApp->setStyleSheet(QString());
    }

    // The `pane*` properties: a group draws its content and its current tab
    // as one shape, with one outline around the two.
    void paneIsDrawnAsOneShapeWithItsTab()
    {
        TwoWindows f;
        f.manager.panel(p("c"))->setFeatures({});
        f.manager.panel(p("c"))->setHeaderVisible(false);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("d"), p("a"), DockArea::Center));
        QVERIFY(f.a->addPanel(p("c"), DockArea::Right));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Bottom));
        DockAreaWidget *area = areaOf(f.a);
        DockTabGroup *group = area->groupOfPanel(p("a"));
        QVERIFY(!group->drawsPane());

        qApp->setStyleSheet(QStringLiteral(R"(
            QFlexDock--DockTabGroup {
                qproperty-paneColor: #102030;
                qproperty-paneBorderColor: #ff0000;
                qproperty-paneActiveBorderColor: #00ff00;
                qproperty-paneRadius: 0;
            }
            QFlexDock--DockTabGroup[headerVisible="false"] {
                qproperty-paneColor: transparent;
                qproperty-paneBorderColor: transparent;
                qproperty-paneActiveBorderColor: transparent;
            }
            QFlexDock--DockTabGroup #dockTitleBar { background: transparent; }
            QFlexDock--DockTabBar { qproperty-activeIndicatorColor: transparent; }
            QFlexDock--DockTabBar::tab {
                background: transparent; border: none; margin: 0; padding: 8px 16px;
            }
        )"));
        QCoreApplication::processEvents();
        // The second tab: the pane has a corner of its own before it.
        QVERIFY(f.manager.activatePanel(p("d")));
        QCoreApplication::processEvents();

        const QColor fill(0x10, 0x20, 0x30);
        const QColor active(0, 0xff, 0);
        const auto tabIn = [](const DockTabGroup *g) {
            const DockTabBar *bar = g->tabBar();
            const QRect rect = bar->tabRect(bar->currentIndex());
            return QRect(bar->mapTo(g, rect.topLeft()), rect.size());
        };
        QVERIFY(group->drawsPane());
        QCOMPARE(group->contentsMargins(), QMargins(1, 1, 1, 1)); // the line around the pane
        QRect host = group->contentHost()->geometry();
        QRect tab = tabIn(group);
        QVERIFY(tab.left() > 20);
        QImage image = picture(group);
        QCOMPARE(image.pixelColor(host.left() + 3, host.bottom() - 3), fill);
        QCOMPARE(image.pixelColor(0, host.center().y()), active);
        // The pane ends below the tab row, where its line runs on either
        // side of the current tab. That tab is open to it, and has the line
        // around itself.
        QCOMPARE(image.pixelColor(tab.left() - 6, host.top() - 1), active);
        QCOMPARE(image.pixelColor(tab.right() + 6, host.top() - 1), active);
        QCOMPARE(image.pixelColor(tab.center().x(), host.top() - 1), fill);
        QCOMPARE(image.pixelColor(tab.left() + 3, tab.center().y()), fill);
        QCOMPARE(image.pixelColor(tab.center().x(), tab.top()), active);
        QCOMPARE(image.pixelColor(tab.left(), tab.center().y()), active);
        QCOMPARE(image.pixelColor(tab.right(), tab.center().y()), active);
        // The rest of the tab row is not the group's to paint.
        const auto untouched = [&](const QColor &color) {
            return color != fill && color != active && color != QColor(0xff, 0, 0);
        };
        QVERIFY(untouched(image.pixelColor(tab.right() + 6, tab.center().y())));
        QVERIFY(untouched(image.pixelColor(tab.left() - 6, tab.top())));
        grab(&f.windowA, p("style-pane-tabs"));

        // Another tab: the shape follows.
        QVERIFY(f.manager.activatePanel(p("a")));
        QCoreApplication::processEvents();
        const QRect first = tabIn(group);
        QVERIFY(first.right() < tab.left() + 2);
        image = picture(group);
        QCOMPARE(image.pixelColor(first.center().x(), host.top() - 1), fill);
        QCOMPARE(image.pixelColor(tab.center().x(), host.top() - 1), active);
        // At the end of the pane a tab goes straight on into its side.
        QCOMPARE(image.pixelColor(0, first.center().y()), active);

        // A group that is not the active one, and one that is no pane.
        const DockTabGroup *other = area->groupOfPanel(p("b"));
        QCOMPARE(pixel(const_cast<DockTabGroup *>(other), QPoint(0, other->height() / 2)),
                 QColor(0xff, 0, 0));
        DockTabGroup *bare = area->groupOfPanel(p("c"));
        QVERIFY(!bare->isHeaderVisible());
        QVERIFY(untouched(pixel(bare, QPoint(0, bare->height() / 2))));
        // It shows nothing, and keeps no room for a line either.
        QCOMPARE(bare->contentsMargins(), QMargins());

        // With a title bar: that is part of the pane, and the tabs are below.
        f.manager.setGroupHeader(DockGroupHeader::TitleBar);
        QCoreApplication::processEvents();
        QTRY_VERIFY(group->tabBar()->isVisible());
        host = group->contentHost()->geometry();
        tab = tabIn(group);
        QVERIFY(tab.top() > host.bottom());
        image = picture(group);
        QCOMPARE(image.pixelColor(group->width() / 2, 0), active);
        QCOMPARE(image.pixelColor(group->width() - 40, host.bottom() + 1), active);
        QCOMPARE(image.pixelColor(tab.center().x(), host.bottom() + 1), fill);
        QCOMPARE(image.pixelColor(tab.center().x(), tab.bottom()), active);
        QVERIFY(untouched(image.pixelColor(tab.right() + 6, tab.center().y())));
        grab(&f.windowA, p("style-pane-title-bar"));
        qApp->setStyleSheet(QString());
    }

    // `paneCornerColor`: the round corners of a pane are drawn over content
    // that fills its rectangle, in the color of what is behind the group.
    void paneCornersAreDrawnOverOpaqueContent()
    {
        TwoWindows f;
        // Content with pixels of its own in every corner.
        QWidget *content = f.manager.panel(p("a"))->widget();
        content->setAutoFillBackground(true);
        QPalette filled = content->palette();
        filled.setColor(QPalette::Window, QColor(0, 0, 0xff));
        content->setPalette(filled);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));
        DockAreaWidget *area = areaOf(f.a);
        DockTabGroup *group = area->groupOfPanel(p("a"));

        const QString pane = QStringLiteral(R"(
            QFlexDock--DockTabGroup {
                qproperty-paneColor: #102030;
                qproperty-paneBorderColor: #ff0000;
                qproperty-paneRadius: 12;
                %1
            }
            QFlexDock--DockTabGroup #dockTitleBar { background: transparent; }
            QFlexDock--DockTabBar::tab {
                background: transparent; border: none; margin: 0; padding: 8px 16px;
            }
        )");
        const QColor own(0, 0, 0xff);
        const QColor behind(0xff, 0xff, 0);

        // Without it the content keeps its square corners.
        qApp->setStyleSheet(pane.arg(QString()));
        QCoreApplication::processEvents();
        QVERIFY(group->drawsPane());
        QRect host = group->contentHost()->geometry();
        QImage image = picture(group);
        QCOMPARE(image.pixelColor(host.bottomRight()), own);
        QCOMPARE(image.pixelColor(host.bottomLeft()), own);

        qApp->setStyleSheet(pane.arg(QStringLiteral("qproperty-paneCornerColor: #ffff00;")));
        QCoreApplication::processEvents();
        QCOMPARE(group->paneCornerColor(), behind);
        host = group->contentHost()->geometry();
        image = picture(group);
        QCOMPARE(image.pixelColor(host.bottomRight()), behind);
        QCOMPARE(image.pixelColor(host.bottomLeft()), behind);
        QCOMPARE(image.pixelColor(host.topRight()), behind);
        // Inside the curve, and everywhere else, the content is its own.
        QCOMPARE(image.pixelColor(host.right() - 12, host.bottom() - 12), own);
        QCOMPARE(image.pixelColor(host.left() + 40, host.bottom() - 40), own);
        QCOMPARE(image.pixelColor(host.center().x(), host.bottom()), own);
        // The mouse goes through the corners to the content.
        QCOMPARE(group->childAt(host.bottomRight() - QPoint(1, 1)), content);
        grab(&f.windowA, p("style-pane-corners"));

        // The corners follow the group as it is resized.
        f.windowA.resize(f.windowA.width() + 80, f.windowA.height() + 60);
        QCoreApplication::processEvents();
        QVERIFY(group->contentHost()->geometry() != host);
        host = group->contentHost()->geometry();
        image = picture(group);
        QCOMPARE(image.pixelColor(host.bottomRight()), behind);
        QCOMPARE(image.pixelColor(host.right() - 12, host.bottom() - 12), own);

        // Nor does a transparent color draw anything: that is how a rule for
        // some of the groups takes the corners away again.
        qApp->setStyleSheet(pane.arg(QStringLiteral("qproperty-paneCornerColor: transparent;")));
        QCoreApplication::processEvents();
        host = group->contentHost()->geometry();
        QCOMPARE(picture(group).pixelColor(host.bottomRight()), own);

        // A pane with square corners has none to draw.
        qApp->setStyleSheet(pane.arg(QStringLiteral("qproperty-paneCornerColor: #ffff00;"))
                                .replace(QStringLiteral("paneRadius: 12"), QStringLiteral("paneRadius: 0")));
        QCoreApplication::processEvents();
        host = group->contentHost()->geometry();
        QCOMPARE(picture(group).pixelColor(host.bottomRight()), own);
        qApp->setStyleSheet(QString());
    }

    void roundingKeepsSharpCornersSharp()
    {
        // A trapezoid like the edge areas: two right-ish corners at the wide
        // end are irrelevant here, the two acute ones at the outer side matter.
        DockOverlayScene scene;
        scene.bounds = QRect(0, 0, 400, 200);
        DockOverlayScene::Zone top;
        top.area = DockArea::Top;
        top.shape = QPolygonF({QPointF(0, 0), QPointF(400, 0), QPointF(300, 60), QPointF(100, 60)});
        scene.zones = {top};

        const auto render = [&scene](qreal radius) {
            DockOverlayStyle style;
            style.zoneColor = QColor(0, 0, 255);
            style.glyphColor = QColor(0, 0, 0, 0);
            style.borderWidth = 0;
            style.zoneGap = 0;
            style.cornerRadius = radius;
            QImage image(400, 200, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            DockDefaultOverlayPainter().paint(&painter, scene, style.resolved(QPalette()));
            painter.end();
            return image;
        };
        const QImage plain = render(0);
        const QImage rounded = render(12);

        // The acute corner at (0,0) has an angle of about 31 degrees. A circle
        // of radius 12 fitted into it would start 43 px away from the tip and
        // leave everything left of x = 30 or so empty. The area must reach
        // close to its tip instead...
        QVERIFY(rounded.pixelColor(16, 3).alpha() > 200);
        QVERIFY(rounded.pixelColor(384, 3).alpha() > 200);
        // ...while the very tip is still taken off,
        QVERIFY(plain.pixelColor(3, 1).alpha() > 200);
        QCOMPARE(rounded.pixelColor(1, 0).alpha(), 0);
        QCOMPARE(rounded.pixelColor(398, 0).alpha(), 0);
        // and the obtuse corners at the narrow end are softened as well.
        QVERIFY(rounded.pixelColor(100, 59).alpha() < plain.pixelColor(100, 59).alpha());
        // Away from the corners both are the same shape.
        QCOMPARE(rounded.pixelColor(200, 30), plain.pixelColor(200, 30));
        QCOMPARE(rounded.pixelColor(60, 20), plain.pixelColor(60, 20));
        QCOMPARE(rounded.pixelColor(200, 70).alpha(), 0);
    }

private:
    QString m_style;
    QPalette m_palette;
    QFont m_font;
};

QTEST_MAIN(tst_Style)
#include "tst_style.moc"
