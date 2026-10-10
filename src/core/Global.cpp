// SPDX-License-Identifier: MIT
#include <QFlexDock/Global.h>

namespace QFlexDock {

QString versionString()
{
    return QStringLiteral("%1.%2.%3")
        .arg(QFLEXDOCK_VERSION_MAJOR)
        .arg(QFLEXDOCK_VERSION_MINOR)
        .arg(QFLEXDOCK_VERSION_PATCH);
}

} // namespace QFlexDock
