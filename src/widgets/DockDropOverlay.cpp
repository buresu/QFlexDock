// SPDX-License-Identifier: MIT
#include "widgets/DockDropOverlay.h"

#include "core/DockManager_p.h"

#include <QtGui/QPainter>

namespace QFlexDock {

DockDropOverlay::DockDropOverlay(DockManagerPrivate *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    m_overrides.borderWidth = -1;
    m_overrides.cornerRadius = -1;
    m_overrides.zoneGap = -1;
    m_overrides.zoneMargin = -1;
    m_overrides.buttonSize = -1;
    hide();
}

void DockDropOverlay::setScene(const DockOverlayScene &scene)
{
    m_scene = scene;
    update();
}

DockOverlayStyle DockDropOverlay::effectiveStyle() const
{
    DockOverlayStyle style = m_manager ? m_manager->theme.overlay : DockOverlayStyle{};
    const auto pick = [](QColor &into, const QColor &from) {
        if (from.isValid())
            into = from;
    };
    pick(style.zoneColor, m_overrides.zoneColor);
    pick(style.zoneBorderColor, m_overrides.zoneBorderColor);
    pick(style.hoverColor, m_overrides.hoverColor);
    pick(style.hoverBorderColor, m_overrides.hoverBorderColor);
    pick(style.previewColor, m_overrides.previewColor);
    pick(style.previewBorderColor, m_overrides.previewBorderColor);
    pick(style.glyphColor, m_overrides.glyphColor);
    pick(style.buttonColor, m_overrides.buttonColor);
    if (m_overrides.borderWidth >= 0)
        style.borderWidth = m_overrides.borderWidth;
    if (m_overrides.cornerRadius >= 0)
        style.cornerRadius = m_overrides.cornerRadius;
    if (m_overrides.zoneGap >= 0)
        style.zoneGap = m_overrides.zoneGap;
    if (m_overrides.zoneMargin >= 0)
        style.zoneMargin = m_overrides.zoneMargin;
    if (m_overrides.buttonSize >= 0)
        style.buttonSize = m_overrides.buttonSize;
    if (m_showPreview)
        style.showPreview = *m_showPreview;
    if (m_guide)
        style.guide = *m_guide;
    return style.resolved(palette());
}

void DockDropOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    DockDefaultOverlayPainter fallback;
    DockOverlayPainter *overlayPainter = m_manager && m_manager->overlayPainter
        ? m_manager->overlayPainter.get() : &fallback;
    overlayPainter->paint(&painter, m_scene, effectiveStyle());
}

} // namespace QFlexDock
