// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include "persistence/LayoutSerializer.h"
#include "widgets/DockFloatingWindow.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRandomGenerator>
#include <QtCore/QTemporaryDir>
#include <QtGui/QScreen>
#include <QtTest/QSignalSpy>

using namespace QFlexDock;
using namespace TestUtils;

namespace {

QJsonObject parse(const QByteArray &bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}

QByteArray bytes(const QJsonObject &object)
{
    return QJsonDocument(object).toJson();
}

QJsonObject tabs(const QStringList &panels, const QString &active = {}, double weight = 1.0)
{
    return QJsonObject{{p("type"), p("tabs")},
                       {p("weight"), weight},
                       {p("panels"), QJsonArray::fromStringList(panels)},
                       {p("active"), active.isEmpty() ? panels.value(0) : active}};
}

QJsonObject split(const char *orientation, const QJsonArray &children, double weight = 1.0)
{
    return QJsonObject{{p("type"), p("split")},
                       {p("weight"), weight},
                       {p("orientation"), p(orientation)},
                       {p("children"), children}};
}

// A document with one workspace "A" showing `layout`.
QJsonObject document(const QJsonValue &layout, int version = LayoutSerializer::SchemaVersion)
{
    const QJsonObject window{{p("id"), p("A")}, {p("kind"), p("workspace")}, {p("layout"), layout}};
    return QJsonObject{{p("format"), p("QFlexDock.Layout")},
                       {p("schemaVersion"), version},
                       {p("windows"), QJsonArray{window}}};
}

// A layout that uses every feature that is saved.
void buildRichLayout(TwoWindows &f)
{
    QVERIFY(f.a->addPanel(p("a")));
    QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right, -1, 0.3));
    QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Center));
    QVERIFY(f.manager.movePanel(p("d"), p("a"), DockArea::Bottom, -1, 0.4));
    QVERIFY(f.b->addPanel(p("e")));
    QVERIFY(f.manager.activatePanel(p("b")));
}

} // namespace

class tst_Persistence : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void savedDocumentShape()
    {
        TwoWindows f;
        f.show();
        buildRichLayout(f);
        const QJsonObject json = parse(f.manager.saveLayout());
        QCOMPARE(json.value(p("format")).toString(), p("QFlexDock.Layout"));
        QCOMPARE(json.value(p("schemaVersion")).toInt(), 1);

        const QJsonArray windows = json.value(p("windows")).toArray();
        QCOMPARE(windows.size(), 2);
        const QJsonObject a = windows.at(0).toObject();
        QCOMPARE(a.value(p("id")).toString(), p("A"));
        QCOMPARE(a.value(p("kind")).toString(), p("workspace"));
        QCOMPARE(a.value(p("geometry")).toObject().value(p("width")).toInt(), 900);
        QVERIFY(a.contains(p("autoHide")));

        // Logical tree with orientations, weights, panel ids, order, active tab.
        const QJsonObject root = a.value(p("layout")).toObject();
        QCOMPARE(root.value(p("type")).toString(), p("split"));
        QCOMPARE(root.value(p("orientation")).toString(), p("horizontal"));
        const QJsonArray children = root.value(p("children")).toArray();
        QCOMPARE(children.size(), 2);
        const QJsonObject right = children.at(1).toObject();
        QCOMPARE(right.value(p("type")).toString(), p("tabs"));
        QCOMPARE(right.value(p("panels")).toArray(), QJsonArray({p("b"), p("c")}));
        QCOMPARE(right.value(p("active")).toString(), p("b"));
        QVERIFY(qAbs(right.value(p("weight")).toDouble() - 0.3) < 1e-9);

        // Only stable ids: nothing that looks like an address or a node id.
        const QByteArray text = f.manager.saveLayout();
        QVERIFY(!text.contains("0x"));
        QVERIFY(!text.contains("nodeId"));
    }

    void roundTripRestoresEverything()
    {
        TwoWindows f;
        f.show();
        buildRichLayout(f);
        QVERIFY(f.manager.floatPanel(p("d"), QRect(70, 80, 300, 220)));
        QVERIFY(f.manager.setPanelAutoHide(p("e"), true, DockArea::Bottom));
        QVERIFY(f.manager.movePanel(p("f"), f.b, DockArea::Center));
        QVERIFY(f.manager.closePanel(p("c")));
        QVERIFY(f.manager.maximizePanel(p("a")));
        const QByteArray saved = f.manager.saveLayout();
        QWidget *contentA = f.widgets[p("a")];

        // Wreck the layout, then restore.
        QVERIFY(f.manager.restoreMaximizedPanel());
        QVERIFY(f.manager.dockPanel(p("d")));
        QVERIFY(f.manager.movePanel(p("a"), f.b, DockArea::Top));
        QVERIFY(f.manager.openPanel(p("c")));
        QVERIFY(f.manager.setPanelAutoHide(p("e"), false));
        QVERIFY(f.manager.closePanel(p("b")));
        QVERIFY(f.manager.saveLayout() != saved);

        DockRestoreReport report;
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);
        QVERIFY(f.manager.restoreLayout(saved, &report));
        QVERIFY(report.warnings.isEmpty());
        QVERIFY(report.missingPanels.isEmpty());
        QCOMPARE(changed.size(), 1); // one transaction

        QCOMPARE(describe(f.a), p("H(a, b)"));
        QCOMPARE(describe(f.b), p("f"));
        QCOMPARE(f.a->maximizedPanel(), p("a"));
        QVERIFY(f.manager.panel(p("d"))->isFloating());
        QTRY_COMPARE(f.widgets[p("d")]->window()->size(), QSize(300, 220));
        if (windowPositionsWork())
            QCOMPARE(f.widgets[p("d")]->window()->geometry().topLeft(), QPoint(70, 80));
        QVERIFY(f.manager.panel(p("e"))->isAutoHidden());
        QVERIFY(!f.manager.panel(p("c"))->isOpen());
        QVERIFY(qAbs(areaOf(f.a)->tree().root()->children[1].weight - 0.3) < 1e-9);
        // The same widgets, not copies.
        QCOMPARE(f.manager.panel(p("a"))->widget(), contentA);
        QCOMPARE(contentA->window(), &f.windowA);

        // Saving again yields the identical document.
        QCOMPARE(f.manager.saveLayout(), saved);

        // The hidden panel still knows it belongs next to b.
        QVERIFY(f.manager.restoreMaximizedPanel());
        QVERIFY(f.manager.openPanel(p("c")));
        QCOMPARE(describe(f.a), p("H(a, b|c)"));

        // And the restore itself can be undone.
        QVERIFY(f.manager.undo());
        QVERIFY(f.manager.undo());
        QVERIFY(f.manager.undo());
        QVERIFY(!f.manager.panel(p("b"))->isOpen());
    }

    void iconifiedColumnsAreSavedAndRestored()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        QVERIFY(f.manager.movePanel(p("b"), p("a"), DockArea::Right));
        QVERIFY(f.manager.movePanel(p("c"), p("b"), DockArea::Bottom));
        QVERIFY(f.manager.movePanel(p("d"), p("a"), DockArea::Left));
        QVERIFY(f.manager.setColumnIconified(p("b"), true));
        QVERIFY(f.manager.setColumnIconified(p("d"), true));
        const QByteArray saved = f.manager.saveLayout();
        QVERIFY(saved.contains("\"iconified\": true"));
        QCOMPARE(saved.count("iconified"), 2); // only where it is

        QVERIFY(f.manager.setColumnIconified(p("b"), false));
        QVERIFY(f.manager.closePanel(p("d")));
        DockRestoreReport report;
        QVERIFY(f.manager.restoreLayout(saved, &report));
        QVERIFY(report.warnings.isEmpty());
        QCOMPARE(describe(f.a), p("H(d, a, V(b, c))"));
        QVERIFY(f.manager.isColumnIconified(p("b")));
        QVERIFY(f.manager.isColumnIconified(p("c")));
        QVERIFY(f.manager.isColumnIconified(p("d")));
        QVERIFY(!f.manager.isColumnIconified(p("a")));
        QCOMPARE(areaOf(f.a)->iconStrips().size(), 2);
        QVERIFY(!f.widgets[p("b")]->isVisible());
        QCOMPARE(f.manager.saveLayout(), saved);

        // A column that is iconified inside another one is repaired.
        QByteArray nested = saved;
        nested.replace("\"active\": \"b\",", "\"active\": \"b\", \"iconified\": true,");
        QVERIFY(nested != saved);
        QVERIFY(f.manager.restoreLayout(nested));
        QVERIFY(f.manager.isColumnIconified(p("c")));
        QCOMPARE(f.manager.saveLayout(), saved);
    }

    void restoreIntoAFreshApplication()
    {
        QByteArray saved;
        {
            TwoWindows first;
            first.show();
            buildRichLayout(first);
            QVERIFY(first.manager.floatPanel(p("d"), QRect(70, 80, 300, 220)));
            saved = first.manager.saveLayout();
        }
        TwoWindows f; // new manager, new windows, new widgets, same ids
        f.show();
        QVERIFY(f.manager.restoreLayout(saved));
        QCOMPARE(describe(f.a), p("H(a, b|c)"));
        QCOMPARE(describe(f.b), p("e"));
        QVERIFY(f.manager.panel(p("d"))->isFloating());
        QCOMPARE(areaOf(f.a)->tree().findPanel(p("b"))->active, p("b"));
        QVERIFY(f.widgets[p("b")]->isVisible());
        QCOMPARE(f.manager.saveLayout(), saved);
    }

    void fileRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(p("layout.json"));
        TwoWindows f;
        f.show();
        buildRichLayout(f);
        QVERIFY(f.manager.saveLayout(path));
        const QString before = describe(f.a);
        QVERIFY(f.manager.closePanel(p("a")));
        QVERIFY(f.manager.loadLayout(path));
        QCOMPARE(describe(f.a), before);

        QCOMPARE(f.manager.loadLayout(dir.filePath(p("missing.json"))).error(), DockError::IoError);
        QCOMPARE(f.manager.saveLayout(dir.filePath(p("no/such/dir/x.json"))).error(),
                 DockError::IoError);
        QCOMPARE(describe(f.a), before);
    }

    void windowGeometryIsRestoredOnRequest()
    {
        // On Wayland the compositor decides position and, for programmatic
        // changes arriving in quick succession, effectively also size.
        if (!windowPositionsWork())
            QSKIP("Top-level window geometry is managed by the compositor on Wayland");
        TwoWindows f;
        f.show();
        buildRichLayout(f);
        f.windowA.setGeometry(40, 50, 820, 560);
        QTRY_COMPARE(f.windowA.size(), QSize(820, 560));
        const QByteArray saved = f.manager.saveLayout();

        f.windowA.setGeometry(10, 10, 600, 400);
        QTRY_COMPARE(f.windowA.size(), QSize(600, 400));
        QVERIFY(f.manager.restoresWindowGeometry());
        QVERIFY(f.manager.restoreLayout(saved));
        QTRY_COMPARE(f.windowA.geometry(), QRect(40, 50, 820, 560));

        f.windowA.setGeometry(10, 10, 600, 400);
        QTRY_COMPARE(f.windowA.size(), QSize(600, 400));
        f.manager.setRestoresWindowGeometry(false);
        QVERIFY(f.manager.restoreLayout(saved));
        QCoreApplication::processEvents();
        QCOMPARE(f.windowA.size(), QSize(600, 400));
    }

    void missingPanelsAreKeptForLater()
    {
        QByteArray saved;
        {
            TwoWindows first;
            first.show();
            buildRichLayout(first);
            saved = first.manager.saveLayout();
        }
        // This time panels b and d do not exist (plugin not loaded yet).
        DockManager manager;
        QMainWindow window;
        DockWorkspace *a = manager.createWorkspace(p("A"));
        window.setCentralWidget(a);
        manager.registerPanel(p("a"), new QLabel(p("A")));
        manager.registerPanel(p("c"), new QLabel(p("C")));
        window.show();

        DockRestoreReport report;
        QVERIFY(manager.restoreLayout(saved, &report));
        QStringList missing = report.missingPanels;
        missing.sort();
        QCOMPARE(missing, QStringList({p("b"), p("d"), p("e")}));
        QCOMPARE(report.unknownWorkspaces, QStringList{p("B")});
        QCOMPARE(describe(a), p("H(a, c)"));
        QVERIFY(areaOf(a)->tree().validate());

        // The plugin arrives: its panels go where the layout had them.
        manager.registerPanel(p("b"), new QLabel(p("B")));
        QCOMPARE(describe(a), p("H(a, b|c)"));
        manager.registerPanel(p("d"), new QLabel(p("D")));
        QCOMPARE(describe(a), p("H(V(a, d), b|c)"));
        QVERIFY(qAbs(areaOf(a)->tree().findPanel(p("d"))->weight - 0.4) < 1e-9);

        // Saving while panels are missing does not lose their place either.
        const QByteArray again = manager.saveLayout();
        QVERIFY(manager.unregisterPanel(p("d")));
        const QByteArray withoutD = manager.saveLayout();
        QVERIFY(parse(withoutD).value(p("panelMemory")).toObject().contains(p("d")));
        manager.registerPanel(p("d"), new QLabel(p("D2")));
        QCOMPARE(manager.saveLayout(), again);
    }

    void restoreDoesNotCreateBackgroundTabs()
    {
        DockManager manager;
        QMainWindow window;
        DockWorkspace *a = manager.createWorkspace(p("A"));
        window.setCentralWidget(a);
        int created = 0;
        const DockPanelFactory factory = [&created](const PanelId &id) {
            ++created;
            return new QLabel(id);
        };
        manager.registerPanelFactory(p("x"), factory);
        manager.registerPanelFactory(p("y"), factory);
        manager.registerPanelFactory(p("z"), factory);
        window.show();

        QVERIFY(manager.restoreLayout(bytes(document(tabs({p("x"), p("y"), p("z")}, p("y"))))));
        QCOMPARE(describe(a), p("x|y|z"));
        QCOMPARE(created, 1); // only the visible one
        QVERIFY(manager.panel(p("y"))->widget());
        QVERIFY(!manager.panel(p("x"))->widget());
        QVERIFY(manager.activatePanel(p("x")));
        QCOMPARE(created, 2);
    }

    void unusableDocumentsAreRejectedWithoutSideEffects_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<DockError>("error");

        QTest::newRow("empty") << QByteArray() << DockError::ParseError;
        QTest::newRow("garbage") << QByteArray("\x01\x02 not json at all") << DockError::ParseError;
        QTest::newRow("truncated") << QByteArray("{\"format\":\"QFlexDock.Layout\",\"windows\":[")
                                   << DockError::ParseError;
        QTest::newRow("array root") << QByteArray("[1,2,3]") << DockError::ParseError;
        QTest::newRow("other format") << bytes(QJsonObject{{p("format"), p("SomethingElse")},
                                                           {p("schemaVersion"), 1}})
                                      << DockError::ParseError;
        QJsonObject noVersion = document(tabs({p("a")}));
        noVersion.remove(p("schemaVersion"));
        QTest::newRow("no version") << bytes(noVersion) << DockError::ParseError;
        QJsonObject badVersion = document(tabs({p("a")}));
        badVersion.insert(p("schemaVersion"), p("one"));
        QTest::newRow("version not a number") << bytes(badVersion) << DockError::ParseError;
        QTest::newRow("newer version") << bytes(document(tabs({p("a")}), 99))
                                       << DockError::UnsupportedVersion;
        QTest::newRow("older version without migration") << bytes(document(tabs({p("a")}), 0))
                                                         << DockError::UnsupportedVersion;
        QJsonObject noWindows = document(tabs({p("a")}));
        noWindows.insert(p("windows"), p("none"));
        QTest::newRow("windows not an array") << bytes(noWindows) << DockError::ParseError;
    }

    void unusableDocumentsAreRejectedWithoutSideEffects()
    {
        QFETCH(QByteArray, data);
        QFETCH(DockError, error);
        TwoWindows f;
        f.show();
        buildRichLayout(f);
        const QByteArray before = f.manager.saveLayout();
        QSignalSpy changed(&f.manager, &DockManager::layoutChanged);
        const bool couldUndo = f.manager.canUndo();

        DockRestoreReport report;
        const DockResult result = f.manager.restoreLayout(data, &report);
        QCOMPARE(result.error(), error);
        QVERIFY(!result.message().isEmpty());
        QCOMPARE(f.manager.saveLayout(), before);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(f.manager.canUndo(), couldUndo);
        QVERIFY(f.widgets[p("a")]->isVisible());
    }

    void damagedDocumentsAreRepaired_data()
    {
        QTest::addColumn<QJsonValue>("layout");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("minWarnings");

        QTest::newRow("duplicate panel")
            << QJsonValue(split("horizontal", {tabs({p("a"), p("b")}), tabs({p("b"), p("c")})}))
            << p("H(a|b, c)") << 1;
        QTest::newRow("duplicate inside one group")
            << QJsonValue(tabs({p("a"), p("a"), p("b")})) << p("a|b") << 1;
        QTest::newRow("unknown node type")
            << QJsonValue(split("horizontal", {tabs({p("a")}),
                                               QJsonObject{{p("type"), p("carousel")},
                                                           {p("panels"), QJsonArray{p("b")}}},
                                               tabs({p("c")})}))
            << p("H(a, c)") << 1;
        QTest::newRow("node that is not an object")
            << QJsonValue(split("vertical", {tabs({p("a")}), 42, tabs({p("b")})}))
            << p("V(a, b)") << 1;
        QTest::newRow("empty group and one-child split")
            << QJsonValue(split("horizontal",
                                {tabs({p("a")}), split("vertical", {tabs({}), tabs({p("b")})})}))
            << p("H(a, b)") << 0;
        QTest::newRow("same orientation nested")
            << QJsonValue(split("horizontal",
                                {tabs({p("a")}), split("horizontal", {tabs({p("b")}), tabs({p("c")})})}))
            << p("H(a, b, c)") << 0;
        QTest::newRow("bad weights")
            << QJsonValue(split("horizontal", {tabs({p("a")}, {}, -5.0), tabs({p("b")}, {}, 0.0),
                                               tabs({p("c")}, {}, 1e308 * 10)}))
            << p("H(a, b, c)") << 0;
        QTest::newRow("bad orientation")
            << QJsonValue(split("diagonal", {tabs({p("a")}), tabs({p("b")})})) << p("H(a, b)") << 1;
        QTest::newRow("active tab not in group")
            << QJsonValue(tabs({p("a"), p("b")}, p("zzz"))) << p("a|b") << 0;
        QTest::newRow("panel ids of wrong type")
            << QJsonValue(QJsonObject{{p("type"), p("tabs")},
                                      {p("panels"), QJsonArray{p("a"), 7, QJsonValue(), p(""), p("b")}}})
            << p("a|b") << 0;
        QTest::newRow("nothing usable") << QJsonValue(p("???")) << p("<empty>") << 1;
        QTest::newRow("null layout") << QJsonValue() << p("<empty>") << 0;

        // Nesting far beyond anything a user builds: cut off, not crashed on.
        QJsonValue deep = tabs({p("a")});
        for (int i = 0; i < 400; ++i)
            deep = split(i % 2 ? "horizontal" : "vertical", {deep, tabs({})});
        QTest::newRow("absurd nesting")
            << QJsonValue(split("horizontal", {tabs({p("b")}), deep})) << p("b") << 1;
    }

    void damagedDocumentsAreRepaired()
    {
        QFETCH(QJsonValue, layout);
        QFETCH(QString, expected);
        QFETCH(int, minWarnings);
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("f")));

        DockRestoreReport report;
        const DockResult result = f.manager.restoreLayout(bytes(document(layout)), &report);
        QVERIFY2(result, qPrintable(result.message()));
        QCOMPARE(describe(f.a), expected);
        QVERIFY(areaOf(f.a)->tree().validate());
        QVERIFY2(report.warnings.size() >= minWarnings, qPrintable(report.warnings.join(u'\n')));
        // Every placed panel is shown exactly once.
        for (const PanelId &panel : f.a->panels())
            QCOMPARE(f.widgets[panel]->window(), &f.windowA);
        QVERIFY(!f.manager.panel(p("f"))->isOpen());
    }

    void damagedWindowEntriesAreSkipped()
    {
        TwoWindows f;
        f.show();
        QJsonObject json = document(tabs({p("a")}));
        QJsonArray windows = json.value(p("windows")).toArray();
        windows.append(QJsonObject{{p("id"), p("A")}, {p("kind"), p("workspace")},
                                   {p("layout"), tabs({p("b")})}});              // duplicate id
        windows.append(QJsonObject{{p("kind"), p("workspace")}});                 // no id
        windows.append(QJsonObject{{p("id"), p("X")}, {p("kind"), p("hologram")}}); // unknown kind
        windows.append(p("not an object"));
        windows.append(QJsonObject{{p("id"), p("F1")}, {p("kind"), p("floating")},
                                   {p("layout"), QJsonValue()}});                 // empty floating
        windows.append(QJsonObject{{p("id"), p("F2")}, {p("kind"), p("floating")},
                                   {p("owner"), p("nobody")},
                                   {p("geometry"), QJsonObject{{p("x"), p("left")}}},
                                   {p("layout"), tabs({p("c")})}});
        windows.append(QJsonObject{{p("id"), p("B")}, {p("kind"), p("workspace")},
                                   {p("layout"), tabs({p("d")})},
                                   {p("maximizedPanel"), p("not here")},
                                   {p("autoHide"), QJsonObject{{p("left"), QJsonArray{p("e"), p("a")}},
                                                               {p("up"), QJsonArray{p("f")}}}}});
        json.insert(p("windows"), windows);
        json.insert(p("panelMemory"), QJsonObject{{p("f"), p("garbage")},
                                                  {p("zz"), QJsonObject{{p("fraction"), 9.0}}}});

        DockRestoreReport report;
        QVERIFY(f.manager.restoreLayout(bytes(json), &report));
        QVERIFY(report.warnings.size() >= 5);
        QCOMPARE(describe(f.a), p("a"));
        QCOMPARE(describe(f.b), p("d"));
        QVERIFY(!f.manager.panel(p("b"))->isOpen());
        QVERIFY(f.manager.panel(p("c"))->isFloating());
        QCOMPARE(f.manager.panel(p("c"))->workspace(), f.a); // an owner that exists
        QVERIFY(f.manager.panel(p("e"))->isAutoHidden());
        QCOMPARE(f.manager.maximizedPanel(), QString());
        QCOMPARE(priv(f.manager)->floatingWindows.size(), 1);
        QVERIFY(priv(f.manager)->state.validate());
    }

    void olderSchemaVersionsAreMigrated()
    {
        // A made-up "version 0" that called the window list "areas".
        QJsonObject old = document(tabs({p("a"), p("b")}), 0);
        old.insert(p("areas"), old.take(p("windows")));

        LayoutMigration migration;
        LayoutSerializer::Document result;
        QCOMPARE(LayoutSerializer::fromJson(old, &result, nullptr, migration).error(),
                 DockError::UnsupportedVersion);

        migration.addStep(0, [](const QJsonObject &json) {
            QJsonObject upgraded = json;
            upgraded.insert(p("windows"), upgraded.take(p("areas")));
            return upgraded;
        });
        QStringList warnings;
        QVERIFY(LayoutSerializer::fromJson(old, &result, &warnings, migration));
        QCOMPARE(result.state.containers.size(), size_t(1));
        QCOMPARE(describe(result.state.containers[0].tree), p("a|b"));

        // Versions newer than this build are never "migrated" down.
        QCOMPARE(LayoutSerializer::fromJson(document(tabs({p("a")}), 2), &result, nullptr, migration)
                     .error(),
                 DockError::UnsupportedVersion);
        QVERIFY(LayoutSerializer::fromJson(document(tabs({p("a")})), &result));
    }

    void windowsAreBroughtBackOnScreen()
    {
        const QList<QRect> screens{QRect(0, 0, 1920, 1080), QRect(1920, 0, 1280, 1024)};
        const auto fit = [&](const QRect &r) { return LayoutSerializer::fitToScreens(r, screens); };

        // Fine as it is, also when straddling two screens.
        QCOMPARE(fit(QRect(100, 100, 800, 600)), QRect(100, 100, 800, 600));
        QCOMPARE(fit(QRect(1800, 200, 400, 300)), QRect(1800, 200, 400, 300));
        QCOMPARE(fit(QRect(2000, 50, 600, 400)), QRect(2000, 50, 600, 400));

        // The monitor it was on is gone.
        const QRect unplugged = fit(QRect(4000, 300, 800, 600));
        QCOMPARE(unplugged.size(), QSize(800, 600));
        QVERIFY(screens[1].contains(unplugged));
        const QRect above = fit(QRect(300, -2000, 800, 600));
        QVERIFY(screens[0].contains(above));

        // Only a sliver visible, or the title bar above the screen.
        QVERIFY(screens[0].contains(fit(QRect(-780, 100, 800, 600))));
        QCOMPARE(fit(QRect(200, -50, 800, 600)).top(), 0);

        // Larger than any screen now (lower resolution): shrunk to fit.
        const QRect huge = fit(QRect(-100, -100, 5000, 3000));
        QVERIFY(screens[0].contains(huge));
        QCOMPARE(huge.size(), screens[0].size());

        // Nothing to go by: left alone.
        QCOMPARE(LayoutSerializer::fitToScreens(QRect(5, 5, 10, 10), {}), QRect(5, 5, 10, 10));
        QCOMPARE(fit(QRect()), QRect());
    }

    void floatingWindowFromAnotherMonitorIsRestoredOnScreen()
    {
        TwoWindows f;
        f.show();
        QJsonObject json = document(tabs({p("a")}));
        QJsonArray windows = json.value(p("windows")).toArray();
        windows.append(QJsonObject{
            {p("id"), p("floating-1")}, {p("kind"), p("floating")}, {p("owner"), p("A")},
            {p("geometry"), QJsonObject{{p("x"), 9000}, {p("y"), -7000}, {p("width"), 320},
                                        {p("height"), 240}}},
            {p("layout"), tabs({p("b")})}});
        json.insert(p("windows"), windows);

        QVERIFY(f.manager.restoreLayout(bytes(json)));
        DockFloatingWindow *window = priv(f.manager)->floatingWindows.value(p("floating-1"));
        QVERIFY(window);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        QVERIFY2(screen.contains(window->geometry()),
                 qPrintable(QStringLiteral("%1,%2").arg(window->x()).arg(window->y())));
        QCOMPARE(window->size(), QSize(320, 240));
    }

    // Whatever sequence of operations led to a layout, saving and restoring it
    // reproduces it exactly.
    void randomLayoutsRoundTrip()
    {
        QRandomGenerator rng(4242);
        const DockArea areas[] = {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                                  DockArea::Center};
        TwoWindows f(12);
        f.show();
        const QStringList ids = f.widgets.keys();
        for (int round = 0; round < 25; ++round) {
            for (int step = 0; step < 30; ++step) {
                const PanelId panel = ids.at(rng.bounded(int(ids.size())));
                const PanelId other = ids.at(rng.bounded(int(ids.size())));
                switch (rng.bounded(8)) {
                case 0:
                    (void)f.manager.movePanel(panel, rng.bounded(2) ? f.a : f.b, areas[rng.bounded(5)],
                                              0.1 + rng.bounded(0.8));
                    break;
                case 1:
                case 2:
                    (void)f.manager.movePanel(panel, other, areas[rng.bounded(5)], rng.bounded(3) - 1,
                                              0.1 + rng.bounded(0.8));
                    break;
                case 3:
                    (void)f.manager.togglePanel(panel);
                    break;
                case 4:
                    (void)f.manager.floatPanel(panel, QRect(rng.bounded(300), rng.bounded(300), 260, 200));
                    break;
                case 5:
                    (void)f.manager.setPanelAutoHide(panel, rng.bounded(2), areas[rng.bounded(4)]);
                    break;
                case 6:
                    (void)f.manager.moveTabGroup(panel, other, areas[rng.bounded(5)]);
                    break;
                case 7:
                    (void)(rng.bounded(2) ? f.manager.maximizePanel(panel)
                                          : f.manager.restoreMaximizedPanel());
                    break;
                }
                const DockResult valid = priv(f.manager)->state.validate();
                QVERIFY2(valid, qPrintable(valid.message()));
            }
            const QByteArray saved = f.manager.saveLayout();
            // Scramble, restore, compare.
            for (int i = 0; i < 6; ++i)
                (void)f.manager.togglePanel(ids.at(rng.bounded(int(ids.size()))));
            DockRestoreReport report;
            QVERIFY(f.manager.restoreLayout(saved, &report));
            QVERIFY2(report.warnings.isEmpty(), qPrintable(report.warnings.join(u'\n')));
            QCOMPARE(f.manager.saveLayout(), saved);
        }
    }
};

QTEST_MAIN(tst_Persistence)
#include "tst_persistence.moc"
