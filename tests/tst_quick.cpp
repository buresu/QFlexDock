// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include <QFlexDockQuick/QmlDockController.h>
#include <QFlexDockQuick/QmlPanelAdapter.h>

#include "core/DockDragController.h"
#include "widgets/DockDropOverlay.h"

#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryDir>
#include <QtGui/QDragEnterEvent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickWidgets/QQuickWidget>
#include <QtTest/QSignalSpy>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

const char PanelQml[] = R"(
import QtQuick
import QFlexDock

Rectangle {
    id: root
    color: "#336699"

    // Bindings on the controller and on a panel object.
    property string active: dock.activePanel
    property string titleOfA: dock.panel("a") ? dock.panel("a").title : ""
    property bool aIsOpen: dock.panel("a") ? dock.panel("a").open : false
    property bool aIsCurrent: dock.panel("a") ? dock.panel("a").current : false
    property int openCount: dock.openPanelIds.length
    property var opened: []

    Connections {
        target: dock
        function onPanelOpenChanged(id, open) { root.opened = root.opened.concat([id + ":" + open]) }
    }

    function hide(id) { return dock.closePanel(id) }
    function hideBoth(a, b) { return dock.closePanels([a, b]) }
    function showBoth(a, b) { return dock.openPanels([a, b]) }
    function tabsOf(id) { return dock.tabGroupPanels(id) }
    function currentOf(id) { return dock.currentPanel(id) }
    function toggle(id) { return dock.togglePanel(id) }
    function activate(id) { return dock.activatePanel(id) }
    function raise(id) { return dock.raisePanel(id) }
    function moveBelow(id, other) { return dock.movePanel(id, other, Dock.Bottom) }
    function tabWith(id, other) { return dock.movePanel(id, other, Dock.Center) }
    function toWorkspace(id, workspace) { return dock.movePanelToWorkspace(id, workspace, Dock.Left) }
    function floatIt(id) { return dock.floatPanel(id) }
    function dockIt(id) { return dock.dockPanel(id) }
    function maximize(id) { return dock.maximizePanel(id) }
    function restore() { return dock.restoreMaximizedPanel() }
    function undo() { return dock.undo() }
}
)";

QVariant call(QObject *root, const char *function, const QVariantList &arguments = {})
{
    QVariant result;
    bool ok = false;
    switch (arguments.size()) {
    case 0:
        ok = QMetaObject::invokeMethod(root, function, Q_RETURN_ARG(QVariant, result));
        break;
    case 1:
        ok = QMetaObject::invokeMethod(root, function, Q_RETURN_ARG(QVariant, result),
                                       Q_ARG(QVariant, arguments.at(0)));
        break;
    default:
        ok = QMetaObject::invokeMethod(root, function, Q_RETURN_ARG(QVariant, result),
                                       Q_ARG(QVariant, arguments.at(0)),
                                       Q_ARG(QVariant, arguments.at(1)));
        break;
    }
    return ok ? result : QVariant();
}

} // namespace

class tst_Quick : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Deterministic and available everywhere, including offscreen. With
        // QFLEXDOCK_TEST_RHI set, the scenes are rendered as an application's
        // are instead (not on "offscreen"): a QQuickWidget then makes itself
        // a new scene window whenever it enters another top-level window.
        if (!qEnvironmentVariableIsSet("QFLEXDOCK_TEST_RHI"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QVERIFY(m_dir.isValid());
        QFile file(m_dir.filePath(p("Panel.qml")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(PanelQml);
        file.close();
        m_source = QUrl::fromLocalFile(file.fileName());
    }

    void qmlPanelIsShownAndMovedBetweenWindows()
    {
        QQmlEngine engine;
        TwoWindows f;
        QmlDockController controller(&f.manager);
        controller.installInto(&engine);
        f.show();

        DockPanel *panel = QmlPanelAdapter::registerPanel(&f.manager, p("q"), &engine, m_source,
                                                          p("QML"));
        QVERIFY(panel);
        QQuickWidget *quick = QmlPanelAdapter::quickWidget(panel);
        QVERIFY(quick);
        QCOMPARE(quick->status(), QQuickWidget::Ready);
        const QPointer<QQuickItem> root = quick->rootObject();
        QVERIFY(root);

        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("q"), p("a"), DockArea::Right));
        QVERIFY(quick->isVisible());
        QCOMPARE(quick->window(), &f.windowA);
        // The root item follows the size of the panel.
        QTRY_COMPARE(root->size().toSize(), quick->size());
        QTRY_COMPARE(quick->grab().toImage().pixelColor(quick->width() / 2, quick->height() / 2),
                     QColor(0x33, 0x66, 0x99));
        grab(&f.windowA, p("quick-panel"));

        // Into the other main window: same widget, same QML object tree.
        QSignalSpy reparented(panel, &DockPanel::reparented);
        QVERIFY(f.manager.movePanel(p("q"), f.b, DockArea::Center));
        QCOMPARE(QmlPanelAdapter::quickWidget(panel), quick);
        QCOMPARE(quick->rootObject(), root.data());
        QCOMPARE(quick->window(), &f.windowB);
        QCOMPARE(reparented.size(), 1);
        QCOMPARE(quick->status(), QQuickWidget::Ready);
        QTRY_COMPARE(root->size().toSize(), quick->size());
        QTRY_COMPARE(quick->grab().toImage().pixelColor(quick->width() / 2, quick->height() / 2),
                     QColor(0x33, 0x66, 0x99));

        // Floating, hidden behind another tab, closed and shown again.
        QVERIFY(f.manager.floatPanel(p("q"), QRect(50, 50, 300, 200)));
        QVERIFY(quick->isVisible());
        QVERIFY(quick->window() != &f.windowB);
        QVERIFY(f.manager.movePanel(p("q"), p("a"), DockArea::Center));
        QVERIFY(f.manager.activatePanel(p("a")));
        QVERIFY(!quick->isVisible());
        QVERIFY(f.manager.closePanel(p("q")));
        QVERIFY(f.manager.openPanel(p("q")));
        QVERIFY(quick->isVisible());
        QCOMPARE(quick->rootObject(), root.data());
        QTRY_COMPARE(quick->grab().toImage().pixelColor(quick->width() / 2, quick->height() / 2),
                     QColor(0x33, 0x66, 0x99));
    }

    void qmlDrivesTheDock()
    {
        QQmlEngine engine;
        TwoWindows f;
        QmlDockController controller(&f.manager);
        controller.installInto(&engine);
        f.show();
        QVERIFY(QmlPanelAdapter::registerPanel(&f.manager, p("q"), &engine, m_source));
        QObject *root = QmlPanelAdapter::quickWidget(f.manager.panel(p("q")))->rootObject();
        QVERIFY(root);
        QVERIFY(f.a->addPanel(p("q")));
        QVERIFY(f.a->addPanel(p("a"), DockArea::Right));
        QVERIFY(f.a->addPanel(p("b"), DockArea::Right));

        // Calls from QML change the layout.
        QCOMPARE(call(root, "moveBelow", {p("b"), p("a")}).toBool(), true);
        QCOMPARE(describe(f.a), p("H(q, V(a, b))"));
        QCOMPARE(call(root, "tabWith", {p("b"), p("a")}).toBool(), true);
        QCOMPARE(describe(f.a), p("H(q, a|b)"));
        QCOMPARE(call(root, "toWorkspace", {p("b"), p("B")}).toBool(), true);
        QCOMPARE(describe(f.b), p("b"));
        QCOMPARE(call(root, "hide", {p("a")}).toBool(), true);
        QVERIFY(!f.manager.panel(p("a"))->isOpen());
        QCOMPARE(call(root, "toggle", {p("a")}).toBool(), true);
        QVERIFY(f.manager.panel(p("a"))->isOpen());
        QCOMPARE(call(root, "floatIt", {p("a")}).toBool(), true);
        QVERIFY(f.manager.panel(p("a"))->isFloating());
        QCOMPARE(call(root, "dockIt", {p("a")}).toBool(), true);
        QVERIFY(!f.manager.panel(p("a"))->isFloating());
        QCOMPARE(call(root, "maximize", {p("a")}).toBool(), true);
        QCOMPARE(f.manager.maximizedPanel(), p("a"));
        QCOMPARE(controller.maximizedPanel(), p("a"));
        QCOMPARE(call(root, "restore").toBool(), true);
        QCOMPARE(call(root, "undo").toBool(), true);
        QCOMPARE(f.manager.maximizedPanel(), p("a"));

        // Bindings in QML follow the dock.
        QCOMPARE(call(root, "activate", {p("a")}).toBool(), true);
        QCOMPARE(root->property("active").toString(), p("a"));
        QVERIFY(f.manager.activatePanel(p("q")));
        QCOMPARE(root->property("active").toString(), p("q"));
        QCOMPARE(call(root, "raise", {p("a")}).toBool(), true);
        QCOMPARE(root->property("active").toString(), p("q"));
        QCOMPARE(root->property("aIsCurrent").toBool(), true);
        QCOMPARE(root->property("titleOfA").toString(), p("a"));
        f.manager.panel(p("a"))->setTitle(p("Renamed"));
        QCOMPARE(root->property("titleOfA").toString(), p("Renamed"));
        QCOMPARE(root->property("aIsOpen").toBool(), true);
        const int open = root->property("openCount").toInt();
        QVERIFY(f.manager.closePanel(p("a")));
        QCOMPARE(root->property("aIsOpen").toBool(), false);
        QCOMPARE(root->property("aIsCurrent").toBool(), false);
        QCOMPARE(root->property("openCount").toInt(), open - 1);
        QVERIFY(root->property("opened").toStringList().contains(p("a:false")));

        // Several at once, from a JavaScript array.
        QCOMPARE(call(root, "hideBoth", {p("q"), p("b")}).toBool(), true);
        QVERIFY(!f.manager.panel(p("q"))->isOpen());
        QVERIFY(!f.manager.panel(p("b"))->isOpen());
        QCOMPARE(call(root, "showBoth", {p("q"), p("b")}).toBool(), true);
        QVERIFY(f.manager.panel(p("q"))->isOpen());
        QCOMPARE(describe(f.b), p("b"));
        QCOMPARE(call(root, "tabsOf", {p("b")}).toStringList(), QStringList{p("b")});
        QVERIFY(call(root, "tabsOf", {p("nope")}).toStringList().isEmpty());
        QCOMPARE(call(root, "currentOf", {p("b")}).toString(), p("b"));
        QVERIFY(call(root, "currentOf", {p("nope")}).toString().isEmpty());

        // Failures are reported, not thrown, and change nothing.
        const QString before = describe(f.a);
        QSignalSpy errorChanged(&controller, &QmlDockController::lastErrorChanged);
        QCOMPARE(call(root, "hide", {p("no such panel")}).toBool(), false);
        QVERIFY(!controller.lastError().isEmpty());
        QCOMPARE(errorChanged.size(), 1);
        QCOMPARE(call(root, "toWorkspace", {p("q"), p("nowhere")}).toBool(), false);
        QCOMPARE(describe(f.a), before);
        QCOMPARE(call(root, "activate", {p("q")}).toBool(), true);
        QVERIFY(controller.lastError().isEmpty());

        QCOMPARE(controller.panelIds().size(), 7);
        QVERIFY(controller.hasPanel(p("q")));
        QVERIFY(!controller.isPanelOpen(p("a")));
        QVERIFY(!controller.panel(p("nope")));
    }

    void qmlPanelsAreCreatedLazilyAndDestroyedCleanly()
    {
        QQmlEngine engine;
        TwoWindows f;
        QmlDockController controller(&f.manager);
        controller.installInto(&engine);
        f.show();
        QVERIFY(f.a->addPanel(p("a")));

        DockPanel *lazy = QmlPanelAdapter::registerLazyPanel(&f.manager, p("lazy"), &engine, m_source);
        QVERIFY(lazy);
        QVERIFY(!QmlPanelAdapter::quickWidget(lazy)); // no QML loaded yet
        QVERIFY(f.manager.movePanel(p("lazy"), p("a"), DockArea::Right));
        const QPointer<QQuickWidget> quick = QmlPanelAdapter::quickWidget(lazy);
        QVERIFY(quick);
        QCOMPARE(quick->status(), QQuickWidget::Ready);
        const QPointer<QQuickItem> root = quick->rootObject();

        QSignalSpy panelsChanged(&controller, &QmlDockController::panelIdsChanged);
        QVERIFY(f.manager.unregisterPanel(p("lazy")));
        QVERIFY(!quick);
        QVERIFY(!root);
        QCOMPARE(describe(f.a), p("a"));
        QTRY_COMPARE(panelsChanged.size(), 1);
        QVERIFY(!controller.panelIds().contains(p("lazy")));

        // Bad arguments register nothing and load nothing.
        QVERIFY(!QmlPanelAdapter::registerPanel(&f.manager, p("a"), &engine, m_source));
        QCOMPARE(f.manager.lastError().error(), DockError::DuplicatePanel);
        QVERIFY(!QmlPanelAdapter::registerPanel(&f.manager, QString(), &engine, m_source));
        QVERIFY(!QmlPanelAdapter::registerPanel(&f.manager, p("x"), nullptr, m_source));
        QVERIFY(!QmlPanelAdapter::quickWidget(f.manager.panel(p("a")))); // a plain widget panel

        // A QML panel that is still open when the manager goes away.
        auto *manager = new DockManager;
        QMainWindow window;
        DockWorkspace *workspace = manager->createWorkspace();
        window.setCentralWidget(workspace);
        QmlDockController second(manager);
        DockPanel *panel = QmlPanelAdapter::registerPanel(manager, p("q"), &engine, m_source);
        QVERIFY(workspace->addPanel(p("q")));
        window.show();
        const QPointer<QQuickWidget> widget = QmlPanelAdapter::quickWidget(panel);
        delete manager;
        QVERIFY(!widget);
        QVERIFY(!second.manager());
        QVERIFY(!second.openPanel(p("q"))); // a controller without manager fails politely
        QVERIFY(second.panelIds().isEmpty());
    }

    // The controller has to be there for as long as a scene reads `dock`. As
    // a child of the engine it goes after the manager and its scenes, and no
    // binding is evaluated against a controller that is gone.
    void controllerOwnedByTheEngineOutlivesTheScenes()
    {
        QTest::failOnWarning(QRegularExpression(p("TypeError")));
        QPointer<QmlDockController> controller;
        {
            QQmlEngine engine;
            TwoWindows f;
            controller = new QmlDockController(&f.manager, &engine);
            controller->installInto(&engine);
            f.show();
            DockPanel *panel = QmlPanelAdapter::registerPanel(&f.manager, p("q"), &engine, m_source);
            QVERIFY(panel);
            QVERIFY(f.a->addPanel(p("q")));
            QVERIFY(f.a->addPanel(p("a"), DockArea::Right));
            const QObject *root = QmlPanelAdapter::quickWidget(panel)->rootObject();
            QVERIFY(root);
            QCOMPARE(root->property("titleOfA").toString(), p("a"));
        }
        QVERIFY(!controller);
    }

    // A QQuickWidget takes every drag that enters it, whatever its items make
    // of it. A dock drag is not for content: it has to get to the dock area
    // the panel is in, or there is no drop guide over a QML panel.
    void dockDragOverAQmlPanelReachesTheDockArea()
    {
        QQmlEngine engine;
        TwoWindows f;
        QmlDockController dock(&f.manager);
        dock.installInto(&engine);
        f.show();
        DockPanel *panel = QmlPanelAdapter::registerPanel(&f.manager, p("q"), &engine, m_source);
        QVERIFY(panel);
        QVERIFY(f.a->addPanel(p("q")));
        QVERIFY(f.a->addPanel(p("a"), DockArea::Right));
        QVERIFY(QTest::qWaitForWindowExposed(&f.windowA));
        QQuickWidget *quick = QmlPanelAdapter::quickWidget(panel);
        QVERIFY(quick && quick->isVisible() && quick->acceptDrops());
        DockAreaWidget *area = areaOf(f.a);
        DockDragController *controller = priv(f.manager)->drag;
        const QPoint middle = quick->rect().center();

        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        // Sent to the QML panel, as Qt does with a drag that is over it.
        QVERIFY(controller->begin(p("a"), false));
        const std::unique_ptr<QMimeData> mime(controller->createMimeData());
        QDragEnterEvent enter(middle, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(quick, &enter);
        QVERIFY(enter.isAccepted());
        QDragMoveEvent move(middle + QPoint(2, 2), Qt::MoveAction, mime.get(), Qt::LeftButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(quick, &move);
        QVERIFY(area->overlay()->isVisible());
        QVERIFY(area->overlay()->scene().preview.isValid());
        QVERIFY(move.isAccepted());
        // And the drop goes where the guide showed it: among the tabs there.
        QDropEvent drop(middle + QPoint(2, 2), Qt::MoveAction, mime.get(), Qt::LeftButton,
                        Qt::NoModifier);
        QCoreApplication::sendEvent(quick, &drop);
        QVERIFY(!controller->isActive());
        QVERIFY(!area->overlay()->isVisible());
        QCOMPARE(describe(f.a), p("q|a"));

        // Any other drag is the scene's to take or leave, as before.
        QMimeData text;
        text.setText(p("text"));
        QDragEnterEvent other(middle, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(quick, &other);
        QVERIFY(other.isAccepted());
        QVERIFY(!area->overlay()->isVisible());
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(quick, &leave);
        QT_WARNING_POP
    }

    // A button in a QML panel that floats, docks or closes that very panel:
    // the layout must not change while Qt Quick is still handing the click
    // to the scene, which the change takes to another window.
    void qmlPanelMovesItselfFromAClick()
    {
        QFile file(m_dir.filePath(p("Buttons.qml")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"(
import QtQuick
import QFlexDock

Rectangle {
    property string action: "float"
    property int clicks: 0
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            parent.clicks++
            if (parent.action === "float") dock.floatPanel("q")
            else if (parent.action === "dock") dock.dockPanel("q")
            else dock.closePanel("q")
        }
    }
}
)");
        file.close();

        QQmlEngine engine;
        TwoWindows f;
        QmlDockController dock(&f.manager);
        dock.installInto(&engine);
        f.show();
        DockPanel *panel = QmlPanelAdapter::registerPanel(&f.manager, p("q"), &engine,
                                                          QUrl::fromLocalFile(file.fileName()));
        QVERIFY(panel);
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.a->addPanel(p("q"), DockArea::Right));
        QVERIFY(QTest::qWaitForWindowExposed(&f.windowA));
        const QPointer<QQuickWidget> quick = QmlPanelAdapter::quickWidget(panel);
        QVERIFY(quick && quick->rootObject());
        QObject *root = quick->rootObject();
        const auto click = [&] {
            // The pointer is where the click is, as it is for a user: a
            // widget that goes or comes under it is told so by Qt at once.
            const QPoint at = quick->mapTo(quick->window(), quick->rect().center());
            QCursor::setPos(quick->mapToGlobal(quick->rect().center()));
            QTest::mouseMove(quick->window()->windowHandle(), at);
            QTest::mouseClick(quick->window()->windowHandle(), Qt::LeftButton, {}, at);
        };

        click();
        QCOMPARE(root->property("clicks").toInt(), 1);
        QTRY_VERIFY(panel->isFloating());
        QVERIFY(quick);
        QCOMPARE(quick->rootObject(), root);
        QVERIFY(QTest::qWaitForWindowExposed(quick->window()));

        root->setProperty("action", p("dock"));
        click();
        QCOMPARE(root->property("clicks").toInt(), 2);
        QTRY_VERIFY(!panel->isFloating());
        QCOMPARE(quick->window(), &f.windowA);
        QCOMPARE(describe(f.a), p("H(a, q)"));

        root->setProperty("action", p("hide"));
        click();
        QCOMPARE(root->property("clicks").toInt(), 3);
        QTRY_VERIFY(!panel->isOpen());
        QVERIFY(quick);

        // The scene is still good for the next click.
        QVERIFY(f.manager.openPanel(p("q")));
        root->setProperty("action", p("float"));
        click();
        QCOMPARE(root->property("clicks").toInt(), 4);
        QTRY_VERIFY(panel->isFloating());
    }

private:
    QTemporaryDir m_dir;
    QUrl m_source;
};

QTEST_MAIN(tst_Quick)
#include "tst_quick.moc"
