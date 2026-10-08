// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockTheme.h>

#include <QtWidgets/QWidget>

#include <optional>

namespace QFlexDock {

class DockManagerPrivate;

/// The drop guide shown over a dock area while something is dragged across
/// it. It only displays a scene computed elsewhere; it takes no input.
///
/// Each colour and metric is a Q_PROPERTY, so a style sheet can set it with
/// `qproperty-<name>`. Precedence: property set here (style sheet or code),
/// then the manager's DockTheme, then the palette.
class QFLEXDOCK_EXPORT DockDropOverlay : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor zoneColor READ zoneColor WRITE setZoneColor)
    Q_PROPERTY(QColor zoneBorderColor READ zoneBorderColor WRITE setZoneBorderColor)
    Q_PROPERTY(QColor hoverColor READ hoverColor WRITE setHoverColor)
    Q_PROPERTY(QColor hoverBorderColor READ hoverBorderColor WRITE setHoverBorderColor)
    Q_PROPERTY(QColor previewColor READ previewColor WRITE setPreviewColor)
    Q_PROPERTY(QColor previewBorderColor READ previewBorderColor WRITE setPreviewBorderColor)
    Q_PROPERTY(QColor glyphColor READ glyphColor WRITE setGlyphColor)
    Q_PROPERTY(qreal borderWidth READ borderWidth WRITE setBorderWidth)
    Q_PROPERTY(qreal cornerRadius READ cornerRadius WRITE setCornerRadius)
    Q_PROPERTY(int zoneGap READ zoneGap WRITE setZoneGap)
    Q_PROPERTY(int zoneMargin READ zoneMargin WRITE setZoneMargin)
    Q_PROPERTY(bool showPreview READ showPreview WRITE setShowPreview)

public:
    explicit DockDropOverlay(DockManagerPrivate *manager, QWidget *parent);

    void setManager(DockManagerPrivate *manager) { m_manager = manager; }
    void setScene(const DockOverlayScene &scene);
    [[nodiscard]] const DockOverlayScene &scene() const { return m_scene; }
    /// The style actually painted with, all layers resolved.
    [[nodiscard]] DockOverlayStyle effectiveStyle() const;

    QColor zoneColor() const { return effectiveStyle().zoneColor; }
    void setZoneColor(const QColor &c) { m_overrides.zoneColor = c; update(); }
    QColor zoneBorderColor() const { return effectiveStyle().zoneBorderColor; }
    void setZoneBorderColor(const QColor &c) { m_overrides.zoneBorderColor = c; update(); }
    QColor hoverColor() const { return effectiveStyle().hoverColor; }
    void setHoverColor(const QColor &c) { m_overrides.hoverColor = c; update(); }
    QColor hoverBorderColor() const { return effectiveStyle().hoverBorderColor; }
    void setHoverBorderColor(const QColor &c) { m_overrides.hoverBorderColor = c; update(); }
    QColor previewColor() const { return effectiveStyle().previewColor; }
    void setPreviewColor(const QColor &c) { m_overrides.previewColor = c; update(); }
    QColor previewBorderColor() const { return effectiveStyle().previewBorderColor; }
    void setPreviewBorderColor(const QColor &c) { m_overrides.previewBorderColor = c; update(); }
    QColor glyphColor() const { return effectiveStyle().glyphColor; }
    void setGlyphColor(const QColor &c) { m_overrides.glyphColor = c; update(); }
    qreal borderWidth() const { return effectiveStyle().borderWidth; }
    void setBorderWidth(qreal w) { m_overrides.borderWidth = w; update(); }
    qreal cornerRadius() const { return effectiveStyle().cornerRadius; }
    void setCornerRadius(qreal r) { m_overrides.cornerRadius = r; update(); }
    int zoneGap() const { return effectiveStyle().zoneGap; }
    void setZoneGap(int gap) { m_overrides.zoneGap = gap; update(); }
    int zoneMargin() const { return effectiveStyle().zoneMargin; }
    void setZoneMargin(int margin) { m_overrides.zoneMargin = margin; update(); }
    bool showPreview() const { return effectiveStyle().showPreview; }
    void setShowPreview(bool show) { m_showPreview = show; update(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    DockManagerPrivate *m_manager;
    DockOverlayScene m_scene;
    // Values set through the properties; invalid / negative means "not set".
    DockOverlayStyle m_overrides;
    std::optional<bool> m_showPreview;
};

} // namespace QFlexDock
