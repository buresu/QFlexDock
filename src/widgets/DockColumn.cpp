// SPDX-License-Identifier: MIT
#include "widgets/DockColumn.h"

#include "widgets/DockAreaWidget.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleOption>

namespace QFlexDock {

namespace {

// How much wider than its icons a strip has to be for the titles to show.
constexpr int LabelRoom = 32;
// The widest a strip gets for its titles.
constexpr int LabelLimit = 240;

bool draggedFar(const QPoint &from, const QPoint &to)
{
    return (to - from).manhattanLength() >= QApplication::startDragDistance();
}

// For a panel without an icon: the first letter of its title.
QIcon letterIcon(const QString &title, const QColor &color, int size)
{
    constexpr qreal Scale = 2.0;
    QPixmap pixmap(int(size * Scale), int(size * Scale));
    pixmap.setDevicePixelRatio(Scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1));
    painter.drawRoundedRect(QRectF(0.5, 0.5, size - 1, size - 1), 3, 3);
    QFont font = painter.font();
    font.setPixelSize(qMax(6, size * 5 / 8));
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, title.left(1).toUpper());
    return QIcon(pixmap);
}

} // namespace

// --- DockColumnBar ---------------------------------------------------------------

DockColumnBar::DockColumnBar(DockManagerPrivate *manager, DockAreaWidget *area)
    : QWidget(area)
    , m_manager(manager)
    , m_area(area)
{
    setAttribute(Qt::WA_StyledBackground);
    const auto makeButton = [this](const char *name) {
        auto *button = new QToolButton(this);
        button->setObjectName(QLatin1String(name));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };
    m_iconify = makeButton("dockIconifyButton");
    m_close = makeButton("dockColumnCloseButton");
    m_close->setToolTip(tr("Close"));
    m_close->setAccessibleName(tr("Close"));
    m_close->hide();

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(1, 0, 1, 0);
    layout->setSpacing(0);
    layout->addWidget(m_iconify);
    layout->addStretch(1);
    layout->addWidget(m_close);

    connect(m_iconify, &QToolButton::clicked, this,
            [this] { m_area->toggleColumnIconified(m_column); });
    connect(m_close, &QToolButton::clicked, this, [this] { window()->close(); });
    refreshAppearance();
}

void DockColumnBar::configure(NodeId column, bool iconified, bool towardsRight, bool closes)
{
    m_column = column;
    auto *row = static_cast<QHBoxLayout *>(layout());
    // The button is at the end the column is pushed to; beside the one that
    // closes a window, where there is one.
    const bool atEnd = closes || towardsRight != iconified;
    const int wanted = atEnd ? 1 : 0;
    if (row->indexOf(m_iconify) != wanted) {
        row->removeWidget(m_iconify);
        row->insertWidget(wanted, m_iconify);
    }
    m_close->setVisible(closes);
    if (m_iconified != iconified || m_towardsRight != towardsRight) {
        m_iconified = iconified;
        m_towardsRight = towardsRight;
        style()->unpolish(this);
        style()->polish(this);
    }
    refreshAppearance();
}

void DockColumnBar::refreshAppearance()
{
    if (!m_manager)
        return;
    const int height = m_manager->columnBarHeight();
    const int icon = qMax(6, height - 4);
    for (QToolButton *button : {m_iconify, m_close}) {
        button->setIconSize(QSize(icon, icon));
        button->setFixedSize(qMax(height, icon + 6), height);
    }
    m_iconify->setIcon(m_manager->icon(m_towardsRight ? DockIcon::IconifyRight
                                                     : DockIcon::IconifyLeft, this));
    m_close->setIcon(m_manager->icon(DockIcon::Close, this));
    const QString tip = m_iconified ? tr("Expand Panels") : tr("Collapse to Icons");
    m_iconify->setToolTip(tip);
    m_iconify->setAccessibleName(tip);
}

void DockColumnBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (testAttribute(Qt::WA_StyleSheetTarget)) {
        QStyleOption option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    } else {
        // A shade apart from the headers below it.
        painter.fillRect(rect(), palette().color(QPalette::Window).darker(112));
    }
}

void DockColumnBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_press = event->position().toPoint();
    }
}

void DockColumnBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && event->buttons().testFlag(Qt::LeftButton)
        && draggedFar(m_press, event->position().toPoint())) {
        m_pressed = false; // one drag per press
        m_area->startColumnDrag(m_column);
    }
}

void DockColumnBar::mouseReleaseEvent(QMouseEvent *)
{
    m_pressed = false;
}

void DockColumnBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = false;
        m_area->toggleColumnIconified(m_column);
    }
}

void DockColumnBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        refreshAppearance();
}

// --- DockIconButton ----------------------------------------------------------------

DockIconButton::DockIconButton(const PanelId &panel, QWidget *parent)
    : QToolButton(parent)
    , m_panel(panel)
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setCheckable(true);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void DockIconButton::mousePressEvent(QMouseEvent *event)
{
    m_press = event->position().toPoint();
    QToolButton::mousePressEvent(event);
}

void DockIconButton::mouseMoveEvent(QMouseEvent *event)
{
    if (isDown() && event->buttons().testFlag(Qt::LeftButton)
        && draggedFar(m_press, event->position().toPoint())) {
        setDown(false);
        Q_EMIT dragStarted();
        return;
    }
    QToolButton::mouseMoveEvent(event);
}

// --- DockIconGrip ------------------------------------------------------------------

DockIconGrip::DockIconGrip(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void DockIconGrip::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (testAttribute(Qt::WA_StyleSheetTarget)) {
        QStyleOption option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    }
    // A short row of ticks in the middle.
    QColor color = palette().color(QPalette::WindowText);
    color.setAlphaF(color.alphaF() * 0.35f);
    const int ticks = qMin(8, width() / 4);
    const int left = (width() - (ticks * 2 - 1)) / 2;
    const int top = (height() - 4) / 2;
    for (int i = 0; i < ticks; ++i)
        painter.fillRect(left + i * 2, top, 1, 4, color);
}

void DockIconGrip::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_press = event->position().toPoint();
    }
}

void DockIconGrip::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && event->buttons().testFlag(Qt::LeftButton)
        && draggedFar(m_press, event->position().toPoint())) {
        m_pressed = false;
        Q_EMIT dragStarted();
    }
}

// --- DockIconStrip -----------------------------------------------------------------

DockIconStrip::DockIconStrip(DockManagerPrivate *manager, DockAreaWidget *area)
    : QFrame(area)
    , m_manager(manager)
    , m_area(area)
{
    setAttribute(Qt::WA_StyledBackground);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addStretch(1);
}

DockIconStrip::~DockIconStrip()
{
    releaseCompactWidgets();
}

// The small forms of panels belong to the manager, like their content: they
// leave before what holds them here is destroyed.
void DockIconStrip::releaseCompactWidgets()
{
    if (!m_manager)
        return;
    for (const Block &block : std::as_const(m_blocks)) {
        for (const PanelId &id : block.panels) {
            const DockPanel *panel = m_manager->panels.value(id);
            QWidget *compact = panel ? panel->compactWidget() : nullptr;
            if (compact && isAncestorOf(compact)) {
                compact->hide();
                compact->setParent(m_manager->parkingWidget());
            }
        }
    }
}

void DockIconStrip::retire()
{
    releaseCompactWidgets();
    m_manager = nullptr;
    hide();
    deleteLater();
}

void DockIconStrip::setColumn(const LayoutNode &column)
{
    m_nodeId = column.id;
    QList<Block> blocks;
    const auto collect = [&blocks](const auto &self, const LayoutNode &node) -> void {
        if (node.isTabs())
            blocks.append(Block{node.id, node.panels, node.active, nullptr});
        for (const auto &child : node.children)
            self(self, child);
    };
    collect(collect, column);

    bool same = blocks.size() == m_blocks.size();
    for (qsizetype i = 0; same && i < blocks.size(); ++i)
        same = blocks.at(i).node == m_blocks.at(i).node && blocks.at(i).panels == m_blocks.at(i).panels;
    if (same) {
        for (qsizetype i = 0; i < blocks.size(); ++i)
            m_blocks[i].active = blocks.at(i).active;
        updateChecked();
        return;
    }
    releaseCompactWidgets();
    for (const Block &block : std::as_const(m_blocks)) {
        m_layout->removeWidget(block.widget);
        block.widget->hide();
        block.widget->deleteLater();
    }
    m_blocks = blocks;
    rebuild();
}

void DockIconStrip::rebuild()
{
    if (!m_manager)
        return;
    int row = 0;
    for (Block &block : m_blocks) {
        block.widget = new QWidget(this);
        block.widget->setObjectName(QStringLiteral("dockIconBlock"));
        auto *layout = new QVBoxLayout(block.widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        const NodeId node = block.node;
        auto *grip = new DockIconGrip(block.widget);
        layout->addWidget(grip);
        connect(grip, &DockIconGrip::dragStarted, this, [this, node] {
            for (const Block &b : std::as_const(m_blocks)) {
                if (b.node == node)
                    m_area->startIconDrag(b.active, true, b.widget);
            }
        });

        for (const PanelId &id : std::as_const(block.panels)) {
            const DockPanel *panel = m_manager->panels.value(id);
            if (QWidget *compact = panel ? panel->compactWidget() : nullptr) {
                layout->addWidget(compact);
                compact->show();
                continue;
            }
            auto *button = new DockIconButton(id, block.widget);
            layout->addWidget(button);
            connect(button, &QToolButton::clicked, this,
                    [this, node, id] { m_area->iconButtonClicked(node, id); });
            connect(button, &DockIconButton::dragStarted, this,
                    [this, id, button] { m_area->startIconDrag(id, false, button); });
        }
        m_layout->insertWidget(row++, block.widget);
        block.widget->show();
    }
    refreshAppearance();
}

QList<DockIconButton *> DockIconStrip::buttons() const
{
    QList<DockIconButton *> all;
    for (const Block &block : m_blocks)
        all += block.widget->findChildren<DockIconButton *>(Qt::FindDirectChildrenOnly);
    return all;
}

DockIconButton *DockIconStrip::button(const PanelId &panel) const
{
    const QList<DockIconButton *> all = buttons();
    for (DockIconButton *button : all) {
        if (button->panelId() == panel)
            return button;
    }
    return nullptr;
}

QList<DockIconGrip *> DockIconStrip::grips() const
{
    QList<DockIconGrip *> all;
    for (const Block &block : m_blocks)
        all += block.widget->findChildren<DockIconGrip *>(Qt::FindDirectChildrenOnly);
    return all;
}

QList<NodeId> DockIconStrip::groups() const
{
    QList<NodeId> nodes;
    for (const Block &block : m_blocks)
        nodes.append(block.node);
    return nodes;
}

QWidget *DockIconStrip::block(NodeId group) const
{
    for (const Block &block : m_blocks) {
        if (block.node == group)
            return block.widget;
    }
    return nullptr;
}

QRect DockIconStrip::blockRect(NodeId group) const
{
    // (As the layout would have it, whether or not it got to it yet.)
    m_layout->activate();
    const QWidget *widget = block(group);
    return widget ? widget->geometry() : QRect();
}

NodeId DockIconStrip::groupAt(const QPoint &pos, bool *below) const
{
    if (below)
        *below = false;
    m_layout->activate();
    for (const Block &block : m_blocks) {
        if (pos.y() <= block.widget->geometry().bottom())
            return block.node;
    }
    if (below)
        *below = !m_blocks.isEmpty();
    return m_blocks.isEmpty() ? NodeId() : m_blocks.constLast().node;
}

void DockIconStrip::setShown(NodeId group)
{
    m_shown = group;
    updateChecked();
}

void DockIconStrip::updateChecked()
{
    for (const Block &block : std::as_const(m_blocks)) {
        const QList<DockIconButton *> all =
            block.widget->findChildren<DockIconButton *>(Qt::FindDirectChildrenOnly);
        for (DockIconButton *button : all) {
            const bool out = block.node == m_shown && block.active == button->panelId();
            button->setChecked(out);
        }
    }
}

QIcon DockIconStrip::iconFor(const DockPanel *panel) const
{
    if (!panel->icon().isNull())
        return panel->icon();
    const int size = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    return letterIcon(panel->title(), palette().color(QPalette::WindowText), size);
}

void DockIconStrip::refreshPanel(const PanelId &panel)
{
    if (!m_manager)
        return;
    const DockPanel *p = m_manager->panels.value(panel);
    const bool compact = p && p->compactWidget();
    const bool inside = std::any_of(m_blocks.cbegin(), m_blocks.cend(), [&panel](const Block &b) {
        return b.panels.contains(panel);
    });
    if (!inside)
        return;
    if (compact == (button(panel) != nullptr)) {
        // A small form came or went: what stands for the panel changes.
        releaseCompactWidgets();
        for (Block &block : m_blocks) {
            m_layout->removeWidget(block.widget);
            block.widget->hide();
            block.widget->deleteLater();
            block.widget = nullptr;
        }
        rebuild();
        return;
    }
    refreshAppearance();
}

void DockIconStrip::refreshAppearance()
{
    if (!m_manager)
        return;
    const int themed = m_manager->theme.iconSize;
    const QList<DockIconButton *> all = buttons();
    for (DockIconButton *button : all) {
        const DockPanel *panel = m_manager->panels.value(button->panelId());
        if (!panel)
            continue;
        QString title = panel->title();
        title.replace(QLatin1Char('&'), QLatin1String("&&")); // no mnemonics in titles
        button->setText(title);
        button->setIcon(iconFor(panel));
        if (themed > 0)
            button->setIconSize(QSize(themed, themed));
        const QString tip = panel->toolTip().isEmpty() ? panel->title() : panel->toolTip();
        button->setToolTip(tip);
        button->setAccessibleName(panel->title());
    }
    measure();
    updateLabels();
    updateChecked();
    m_area->contentLimitsChanged();
}

// How wide the strip is with icons alone, and how wide with the titles.
void DockIconStrip::measure()
{
    int iconWidth = 0;
    int labelWidth = 0;
    const QList<DockIconButton *> all = buttons();
    for (DockIconButton *button : all) {
        button->ensurePolished();
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        iconWidth = qMax(iconWidth, button->sizeHint().width());
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        labelWidth = qMax(labelWidth, qMin(button->sizeHint().width(), LabelLimit));
        button->setToolButtonStyle(m_labelled ? Qt::ToolButtonTextBesideIcon
                                              : Qt::ToolButtonIconOnly);
    }
    if (m_manager) {
        for (const Block &block : std::as_const(m_blocks)) {
            for (const PanelId &id : block.panels) {
                const DockPanel *panel = m_manager->panels.value(id);
                const QWidget *compact = panel ? panel->compactWidget() : nullptr;
                if (!compact)
                    continue;
                // (An explicit minimum wins over the hint, as in a layout.)
                const int least = qMin(compact->minimumWidth() > 0
                                           ? compact->minimumWidth()
                                           : qMax(0, compact->minimumSizeHint().width()),
                                       compact->maximumWidth());
                const int most = compact->maximumWidth() < QWIDGETSIZE_MAX
                    ? compact->maximumWidth() : qMax(least, compact->sizeHint().width());
                iconWidth = qMax(iconWidth, least);
                labelWidth = qMax(labelWidth, most);
            }
        }
    }
    const QMargins frame = contentsMargins();
    m_iconWidth = iconWidth + frame.left() + frame.right();
    m_labelWidth = qMax(iconWidth, labelWidth) + frame.left() + frame.right();
}

void DockIconStrip::updateLabels()
{
    const QList<DockIconButton *> all = buttons();
    const bool labelled = !all.isEmpty() && m_labelWidth > m_iconWidth
        && width() >= qMin(m_iconWidth + LabelRoom, m_labelWidth);
    for (DockIconButton *button : all) {
        button->setToolButtonStyle(labelled ? Qt::ToolButtonTextBesideIcon
                                            : Qt::ToolButtonIconOnly);
    }
    if (labelled != m_labelled) {
        m_labelled = labelled;
        style()->unpolish(this);
        style()->polish(this);
        update();
    }
}

SizeLimits DockIconStrip::sizeLimits() const
{
    SizeLimits limits;
    const QMargins frame = contentsMargins();
    limits.min = QSize(m_iconWidth, m_layout->minimumSize().height() + frame.top() + frame.bottom());
    limits.max = QSize(m_labelWidth, UnboundedSize);
    return limits;
}

QSize DockIconStrip::preferredSize() const
{
    const SizeLimits limits = sizeLimits();
    return QSize(limits.max.width(), limits.min.height());
}

void DockIconStrip::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateLabels();
}

void DockIconStrip::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    switch (event->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::FontChange:
        refreshAppearance();
        break;
    default:
        break;
    }
}

} // namespace QFlexDock
