// SPDX-License-Identifier: MIT
#pragma once

#include "Style.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QToolButton>

#include <functional>

// The window around the dock areas: no frame from the window system, a title
// row with the menus in it, a row of tool buttons and a status bar. None of
// this is docking; it is what an application with this look brings itself.
namespace VisualStudio {

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

/// The frameless top-level window: a rounded shape outlined in the accent
/// colour while it floats on the desktop, a plain rectangle when maximized.
class Window : public QWidget
{
public:
    static constexpr int Radius = 8;
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

    /// From top to bottom. The first part must not paint a background of its
    /// own: the window's shape shows through it.
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
            painter.fillRect(rect(), QColor(Shell));
            return;
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(isActiveWindow() ? Accent : Border), 1));
        painter.setBrush(QColor(Shell));
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
        if (event->type() == QEvent::ActivationChange)
            update(); // the outline tells
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

inline QToolButton *chromeButton(QWidget *parent, Glyph glyph, const QString &toolTip,
                                 QRgb rgb = Text)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(glyph, rgb));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(toolTip);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

/// The row at the top: the menus, a search box, and the window's own three
/// buttons. Dragging it moves the window.
class TitleBar : public QWidget
{
public:
    explicit TitleBar(Window *window)
        : QWidget(window)
        , m_window(window)
    {
        setObjectName(u"titleBar"_s);
        setFixedHeight(36);

        auto *logo = new QLabel(this);
        logo->setPixmap(pixmap(Glyph::Logo, Accent, 20));
        logo->setAttribute(Qt::WA_TransparentForMouseEvents);
        logo->setContentsMargins(10, 0, 4, 0);
        m_menuBar = new QMenuBar(this);
        m_menuBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto *search = new QToolButton(this);
        search->setObjectName(u"search"_s);
        search->setIcon(icon(Glyph::Search));
        search->setText(u"Search"_s);
        search->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        search->setFocusPolicy(Qt::NoFocus);

        auto *signIn = new QToolButton(this);
        signIn->setIcon(icon(Glyph::Account));
        signIn->setText(u"Sign in"_s);
        signIn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        signIn->setFocusPolicy(Qt::NoFocus);

        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(2);
        m_layout->addWidget(logo);
        m_layout->addWidget(m_menuBar);
        m_layout->addWidget(search);
        m_layout->addStretch(1);
        m_layout->addWidget(signIn);
        m_layout->addSpacing(12);

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
        QToolButton *b = chromeButton(this, glyph, toolTip);
        b->setObjectName(u"windowButton"_s);
        m_layout->addWidget(b);
        return b;
    }

    Window *m_window;
    QHBoxLayout *m_layout;
    QMenuBar *m_menuBar;
    QToolButton *m_maximize;
    QPoint m_press;
    bool m_pressed = false;
};

/// The row of tool buttons under the title: groups in rounded boxes. The
/// buttons are for show, except those given something to do.
class ToolBar : public QWidget
{
public:
    explicit ToolBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(u"toolBar"_s);
        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(8, 2, 8, 4);
        m_layout->setSpacing(6);
        m_layout->addStretch(1);
    }

    /// Starts a box of buttons; add() fills the last one started.
    void addGroup(bool atTheEnd = false)
    {
        auto *group = new QFrame(this);
        group->setObjectName(u"toolGroup"_s);
        m_group = new QHBoxLayout(group);
        m_group->setContentsMargins(4, 2, 4, 2);
        m_group->setSpacing(1);
        m_layout->insertWidget(atTheEnd ? m_layout->count() : m_layout->count() - 1, group);
    }

    QToolButton *add(Glyph glyph, const QString &toolTip, QRgb rgb = Text, const QString &text = {})
    {
        QToolButton *b = chromeButton(this, glyph, toolTip, rgb);
        if (!text.isEmpty()) {
            b->setText(text);
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
        m_group->addWidget(b);
        return b;
    }

    void addSeparator()
    {
        auto *line = new QFrame(this);
        line->setObjectName(u"toolSeparator"_s);
        m_group->addWidget(line);
    }

private:
    QHBoxLayout *m_layout;
    QHBoxLayout *m_group = nullptr;
};

/// The line at the bottom of the window.
class StatusBar : public QWidget
{
public:
    explicit StatusBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(u"statusBar"_s);
        setAttribute(Qt::WA_StyledBackground);
        setFixedHeight(26);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        layout->setSpacing(4);
        m_message = new QToolButton(this);
        m_message->setIcon(icon(Glyph::Comment));
        m_message->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_message->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(m_message);
        layout->addStretch(1);
        auto *repository = new QToolButton(this);
        repository->setIcon(icon(Glyph::Repository));
        repository->setText(u"Select Repository"_s);
        repository->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        repository->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(repository);
        layout->addWidget(chromeButton(this, Glyph::Bell, u"Notifications"_s));
        setMessage(u"Ready"_s);
    }

    void setMessage(const QString &text) { m_message->setText(text); }

private:
    QToolButton *m_message;
};

} // namespace VisualStudio
