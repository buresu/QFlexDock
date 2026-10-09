// SPDX-License-Identifier: MIT
#pragma once

#include "Style.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QToolButton>

#include <functional>

// The window around the dock areas: no frame from the window system, a title
// bar of its own, rounded corners while it is not maximized. None of this is
// docking; it is what an application with this look has to bring itself.
namespace VsStyle {

/// A strip along the border of the window by which it is resized. It lies
/// over the content and paints nothing.
class ResizeGrip : public QWidget
{
public:
    ResizeGrip(Qt::Edges edges, QWidget *window)
        : QWidget(window)
        , m_edges(edges)
    {
        if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
            setCursor(Qt::SizeFDiagCursor);
        else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
            setCursor(Qt::SizeBDiagCursor);
        else if (edges & (Qt::LeftEdge | Qt::RightEdge))
            setCursor(Qt::SizeHorCursor);
        else
            setCursor(Qt::SizeVerCursor);
    }

    [[nodiscard]] Qt::Edges edges() const { return m_edges; }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemResize(m_edges);
    }

private:
    Qt::Edges m_edges;
};

/// The frameless top-level window: a rounded shape with a thin outline while
/// it floats on the desktop, a plain rectangle when maximized.
class Window : public QWidget
{
public:
    static constexpr int Radius = 9;
    // Thin enough to leave the dock area its own edges (a side bar that is
    // put away is pulled back out of one).
    static constexpr int GripWidth = 4;

    Window()
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground); // for the corners
        m_layout = new QVBoxLayout(this);
        m_layout->setSpacing(0);
        m_layout->setContentsMargins(1, 1, 1, 1);
        for (Qt::Edges edges : {Qt::Edges(Qt::LeftEdge), Qt::Edges(Qt::RightEdge),
                                Qt::Edges(Qt::TopEdge), Qt::Edges(Qt::BottomEdge),
                                Qt::LeftEdge | Qt::TopEdge, Qt::RightEdge | Qt::TopEdge,
                                Qt::LeftEdge | Qt::BottomEdge, Qt::RightEdge | Qt::BottomEdge}) {
            m_grips.append(new ResizeGrip(edges, this));
        }
    }

    /// From top to bottom. The first and the last part must not paint a
    /// background of their own: the window's shape shows through them.
    void addPart(QWidget *part, int stretch = 0)
    {
        m_layout->addWidget(part, stretch);
        for (ResizeGrip *grip : std::as_const(m_grips))
            grip->raise();
    }

    [[nodiscard]] bool fillsTheScreen() const { return isMaximized() || isFullScreen(); }
    void toggleMaximized() { fillsTheScreen() ? showNormal() : showMaximized(); }
    /// Called when the window is maximized or restored.
    std::function<void()> stateChanged;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (fillsTheScreen()) {
            painter.fillRect(rect(), QColor(Chrome));
            return;
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(0xff3c3c3c), 1));
        painter.setBrush(QColor(Chrome));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Radius, Radius);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        const int w = width();
        const int h = height();
        const int g = GripWidth;
        const int c = g * 3; // corners reach further than the sides are thick
        for (ResizeGrip *grip : std::as_const(m_grips)) {
            const Qt::Edges e = grip->edges();
            if (e == Qt::Edges(Qt::LeftEdge))
                grip->setGeometry(0, c, g, h - 2 * c);
            else if (e == Qt::Edges(Qt::RightEdge))
                grip->setGeometry(w - g, c, g, h - 2 * c);
            else if (e == Qt::Edges(Qt::TopEdge))
                grip->setGeometry(c, 0, w - 2 * c, g);
            else if (e == Qt::Edges(Qt::BottomEdge))
                grip->setGeometry(c, h - g, w - 2 * c, g);
            else
                grip->setGeometry(e & Qt::LeftEdge ? 0 : w - c, e & Qt::TopEdge ? 0 : h - c, c, c);
        }
    }

    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() != QEvent::WindowStateChange)
            return;
        // Maximized, there is neither an outline nor anything to resize by.
        const int margin = fillsTheScreen() ? 0 : 1;
        m_layout->setContentsMargins(margin, margin, margin, margin);
        for (ResizeGrip *grip : std::as_const(m_grips))
            grip->setVisible(!fillsTheScreen());
        update();
        if (stateChanged)
            stateChanged();
    }

private:
    QVBoxLayout *m_layout;
    QList<ResizeGrip *> m_grips;
};

/// The row at the top: menus, the command centre, the buttons that put the
/// side areas away, and the window's own three buttons. Dragging it moves the
/// window.
class TitleBar : public QWidget
{
public:
    explicit TitleBar(Window *window)
        : QWidget(window)
        , m_window(window)
    {
        setObjectName(u"titleBar"_s);
        setAttribute(Qt::WA_StyledBackground);
        setFixedHeight(36);

        auto *logo = new QLabel(this);
        logo->setPixmap(pixmap(Glyph::Logo, Accent, 18));
        logo->setAttribute(Qt::WA_TransparentForMouseEvents);
        logo->setContentsMargins(10, 0, 6, 0);
        m_menuBar = new QMenuBar(this);
        m_menuBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        m_commandCenter = new QPushButton(this);
        m_commandCenter->setObjectName(u"commandCenter"_s);
        m_commandCenter->setIcon(icon(Glyph::Search, TextMuted, 14));
        m_commandCenter->setFocusPolicy(Qt::NoFocus);
        m_commandCenter->setMinimumWidth(160);
        m_commandCenter->setMaximumWidth(520);
        m_commandCenter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 1);
        m_layout->setSpacing(2);
        m_layout->addWidget(logo);
        m_layout->addWidget(m_menuBar);
        m_layout->addStretch(1);
        m_layout->addWidget(button(Glyph::ArrowLeft, u"Go Back"_s, IconMuted));
        m_layout->addWidget(button(Glyph::ArrowRight, u"Go Forward"_s, IconMuted));
        m_layout->addSpacing(4);
        m_layout->addWidget(m_commandCenter, 4);
        m_layout->addStretch(1);
        m_buttons = new QHBoxLayout;
        m_buttons->setSpacing(2);
        m_layout->addLayout(m_buttons);
        m_layout->addSpacing(8);

        QToolButton *minimize = windowButton(Glyph::Minimize, u"Minimize"_s);
        m_maximize = windowButton(Glyph::Maximize, u"Maximize"_s);
        QToolButton *close = windowButton(Glyph::Close, u"Close"_s);
        close->setObjectName(u"closeButton"_s);
        connect(minimize, &QToolButton::clicked, window, &QWidget::showMinimized);
        connect(m_maximize, &QToolButton::clicked, window, &Window::toggleMaximized);
        connect(close, &QToolButton::clicked, window, &QWidget::close);
        window->stateChanged = [this] {
            const bool filled = m_window->fillsTheScreen();
            m_maximize->setIcon(icon(filled ? Glyph::Restore : Glyph::Maximize));
            m_maximize->setToolTip(filled ? u"Restore"_s : u"Maximize"_s);
        };
    }

    [[nodiscard]] QMenuBar *menuBar() const { return m_menuBar; }
    [[nodiscard]] QPushButton *commandCenter() const { return m_commandCenter; }

    /// A checkable button left of the window buttons.
    QToolButton *addLayoutButton(Glyph off, Glyph on, const QString &toolTip)
    {
        auto *b = new QToolButton(this);
        b->setIcon(icon(off, Text, on, Text));
        b->setIconSize(QSize(16, 16));
        b->setCheckable(true);
        b->setToolTip(toolTip);
        b->setFocusPolicy(Qt::NoFocus);
        m_buttons->addWidget(b);
        return b;
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressed = true;
            m_press = event->position().toPoint();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_pressed && event->buttons().testFlag(Qt::LeftButton)
            && (event->position().toPoint() - m_press).manhattanLength()
                   >= QApplication::startDragDistance()) {
            m_pressed = false;
            if (QWindow *handle = m_window->windowHandle())
                handle->startSystemMove();
        }
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_pressed = false; }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressed = false;
            m_window->toggleMaximized();
        }
    }

private:
    QToolButton *button(Glyph glyph, const QString &toolTip, QRgb rgb = Text)
    {
        auto *b = new QToolButton(this);
        b->setIcon(icon(glyph, rgb));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(toolTip);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    }

    QToolButton *windowButton(Glyph glyph, const QString &toolTip)
    {
        QToolButton *b = button(glyph, toolTip);
        b->setObjectName(u"windowButton"_s);
        m_layout->addWidget(b);
        return b;
    }

    Window *m_window;
    QHBoxLayout *m_layout;
    QHBoxLayout *m_buttons;
    QMenuBar *m_menuBar;
    QPushButton *m_commandCenter;
    QToolButton *m_maximize;
    QPoint m_press;
    bool m_pressed = false;
};

/// The list that drops down from the top of the window: type to narrow it
/// down, Enter to run what is selected. It is a child of the window, not a
/// window of its own.
class CommandPalette : public QFrame
{
public:
    struct Entry
    {
        QString text;
        /// Shown after the text, in a muted colour.
        QString detail;
        QIcon icon;
        std::function<void()> run;
        /// Listed after ">"; everything else is a file.
        bool command = false;
    };

    explicit CommandPalette(QWidget *window)
        : QFrame(window)
    {
        setObjectName(u"commandPalette"_s);
        m_input = new QLineEdit(this);
        m_input->setPlaceholderText(u"Search files by name, or type > for commands"_s);
        m_list = new QListWidget(this);
        m_list->setFocusPolicy(Qt::NoFocus);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setItemDelegate(new Delegate(m_list));
        m_list->setIconSize(QSize(16, 16));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(4);
        layout->addWidget(m_input);
        layout->addWidget(m_list);
        hide();

        m_input->installEventFilter(this);
        window->installEventFilter(this);
        connect(m_input, &QLineEdit::textChanged, this, [this] { refill(); });
        connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            runEntry(item->data(Qt::UserRole + 1).toInt());
        });
        // Gone as soon as the user turns to something else.
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
            if (isVisible() && now != m_input)
                hide();
        });
    }

    /// Asked for the entries each time the palette opens.
    std::function<QList<Entry>()> entries;

    void open(const QString &text = {})
    {
        m_entries = entries ? entries() : QList<Entry>();
        m_input->setText(text);
        refill();
        place();
        show();
        raise();
        m_input->setFocus(Qt::ShortcutFocusReason);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
            place();
        if (watched == m_input && event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            const int row = m_list->currentRow();
            if (key == Qt::Key_Escape) {
                hide();
                return true;
            }
            if (key == Qt::Key_Down || key == Qt::Key_Up) {
                const int count = m_list->count();
                if (count > 0)
                    m_list->setCurrentRow((row + (key == Qt::Key_Down ? 1 : count - 1)) % count);
                return true;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                if (QListWidgetItem *item = m_list->currentItem())
                    runEntry(item->data(Qt::UserRole + 1).toInt());
                return true;
            }
        }
        return QFrame::eventFilter(watched, event);
    }

private:
    /// Name in the text colour, where it is found in a muted one after it.
    class Delegate : public QStyledItemDelegate
    {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        void paint(QPainter *painter, const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
        {
            QStyleOptionViewItem opt = option;
            initStyleOption(&opt, index);
            const QString name = opt.text;
            opt.text.clear();
            const QWidget *widget = opt.widget;
            widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

            const QRect text = opt.rect.adjusted(32, 0, -8, 0);
            const bool selected = opt.state.testFlag(QStyle::State_Selected);
            painter->setPen(QColor(selected ? TextBright : Text));
            painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft, name);
            const int used = opt.fontMetrics.horizontalAdvance(name) + 10;
            painter->setPen(QColor(TextMuted));
            painter->drawText(text.adjusted(used, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                              opt.fontMetrics.elidedText(index.data(Qt::UserRole).toString(),
                                                         Qt::ElideRight, text.width() - used));
        }

        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            return QSize(QStyledItemDelegate::sizeHint(option, index).width(), RowHeight);
        }
    };

    static constexpr int RowHeight = 26;

    void refill()
    {
        // "> " lists the commands; anything else is matched against all names.
        QString needle = m_input->text().trimmed();
        const bool commandsOnly = needle.startsWith(u'>');
        if (commandsOnly)
            needle = needle.mid(1).trimmed();
        m_list->clear();
        for (int i = 0; i < m_entries.size(); ++i) {
            const Entry &entry = m_entries.at(i);
            if ((commandsOnly && !entry.command)
                || !entry.text.contains(needle, Qt::CaseInsensitive)) {
                continue;
            }
            auto *item = new QListWidgetItem(entry.icon, entry.text, m_list);
            item->setData(Qt::UserRole, entry.detail);
            item->setData(Qt::UserRole + 1, i);
        }
        m_list->setCurrentRow(0);
        const int rows = qBound(1, m_list->count(), 12);
        m_list->setFixedHeight(rows * RowHeight + 2);
        adjustSize();
    }

    void place()
    {
        const QWidget *window = parentWidget();
        setFixedWidth(qBound(280, window->width() - 120, 600));
        adjustSize();
        move((window->width() - width()) / 2, 5);
    }

    void runEntry(int index)
    {
        hide();
        if (index >= 0 && index < m_entries.size() && m_entries.at(index).run)
            m_entries.at(index).run();
    }

    QLineEdit *m_input;
    QListWidget *m_list;
    QList<Entry> m_entries;
};

} // namespace VsStyle
