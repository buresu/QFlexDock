// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDockQuick/QuickGlobal.h>

#include <QtCore/QObject>
#include <QtCore/QPointer>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace QFlexDock {

class DockManager;

/// The dock operations QML content may perform, as a thin layer over
/// DockManager: it adds no behaviour and holds no state of its own.
///
/// Make it available to QML with installInto(); QML then calls, for example,
///
///     import QFlexDock
///     Button { onClicked: dock.movePanel("console", "scene", Dock.Bottom) }
///
/// Every call returns true on success. After a failure `lastError` holds the
/// reason; the layout is unchanged, as with the C++ API.
class QFLEXDOCKQUICK_EXPORT QmlDockController : public QObject
{
    Q_OBJECT
    /// Id of the active panel, or an empty string.
    Q_PROPERTY(QString activePanel READ activePanel NOTIFY activePanelChanged)
    /// Ids of all registered panels, in registration order.
    Q_PROPERTY(QStringList panels READ panels NOTIFY panelsChanged)
    /// Ids of the panels currently placed somewhere.
    Q_PROPERTY(QStringList openPanels READ openPanels NOTIFY layoutChanged)
    Q_PROPERTY(QString maximizedPanel READ maximizedPanel NOTIFY layoutChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    /// Mirrors QFlexDock::DockArea for QML (`Dock.Left`, ...).
    enum Area { Left, Right, Top, Bottom, Center };
    Q_ENUM(Area)

    explicit QmlDockController(DockManager *manager, QObject *parent = nullptr);

    [[nodiscard]] DockManager *manager() const;

    /// Exposes this controller to all QML run by `engine` as the context
    /// property `name`, and registers the `QFlexDock` import (for `Dock.Left`
    /// and friends). The controller must outlive the engine's QML.
    void installInto(QQmlEngine *engine, const QString &name = QStringLiteral("dock"));

    [[nodiscard]] QString activePanel() const;
    [[nodiscard]] QStringList panels() const;
    [[nodiscard]] QStringList openPanels() const;
    [[nodiscard]] QString maximizedPanel() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool hasPanel(const QString &id) const;
    Q_INVOKABLE bool isPanelOpen(const QString &id) const;
    /// The panel object (title, icon, open, active, floating, dirty... as
    /// properties with change signals), or null. Owned by the dock manager.
    Q_INVOKABLE QObject *panel(const QString &id) const;

    Q_INVOKABLE bool showPanel(const QString &id);
    Q_INVOKABLE bool hidePanel(const QString &id);
    /// Several panels as one change, e.g. an area that is put away.
    Q_INVOKABLE bool showPanels(const QStringList &ids);
    Q_INVOKABLE bool hidePanels(const QStringList &ids);
    Q_INVOKABLE bool togglePanel(const QString &id);
    Q_INVOKABLE bool activatePanel(const QString &id);
    /// Docks `id` relative to the tab group `relativeTo` is in.
    Q_INVOKABLE bool movePanel(const QString &id, const QString &relativeTo, Area area);
    /// Docks `id` onto workspace `workspaceId` as a whole.
    Q_INVOKABLE bool movePanelToWorkspace(const QString &id, const QString &workspaceId, Area area);
    Q_INVOKABLE bool floatPanel(const QString &id);
    Q_INVOKABLE bool dockPanel(const QString &id);
    Q_INVOKABLE bool maximizePanel(const QString &id);
    Q_INVOKABLE bool restoreMaximizedPanel();
    Q_INVOKABLE bool setPanelAutoHide(const QString &id, bool autoHide);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool resetLayout();

Q_SIGNALS:
    void activePanelChanged();
    void panelsChanged();
    void layoutChanged();
    void undoStateChanged();
    void lastErrorChanged();
    void panelOpenChanged(const QString &id, bool open);

private:
    bool report(const DockResult &result);

    QPointer<DockManager> m_manager;
    QString m_lastError;
};

} // namespace QFlexDock
