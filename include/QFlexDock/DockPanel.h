// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockPolicy.h>
#include <QFlexDock/Global.h>

#include <QtCore/QObject>
#include <QtGui/QIcon>

#include <memory>

QT_BEGIN_NAMESPACE
class QAction;
class QWidget;
QT_END_NAMESPACE

namespace QFlexDock {

class DockManager;
class DockManagerPrivate;
class DockWorkspace;

/// A place for a panel, the way DockManager::movePanel() takes one: beside or
/// among the tabs of another panel, or against a workspace as a whole.
struct QFLEXDOCK_EXPORT DockPlacement
{
    /// The panel whose tab group is docked onto: an edge area splits that
    /// group, Center joins its tabs. Used while that panel is in a tab group;
    /// otherwise, and when empty, `workspace` is.
    PanelId relativeTo;
    /// The workspace (by id) docked onto as a whole: an edge area along the
    /// outside of everything in it, Center as a tab of the group used last.
    /// Empty, or not there: the first workspace.
    QString workspace;
    /// None: no place at all.
    DockArea area = DockArea::None;
    /// The share of the space taken (edge areas only); negative picks the
    /// default.
    double fraction = -1.0;

    [[nodiscard]] bool isValid() const { return area != DockArea::None; }

    friend bool operator==(const DockPlacement &, const DockPlacement &) = default;
};

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
    Q_PROPERTY(bool current READ isCurrent NOTIFY currentChanged)
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
    /// The tab in front of its tab group: the panel of the group that is
    /// shown. Every group has one, whether the user works in it or not.
    /// False while the panel is closed or in an auto-hide bar.
    [[nodiscard]] bool isCurrent() const;
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

    /// Whether the panel's tab has a button to close it, where the panel may
    /// be closed (default true). Without one it is still closed from its
    /// menu, with the middle mouse button, or with the window it floats in.
    [[nodiscard]] bool hasTabCloseButton() const;
    void setTabCloseButton(bool shown);

    /// Whether the panel is closed when the user asks for it (default true):
    /// with its tab button, the close button of its header, the middle mouse
    /// button, its menu, or by closing the floating window it is in. Either
    /// way closeRequested() tells of it first. With false that is all that
    /// happens, and the application decides: it asks about unsaved changes,
    /// say, and then calls close(), or unregisters the panel, or leaves it
    /// open. Calls of DockManager::closePanel() close the panel regardless.
    [[nodiscard]] bool closesOnRequest() const;
    void setClosesOnRequest(bool closes);

    /// Where the panel goes when it is to be shown and nothing says where:
    /// DockManager::openPanel() of a panel no place is remembered for, and a
    /// layout put in place as a whole (a restored one, a preset, the default)
    /// that knows nothing of the panel. Such a layout leaves a panel with a
    /// default placement as it is: closed if it was closed, and shown at this
    /// place if it was open. (One without is closed by it.) Not set, the
    /// default: openPanel() makes the panel a tab in the first workspace.
    [[nodiscard]] DockPlacement defaultPlacement() const;
    void setDefaultPlacement(const DockPlacement &placement);

    /// Whether the tab group shows a header while this panel is alone in it
    /// (default true). Without one there is nothing to drag, close or float
    /// the panel by, which is the point: together with setFeatures({}) it
    /// makes content that just stays where the application put it, like the
    /// central view other panels are docked around.
    [[nodiscard]] bool isHeaderVisible() const;
    void setHeaderVisible(bool visible);

    /// Whether dragging a split handle far enough against the panel's tab
    /// group closes it (default false): once the group would be left less
    /// than half of its minimum size, it gives way, and comes back if the
    /// drag returns. Released there, its panels are closed; openPanel()
    /// brings each back at the size the group had before the drag. Every
    /// panel of a group has to allow this for the group to go.
    ///
    /// Collapsible panels that are closed, in whatever way, can also be
    /// pulled back out: dragging inwards from the edge they went to shows
    /// them again (those that were closed together, see
    /// DockManager::closePanels()).
    [[nodiscard]] bool isCollapsible() const;
    void setCollapsible(bool collapsible);

    /// A small form of the panel for where its column is iconified
    /// (DockManager::setColumnIconified()): shown there in place of the
    /// button that would bring the panel out, which then is not needed. A
    /// palette of tools that is one button wide instead of two, say. The
    /// manager takes ownership; the widget set before is destroyed.
    [[nodiscard]] QWidget *compactWidget() const;
    void setCompactWidget(QWidget *widget);

    /// Actions of the application's own in the header of the panel's tab
    /// group, shown while the panel is the current one there. An ordinary
    /// action becomes a button (one with a menu opens it), a separator a thin
    /// line, and a QWidgetAction puts its widget there. The actions stay the
    /// caller's; one that is destroyed drops out.
    ///
    /// There are three places for them, each with a list of its own: the end
    /// of the header, before the built-in buttons (the default), its start,
    /// before the tabs, and right behind the last tab, where the tabs then
    /// take only the room they need.
    [[nodiscard]] QList<QAction *> titleActions(DockTitlePlace place = DockTitlePlace::End) const;
    void setTitleActions(const QList<QAction *> &actions,
                         DockTitlePlace place = DockTitlePlace::End);

    /// Whether the content is hidden while a dock drag is in progress
    /// (default false). Meant for content that is a native window (see
    /// NativeWindowAdapter): such a window covers every widget, the drop guide
    /// included, and on some platforms receives the drag itself.
    [[nodiscard]] bool hidesContentDuringDrag() const;
    void setHidesContentDuringDrag(bool hide);

    /// A checkable action that shows the panel and closes it, for a menu of
    /// panels: it has the panel's title for its text and is checked while
    /// the panel is open. The panel owns it.
    [[nodiscard]] QAction *toggleViewAction();

public Q_SLOTS:
    /// Shortcuts for the DockManager functions of the same meaning.
    void open();
    void close();
    void toggle();
    void activate();
    void raise();

Q_SIGNALS:
    void titleChanged(const QString &title);
    void iconChanged();
    void toolTipChanged();
    void policyChanged();
    void metadataChanged();
    void openChanged(bool open);
    /// The user asks for the panel to be closed (see setClosesOnRequest()).
    /// Not emitted when the application closes it, or a layout does.
    void closeRequested();
    void activeChanged(bool active);
    void currentChanged(bool current);
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
