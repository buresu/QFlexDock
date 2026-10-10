// SPDX-License-Identifier: MIT
#pragma once

#include "Panels.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenuBar>

#include <functional>

// The window around the dock areas: no frame from the window system, a row
// with the menus and the window's buttons, and a row of options for the
// current tool. None of this is docking; it is what an application with this
// look brings itself.
namespace Photoshop {

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

/// The frameless top-level window: its parts one below the other inside a
/// thin line with round corners. It paints the colour of the panels behind
/// everything, so that whatever lies in a corner and paints nothing itself
/// is round with it.
class Window : public QWidget
{
public:
    static constexpr int Radius = 8;
    // (Thin: what is dragged to the border of the window to be docked there
    // is over the dock area inside, not over a grip.)
    static constexpr int GripWidth = 3;

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
            painter.fillRect(rect(), QColor(Frame));
            return;
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(0xff2b2b2b), 1));
        painter.setBrush(QColor(Frame));
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
        // Maximized, there is neither a line around it nor anything to resize by.
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

/// The row at the top: the menus and the window's own three buttons.
/// Dragging it moves the window.
class TitleBar : public QWidget
{
public:
    explicit TitleBar(Window *window)
        : QWidget(window)
        , m_window(window)
    {
        setObjectName(u"titleBar"_s);
        setProperty("rounded", true);
        setFixedHeight(32);

        auto *logo = new QLabel(this);
        logo->setPixmap(pixmap(Glyph::Logo, Text, 18));
        logo->setAttribute(Qt::WA_TransparentForMouseEvents);
        logo->setContentsMargins(10, 0, 4, 0);
        m_menuBar = new QMenuBar(this);
        m_menuBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(2);
        m_layout->addWidget(logo);
        m_layout->addWidget(m_menuBar);
        m_layout->addStretch(1);

        QToolButton *minimize = windowButton(Glyph::Minimize, u"Minimize"_s);
        m_maximize = windowButton(Glyph::Maximize, u"Maximize"_s);
        m_close = windowButton(Glyph::Close, u"Close"_s);
        QToolButton *close = m_close;
        close->setObjectName(u"closeButton"_s);
        connect(minimize, &QToolButton::clicked, window, &QWidget::showMinimized);
        connect(m_maximize, &QToolButton::clicked, window, &Window::toggleMaximized);
        connect(close, &QToolButton::clicked, window, &QWidget::close);
        window->stateChanged = [this] {
            const bool filled = m_window->fillsTheScreen();
            // (The button in the corner is round with the window.)
            setProperty("rounded", !filled);
            m_close->style()->unpolish(m_close);
            m_close->style()->polish(m_close);
            m_maximize->setIcon(icon(filled ? Glyph::Restore : Glyph::Maximize, Text, 12));
            m_maximize->setToolTip(filled ? u"Restore"_s : u"Maximize"_s);
        };
    }

    [[nodiscard]] QMenuBar *menuBar() const { return m_menuBar; }

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
    QToolButton *windowButton(Glyph glyph, const QString &toolTip)
    {
        QToolButton *button = toolButton(this, glyph, toolTip, false, 12);
        button->setObjectName(u"windowButton"_s);
        m_layout->addWidget(button);
        return button;
    }

    Window *m_window;
    QHBoxLayout *m_layout;
    QMenuBar *m_menuBar;
    QToolButton *m_maximize;
    QToolButton *m_close;
    QPoint m_press;
    bool m_pressed = false;
};

/// The row under the menus: what the current tool can be told. For show.
class OptionsBar : public QWidget
{
public:
    explicit OptionsBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(u"optionsBar"_s);
        setAttribute(Qt::WA_StyledBackground);
        setFixedHeight(40);
        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(10, 0, 10, 0);
        m_layout->setSpacing(4);

        add(Glyph::Home, u"Home"_s, false, 18);
        separator();
        auto *preset = add(Glyph::Marquee, u"Tool preset"_s, false, 18);
        preset->setPopupMode(QToolButton::MenuButtonPopup);
        separator();
        auto *modes = new QButtonGroup(this);
        for (const auto &[glyph, tip] : {std::pair{Glyph::SelectNew, u"New selection"_s},
                                         std::pair{Glyph::SelectAdd, u"Add to selection"_s},
                                         std::pair{Glyph::SelectSubtract, u"Subtract from selection"_s},
                                         std::pair{Glyph::SelectIntersect, u"Intersect with selection"_s}}) {
            QToolButton *button = add(glyph, tip, true);
            modes->addButton(button);
            button->setChecked(glyph == Glyph::SelectNew);
        }
        separator();
        label(u"Feather:"_s);
        field(u"0 px"_s, 64);
        auto *smooth = new QCheckBox(u"Anti-alias"_s, this);
        smooth->setEnabled(false);
        m_layout->addWidget(smooth);
        separator();
        label(u"Style:"_s);
        auto *style = new QComboBox(this);
        style->addItems({u"Normal"_s, u"Fixed Ratio"_s, u"Fixed Size"_s});
        style->setFixedWidth(108);
        m_layout->addWidget(style);
        label(u"Width:"_s)->setEnabled(false);
        field(QString(), 48)->setEnabled(false);
        label(u"Height:"_s)->setEnabled(false);
        field(QString(), 48)->setEnabled(false);
        separator();
        auto *refine = new QPushButton(u"Select and Mask..."_s, this);
        refine->setFocusPolicy(Qt::NoFocus);
        m_layout->addWidget(refine);
        m_layout->addStretch(1);
        add(Glyph::Share, u"Share"_s, false, 18);
        add(Glyph::Search, u"Search"_s, false, 18);
        m_workspace = add(Glyph::Workspace, u"Choose a workspace"_s, false, 18);
        m_workspace->setPopupMode(QToolButton::InstantPopup);
    }

    /// The button at the end, which the menu of workspaces belongs to.
    [[nodiscard]] QToolButton *workspaceButton() const { return m_workspace; }

private:
    QToolButton *add(Glyph glyph, const QString &toolTip, bool checkable = false, int size = 16)
    {
        QToolButton *button = toolButton(this, glyph, toolTip, checkable, size);
        m_layout->addWidget(button);
        return button;
    }

    QLabel *label(const QString &text)
    {
        auto *label = new QLabel(text, this);
        m_layout->addWidget(label);
        return label;
    }

    QLineEdit *field(const QString &text, int width)
    {
        auto *edit = new QLineEdit(text, this);
        edit->setFixedWidth(width);
        m_layout->addWidget(edit);
        return edit;
    }

    void separator()
    {
        auto *line = new QFrame(this);
        line->setObjectName(u"optionsSeparator"_s);
        m_layout->addWidget(line);
    }

    QHBoxLayout *m_layout;
    QToolButton *m_workspace;
};

} // namespace Photoshop
