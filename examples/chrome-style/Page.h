// SPDX-License-Identifier: MIT
#pragma once

#include "Style.h"

#include <QtGui/QMouseEvent>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QToolButton>

#include <functional>

// What a tab shows: the toolbar and a page, as a browser that was just
// installed has them. All of it is a mock that looks the part; nothing is
// loaded from anywhere.
namespace ChromeStyle {

struct Site
{
    QString name;
    QRgb color = 0xff5f6368;
};

/// What the page of a new tab offers to open: as little as a browser that
/// was never used has.
inline QList<Site> shortcuts()
{
    return {{u"Web Store"_s, 0xff5f8fd9}};
}

inline QToolButton *toolButton(Glyph glyph, const QString &toolTip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(u"toolButton"_s);
    button->setIcon(icon(glyph));
    button->setIconSize(QSize(18, 18));
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

inline QFrame *barSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(u"barSeparator"_s);
    line->setFixedSize(1, 18);
    return line;
}

/// The address field: a pill that lights up around the text while it is edited.
class AddressBar : public QWidget
{
public:
    explicit AddressBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(36);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::IBeamCursor);
        m_edit = new QLineEdit(this);
        m_edit->setObjectName(u"addressField"_s);
        m_edit->setFrame(false);
        m_edit->setPlaceholderText(u"Search Google or type a URL"_s);
        m_edit->installEventFilter(this);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(46, 0, 16, 0);
        layout->addWidget(m_edit);
        QObject::connect(m_edit, &QLineEdit::returnPressed, this, [this] {
            if (entered && !m_edit->text().trimmed().isEmpty())
                entered(m_edit->text().trimmed());
        });
    }

    [[nodiscard]] QLineEdit *edit() const { return m_edit; }
    void setSite(const QString &text, const QPixmap &mark)
    {
        m_edit->setText(text);
        m_mark = mark;
        update();
    }

    std::function<void(const QString &)> entered;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool focused = m_edit->hasFocus();
        const QRectF pill = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.setPen(focused ? QPen(QColor(Accent), 2) : QPen(Qt::NoPen));
        painter.setBrush(QColor(Field));
        painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        // What the page is, in a disc of its own.
        const QRectF disc(5, 4, 28, 28);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Toolbar));
        painter.drawEllipse(disc);
        const QPixmap mark = m_mark.isNull() ? pixmap(Glyph::Search) : m_mark;
        painter.drawPixmap(QRectF(disc.center() - QPointF(8, 8), QSizeF(16, 16)), mark,
                           QRectF(mark.rect()));
    }
    void mousePressEvent(QMouseEvent *) override
    {
        m_edit->setFocus(Qt::MouseFocusReason);
        m_edit->selectAll();
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
            update();
        return QWidget::eventFilter(watched, event);
    }

private:
    QLineEdit *m_edit;
    QPixmap m_mark;
};

/// The page of a new tab: a wordmark, a search field and shortcuts to sites.
class NewTabPage : public QWidget
{
public:
    explicit NewTabPage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }

    std::function<void(const Site &)> siteChosen;
    std::function<void()> searchClicked;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(Page));
        const Layout l = arrange();

        // Top right: two links and the apps grid.
        QFont small = font();
        small.setPixelSize(13);
        painter.setFont(small);
        int x = width() - 28;
        painter.drawPixmap(QPointF(x - 18, 25), pixmap(Glyph::Grid, Text, 18));
        x -= 18 + 24;
        painter.setPen(QColor(Text));
        for (const QString &link : {u"Images"_s, u"Gmail"_s}) {
            const int w = painter.fontMetrics().horizontalAdvance(link);
            painter.drawText(QRect(x - w, 18, w, 32), Qt::AlignVCenter, link);
            x -= w + 18;
        }

        // The name of the search engine, in the application's own font.
        QFont mark = font();
        mark.setPixelSize(qBound(40, height() / 7, 84));
        mark.setWeight(QFont::Medium);
        mark.setLetterSpacing(QFont::AbsoluteSpacing, -2);
        painter.setFont(mark);
        painter.setPen(Qt::white);
        painter.drawText(l.mark, Qt::AlignCenter, u"Google"_s);

        // The search field.
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(l.search, l.search.height() / 2, l.search.height() / 2);
        painter.drawPixmap(QPointF(l.search.left() + 18, l.search.center().y() - 9),
                           pixmap(Glyph::Search, 0xff5f6368, 18));
        QFont field = font();
        field.setPixelSize(15);
        painter.setFont(field);
        painter.setPen(QColor(0xff5f6368));
        painter.drawText(l.search.adjusted(50, 0, -150, 0), Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(u"Search Google or type a URL"_s,
                                                          Qt::ElideRight, int(l.search.width()) - 200));
        const QRectF chip(l.search.right() - 96, l.search.top() + 6, 88, l.search.height() - 12);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xfff1f3f4));
        painter.drawRoundedRect(chip, chip.height() / 2, chip.height() / 2);
        painter.drawPixmap(QPointF(chip.left() + 10, chip.center().y() - 8),
                           pixmap(Glyph::Sparkle, 0xff1f1f1f));
        painter.setPen(QColor(0xff1f1f1f));
        painter.setFont(small);
        painter.drawText(chip.adjusted(30, 0, -8, 0), Qt::AlignVCenter, u"AI Mode"_s);
        painter.drawPixmap(QPointF(chip.left() - 36, l.search.center().y() - 9),
                           pixmap(Glyph::Camera, 0xff1f1f1f, 18));
        painter.drawPixmap(QPointF(chip.left() - 72, l.search.center().y() - 9),
                           pixmap(Glyph::Microphone, 0xff1f1f1f, 18));

        // Shortcuts: a disc and a name each, and one to add another.
        const QList<Site> sites = shortcuts();
        for (qsizetype i = 0; i < l.tiles.size(); ++i) {
            const QRectF tile = l.tiles.at(i);
            const QRectF disc(tile.center().x() - 24, tile.top() + 4, 48, 48);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(i == m_hovered ? 0xff3c4043 : 0xff2d2e30));
            painter.drawEllipse(disc);
            const bool add = i >= sites.size();
            if (add) {
                painter.drawPixmap(disc.center() - QPointF(9, 9), pixmap(Glyph::Plus, Text, 18));
            } else {
                painter.drawPixmap(disc.center() - QPointF(12, 12),
                                   letterPixmap(sites.at(i).name, sites.at(i).color, 24));
            }
            painter.setPen(QColor(Text));
            painter.setFont(small);
            const QString name = add ? u"Add shortcut"_s : sites.at(i).name;
            painter.drawText(QRectF(tile.left(), disc.bottom() + 10, tile.width(), 20),
                             Qt::AlignHCenter | Qt::AlignTop,
                             painter.fontMetrics().elidedText(name, Qt::ElideRight, int(tile.width())));
        }

        // The button in the corner.
        const QString customize = u"Customize Chrome"_s;
        const int w = painter.fontMetrics().horizontalAdvance(customize) + 54;
        const QRectF pill(width() - w - 16, height() - 50, w, 34);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xff282a2d));
        painter.drawRoundedRect(pill, 17, 17);
        painter.drawPixmap(QPointF(pill.left() + 14, pill.center().y() - 8),
                           pixmap(Glyph::Pencil, Accent));
        painter.setPen(QColor(Accent));
        painter.drawText(pill.adjusted(38, 0, -12, 0), Qt::AlignVCenter, customize);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const qsizetype hovered = tileAt(event->position());
        if (hovered != m_hovered) {
            m_hovered = hovered;
            setCursor(hovered >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hovered = -1;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const qsizetype tile = tileAt(event->position());
        const QList<Site> sites = shortcuts();
        if (event->button() != Qt::LeftButton)
            return;
        if (tile >= 0 && tile < sites.size() && siteChosen)
            siteChosen(sites.at(tile));
        else if (arrange().search.contains(event->position()) && searchClicked)
            searchClicked();
    }

private:
    struct Layout
    {
        QRectF mark;
        QRectF search;
        QList<QRectF> tiles;
    };

    [[nodiscard]] Layout arrange() const
    {
        Layout l;
        const qreal middle = width() / 2.0;
        const qreal markHeight = qBound(56, height() / 5, 110);
        const qreal top = qMax(70.0, height() * 0.17);
        l.mark = QRectF(0, top, width(), markHeight);
        const qreal searchWidth = qBound(280.0, width() - 120.0, 700.0);
        l.search = QRectF(middle - searchWidth / 2, l.mark.bottom() + 28, searchWidth, 46);
        const qsizetype count = shortcuts().size() + 1;
        const qreal tileWidth = qMin(104.0, (width() - 40.0) / count);
        const qreal left = middle - tileWidth * count / 2;
        for (qsizetype i = 0; i < count; ++i)
            l.tiles.append(QRectF(left + i * tileWidth, l.search.bottom() + 30, tileWidth, 90));
        return l;
    }

    [[nodiscard]] qsizetype tileAt(const QPointF &pos) const
    {
        const Layout l = arrange();
        for (qsizetype i = 0; i < l.tiles.size(); ++i) {
            if (l.tiles.at(i).contains(pos))
                return i;
        }
        return -1;
    }

    qsizetype m_hovered = -1;
};

/// A page that was "opened": a header in the colour of the site and grey bars
/// where its text would be. It is there to tell one tab from another.
class SitePage : public QWidget
{
public:
    using QWidget::QWidget;

    void setSite(const Site &site)
    {
        m_site = site;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(0xfff8f9fa));
        painter.fillRect(QRect(0, 0, width(), 64), QColor(m_site.color));
        QFont title = font();
        title.setPixelSize(22);
        title.setBold(true);
        painter.setFont(title);
        painter.setPen(Qt::white);
        painter.drawText(QRect(32, 0, width() - 64, 64), Qt::AlignVCenter, m_site.name);

        painter.setPen(Qt::NoPen);
        const int left = qMax(32, width() / 2 - 380);
        const int right = width() - left;
        int y = 104;
        painter.setBrush(QColor(0xff3c4043));
        painter.drawRoundedRect(QRectF(left, y, qMin(420, right - left), 22), 4, 4);
        y += 52;
        painter.setBrush(QColor(0xffdadce0));
        static constexpr int Lines[] = {100, 96, 88, 100, 62, 0, 100, 92, 97, 40, 0, 94, 100, 71};
        for (const int percent : Lines) {
            if (percent > 0 && y < height() - 20)
                painter.drawRoundedRect(QRectF(left, y, (right - left) * percent / 100.0, 10), 3, 3);
            y += 24;
        }
    }

private:
    Site m_site;
};

/// The content of one tab.
class BrowserTab : public QWidget
{
public:
    explicit BrowserTab(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(420, 260);
        auto *toolbar = new QWidget(this);
        toolbar->setObjectName(u"toolbar"_s);
        toolbar->setAttribute(Qt::WA_StyledBackground);
        toolbar->setFixedHeight(ToolbarHeight);
        QToolButton *back = toolButton(Glyph::Back, u"Back"_s, toolbar);
        QToolButton *forward = toolButton(Glyph::Forward, u"Forward"_s, toolbar);
        back->setEnabled(false);
        forward->setEnabled(false);
        QToolButton *reload = toolButton(Glyph::Reload, u"Reload this page"_s, toolbar);
        m_address = new AddressBar(toolbar);
        QToolButton *extensions = toolButton(Glyph::Extension, u"Extensions"_s, toolbar);
        QToolButton *profile = toolButton(Glyph::Person, u"You"_s, toolbar);
        profile->setIcon(icon(Glyph::Person, 0xff9dbcf1));
        m_menuButton = toolButton(Glyph::Dots, u"Customize and control"_s, toolbar);
        m_menuButton->setPopupMode(QToolButton::InstantPopup);

        auto *row = new QHBoxLayout(toolbar);
        row->setContentsMargins(8, 0, 8, 2);
        row->setSpacing(4);
        row->addWidget(back);
        row->addWidget(forward);
        row->addWidget(reload);
        row->addSpacing(2);
        row->addWidget(m_address, 1);
        row->addSpacing(6);
        row->addWidget(extensions);
        row->addWidget(barSeparator(toolbar));
        row->addWidget(profile);
        row->addWidget(m_menuButton);

        m_newTab = new NewTabPage(this);
        m_site = new SitePage(this);
        m_pages = new QStackedWidget(this);
        m_pages->addWidget(m_newTab);
        m_pages->addWidget(m_site);

        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        column->addWidget(toolbar);
        column->addWidget(m_pages, 1);

        m_newTab->siteChosen = [this](const Site &site) { open(site); };
        m_newTab->searchClicked = [this] { focusAddress(); };
        m_address->entered = [this](const QString &text) { open({text, 0xff5f6368}); };
    }

    /// Title and icon of the tab changed.
    std::function<void(const QString &, const QIcon &)> pageChanged;

    [[nodiscard]] QToolButton *menuButton() const { return m_menuButton; }
    [[nodiscard]] bool showsSite() const { return m_pages->currentWidget() == m_site; }
    [[nodiscard]] Site site() const { return m_current; }

    void focusAddress()
    {
        m_address->edit()->setFocus(Qt::OtherFocusReason);
        m_address->edit()->selectAll();
    }

    void open(const Site &site)
    {
        m_current = site;
        m_site->setSite(site);
        m_pages->setCurrentWidget(m_site);
        const QPixmap mark = letterPixmap(site.name, site.color, 16);
        m_address->setSite(site.name.contains(u'.') ? site.name
                                                    : site.name.toLower().remove(u' ') + u".example"_s,
                           mark);
        m_site->setFocus(Qt::OtherFocusReason);
        if (pageChanged)
            pageChanged(site.name, QIcon(mark));
    }

private:
    AddressBar *m_address;
    QToolButton *m_menuButton;
    QStackedWidget *m_pages;
    NewTabPage *m_newTab;
    SitePage *m_site;
    Site m_current;
};

} // namespace ChromeStyle
