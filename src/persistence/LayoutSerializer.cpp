// SPDX-License-Identifier: MIT
#include "persistence/LayoutSerializer.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSet>

#include <cmath>

namespace QFlexDock {

namespace {

const QLatin1String FormatName("QFlexDock.Layout");

DockResult fail(DockError error, const QString &message)
{
    return DockResult::failure(error, message);
}

QString edgeName(DockArea area)
{
    switch (area) {
    case DockArea::Left:
        return QStringLiteral("left");
    case DockArea::Right:
        return QStringLiteral("right");
    case DockArea::Top:
        return QStringLiteral("top");
    case DockArea::Bottom:
        return QStringLiteral("bottom");
    case DockArea::Center:
        return QStringLiteral("center");
    case DockArea::None:
        break;
    }
    return QStringLiteral("none");
}

DockArea edgeFromName(const QString &name)
{
    for (DockArea area : {DockArea::Left, DockArea::Right, DockArea::Top, DockArea::Bottom,
                          DockArea::Center}) {
        if (edgeName(area) == name)
            return area;
    }
    return DockArea::None;
}

QJsonObject rectToJson(const QRect &rect)
{
    return QJsonObject{{QStringLiteral("x"), rect.x()},
                       {QStringLiteral("y"), rect.y()},
                       {QStringLiteral("width"), rect.width()},
                       {QStringLiteral("height"), rect.height()}};
}

// A number that is a sane pixel coordinate, or `fallback`.
int pixelValue(const QJsonValue &value, int fallback)
{
    const double number = value.toDouble(std::nan(""));
    if (!std::isfinite(number) || std::abs(number) > 1e7)
        return fallback;
    return int(std::lround(number));
}

QRect rectFromJson(const QJsonValue &value)
{
    if (!value.isObject())
        return {};
    const QJsonObject o = value.toObject();
    const QRect rect(pixelValue(o.value(QStringLiteral("x")), 0),
                     pixelValue(o.value(QStringLiteral("y")), 0),
                     pixelValue(o.value(QStringLiteral("width")), -1),
                     pixelValue(o.value(QStringLiteral("height")), -1));
    return rect.isValid() ? rect : QRect();
}

QJsonArray stringsToJson(const QStringList &list)
{
    QJsonArray array;
    for (const QString &s : list)
        array.append(s);
    return array;
}

QStringList stringsFromJson(const QJsonValue &value)
{
    QStringList list;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &v : array) {
        if (v.isString() && !v.toString().isEmpty())
            list << v.toString();
    }
    return list;
}

QJsonValue nodeToJson(const LayoutNode &node)
{
    QJsonObject o;
    o.insert(QStringLiteral("weight"), node.weight);
    if (node.isTabs()) {
        o.insert(QStringLiteral("type"), QStringLiteral("tabs"));
        o.insert(QStringLiteral("panels"), stringsToJson(node.panels));
        o.insert(QStringLiteral("active"), node.active);
    } else {
        o.insert(QStringLiteral("type"), QStringLiteral("split"));
        o.insert(QStringLiteral("orientation"), node.orientation == Qt::Horizontal
                     ? QStringLiteral("horizontal") : QStringLiteral("vertical"));
        QJsonArray children;
        for (const auto &child : node.children)
            children.append(nodeToJson(child));
        o.insert(QStringLiteral("children"), children);
    }
    if (node.iconified)
        o.insert(QStringLiteral("iconified"), true);
    return o;
}

QJsonObject memoryToJson(const PanelMemory &m)
{
    QJsonObject o;
    o.insert(QStringLiteral("container"), m.container);
    o.insert(QStringLiteral("floating"), m.floating);
    o.insert(QStringLiteral("owner"), m.owner);
    if (m.geometry.isValid())
        o.insert(QStringLiteral("geometry"), rectToJson(m.geometry));
    o.insert(QStringLiteral("tabSiblings"), stringsToJson(m.tabSiblings));
    o.insert(QStringLiteral("tabIndex"), m.tabIndex);
    if (m.front)
        o.insert(QStringLiteral("front"), true);
    o.insert(QStringLiteral("neighbors"), stringsToJson(m.neighbors));
    o.insert(QStringLiteral("neighborArea"), edgeName(m.neighborArea));
    o.insert(QStringLiteral("fraction"), m.fraction);
    o.insert(QStringLiteral("autoHideEdge"), edgeName(m.autoHideEdge));
    o.insert(QStringLiteral("reopen"), m.reopen);
    return o;
}

PanelMemory memoryFromJson(const QJsonObject &o)
{
    PanelMemory m;
    m.container = o.value(QStringLiteral("container")).toString();
    m.floating = o.value(QStringLiteral("floating")).toBool();
    m.owner = o.value(QStringLiteral("owner")).toString();
    m.geometry = rectFromJson(o.value(QStringLiteral("geometry")));
    m.tabSiblings = stringsFromJson(o.value(QStringLiteral("tabSiblings")));
    m.tabIndex = pixelValue(o.value(QStringLiteral("tabIndex")), -1);
    m.front = o.value(QStringLiteral("front")).toBool();
    m.neighbors = stringsFromJson(o.value(QStringLiteral("neighbors")));
    m.neighborArea = edgeFromName(o.value(QStringLiteral("neighborArea")).toString());
    if (!isEdgeArea(m.neighborArea))
        m.neighborArea = DockArea::None;
    const double fraction = o.value(QStringLiteral("fraction")).toDouble(0.5);
    m.fraction = std::isfinite(fraction) && fraction > 0.0 && fraction < 1.0 ? fraction : 0.5;
    m.autoHideEdge = edgeFromName(o.value(QStringLiteral("autoHideEdge")).toString());
    if (!isEdgeArea(m.autoHideEdge))
        m.autoHideEdge = DockArea::None;
    m.reopen = o.value(QStringLiteral("reopen")).toBool();
    return m;
}

struct Reader
{
    QSet<PanelId> seenPanels;
    QStringList warnings;

    void warn(const QString &message) { warnings << message; }

    // Keeps the panels not seen anywhere before; reports the rest.
    QStringList claim(const QStringList &panels, const QString &where)
    {
        QStringList kept;
        for (const PanelId &panel : panels) {
            if (seenPanels.contains(panel)) {
                warn(QStringLiteral("panel '%1' appears more than once; ignored in %2")
                         .arg(panel, where));
                continue;
            }
            seenPanels.insert(panel);
            kept << panel;
        }
        return kept;
    }

    // Returns false when nothing usable is left of the node.
    bool readNode(const QJsonValue &value, int depth, const QString &where, LayoutNode *out)
    {
        if (!value.isObject()) {
            warn(QStringLiteral("a layout node in %1 is not an object; skipped").arg(where));
            return false;
        }
        if (depth > LayoutTree::MaxDepth) {
            warn(QStringLiteral("the layout of %1 is nested too deeply; the excess was dropped")
                     .arg(where));
            return false;
        }
        const QJsonObject o = value.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        const double weight = o.value(QStringLiteral("weight")).toDouble(1.0);

        if (type == QLatin1String("tabs")) {
            const QStringList panels = claim(stringsFromJson(o.value(QStringLiteral("panels"))),
                                             where);
            if (panels.isEmpty())
                return false;
            *out = LayoutNode::makeTabs(panels, o.value(QStringLiteral("active")).toString());
        } else if (type == QLatin1String("split")) {
            const QString orientation = o.value(QStringLiteral("orientation")).toString();
            if (orientation != QLatin1String("horizontal") && orientation != QLatin1String("vertical")) {
                warn(QStringLiteral("a split in %1 has no valid orientation; assumed horizontal")
                         .arg(where));
            }
            std::vector<LayoutNode> children;
            const QJsonArray array = o.value(QStringLiteral("children")).toArray();
            for (const QJsonValue &child : array) {
                LayoutNode node;
                if (readNode(child, depth + 1, where, &node))
                    children.push_back(std::move(node));
            }
            if (children.empty())
                return false;
            *out = LayoutNode::makeSplit(orientation == QLatin1String("vertical") ? Qt::Vertical
                                                                                 : Qt::Horizontal,
                                         std::move(children));
        } else {
            warn(QStringLiteral("unknown layout node type '%1' in %2; skipped").arg(type, where));
            return false;
        }
        // Bad weights are repaired by normalization, and so is a column that
        // is iconified inside another.
        out->weight = weight;
        out->iconified = o.value(QStringLiteral("iconified")).toBool();
        return true;
    }
};

} // namespace

// --- LayoutMigration ---------------------------------------------------------

void LayoutMigration::addStep(int fromVersion, Step step)
{
    m_steps.insert(fromVersion, std::move(step));
}

DockResult LayoutMigration::run(QJsonObject &json, int targetVersion) const
{
    const QJsonValue versionValue = json.value(QStringLiteral("schemaVersion"));
    if (!versionValue.isDouble())
        return fail(DockError::ParseError, QStringLiteral("the layout has no schema version"));
    int version = versionValue.toInt(-1);
    if (version < 0)
        return fail(DockError::ParseError, QStringLiteral("the layout has no valid schema version"));
    if (version > targetVersion) {
        return fail(DockError::UnsupportedVersion,
                    QStringLiteral("the layout was written by a newer version (schema %1, "
                                   "this build reads up to %2)").arg(version).arg(targetVersion));
    }
    while (version < targetVersion) {
        const auto it = m_steps.constFind(version);
        if (it == m_steps.constEnd()) {
            return fail(DockError::UnsupportedVersion,
                        QStringLiteral("layouts of schema version %1 can no longer be read")
                            .arg(version));
        }
        json = it.value()(json);
        ++version;
        json.insert(QStringLiteral("schemaVersion"), version);
    }
    return DockResult::success();
}

const LayoutMigration &LayoutMigration::builtIn()
{
    // Schema 1 is the first one; steps get added here as the schema evolves.
    static const LayoutMigration migration;
    return migration;
}

// --- LayoutSerializer --------------------------------------------------------

QJsonObject LayoutSerializer::toJson(const Document &document)
{
    QJsonArray windows;
    for (const auto &c : document.state.containers) {
        QJsonObject window;
        window.insert(QStringLiteral("id"), c.id);
        const bool floating = c.kind == ContainerKind::Floating;
        window.insert(QStringLiteral("kind"), floating ? QStringLiteral("floating")
                                                       : QStringLiteral("workspace"));
        if (floating) {
            window.insert(QStringLiteral("owner"), c.owner);
            if (c.geometry.isValid())
                window.insert(QStringLiteral("geometry"), rectToJson(c.geometry));
        } else {
            const auto it = document.windows.constFind(c.id);
            if (it != document.windows.constEnd()) {
                if (it->geometry.isValid())
                    window.insert(QStringLiteral("geometry"), rectToJson(it->geometry));
                window.insert(QStringLiteral("maximized"), it->maximized);
                window.insert(QStringLiteral("fullScreen"), it->fullScreen);
            }
            QJsonObject autoHide;
            for (int i = 0; i < 4; ++i)
                autoHide.insert(edgeName(DockEdges[size_t(i)]), stringsToJson(c.autoHide[size_t(i)]));
            window.insert(QStringLiteral("autoHide"), autoHide);
        }
        window.insert(QStringLiteral("layout"),
                      c.tree.root() ? nodeToJson(*c.tree.root()) : QJsonValue());
        window.insert(QStringLiteral("maximizedPanel"), c.maximized);
        windows.append(window);
    }

    QJsonObject memory;
    for (auto it = document.state.memory.cbegin(); it != document.state.memory.cend(); ++it)
        memory.insert(it.key(), memoryToJson(it.value()));

    QJsonObject root;
    root.insert(QStringLiteral("format"), QString(FormatName));
    root.insert(QStringLiteral("schemaVersion"), SchemaVersion);
    root.insert(QStringLiteral("windows"), windows);
    root.insert(QStringLiteral("panelMemory"), memory);
    return root;
}

QByteArray LayoutSerializer::toBytes(const Document &document)
{
    return QJsonDocument(toJson(document)).toJson(QJsonDocument::Indented);
}

DockResult LayoutSerializer::fromBytes(const QByteArray &bytes, Document *document,
                                       QStringList *warnings, const LayoutMigration &migration)
{
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(DockError::ParseError,
                    QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    }
    if (!json.isObject())
        return fail(DockError::ParseError, QStringLiteral("the layout is not a JSON object"));
    return fromJson(json.object(), document, warnings, migration);
}

DockResult LayoutSerializer::fromJson(const QJsonObject &input, Document *document,
                                      QStringList *warnings, const LayoutMigration &migration)
{
    if (input.value(QStringLiteral("format")).toString() != FormatName)
        return fail(DockError::ParseError, QStringLiteral("not a QFlexDock layout"));
    QJsonObject json = input;
    if (DockResult r = migration.run(json, SchemaVersion); !r)
        return r;
    if (!json.value(QStringLiteral("windows")).isArray())
        return fail(DockError::ParseError, QStringLiteral("the layout lists no windows"));

    Reader reader;
    Document result;
    QSet<QString> ids;
    const QJsonArray windows = json.value(QStringLiteral("windows")).toArray();
    for (const QJsonValue &value : windows) {
        const QJsonObject window = value.toObject();
        const QString id = window.value(QStringLiteral("id")).toString();
        const QString kind = window.value(QStringLiteral("kind")).toString();
        if (!value.isObject() || id.isEmpty()) {
            reader.warn(QStringLiteral("a window entry without id was skipped"));
            continue;
        }
        if (ids.contains(id)) {
            reader.warn(QStringLiteral("window '%1' is listed twice; the second was skipped").arg(id));
            continue;
        }
        ContainerState c;
        c.id = id;
        if (kind == QLatin1String("workspace")) {
            c.kind = ContainerKind::Workspace;
        } else if (kind == QLatin1String("floating")) {
            c.kind = ContainerKind::Floating;
        } else {
            reader.warn(QStringLiteral("window '%1' is of unknown kind '%2'; skipped").arg(id, kind));
            continue;
        }
        ids.insert(id);

        const QString where = QStringLiteral("window '%1'").arg(id);
        const QJsonValue layout = window.value(QStringLiteral("layout"));
        LayoutNode root;
        if (!layout.isNull() && !layout.isUndefined() && reader.readNode(layout, 1, where, &root))
            c.tree = LayoutTree(std::move(root));

        const QRect geometry = rectFromJson(window.value(QStringLiteral("geometry")));
        if (c.kind == ContainerKind::Floating) {
            c.owner = window.value(QStringLiteral("owner")).toString();
            c.geometry = geometry;
        } else {
            WindowPlacement placement;
            placement.geometry = geometry;
            placement.maximized = window.value(QStringLiteral("maximized")).toBool();
            placement.fullScreen = window.value(QStringLiteral("fullScreen")).toBool();
            if (window.contains(QStringLiteral("geometry")) || placement.maximized
                || placement.fullScreen) {
                result.windows.insert(id, placement);
            }
            const QJsonObject autoHide = window.value(QStringLiteral("autoHide")).toObject();
            for (int i = 0; i < 4; ++i) {
                c.autoHide[size_t(i)] = reader.claim(
                    stringsFromJson(autoHide.value(edgeName(DockEdges[size_t(i)]))), where);
            }
        }
        c.maximized = window.value(QStringLiteral("maximizedPanel")).toString();
        result.state.containers.push_back(std::move(c));
    }

    const QJsonObject memory = json.value(QStringLiteral("panelMemory")).toObject();
    for (auto it = memory.begin(); it != memory.end(); ++it) {
        // Only panels that are not placed have anything to remember.
        if (it.key().isEmpty() || !it.value().isObject())
            continue;
        result.state.memory.insert(it.key(), memoryFromJson(it.value().toObject()));
    }

    result.state.normalize();
    if (DockResult r = result.state.validate(); !r)
        return r;
    if (warnings)
        *warnings += reader.warnings;
    *document = std::move(result);
    return DockResult::success();
}

QRect LayoutSerializer::fitToScreens(const QRect &geometry, const QList<QRect> &screens)
{
    if (!geometry.isValid() || screens.isEmpty())
        return geometry;

    // Good enough as it is: a grabbable part of the window, including its top
    // edge (where the title bar is), lies on some screen.
    const QRect *best = nullptr;
    qint64 bestArea = -1;
    for (const QRect &screen : screens) {
        const QRect visible = geometry.intersected(screen);
        const qint64 area = visible.isValid() ? qint64(visible.width()) * visible.height() : 0;
        if (area > bestArea) {
            bestArea = area;
            best = &screen;
        }
        const bool topVisible = geometry.top() >= screen.top() && geometry.top() <= screen.bottom();
        if (topVisible && visible.width() >= qMin(120, geometry.width())
            && visible.height() >= qMin(60, geometry.height())) {
            return geometry;
        }
    }
    if (bestArea <= 0) {
        // Entirely off screen: take the nearest screen.
        qint64 bestDistance = -1;
        for (const QRect &screen : screens) {
            const QPoint d = screen.center() - geometry.center();
            const qint64 distance = qint64(d.x()) * d.x() + qint64(d.y()) * d.y();
            if (bestDistance < 0 || distance < bestDistance) {
                bestDistance = distance;
                best = &screen;
            }
        }
    }

    QRect fitted = geometry;
    fitted.setSize(geometry.size().boundedTo(best->size()));
    fitted.moveLeft(qBound(best->left(), fitted.left(), best->right() - fitted.width() + 1));
    fitted.moveTop(qBound(best->top(), fitted.top(), best->bottom() - fitted.height() + 1));
    return fitted;
}

} // namespace QFlexDock
