// SPDX-License-Identifier: MIT
#pragma once

#include "core/LayoutState.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>

#include <functional>

namespace QFlexDock {

/// Geometry of the top-level window a workspace sits in, as saved with a
/// layout. Not part of LayoutState: those windows belong to the application.
struct WindowPlacement
{
    QRect geometry;
    bool maximized = false;
    bool fullScreen = false;
};

/// Upgrades saved layouts of older schema versions, one version at a time.
class QFLEXDOCK_EXPORT LayoutMigration
{
public:
    /// Turns a document of version N into one of version N + 1.
    using Step = std::function<QJsonObject(const QJsonObject &)>;

    void addStep(int fromVersion, Step step);
    /// Brings `json` up to `targetVersion`. Fails if the document is newer
    /// than that or a step on the way is missing.
    DockResult run(QJsonObject &json, int targetVersion) const;

    /// The steps for all schema versions this library has shipped.
    static const LayoutMigration &builtIn();

private:
    QMap<int, Step> m_steps;
};

/// JSON form of a LayoutState.
///
/// Reading is defensive: anything that is not a QFlexDock layout at all, or is
/// from a newer schema, is rejected and nothing is produced. Damage inside an
/// otherwise recognisable document (unknown node types, duplicated panels,
/// bad numbers, absurd nesting) is repaired, and each repair is reported as a
/// warning. Node ids are not stored; a loaded tree gets fresh ones.
class QFLEXDOCK_EXPORT LayoutSerializer
{
public:
    static constexpr int SchemaVersion = 1;

    struct Document
    {
        LayoutState state;
        QHash<QString, WindowPlacement> windows;
    };

    [[nodiscard]] static QJsonObject toJson(const Document &document);
    [[nodiscard]] static QByteArray toBytes(const Document &document);

    static DockResult fromJson(const QJsonObject &json, Document *document,
                               QStringList *warnings = nullptr,
                               const LayoutMigration &migration = LayoutMigration::builtIn());
    static DockResult fromBytes(const QByteArray &bytes, Document *document,
                                QStringList *warnings = nullptr,
                                const LayoutMigration &migration = LayoutMigration::builtIn());

    /// Moves (and if necessary shrinks) a window geometry so that it is usable
    /// on one of `screens`: unchanged if enough of it, including its top edge,
    /// is already on a screen. Covers unplugged monitors and changed
    /// resolutions.
    [[nodiscard]] static QRect fitToScreens(const QRect &geometry, const QList<QRect> &screens);
};

} // namespace QFlexDock
