// SPDX-License-Identifier: MIT
#include "TestUtils.h"

#include <QFlexDock/NativeWindowAdapter.h>

#include "core/DockDragController.h"
#include "widgets/DockDropOverlay.h"

#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QWindow>
#include <QtTest/QSignalSpy>

#ifdef QFLEXDOCK_TEST_HAS_OPENGLWIDGETS
#  include <QtOpenGLWidgets/QOpenGLWidget>
#endif

using namespace QFlexDock;
using namespace TestUtils;

#ifdef QFLEXDOCK_TEST_HAS_OPENGLWIDGETS
namespace {

/// A GL panel written the way Qt documents it: resources are created in
/// initializeGL() and released when the context is about to go away.
class GlPanel : public QOpenGLWidget
{
public:
    int initialized = 0;
    int released = 0;
    int painted = 0;
    bool hasResources = false;

protected:
    void initializeGL() override
    {
        ++initialized;
        hasResources = true;
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
            ++released;
            hasResources = false;
        }, Qt::DirectConnection);
    }

    void paintGL() override
    {
        ++painted;
        QOpenGLFunctions *gl = context()->functions();
        gl->glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
    }
};

} // namespace
#endif

class tst_Gpu : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nativeWindowIsEmbeddedAndMoved()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));

        auto *window = new QWindow;
        window->setSurfaceType(QSurface::RasterSurface);
        NativeWindowAdapter *adapter = NativeWindowAdapter::registerPanel(&f.manager, p("native"),
                                                                         window, p("Native"));
        QVERIFY(adapter);
        const QPointer<QWindow> windowGuard(window);
        const QPointer<NativeWindowAdapter> adapterGuard(adapter);
        const QPointer<QWidget> container = adapter->container();
        DockPanel *panel = adapter->panel();
        QVERIFY(panel);
        QCOMPARE(panel->widget(), container.data());
        QCOMPARE(adapter->window(), window);
        QVERIFY(panel->hidesContentDuringDrag());
        QVERIFY(!window->isVisible());

        QSignalSpy created(adapter, &NativeWindowAdapter::surfaceCreated);
        QSignalSpy destroyed(adapter, &NativeWindowAdapter::surfaceAboutToBeDestroyed);
        QSignalSpy exposed(adapter, &NativeWindowAdapter::exposedChanged);
        QSignalSpy topLevelChanged(panel, &DockPanel::topLevelChanged);
        QSignalSpy visibilityChanged(panel, &DockPanel::visibilityChanged);

        // Shown in a panel: the window gets its native surface.
        QVERIFY(f.manager.movePanel(p("native"), p("a"), DockArea::Right));
        QTRY_VERIFY(window->isVisible());
        QVERIFY(adapter->hasSurface());
        QTRY_VERIFY(created.size() >= 1);
        QCOMPARE(container->window(), &f.windowA);
        QVERIFY(visibilityChanged.size() >= 1);
        QCOMPARE(visibilityChanged.constLast().at(0).toBool(), true);
        // It fills the panel's content area.
        QTRY_COMPARE(window->size(), container->size());

        // Moved into the other main window: the same QWindow object, and the
        // application was told about anything that happened to its surface.
        const int createdBefore = int(created.size());
        const int destroyedBefore = int(destroyed.size());
        QVERIFY(f.manager.movePanel(p("native"), f.b, DockArea::Center));
        QCOMPARE(adapter->window(), window);
        QCOMPARE(panel->widget(), container.data());
        QCOMPARE(container->window(), &f.windowB);
        QCOMPARE(topLevelChanged.constLast().at(0).value<QWidget *>(), &f.windowB);
        QTRY_VERIFY(window->isVisible());
        QVERIFY(adapter->hasSurface());
        // Whatever the platform did, creations and destructions pair up: a
        // surface that was destroyed has been created again.
        QCOMPARE(created.size() - createdBefore, destroyed.size() - destroyedBefore);
        qInfo("%s: moving to another top-level window recreated the surface %d time(s)",
              qPrintable(QGuiApplication::platformName()), int(created.size()) - createdBefore);

        // Floating and back.
        QVERIFY(f.manager.floatPanel(p("native"), QRect(40, 40, 320, 240)));
        QTRY_VERIFY(window->isVisible());
        QVERIFY(container->window() != &f.windowB);
        QVERIFY(f.manager.dockPanel(p("native")));
        QCOMPARE(container->window(), &f.windowB);
        QTRY_VERIFY(window->isVisible());

        // Behind another tab it is hidden; closing hides it too.
        const int createdBeforeHide = int(created.size());
        QVERIFY(f.manager.movePanel(p("b"), p("native"), DockArea::Center));
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(f.manager.activatePanel(p("native")));
        QTRY_VERIFY(window->isVisible());
        QTRY_COMPARE(created.size(), destroyed.size() + 1);
        qInfo("%s: hiding behind a tab and showing again recreated the surface %d time(s)",
              qPrintable(QGuiApplication::platformName()), int(created.size()) - createdBeforeHide);
        QVERIFY(f.manager.hidePanel(p("native")));
        QTRY_VERIFY(!window->isVisible());
        QCOMPARE(visibilityChanged.constLast().at(0).toBool(), false);
        QVERIFY(windowGuard); // closed is not destroyed
        QVERIFY(f.manager.showPanel(p("native")));
        QTRY_VERIFY(window->isVisible());
        Q_UNUSED(exposed);

        // Unregistering destroys container, window and adapter together.
        const int destroyedSoFar = int(destroyed.size());
        QVERIFY(f.manager.unregisterPanel(p("native")));
        QVERIFY(!container);
        QTRY_VERIFY(!windowGuard);
        QVERIFY(!adapterGuard);
        Q_UNUSED(destroyedSoFar);
    }

    void nativeContentStepsAsideDuringADrag()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        NativeWindowAdapter *adapter =
            NativeWindowAdapter::registerPanel(&f.manager, p("native"), new QWindow);
        QVERIFY(f.manager.movePanel(p("native"), p("a"), DockArea::Right));
        QWindow *window = adapter->window();
        QTRY_VERIFY(window->isVisible());
        DockAreaWidget *area = areaOf(f.a);
        DockDragController *controller = priv(f.manager)->drag;

        // While something is dragged the native window is out of the way, so
        // the guide can be seen over its panel and the drop lands on the dock.
        QVERIFY(controller->begin(p("a"), false));
        QTRY_VERIFY(!window->isVisible());
        const QPoint pos = area->groupOfPanel(p("native"))->geometry().center();
        const DropCandidate candidate = area->candidateAt(pos, *controller->session());
        QVERIFY(candidate.valid);
        QCOMPARE(candidate.target.node, f.a->layoutTree().findPanel(p("native"))->id);
        area->showOverlay(candidate);
        QVERIFY(area->overlay()->isVisible());

        controller->cancel();
        QTRY_VERIFY(window->isVisible());
        QVERIFY(!area->overlay()->isVisible());

        // Also when the drag ends in a drop that moves the native panel itself.
        QVERIFY(controller->begin(p("native"), false));
        QTRY_VERIFY(!window->isVisible());
        const DropTarget target{p("A"), f.a->layoutTree().findPanel(p("a"))->id, DockArea::Bottom,
                                -1, 0.5};
        QVERIFY(controller->drop(target));
        QCOMPARE(describe(f.a), p("V(a, native)"));
        QTRY_VERIFY(window->isVisible());

        // A panel can opt out.
        adapter->panel()->setHidesContentDuringDrag(false);
        QVERIFY(controller->begin(p("a"), false));
        QCoreApplication::processEvents();
        QVERIFY(window->isVisible());
        controller->cancel();
    }

    void focusInANativeWindowActivatesItsPanel()
    {
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        NativeWindowAdapter *adapter =
            NativeWindowAdapter::registerPanel(&f.manager, p("native"), new QWindow);
        QVERIFY(f.manager.movePanel(p("native"), p("a"), DockArea::Right));
        QTRY_VERIFY(adapter->window()->isVisible());
        QVERIFY(f.manager.activatePanel(p("a")));
        QCOMPARE(f.manager.activePanel(), f.manager.panel(p("a")));

        adapter->window()->requestActivate();
        if (!QTest::qWaitFor([&] { return QGuiApplication::focusWindow() == adapter->window(); }, 1500))
            QSKIP("This platform does not let a test move input focus to an embedded window");
        QTRY_COMPARE(f.manager.activePanel(), adapter->panel());
        // The native window keeps the focus it was given.
        QCOMPARE(QGuiApplication::focusWindow(), adapter->window());
    }

    void badRegistrationsLeaveTheWindowAlone()
    {
        TwoWindows f;
        QWindow window;
        QVERIFY(!NativeWindowAdapter::registerPanel(&f.manager, p("a"), &window)); // id taken
        QCOMPARE(f.manager.lastError().error(), DockError::DuplicatePanel);
        QVERIFY(!NativeWindowAdapter::registerPanel(&f.manager, QString(), &window));
        QVERIFY(!NativeWindowAdapter::registerPanel(nullptr, p("x"), &window));
        QVERIFY(!NativeWindowAdapter::registerPanel(&f.manager, p("x"), nullptr));
        QVERIFY(!window.parent()); // still ours, not wrapped in a container
    }

    void openGLWidgetSurvivesMovesBetweenWindows()
    {
#ifndef QFLEXDOCK_TEST_HAS_OPENGLWIDGETS
        QSKIP("Qt was built without the OpenGL widgets module");
#else
        // The offscreen platform has no compositing of GL content into widgets.
        if (QGuiApplication::platformName() == QLatin1String("offscreen"))
            QSKIP("QOpenGLWidget is not supported on the 'offscreen' platform; run with xcb");
        {
            QOpenGLContext probe;
            if (!probe.create())
                QSKIP("This platform cannot create an OpenGL context");
        }
        TwoWindows f;
        f.show();
        QVERIFY(f.a->addPanel(p("a")));
        auto *gl = new GlPanel;
        DockPanel *panel = f.manager.registerPanel(p("gl"), gl, p("GL"));
        QVERIFY(f.manager.movePanel(p("gl"), p("a"), DockArea::Right));
        if (!QTest::qWaitFor([&] { return gl->isValid() && gl->painted > 0; }, 2000))
            QSKIP("QOpenGLWidget does not render on this platform");
        QCOMPARE(gl->initialized, 1);
        const QImage first = gl->grabFramebuffer();
        QCOMPARE(first.pixelColor(first.width() / 2, first.height() / 2), QColor(51, 102, 153));

        // The order an application relies on: told first, context gone after.
        int releasedWhenTold = -1;
        connect(panel, &DockPanel::aboutToBeReparented, this,
                [&] { releasedWhenTold = gl->released; });
        QSignalSpy topLevelChanged(panel, &DockPanel::topLevelChanged);

        // Another top-level window means another GL context for the widget
        // (unless contexts are shared application-wide). Same widget, though.
        QVERIFY(f.manager.movePanel(p("gl"), f.b, DockArea::Center));
        QCOMPARE(panel->widget(), gl);
        QCOMPARE(gl->window(), &f.windowB);
        QCOMPARE(topLevelChanged.size(), 1);
        QCOMPARE(releasedWhenTold, 0);
        const int paintedBefore = gl->painted;
        QTRY_VERIFY(gl->painted > paintedBefore);
        QVERIFY(gl->isValid());
        QVERIFY(gl->hasResources);
        // Every context that was torn down has been replaced by a new one.
        QCOMPARE(gl->initialized, gl->released + 1);
        const QImage second = gl->grabFramebuffer();
        QCOMPARE(second.pixelColor(second.width() / 2, second.height() / 2), QColor(51, 102, 153));

        // Within one window nothing GL-related happens at all.
        const int initialized = gl->initialized;
        QVERIFY(f.manager.movePanel(p("b"), f.b, DockArea::Left));
        QVERIFY(f.manager.movePanel(p("gl"), p("b"), DockArea::Bottom));
        QCoreApplication::processEvents();
        QCOMPARE(gl->initialized, initialized);
        QVERIFY(gl->isValid());

        // Floating window and back, hidden and shown: still rendering.
        QVERIFY(f.manager.floatPanel(p("gl"), QRect(30, 30, 300, 200)));
        QTRY_VERIFY(gl->isValid() && gl->hasResources);
        QVERIFY(f.manager.dockPanel(p("gl")));
        QVERIFY(f.manager.hidePanel(p("gl")));
        QVERIFY(f.manager.showPanel(p("gl")));
        const int paintedAfter = gl->painted;
        QTRY_VERIFY(gl->painted > paintedAfter);
        QCOMPARE(gl->initialized, gl->released + 1);

        const QPointer<GlPanel> guard(gl);
        QVERIFY(f.manager.unregisterPanel(p("gl")));
        QVERIFY(!guard);
#endif
    }
};

QTEST_MAIN(tst_Gpu)
#include "tst_gpu.moc"
