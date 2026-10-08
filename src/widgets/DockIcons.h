// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockTheme.h>

namespace QFlexDock {

/// A simple line glyph drawn in `color`, for the buttons Qt's styles have no
/// standard icon for (pin, float, menu...). Vector, so it is sharp at any
/// size and device pixel ratio.
QFLEXDOCK_EXPORT QIcon makeGlyphIcon(DockIcon which, const QColor &color);

} // namespace QFlexDock
