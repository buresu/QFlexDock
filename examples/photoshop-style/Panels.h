// SPDX-License-Identifier: MIT
#pragma once

#include "Style.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QWindow>

#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QTreeWidget>

// What is in the panels of this example: pictures of contents, with nothing
// behind them. The layout and the look are the point, not the functions.
namespace Photoshop {

inline QToolButton *toolButton(QWidget *parent, Glyph glyph, const QString &toolTip,
                               bool checkable = false, int size = 16)
{
    auto *button = new QToolButton(parent);
    button->setIcon(icon(glyph, Text, size));
    button->setIconSize(QSize(size, size));
    button->setToolTip(toolTip);
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

/// The row of small buttons at the bottom of a panel.
inline QWidget *footer(QWidget *parent, std::initializer_list<std::pair<Glyph, QString>> buttons)
{
    auto *bar = new QWidget(parent);
    bar->setObjectName(u"panelFooter"_s);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->setSpacing(6);
    layout->addStretch(1);
    for (const auto &[glyph, tip] : buttons)
        layout->addWidget(toolButton(bar, glyph, tip, false, 14));
    return bar;
}

/// A panel is at least this wide: a column of them is never a sliver.
inline QWidget *panel(QLayout *content)
{
    auto *widget = new QWidget;
    widget->setObjectName(u"panelBar"_s);
    widget->setMinimumSize(236, 90);
    content->setContentsMargins(0, 0, 0, 0);
    widget->setLayout(content);
    return widget;
}

// --- The tool palette ------------------------------------------------------------

/// Foreground and background colour, one square over the other.
class ColorWells : public QWidget
{
public:
    explicit ColorWells(int extent, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_extent(extent)
    {
        setFixedSize(extent, extent);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int side = m_extent * 5 / 8;
        const auto well = [&](int at, const QColor &color) {
            p.fillRect(at, at, side, side, QColor(0xfff0f0f0));
            p.fillRect(at + 1, at + 1, side - 2, side - 2, QColor(0xff202020));
            p.fillRect(at + 2, at + 2, side - 4, side - 4, color);
        };
        well(m_extent - side, Qt::white);
        well(0, Qt::black);
    }

private:
    int m_extent;
};

/// The tools, in one column or in two. In two it is the panel itself, with
/// a grip drawn at its top like the one its small form has in the strip.
class ToolBox : public QWidget
{
public:
    ToolBox(int columns, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_columns(columns)
    {
        static const std::pair<Glyph, const char *> tools[] = {
            {Glyph::Move, "Move Tool"}, {Glyph::Marquee, "Rectangular Marquee Tool"},
            {Glyph::Lasso, "Lasso Tool"}, {Glyph::ObjectSelect, "Object Selection Tool"},
            {Glyph::Crop, "Crop Tool"}, {Glyph::Frame, "Frame Tool"},
            {Glyph::Eyedropper, "Eyedropper Tool"}, {Glyph::Heal, "Healing Brush Tool"},
            {Glyph::Brush, "Brush Tool"}, {Glyph::Stamp, "Clone Stamp Tool"},
            {Glyph::HistoryBrush, "History Brush Tool"}, {Glyph::Eraser, "Eraser Tool"},
            {Glyph::Gradient, "Gradient Tool"}, {Glyph::Drop, "Blur Tool"},
            {Glyph::Dodge, "Dodge Tool"}, {Glyph::Zoom, "Zoom Tool"},
            {Glyph::Pen, "Pen Tool"}, {Glyph::Type, "Type Tool"},
            {Glyph::Arrow, "Path Selection Tool"}, {Glyph::Rectangle, "Rectangle Tool"},
            {Glyph::Hand, "Hand Tool"}, {Glyph::More, "More Tools"},
        };
        constexpr int Cell = 28;
        auto *grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(1);
        auto *group = new QButtonGroup(this);
        int index = 0;
        for (const auto &[glyph, name] : tools) {
            QToolButton *button = toolButton(this, glyph, QString::fromLatin1(name), true, 18);
            button->setFixedSize(Cell, Cell - 2);
            group->addButton(button);
            button->setChecked(glyph == Glyph::Marquee);
            grid->addWidget(button, index / columns, index % columns);
            ++index;
        }

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(3, columns > 1 ? GripHeight : 2, 3, 4);
        layout->setSpacing(6);
        layout->addLayout(grid);
        layout->addWidget(new ColorWells(columns > 1 ? 44 : 28, this), 0, Qt::AlignHCenter);
        auto *modes = new QGridLayout;
        modes->setContentsMargins(0, 0, 0, 0);
        modes->setSpacing(1);
        int mode = 0;
        for (const auto &[glyph, name] : {std::pair{Glyph::QuickMask, "Edit in Quick Mask Mode"},
                                          std::pair{Glyph::ScreenMode, "Change Screen Mode"},
                                          std::pair{Glyph::EditToolbar, "Edit Toolbar"}}) {
            QToolButton *button = toolButton(this, glyph, QString::fromLatin1(name), false, 18);
            button->setFixedSize(Cell, Cell - 2);
            // (The first of them has a row to itself, as it has in two columns.)
            const int cell = columns > 1 && mode > 0 ? mode + 1 : mode;
            modes->addWidget(button, cell / columns, cell % columns);
            ++mode;
        }
        layout->addLayout(modes);
        layout->addStretch(1);
        setObjectName(u"panelBar"_s);
        setFixedWidth(columns * (Cell + 1) - 1 + 6);
    }

    // As high as there is room: the window is not kept tall for it.
    QSize minimumSizeHint() const override { return QSize(width(), 0); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_columns < 2)
            return;
        QPainter p(this);
        p.fillRect(0, 0, width(), 1, QColor(Line));
        const int ticks = 8;
        const int left = (width() - (ticks * 2 - 1)) / 2;
        for (int i = 0; i < ticks; ++i)
            p.fillRect(left + i * 2, 3, 1, 4, QColor(0xff808080));
    }

private:
    static constexpr int GripHeight = 10;
    int m_columns;
};

// --- Panels that are painted -------------------------------------------------------

class ColorPicker : public QWidget
{
public:
    explicit ColorPicker(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(120);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect area = rect().adjusted(10, 10, -10, -12);
        // The two colours, then the field of the current hue, then the hues.
        const auto well = [&](const QPoint &at, const QColor &color) {
            p.fillRect(QRect(at, QSize(22, 22)), QColor(0xfff0f0f0));
            p.fillRect(QRect(at + QPoint(1, 1), QSize(20, 20)), QColor(0xff202020));
            p.fillRect(QRect(at + QPoint(2, 2), QSize(18, 18)), color);
        };
        well(area.topLeft() + QPoint(12, 12), Qt::white);
        well(area.topLeft(), Qt::black);

        const QRect hues(area.right() - 25, area.top(), 26, area.height());
        QLinearGradient spectrum(hues.topLeft(), hues.bottomLeft());
        for (int i = 0; i <= 6; ++i)
            spectrum.setColorAt(i / 6.0, QColor::fromHsvF(float(1.0 - i / 6.0) * 0.999f, 1, 1));
        p.fillRect(hues, spectrum);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(Text));
        const QPointF mark(hues.left() - 5, hues.bottom());
        p.drawPolygon(QPolygonF({mark, mark + QPointF(-6, -4), mark + QPointF(-6, 4)}));

        const QRect field(area.left() + 48, area.top(), hues.left() - 16 - area.left() - 48,
                          area.height());
        if (field.width() < 10)
            return;
        QLinearGradient saturation(field.topLeft(), field.topRight());
        saturation.setColorAt(0, Qt::white);
        saturation.setColorAt(1, QColor(0xffff0000));
        p.fillRect(field, saturation);
        QLinearGradient value(field.topLeft(), field.bottomLeft());
        value.setColorAt(0, QColor(0, 0, 0, 0));
        value.setColorAt(1, Qt::black);
        p.fillRect(field, value);
        p.setPen(QPen(Qt::white, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(field.left() + 1, field.bottom()), 5, 5);
    }
};

class SwatchGrid : public QWidget
{
public:
    explicit SwatchGrid(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(120);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        constexpr int Cell = 20;
        const int columns = qMax(1, (width() - 16) / Cell);
        for (int i = 0; i < 48; ++i) {
            const int row = i / columns;
            const QRect cell(8 + (i % columns) * Cell, 8 + row * Cell, Cell - 3, Cell - 3);
            if (cell.bottom() > height())
                break;
            const QColor color = i < 8 ? QColor::fromHsvF(0, 0, float(i) / 7)
                                       : QColor::fromHsvF(float((i - 8) % 10) / 10,
                                                          0.35f + 0.2f * float((i - 8) / 10),
                                                          1.0f - 0.18f * float((i - 8) / 10));
            p.fillRect(cell, color);
        }
    }
};

class GradientList : public QWidget
{
public:
    explicit GradientList(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(120);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        constexpr QRgb pairs[][2] = {{0xff000000, 0xffffffff}, {0xff1d4ed8, 0xff93c5fd},
                                     {0xff7c3aed, 0xfff0abfc}, {0xffdb2777, 0xfffde68a},
                                     {0xff059669, 0xffa7f3d0}, {0xffea580c, 0xfffef08a}};
        int x = 8;
        int y = 8;
        for (const auto &pair : pairs) {
            if (x + 44 > width()) {
                x = 8;
                y += 50;
            }
            const QRect cell(x, y, 44, 44);
            QLinearGradient fade(cell.topLeft(), cell.bottomRight());
            fade.setColorAt(0, QColor(pair[0]));
            fade.setColorAt(1, QColor(pair[1]));
            p.fillRect(cell, fade);
            x += 50;
        }
    }
};

// --- Panels made of ordinary widgets -------------------------------------------------

inline QWidget *makeColor()
{
    auto *layout = new QVBoxLayout;
    layout->addWidget(new ColorPicker, 1);
    return panel(layout);
}

inline QWidget *makeSwatches()
{
    auto *layout = new QVBoxLayout;
    layout->addWidget(new SwatchGrid, 1);
    layout->addWidget(footer(nullptr, {{Glyph::Folder, u"Create New Group"_s},
                                       {Glyph::NewItem, u"Create New Swatch"_s},
                                       {Glyph::Trash, u"Delete Swatch"_s}}));
    return panel(layout);
}

inline QWidget *makeGradients()
{
    auto *layout = new QVBoxLayout;
    layout->addWidget(new GradientList, 1);
    layout->addWidget(footer(nullptr, {{Glyph::Folder, u"Create New Group"_s},
                                       {Glyph::NewItem, u"Create New Gradient"_s},
                                       {Glyph::Trash, u"Delete Gradient"_s}}));
    return panel(layout);
}

inline QWidget *makePatterns()
{
    auto *search = new QLineEdit;
    search->setPlaceholderText(u"Search Patterns"_s);
    auto *tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    tree->setIndentation(14);
    for (const QString &name : {u"Trees"_s, u"Grass"_s, u"Water"_s}) {
        auto *item = new QTreeWidgetItem(tree, {name});
        item->setIcon(0, icon(Glyph::Folder));
        new QTreeWidgetItem(item, {name + u" 1"_s});
    }
    auto *top = new QVBoxLayout;
    top->setContentsMargins(8, 8, 8, 4);
    top->addWidget(search);
    auto *layout = new QVBoxLayout;
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(tree, 1);
    layout->addWidget(footer(nullptr, {{Glyph::Folder, u"Create New Group"_s},
                                       {Glyph::NewItem, u"Create New Pattern"_s},
                                       {Glyph::Trash, u"Delete Pattern"_s}}));
    return panel(layout);
}

inline QWidget *makeProperties()
{
    const auto field = [](const QString &text, bool enabled = true) {
        auto *edit = new QLineEdit(text);
        edit->setEnabled(enabled);
        edit->setFixedWidth(74);
        return edit;
    };
    const auto caption = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return label;
    };
    auto *title = new QHBoxLayout;
    auto *sheet = new QLabel;
    sheet->setPixmap(pixmap(Glyph::Document));
    title->addWidget(sheet);
    title->addWidget(new QLabel(u"Document"_s), 1);

    auto *section = new QLabel(u"Canvas"_s);
    section->setObjectName(u"sectionTitle"_s);
    auto *size = new QGridLayout;
    size->setHorizontalSpacing(6);
    size->setVerticalSpacing(8);
    size->addWidget(caption(u"W"_s), 0, 0);
    size->addWidget(field(u"2133 px"_s), 0, 1);
    size->addWidget(caption(u"X"_s), 0, 2);
    size->addWidget(field(u"0 px"_s, false), 0, 3);
    size->addWidget(caption(u"H"_s), 1, 0);
    size->addWidget(field(u"2133 px"_s), 1, 1);
    size->addWidget(caption(u"Y"_s), 1, 2);
    size->addWidget(field(u"0 px"_s, false), 1, 3);
    size->setColumnStretch(4, 1);

    auto *orientation = new QHBoxLayout;
    orientation->addSpacing(22);
    QToolButton *portrait = toolButton(nullptr, Glyph::Portrait, u"Portrait"_s, true);
    portrait->setChecked(true);
    orientation->addWidget(portrait);
    orientation->addWidget(toolButton(nullptr, Glyph::Landscape, u"Landscape"_s, true));
    orientation->addStretch(1);

    auto *mode = new QComboBox;
    mode->addItems({u"RGB Color"_s, u"CMYK Color"_s, u"Grayscale"_s});
    auto *depth = new QComboBox;
    depth->addItems({u"8 Bits/Channel"_s, u"16 Bits/Channel"_s});
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(u"Mode"_s, mode);
    form->addRow(QString(), depth);

    auto *layout = new QVBoxLayout;
    layout->setSpacing(10);
    layout->addLayout(title);
    layout->addWidget(section);
    layout->addLayout(size);
    layout->addLayout(orientation);
    layout->addWidget(new QLabel(u"      Resolution: 300 pixels/inch"_s));
    layout->addLayout(form);
    layout->addStretch(1);
    layout->setContentsMargins(12, 10, 12, 8);

    // More than fits a panel that is given little room: it scrolls.
    auto *page = new QWidget;
    page->setObjectName(u"panelBar"_s);
    page->setLayout(layout);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    auto *outer = new QVBoxLayout;
    outer->addWidget(scroll);
    return panel(outer);
}

/// A panel that is a list of lines, each with a small picture.
inline QWidget *makeList(std::initializer_list<std::pair<Glyph, QString>> lines, int current,
                         std::initializer_list<std::pair<Glyph, QString>> buttons)
{
    auto *list = new QListWidget;
    list->setIconSize(QSize(16, 16));
    for (const auto &[glyph, text] : lines)
        list->addItem(new QListWidgetItem(icon(glyph), text));
    list->setCurrentRow(current);
    auto *layout = new QVBoxLayout;
    layout->setSpacing(0);
    layout->addWidget(list, 1);
    if (buttons.size() > 0)
        layout->addWidget(footer(nullptr, buttons));
    return panel(layout);
}

inline QWidget *makeNote(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(u"muted"_s);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    auto *layout = new QVBoxLayout;
    layout->addWidget(label, 1);
    return panel(layout);
}

inline QWidget *makeLayers()
{
    auto *kind = new QComboBox;
    kind->addItem(icon(Glyph::Search), u"Kind"_s);
    auto *filter = new QHBoxLayout;
    filter->setSpacing(3);
    filter->addWidget(kind, 1);
    for (const auto &[glyph, tip] : {std::pair{Glyph::Image, u"Pixel layers"_s},
                                     std::pair{Glyph::Adjustments, u"Adjustment layers"_s},
                                     std::pair{Glyph::Type, u"Type layers"_s},
                                     std::pair{Glyph::Artboard, u"Shape layers"_s}}) {
        filter->addWidget(toolButton(nullptr, glyph, tip, false, 14));
    }

    auto *blend = new QComboBox;
    blend->addItems({u"Normal"_s, u"Multiply"_s, u"Screen"_s});
    blend->setEnabled(false);
    auto *opacity = new QLineEdit(u"100%"_s);
    opacity->setEnabled(false);
    opacity->setFixedWidth(52);
    auto *blending = new QHBoxLayout;
    blending->addWidget(blend, 1);
    blending->addWidget(new QLabel(u"Opacity:"_s));
    blending->addWidget(opacity);

    auto *locks = new QHBoxLayout;
    locks->setSpacing(2);
    locks->addWidget(new QLabel(u"Lock:"_s));
    for (const auto &[glyph, tip] : {std::pair{Glyph::Pixels, u"Lock transparent pixels"_s},
                                     std::pair{Glyph::Brush, u"Lock image pixels"_s},
                                     std::pair{Glyph::Position, u"Lock position"_s},
                                     std::pair{Glyph::Lock, u"Lock all"_s}}) {
        locks->addWidget(toolButton(nullptr, glyph, tip, false, 14));
    }
    locks->addStretch(1);

    auto *list = new QListWidget;
    list->setIconSize(QSize(36, 28));
    QPixmap thumbnail(36, 28);
    thumbnail.fill(Qt::white);
    QIcon sheet(thumbnail);
    sheet.addPixmap(thumbnail, QIcon::Selected); // white, also in the selected line
    list->addItem(new QListWidgetItem(sheet, u"Background"_s));
    list->setCurrentRow(0);

    auto *top = new QVBoxLayout;
    top->setContentsMargins(8, 8, 8, 6);
    top->setSpacing(6);
    top->addLayout(filter);
    top->addLayout(blending);
    top->addLayout(locks);
    auto *layout = new QVBoxLayout;
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(list, 1);
    layout->addWidget(footer(nullptr, {{Glyph::Link, u"Link layers"_s},
                                       {Glyph::Fx, u"Add a layer style"_s},
                                       {Glyph::Mask, u"Add a mask"_s},
                                       {Glyph::Adjustments, u"Create new adjustment layer"_s},
                                       {Glyph::Folder, u"Create a new group"_s},
                                       {Glyph::NewItem, u"Create a new layer"_s},
                                       {Glyph::Trash, u"Delete layer"_s}}));
    return panel(layout);
}

// --- A document --------------------------------------------------------------------

/// The sheet in the middle of its surroundings.
class Canvas : public QWidget
{
public:
    explicit Canvas(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

    /// Where a bar `height` high belongs that is to hang below the sheet:
    /// the middle of its top edge.
    [[nodiscard]] QPoint below(int height) const
    {
        return QPoint(width() / 2, qMin(sheet().bottom() + 10, this->height() - height - 8));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(Well));
        p.fillRect(sheet(), Qt::white);
    }

private:
    [[nodiscard]] QRect sheet() const
    {
        const int side = qMax(40, qMin(width() - 60, height() - 110));
        return QRect((width() - side) / 2, qMax(20, (height() - side) / 2 - 20), side, side);
    }
};

/// The bar of suggestions that hangs below the current document: a window
/// of its own, which is moved by the grip at its start and docks nowhere.
class TaskBar : public QWidget
{
public:
    explicit TaskBar(QWidget *owner)
        : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint)
    {
        setObjectName(u"taskBar"_s);
        setAttribute(Qt::WA_TranslucentBackground); // for the round corners
        setAttribute(Qt::WA_ShowWithoutActivating);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 5, 6, 5);
        layout->setSpacing(6);
        m_grip = new QWidget(this);
        m_grip->setFixedWidth(10);
        m_grip->setCursor(Qt::SizeAllCursor);
        m_grip->installEventFilter(this);
        layout->addWidget(m_grip);
        for (const auto &[glyph, text] : {std::pair{Glyph::Sparkle, u"Generate image"_s},
                                          std::pair{Glyph::Image, u"Add from device"_s}}) {
            auto *button = new QToolButton(this);
            button->setIcon(icon(glyph));
            button->setText(text);
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setFocusPolicy(Qt::NoFocus);
            layout->addWidget(button);
        }
        layout->addWidget(toolButton(this, Glyph::More, u"More options"_s));
    }

    /// Whether the user has put the bar somewhere: it stays there then.
    [[nodiscard]] bool wasMoved() const { return m_moved; }

protected:
    // Nobody paints the background of a translucent window, the style sheet's
    // included: it is asked for here.
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QStyleOption option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_grip)
            return QWidget::eventFilter(watched, event);
        if (event->type() == QEvent::Paint) {
            QPainter p(m_grip);
            for (int row = 0; row < 6; ++row)
                p.fillRect(3, m_grip->height() / 2 - 11 + row * 4, 4, 2, QColor(Text));
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress && windowHandle()
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            m_moved = true;
            windowHandle()->startSystemMove();
            return true;
        }
        return false;
    }

private:
    QWidget *m_grip;
    bool m_moved = false;
};

inline QWidget *makeDocument()
{
    auto *widget = new QWidget;
    widget->setMinimumSize(260, 200);
    auto *zoom = new QLabel(u"66.67%"_s);
    auto *info = new QLabel(u"2133 px x 2133 px (300 ppi)"_s);
    auto *status = new QWidget;
    status->setObjectName(u"statusRow"_s);
    auto *row = new QHBoxLayout(status);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(zoom);
    row->addWidget(info);
    row->addWidget(toolButton(status, Glyph::ChevronRight, u"Document information"_s, false, 12));
    auto *across = new QScrollBar(Qt::Horizontal);
    across->setRange(0, 100);
    across->setPageStep(60);
    across->setValue(20);
    row->addWidget(across, 1);
    // (Short of the corner, which is round where the document is a window.)
    row->addSpacing(12);

    auto *down = new QScrollBar(Qt::Vertical);
    down->setRange(0, 100);
    down->setPageStep(60);
    down->setValue(20);
    auto *grid = new QGridLayout(widget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(new Canvas, 0, 0);
    grid->addWidget(down, 0, 1);
    grid->addWidget(status, 1, 0, 1, 2);
    grid->setRowStretch(0, 1);
    grid->setColumnStretch(0, 1);
    return widget;
}

} // namespace Photoshop
