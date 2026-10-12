// SPDX-License-Identifier: MIT
#pragma once

#include <QtCore/QRect>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

// What Qt does not say about the title bar of a macOS window. For windows
// that exist already (asking for the winId() of another one creates it).
namespace QFlexDock::MacWindow {

/// Leaves the title of `window` out of its title bar.
void hideTitle(QWindow *window);
/// Where the close, minimize and zoom buttons of `window` are, together, in
/// its coordinates; null if it has none.
[[nodiscard]] QRect buttonsRect(const QWindow *window);

} // namespace QFlexDock::MacWindow
