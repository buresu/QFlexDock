// SPDX-License-Identifier: MIT
//
// GPU content in dock panels, two ways:
//  - a QOpenGLWidget, which is an ordinary widget as far as docking goes;
//  - a raw QWindow rendered with its own QOpenGLContext, embedded through
//    NativeWindowAdapter. The same pattern applies to Vulkan, Direct3D or
//    Metal: QFlexDock moves the window and reports what happens to its
//    surface; the application owns device, swapchain and render loop.
// The log panel shows the lifecycle notifications as panels are moved between
// tab groups, floated, or dragged into the second window.

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>
#include <QFlexDock/NativeWindowAdapter.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QWindow>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPlainTextEdit>

#include <cmath>

using namespace QFlexDock;

namespace {

QElapsedTimer clockTimer;

float pulse(float speed)
{
    return 0.5f + 0.5f * std::sin(float(clockTimer.elapsed()) / 1000.0f * speed);
}

/// A widget that renders with OpenGL. Resources created in initializeGL()
/// belong to the widget's context; that context is replaced when the widget
/// ends up in a different top-level window, and Qt calls initializeGL() again.
class GlWidget : public QOpenGLWidget
{
public:
    explicit GlWidget(QPlainTextEdit *log)
        : m_log(log)
    {
        setMinimumSize(160, 120);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
        timer->start(16);
    }

protected:
    void initializeGL() override
    {
        m_log->appendPlainText(QStringLiteral("QOpenGLWidget: initializeGL (new context)"));
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
            m_log->appendPlainText(QStringLiteral("QOpenGLWidget: context about to be destroyed"));
        }, Qt::DirectConnection);
    }

    void paintGL() override
    {
        QOpenGLFunctions *gl = context()->functions();
        gl->glClearColor(0.1f, 0.3f * pulse(2.0f) + 0.2f, 0.5f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
    }

private:
    QPlainTextEdit *m_log;
};

/// A native window with a render loop of its own, as an engine would have.
class RenderWindow : public QWindow
{
public:
    RenderWindow()
    {
        setSurfaceType(QSurface::OpenGLSurface);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &RenderWindow::render);
        timer->start(16);
    }

    /// Everything that depends on the native surface goes here.
    void releaseSurfaceResources()
    {
        if (m_context && m_context->makeCurrent(this))
            m_context->doneCurrent();
    }

    void render()
    {
        if (!isExposed())
            return;
        if (!m_context) {
            m_context = new QOpenGLContext(this);
            m_context->setFormat(requestedFormat());
            if (!m_context->create())
                return;
        }
        if (!m_context->makeCurrent(this))
            return;
        QOpenGLFunctions *gl = m_context->functions();
        gl->glViewport(0, 0, int(width() * devicePixelRatio()), int(height() * devicePixelRatio()));
        gl->glClearColor(0.5f * pulse(1.3f) + 0.2f, 0.15f, 0.25f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        m_context->swapBuffers(this);
    }

private:
    QOpenGLContext *m_context = nullptr;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    clockTimer.start();
    DockManager manager;

    QMainWindow window;
    QMainWindow second;
    DockWorkspace *workspace = manager.createWorkspace(QStringLiteral("main"));
    DockWorkspace *other = manager.createWorkspace(QStringLiteral("second"));
    window.setCentralWidget(workspace);
    second.setCentralWidget(other);
    window.setWindowTitle(QStringLiteral("QFlexDock GPU panels"));
    second.setWindowTitle(QStringLiteral("Drag a GPU panel in here"));

    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    manager.registerPanel(QStringLiteral("log"), log, QStringLiteral("Lifecycle Log"));

    DockPanel *glPanel = manager.registerPanel(QStringLiteral("gl"), new GlWidget(log),
                                               QStringLiteral("QOpenGLWidget"));
    QObject::connect(glPanel, &DockPanel::topLevelChanged, log, [log](QWidget *topLevel) {
        log->appendPlainText(QStringLiteral("QOpenGLWidget panel: now in window \"%1\"")
                                 .arg(topLevel ? topLevel->windowTitle() : QStringLiteral("(none)")));
    });

    auto *renderWindow = new RenderWindow;
    NativeWindowAdapter *native = NativeWindowAdapter::registerPanel(
        &manager, QStringLiteral("native"), renderWindow, QStringLiteral("Native QWindow"));
    QObject::connect(native, &NativeWindowAdapter::surfaceAboutToBeDestroyed, log,
                     [log, renderWindow] {
                         renderWindow->releaseSurfaceResources();
                         log->appendPlainText(QStringLiteral("Native window: surface about to be destroyed"));
                     });
    QObject::connect(native, &NativeWindowAdapter::surfaceCreated, log, [log] {
        log->appendPlainText(QStringLiteral("Native window: surface created"));
    });
    QObject::connect(native->panel(), &DockPanel::reparented, log, [log](bool topLevelChanged) {
        log->appendPlainText(topLevelChanged
                                 ? QStringLiteral("Native panel: moved to another top-level window")
                                 : QStringLiteral("Native panel: moved within its window"));
    });

    workspace->addPanel(QStringLiteral("gl"));
    workspace->addPanel(QStringLiteral("native"), DockArea::Right, 0.5);
    workspace->addPanel(QStringLiteral("log"), DockArea::Bottom, 0.3);

    window.resize(1000, 700);
    second.resize(500, 400);
    window.show();
    second.show();
    return app.exec();
}
