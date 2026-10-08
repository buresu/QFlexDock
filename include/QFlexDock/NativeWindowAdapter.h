// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#include <QtCore/QObject>
#include <QtCore/QPointer>

QT_BEGIN_NAMESPACE
class QScreen;
class QWidget;
class QWindow;
QT_END_NAMESPACE

namespace QFlexDock {

class DockManager;
class DockPanel;

/// Puts a QWindow into a dock panel and reports what happens to its native
/// surface, for content rendered with Vulkan, Direct3D, Metal, raw OpenGL or
/// another GPU API.
///
/// QFlexDock owns no device, swapchain or render loop. What it does is move
/// the window between tab groups, workspaces and floating windows, and say
/// when that affects the surface. Whether resources must be rebuilt is for the
/// application's rendering backend to decide:
///
///  - surfaceAboutToBeDestroyed(): the platform window is going away. Anything
///    tied to the surface, a swapchain for example, must be released in a slot
///    connected to this, before returning.
///  - surfaceCreated(): there is a native surface again; create what you need.
///
/// Both forward Qt's QPlatformSurfaceEvent. When they occur is up to the
/// platform and the Qt version (possibly on a move to another top-level
/// window, or on hide and show), so write the application to cope with them
/// at any time; docs/platform-notes.md lists what was observed where.
/// Never cache QWindow::winId() across these two. DockPanel's
/// aboutToBeReparented(), reparented() and topLevelChanged() tell the same
/// story from the panel's side.
///
/// The window is embedded with QWidget::createWindowContainer(), with that
/// function's limits: the window is drawn above all widgets of its top-level
/// window, cannot be made translucent and clips nothing drawn over it. For
/// that reason registerPanel() makes the panel hide its content while a dock
/// drag is in progress (DockPanel::setHidesContentDuringDrag()), so the drop
/// guide is visible and the drag reaches the dock area.
class QFLEXDOCK_EXPORT NativeWindowAdapter : public QObject
{
    Q_OBJECT

public:
    /// Embeds `window` in a new container widget, which takes ownership of the
    /// window. The adapter itself is owned by the container.
    explicit NativeWindowAdapter(QWindow *window);
    ~NativeWindowAdapter() override;

    /// Embeds `window` and registers the container as panel `id`. The manager
    /// owns the container (and through it the window and the adapter). Returns
    /// nullptr, deleting nothing, if the panel cannot be registered (see
    /// DockManager::lastError()).
    static NativeWindowAdapter *registerPanel(DockManager *manager, const PanelId &id,
                                              QWindow *window, const QString &title = {});

    [[nodiscard]] QWindow *window() const;
    /// The widget to use as panel content.
    [[nodiscard]] QWidget *container() const;
    /// The panel, if created through registerPanel().
    [[nodiscard]] DockPanel *panel() const;
    /// The window currently has a native surface.
    [[nodiscard]] bool hasSurface() const;

Q_SIGNALS:
    void surfaceCreated();
    void surfaceAboutToBeDestroyed();
    /// The window became visible on screen or stopped being so.
    void exposedChanged(bool exposed);
    /// The window moved to another screen (the device pixel ratio may differ).
    void screenChanged(QScreen *screen);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QWindow> m_window;
    QPointer<QWidget> m_container;
    QPointer<DockPanel> m_panel;
    bool m_exposed = false;
};

} // namespace QFlexDock
