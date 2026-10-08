// SPDX-License-Identifier: MIT
#include <QFlexDock/Global.h>

#include <atomic>

namespace QFlexDock {

QString versionString()
{
    return QStringLiteral("%1.%2.%3")
        .arg(QFLEXDOCK_VERSION_MAJOR)
        .arg(QFLEXDOCK_VERSION_MINOR)
        .arg(QFLEXDOCK_VERSION_PATCH);
}

NodeId NodeId::create()
{
    static std::atomic<quint64> counter{0};
    return NodeId{++counter};
}

} // namespace QFlexDock
