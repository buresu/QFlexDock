// SPDX-License-Identifier: MIT
#pragma once

#include "Style.h"

#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolButton>

// Stand-ins for the contents of the docks. They only have to look the part:
// nothing here records, mixes or streams anything.
namespace ObsStyle {

/// The strip of small buttons along the bottom of a dock.
class ToolBar : public QWidget
{
public:
    explicit ToolBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("dockToolBar"));
        setAttribute(Qt::WA_StyledBackground);
        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(6, 6, 6, 6);
        m_layout->setSpacing(6);
    }

    QToolButton *addButton(Glyph glyph, const QString &toolTip, bool plain = false)
    {
        auto *button = new QToolButton(this);
        button->setIcon(icon(glyph));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(toolTip);
        button->setFocusPolicy(Qt::NoFocus);
        if (plain)
            button->setObjectName(QStringLiteral("plain"));
        m_layout->addWidget(button);
        return button;
    }
    void addSeparator()
    {
        auto *line = new QFrame(this);
        line->setObjectName(QStringLiteral("toolSeparator"));
        line->setFixedHeight(22);
        m_layout->addWidget(line);
    }
    void addLabel(const QString &text) { m_layout->addWidget(new QLabel(text, this)); }
    void addStretch() { m_layout->addStretch(1); }

private:
    QHBoxLayout *m_layout;
};

inline QWidget *withToolBar(QWidget *content, ToolBar *bar)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(content, 1);
    layout->addWidget(bar);
    return page;
}

inline QWidget *makeScenes()
{
    auto *list = new QListWidget;
    list->addItem(QStringLiteral("Scene"));
    list->setCurrentRow(0);

    auto *bar = new ToolBar;
    bar->addButton(Glyph::Plus, QStringLiteral("Add scene"), true);
    bar->addButton(Glyph::Trash, QStringLiteral("Remove scene"));
    bar->addSeparator();
    bar->addButton(Glyph::Filters, QStringLiteral("Scene filters"));
    bar->addSeparator();
    bar->addButton(Glyph::Up, QStringLiteral("Move up"));
    bar->addButton(Glyph::Down, QStringLiteral("Move down"));
    bar->addStretch();
    return withToolBar(list, bar);
}

/// One row of the source list: kind, name, visibility and lock.
inline QWidget *makeSourceRow(Glyph kind, const QString &name, bool visible)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(10);
    const QRgb color = visible ? Text : TextMuted;
    const auto addIcon = [&](Glyph glyph, QRgb rgb) {
        auto *label = new QLabel(row);
        label->setPixmap(icon(glyph, rgb).pixmap(16, 16));
        layout->addWidget(label);
    };
    addIcon(kind, color);
    auto *label = new QLabel(name, row);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(QColor(color).name()));
    layout->addWidget(label, 1);
    addIcon(visible ? Glyph::Eye : Glyph::EyeOff, color);
    addIcon(Glyph::Lock, TextMuted);
    return row;
}

inline QWidget *makeSources()
{
    auto *list = new QListWidget;
    const auto add = [list](Glyph kind, const QString &name, bool visible) {
        auto *item = new QListWidgetItem(list);
        item->setSizeHint(QSize(0, 36));
        list->setItemWidget(item, makeSourceRow(kind, name, visible));
    };
    add(Glyph::Play, QStringLiteral("Media"), false);
    add(Glyph::Camera, QStringLiteral("Camera"), true);

    auto *bar = new ToolBar;
    bar->addButton(Glyph::Plus, QStringLiteral("Add source"), true);
    bar->addButton(Glyph::Trash, QStringLiteral("Remove source"));
    bar->addSeparator();
    bar->addButton(Glyph::Gear, QStringLiteral("Source properties"));
    bar->addSeparator();
    bar->addButton(Glyph::Up, QStringLiteral("Move up"));
    bar->addButton(Glyph::Down, QStringLiteral("Move down"));
    bar->addStretch();
    return withToolBar(list, bar);
}

/// One channel of the mixer: fader, level meter and scale, all painted.
class MixerStrip : public QWidget
{
public:
    MixerStrip(const QString &group, const QString &name, double level, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_group(group)
        , m_name(name)
        , m_level(level)
    {
        setFixedWidth(136);
        setMinimumHeight(190);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int w = width();

        // Which group the channel belongs to.
        const QRect band(0, 0, w - 1, 17);
        p.fillRect(band, QColor(0x1f, 0x3a, 0x8f));
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.82);
        small.setBold(true);
        p.setFont(small);
        p.setPen(QColor(Text));
        p.drawText(band, Qt::AlignCenter, m_group);

        p.setFont(font());
        p.drawText(QRect(6, 22, w - 26, 20), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(m_name, Qt::ElideRight, w - 26));
        p.setBrush(QColor(TextMuted));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(w - 16, 29), QPointF(w - 8, 29), QPointF(w - 12, 34)}));
        p.setPen(QColor(TextMuted));
        p.drawText(QRect(9, 44, w - 12, 18), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("0.0 dB"));

        const int top = 68;
        const int bottom = height() - 34;
        const int span = qMax(10, bottom - top);

        // Fader: a thin track with the handle at the top.
        p.setPen(QPen(QColor(0x55, 0x6f, 0xd0), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(40, top + 8), QPointF(40, bottom));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xf2, 0xf3, 0xf5));
        p.drawRoundedRect(QRectF(35, top - 2, 10, 22), 3, 3);

        // Two meter bars: dim where there is no signal, lit up to the level.
        const auto zoneColor = [](double position, bool lit) {
            // position: 0 at the bottom (-60 dB), 1 at the top (0 dB)
            const QColor c = position > 0.85 ? QColor(0xd2, 0x3c, 0x4b)
                : position > 0.66 ? QColor(0xe0, 0xa8, 0x1e) : QColor(0x3f, 0xd2, 0x4b);
            return lit ? c : c.darker(280);
        };
        for (int bar = 0; bar < 2; ++bar) {
            const int x = 62 + bar * 11;
            for (int y = 0; y < span; ++y) {
                const double position = 1.0 - double(y) / span;
                p.fillRect(QRect(x, top + y, 9, 1), zoneColor(position, position <= m_level));
            }
        }
        if (m_level > 0) {
            const int peak = top + int((1.0 - m_level) * span);
            p.fillRect(QRect(62, peak - 2, 20, 2), QColor(Program));
        }

        // Scale.
        small.setBold(false);
        p.setFont(small);
        p.setPen(QColor(0xc8, 0xca, 0xd0));
        const QStringList marks{QStringLiteral("0"), QStringLiteral("-6"), QStringLiteral("-12"),
                                QStringLiteral("-18"), QStringLiteral("-24"), QStringLiteral("-30"),
                                QStringLiteral("-36"), QStringLiteral("-42"), QStringLiteral("-48"),
                                QStringLiteral("-54"), QStringLiteral("-60")};
        for (int i = 0; i < marks.size(); ++i) {
            const int y = top + int(double(i) / double(marks.size() - 1) * span);
            p.drawText(QRect(86, y - 7, 30, 14), Qt::AlignVCenter | Qt::AlignLeft, marks.at(i));
        }

        icon(Glyph::Speaker).paint(&p, QRect(50, height() - 26, 16, 16));
        icon(Glyph::Monitor, TextMuted).paint(&p, QRect(82, height() - 26, 16, 16));

        p.setPen(QColor(Header));
        p.drawLine(w - 1, 0, w - 1, height());
    }

private:
    QString m_group;
    QString m_name;
    double m_level;
};

inline QWidget *makeMixer()
{
    auto *strips = new QWidget;
    auto *row = new QHBoxLayout(strips);
    row->setContentsMargins(1, 1, 0, 0);
    row->setSpacing(0);
    row->addWidget(new MixerStrip(QStringLiteral("Global"), QStringLiteral("Desktop Audio"), 0.62));
    row->addWidget(new MixerStrip(QStringLiteral("Global"), QStringLiteral("Mic/Aux"), 0.38));
    row->addWidget(new MixerStrip(QStringLiteral("Active"), QStringLiteral("Camera"), 0.0));
    row->addStretch(1);

    auto *bar = new ToolBar;
    bar->addLabel(QStringLiteral("0 hidden"));
    bar->addSeparator();
    bar->addStretch();
    bar->addSeparator();
    bar->addButton(Glyph::Layout, QStringLiteral("Layout"), true);
    bar->addSeparator();
    bar->addButton(Glyph::Gear, QStringLiteral("Advanced audio properties"), true);
    bar->addSeparator();
    bar->addLabel(QStringLiteral("Options  ▾"));
    return withToolBar(strips, bar);
}

inline QWidget *makeTransitions()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 8, 6, 8);
    layout->setSpacing(8);

    auto *kind = new QComboBox(page);
    kind->addItems({QStringLiteral("Fade"), QStringLiteral("Cut")});
    layout->addWidget(kind);

    auto *durationRow = new QHBoxLayout;
    auto *durationLabel = new QLabel(QStringLiteral("Duration"), page);
    durationLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    auto *duration = new QSpinBox(page);
    duration->setRange(50, 20000);
    duration->setSingleStep(50);
    duration->setValue(300);
    duration->setSuffix(QStringLiteral(" ms"));
    durationRow->addWidget(durationLabel);
    durationRow->addWidget(duration, 1);
    layout->addLayout(durationRow);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    for (Glyph glyph : {Glyph::Plus, Glyph::Trash, Glyph::Dots}) {
        auto *button = new QToolButton(page);
        button->setIcon(icon(glyph));
        button->setIconSize(QSize(16, 16));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    layout->addStretch(1);
    return page;
}

inline QWidget *makeControls()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    for (const char *text : {"Start Streaming", "Start Recording", "Studio Mode", "Settings"})
        layout->addWidget(new QPushButton(QString::fromLatin1(text), page));
    layout->addStretch(1);
    return page;
}

/// The program output: a black canvas kept at 16:9 inside whatever room there is.
class Canvas : public QWidget
{
public:
    using QWidget::QWidget;
    QSize minimumSizeHint() const override { return QSize(240, 135); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(PreviewBack));
        const QSize canvas = QSize(16, 9).scaled(size() - QSize(8, 8), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - canvas.width()) / 2, (height() - canvas.height()) / 2),
                           canvas);
        p.fillRect(target, QColor(Program));
    }
};

/// What the docks are arranged around: the preview with its two bars below.
inline QWidget *makePreview()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *canvas = new Canvas(page);
    canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(canvas, 1);

    auto *zoom = new QWidget(page);
    zoom->setObjectName(QStringLiteral("zoomBar"));
    auto *zoomLayout = new QHBoxLayout(zoom);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    zoomLayout->setSpacing(10);
    const auto zoomButton = [zoom](const QString &text) {
        auto *button = new QToolButton(zoom);
        button->setText(text);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    zoomLayout->addWidget(zoomButton(QStringLiteral("−")));
    zoomLayout->addWidget(new QLabel(QStringLiteral("100%"), zoom));
    zoomLayout->addWidget(zoomButton(QStringLiteral("+")));
    zoomLayout->addWidget(new QLabel(QStringLiteral("Scale to Window  ▾"), zoom));
    zoomLayout->addStretch(1);
    layout->addWidget(zoom);

    auto *context = new QWidget(page);
    context->setObjectName(QStringLiteral("contextBar"));
    context->setAttribute(Qt::WA_StyledBackground);
    auto *contextLayout = new QHBoxLayout(context);
    contextLayout->setContentsMargins(10, 8, 10, 8);
    contextLayout->setSpacing(8);
    contextLayout->addWidget(new QLabel(QStringLiteral("No source selected"), context));
    contextLayout->addSpacing(24);
    for (const auto &[glyph, text] : {std::pair{Glyph::Gear, "Properties"},
                                      std::pair{Glyph::Filters, "Filters"}}) {
        auto *button = new QPushButton(icon(glyph, TextMuted), QString::fromLatin1(text), context);
        button->setObjectName(QStringLiteral("flat"));
        button->setFocusPolicy(Qt::NoFocus);
        contextLayout->addWidget(button);
    }
    contextLayout->addStretch(1);
    layout->addWidget(context);
    return page;
}

} // namespace ObsStyle
