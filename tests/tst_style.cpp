// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include "core/DockDragController.h"
#include "widgets/DockAutoHide.h"
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
    return widget->grab().toImage().pixelColor(pos);
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
        QVERIFY(f.manager.hidePanel(p("e")));
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

    // The parts that come with GroupHeader::TitleBar, with the title buttons
    // and with a panel that has no header.
    void styleSheetsStyleTheTitleBarHeader()
    {
        TwoWindows f;
        f.manager.setGroupHeader(DockManager::GroupHeader::TitleBar);
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
