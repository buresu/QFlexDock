// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockPolicy.h>
#include <QFlexDock/Global.h>

#include <QtCore/QObject>
#include <QtGui/QIcon>

#include <memory>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace QFlexDock {

class DockManager;
class DockManagerPrivate;
class DockWorkspace;

/// A dockable panel: the logical identity (stable id, title, icon, policy)
/// of one content widget.
///
/// Panels are created by DockManager::registerPanel() / registerPanelFactory()
/// and owned by the manager. The content widget is owned by the manager too;
/// it is the same instance for the panel's whole life and is moved between
/// tab groups, workspaces and floating windows, never recreated.
///
/// All members must be used from the GUI thread.
class QFLEXDOCK_EXPORT DockPanel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QIcon icon READ icon WRITE setIcon NOTIFY iconChanged)
    Q_PROPERTY(QString toolTip READ toolTip WRITE setToolTip NOTIFY toolTipChanged)
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(bool floating READ isFloating NOTIFY floatingChanged)
    Q_PROPERTY(bool autoHidden READ isAutoHidden NOTIFY autoHiddenChanged)
    Q_PROPERTY(bool dirty READ isDirty WRITE setDirty NOTIFY metadataChanged)
    Q_PROPERTY(bool pinnedTab READ isPinnedTab WRITE setPinnedTab NOTIFY metadataChanged)
    Q_PROPERTY(bool previewTab READ isPreviewTab WRITE setPreviewTab NOTIFY metadataChanged)

public:
    ~DockPanel() override;

    [[nodiscard]] PanelId id() const;
    [[nodiscard]] DockManager *manager() const;

    [[nodiscard]] QString title() const;
    void setTitle(const QString &title);
    [[nodiscard]] QIcon icon() const;
    void setIcon(const QIcon &icon);
    [[nodiscard]] QString toolTip() const;
    void setToolTip(const QString &toolTip);

    /// The content widget. Null only for a factory-backed panel whose content
    /// has not been needed yet (see DockManager::registerPanelFactory()).
    [[nodiscard]] QWidget *widget() const;

    [[nodiscard]] DockPolicy policy() const;
    void setPolicy(const DockPolicy &policy);
    [[nodiscard]] DockFeatures features() const { return policy().features; }
    void setFeatures(DockFeatures features);

    /// Placed somewhere: docked, floating or in an auto-hide bar.
    [[nodiscard]] bool isOpen() const;
    /// This is the manager's active panel.
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool isFloating() const;
    [[nodiscard]] bool isAutoHidden() const;
    /// The workspace the panel is docked in, or the owner of the floating
    /// window it is in. Null when closed.
    [[nodiscard]] DockWorkspace *workspace() const;

    // Display-only tab state supplied by the application.
    /// Unsaved changes: the tab shows a marker.
    [[nodiscard]] bool isDirty() const;
    void setDirty(bool dirty);
    /// Pinned tab: shown with a pin instead of a close button.
    [[nodiscard]] bool isPinnedTab() const;
    void setPinnedTab(bool pinned);
    /// Preview tab: the title is shown in italics.
    [[nodiscard]] bool isPreviewTab() const;
    void setPreviewTab(bool preview);

    /// Whether the tab group shows a header while this panel is alone in it
    /// (default true). Without one there is nothing to drag, close or float
    /// the panel by, which is the point: together with setFeatures({}) it
    /// makes content that just stays where the application put it, like the
    /// central view other panels are docked around.
    [[nodiscard]] bool isHeaderVisible() const;
    void setHeaderVisible(bool visible);

    /// Whether the content is hidden while a dock drag is in progress
    /// (default false). Meant for content that is a native window (see
    /// NativeWindowAdapter): such a window covers every widget, the drop guide
    /// included, and on some platforms receives the drag itself.
    [[nodiscard]] bool hidesContentDuringDrag() const;
    void setHidesContentDuringDrag(bool hide);

public Q_SLOTS:
    /// Shortcuts for the DockManager functions of the same meaning.
    void open();
    void close();
    void toggle();
    void activate();

Q_SIGNALS:
    void titleChanged(const QString &title);
    void iconChanged();
    void toolTipChanged();
    void policyChanged();
    void metadataChanged();
    void openChanged(bool open);
    void activeChanged(bool active);
    void floatingChanged(bool floating);
    void autoHiddenChanged(bool autoHidden);

    // --- Content lifecycle, mainly for GPU / native-window content ----------
    /// A factory-backed panel just created its content.
    void widgetCreated(QWidget *widget);
    /// The content is about to get a new parent widget. A native surface may
    /// be destroyed by this; release what depends on it if your backend needs
    /// that.
    void aboutToBeReparented();
    /// The content has its new parent. `topLevelChanged` tells whether it also
    /// ended up in a different top-level window.
    void reparented(bool topLevelChanged);
    /// The content now lives in another top-level window (or none).
    void topLevelChanged(QWidget *topLevel);
    /// The content became visible on screen or stopped being so (tab switched
    /// away, auto-hide collapsed, window hidden, panel closed).
    void visibilityChanged(bool visible);

private:
    friend class DockManager;
    friend class DockManagerPrivate;
    struct Private;
    explicit DockPanel(DockManager *manager, const PanelId &id);
    bool eventFilter(QObject *watched, QEvent *event) override;
    std::unique_ptr<Private> d;
};

} // namespace QFlexDock
