// SPDX-License-Identifier: MIT
#pragma once

#include <QtCore/QFlags>
#include <QtCore/QHashFunctions>
#include <QtCore/QMetaType>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <compare>

#if defined(QFLEXDOCK_STATIC)
#  define QFLEXDOCK_EXPORT
#elif defined(QFLEXDOCK_BUILD)
#  define QFLEXDOCK_EXPORT Q_DECL_EXPORT
#else
#  define QFLEXDOCK_EXPORT Q_DECL_IMPORT
#endif

#define QFLEXDOCK_VERSION_MAJOR 0
#define QFLEXDOCK_VERSION_MINOR 1
#define QFLEXDOCK_VERSION_PATCH 0

namespace QFlexDock {

Q_NAMESPACE_EXPORT(QFLEXDOCK_EXPORT)

/// Library version as "major.minor.patch".
QFLEXDOCK_EXPORT QString versionString();

/// Stable, application-chosen identifier of a panel. This is what gets
/// serialized; widget pointers never are.
using PanelId = QString;

/// Identifier of a node of a layout tree. Unique within the process and never
/// reused; it is not persisted (restored layouts get fresh ids).
struct QFLEXDOCK_EXPORT NodeId
{
    quint64 value = 0;

    [[nodiscard]] bool isNull() const { return value == 0; }
    [[nodiscard]] static NodeId create();

    friend auto operator<=>(const NodeId &, const NodeId &) = default;
};

inline size_t qHash(NodeId id, size_t seed = 0) noexcept
{
    return ::qHash(id.value, seed);
}

/// Where something is docked relative to a target (a tab group or a whole
/// workspace). The four edges split the target, Center joins its tabs.
enum class DockArea {
    None = 0x00,
    Left = 0x01,
    Right = 0x02,
    Top = 0x04,
    Bottom = 0x08,
    Center = 0x10,
};
Q_ENUM_NS(DockArea)
Q_DECLARE_FLAGS(DockAreas, DockArea)
Q_DECLARE_OPERATORS_FOR_FLAGS(DockAreas)

inline constexpr DockAreas AllDockAreas =
    DockAreas(0x1f); // Left | Right | Top | Bottom | Center
inline constexpr DockAreas EdgeDockAreas = DockAreas(0x0f);

/// Alias kept for readability at call sites: addPanel(id, DockPosition::Right).
using DockPosition = DockArea;

/// True for Left/Right/Top/Bottom.
[[nodiscard]] constexpr bool isEdgeArea(DockArea area)
{
    return area == DockArea::Left || area == DockArea::Right || area == DockArea::Top
        || area == DockArea::Bottom;
}

/// Orientation of the split an edge area produces (Left/Right -> Horizontal).
[[nodiscard]] constexpr Qt::Orientation splitOrientation(DockArea area)
{
    return (area == DockArea::Left || area == DockArea::Right) ? Qt::Horizontal : Qt::Vertical;
}

/// What the user may do with a panel. These restrict user interaction (drag
/// and drop, tab buttons, context menu, closing a floating window); calls made
/// by the application through the API are not restricted by them.
enum class DockFeature {
    None = 0x00,
    Movable = 0x01,
    Closable = 0x02,
    Floatable = 0x04,
    /// May share a tab group with other panels.
    Tabbable = 0x08,
    AutoHideable = 0x10,
    Maximizable = 0x20,
};
Q_ENUM_NS(DockFeature)
Q_DECLARE_FLAGS(DockFeatures, DockFeature)
Q_DECLARE_OPERATORS_FOR_FLAGS(DockFeatures)

inline constexpr DockFeatures AllDockFeatures = DockFeatures(0x3f);

/// What the drop guide shows while something is dragged over a dock area, and
/// with that what the user aims at (DockOverlayStyle::guide).
enum class DockGuide {
    /// Five large areas that together cover the tab group under the pointer,
    /// and a band along the border of the workspace. Nothing small to aim at.
    Zones,
    /// The same areas, not drawn: all there is to see is the rectangle the
    /// dropped content would take.
    Preview,
    /// Small buttons: a cross of them in the middle of the tab group under
    /// the pointer, and one at each border of the workspace. Only a button
    /// (or a header) takes the drop; let go of anywhere else, a panel floats
    /// where that is enabled (DockManager::setFloatsOnOutsideDrop()).
    Buttons,
};
Q_ENUM_NS(DockGuide)

/// Where in the header of a tab group a panel's own actions are shown (see
/// DockPanel::setTitleActions()).
enum class DockTitlePlace {
    /// At the end of the header, before the built-in buttons.
    End,
    /// At its start, before the tabs.
    Start,
    /// Right behind the last tab.
    AfterTabs,
};
Q_ENUM_NS(DockTitlePlace)

enum class DockError {
    None,
    InvalidArgument,
    UnknownPanel,
    DuplicatePanel,
    UnknownWorkspace,
    UnknownNode,
    NotPlaced,
    PolicyViolation,
    InvalidLayout,
    ParseError,
    UnsupportedVersion,
    IoError,
    Busy,
};
Q_ENUM_NS(DockError)

/// Outcome of an operation that can fail. Converts to true on success.
/// Failed operations leave the layout exactly as it was.
class QFLEXDOCK_EXPORT DockResult
{
public:
    DockResult() = default;

    [[nodiscard]] static DockResult success() { return {}; }
    [[nodiscard]] static DockResult failure(DockError error, const QString &message)
    {
        DockResult r;
        r.m_error = error;
        r.m_message = message;
        return r;
    }

    [[nodiscard]] bool ok() const { return m_error == DockError::None; }
    explicit operator bool() const { return ok(); }
    [[nodiscard]] DockError error() const { return m_error; }
    [[nodiscard]] QString message() const { return m_message; }

private:
    DockError m_error = DockError::None;
    QString m_message;
};

} // namespace QFlexDock

Q_DECLARE_METATYPE(QFlexDock::NodeId)
