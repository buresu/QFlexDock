// SPDX-License-Identifier: MIT
#include "widgets/DockFloatingWindow_mac.h"

#include <QtGui/QWindow>

#include <AppKit/AppKit.h>

namespace QFlexDock::MacWindow {

namespace {

// The view of a QWindow is its winId().
NSWindow *nsWindowOf(const QWindow *window)
{
    return window ? reinterpret_cast<NSView *>(window->winId()).window : nil;
}

} // namespace

void hideTitle(QWindow *window)
{
    if (NSWindow *nsWindow = nsWindowOf(window))
        nsWindow.titleVisibility = NSWindowTitleHidden;
}

QRect buttonsRect(const QWindow *window)
{
    NSWindow *nsWindow = nsWindowOf(window);
    if (!nsWindow)
        return {};
    // In the coordinates of the window's frame, from its bottom; the content
    // of a window with Qt::ExpandedClientAreaHint covers all of that frame.
    const CGFloat height = nsWindow.frame.size.height;
    QRect buttons;
    for (const NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton *button = [nsWindow standardWindowButton:kind];
        if (!button || button.hidden)
            continue;
        const NSRect rect = [button convertRect:button.bounds toView:nil];
        buttons |= QRectF(rect.origin.x, height - NSMaxY(rect), rect.size.width, rect.size.height)
                       .toAlignedRect();
    }
    return buttons;
}

} // namespace QFlexDock::MacWindow
