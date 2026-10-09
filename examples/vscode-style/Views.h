// SPDX-License-Identifier: MIT
#pragma once

#include "Style.h"

#include <QtCore/QRegularExpression>
#include <QtGui/QFontDatabase>
#include <QtGui/QSyntaxHighlighter>
#include <QtGui/QTextBlock>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QTreeWidget>

#include <functional>

// Stand-ins for what the areas hold. They only have to look the part: nothing
// here searches, builds, debugs or talks to anything.
namespace VsStyle {

// --- The files of the make-believe project ---------------------------------------

struct File
{
    QString path;
    QString text;
};

inline QList<File> projectFiles()
{
    return {
        {u"src/CMakeLists.txt"_s, uR"(add_library(Docking)
add_library(Docking::Docking ALIAS Docking)

target_sources(Docking
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES
            ${PROJECT_SOURCE_DIR}/include/Docking/Layout.h
            ${PROJECT_SOURCE_DIR}/include/Docking/TabBar.h
    PRIVATE
        core/Layout.cpp
        core/Layout.h
        widgets/TabBar.cpp
        widgets/TabBar.h
)

target_compile_features(Docking PUBLIC cxx_std_20)
target_link_libraries(Docking PUBLIC Qt6::Widgets)

# Warnings are errors while developing, never for those who only build it.
if(DOCKING_DEVELOPER_BUILD)
    target_compile_options(Docking PRIVATE -Wall -Wextra -Werror)
endif()
)"_s},
        {u"src/core/Layout.h"_s, uR"(// A layout is a tree: splits inside, tab groups at the leaves.
#pragma once

#include <QtCore/QStringList>

#include <vector>

namespace Docking {

struct Node
{
    Qt::Orientation orientation = Qt::Horizontal;
    double weight = 1.0;
    std::vector<Node> children; // a split has children,
    QStringList panels;         // a tab group has panels
    QString current;

    [[nodiscard]] bool isTabs() const { return children.empty(); }
};

class Layout
{
public:
    [[nodiscard]] const Node *find(const QString &panel) const;
    bool insert(const QString &panel, const QString &beside, Qt::Edge edge);
    bool remove(const QString &panel);
    void normalize();

private:
    Node m_root;
};

} // namespace Docking
)"_s},
        {u"src/core/Layout.cpp"_s, uR"(#include "core/Layout.h"

#include <algorithm>

namespace Docking {

namespace {

const Node *findIn(const Node &node, const QString &panel)
{
    if (node.isTabs())
        return node.panels.contains(panel) ? &node : nullptr;
    for (const Node &child : node.children) {
        if (const Node *found = findIn(child, panel))
            return found;
    }
    return nullptr;
}

} // namespace

const Node *Layout::find(const QString &panel) const
{
    return findIn(m_root, panel);
}

bool Layout::remove(const QString &panel)
{
    Node *group = const_cast<Node *>(find(panel));
    if (!group)
        return false;
    group->panels.removeAll(panel);
    normalize(); // 1 child left: the split goes, 0 panels left: the group goes
    return true;
}

} // namespace Docking
)"_s},
        {u"src/widgets/TabBar.h"_s, uR"(#pragma once

#include <QtWidgets/QTabBar>

namespace Docking {

/// The tabs of one group. Dragging a tab away starts a dock drag.
class TabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit TabBar(QWidget *parent = nullptr);

    [[nodiscard]] int insertIndexAt(const QPoint &pos) const;

Q_SIGNALS:
    void dragStarted(int index);

protected:
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QPoint m_press;
    int m_pressed = -1;
};

} // namespace Docking
)"_s},
        {u"CMakeLists.txt"_s, uR"(cmake_minimum_required(VERSION 3.24)
project(Docking VERSION 0.1.0 LANGUAGES CXX)

option(DOCKING_BUILD_EXAMPLES "Build the examples" ON)
option(DOCKING_DEVELOPER_BUILD "Warnings as errors" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_subdirectory(src)
if(DOCKING_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
)"_s},
        {u"README.md"_s, uR"(# Docking

A make-believe project, here to give this window something to show.

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

## What to try

- Drag a tab onto the edge of an editor to split it.
- Drag the point where two boundaries meet to move both.
- Push the edge of a side bar far enough against it, and it is put away.
- Drag inwards from the edge it went to, and it is back.
- `Ctrl+B`, `Ctrl+J` and `Ctrl+Alt+B` put the side areas away and bring them back.
- `Ctrl+P` opens a file by name.
)"_s},
        {u".gitignore"_s, u"build/\n*.user\n.cache/\n"_s},
    };
}

// --- The editor --------------------------------------------------------------------

/// Colours a few things per line. Enough to look like code.
class Highlighter : public QSyntaxHighlighter
{
public:
    Highlighter(const QString &fileName, QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        const auto rule = [this](const QString &pattern, QRgb rgb, bool italic = false) {
            QTextCharFormat format;
            format.setForeground(QColor(rgb));
            format.setFontItalic(italic);
            m_rules.append({QRegularExpression(pattern), format});
        };
        constexpr QRgb Keyword = 0xfff0707a;
        constexpr QRgb Name = 0xffd7b8f5;
        constexpr QRgb Constant = 0xff73b8f5;
        constexpr QRgb String = 0xff9ed0f7;
        constexpr QRgb Comment = 0xff7a8791;
        constexpr QRgb Command = 0xffe2c48c;

        if (fileName.endsWith(".cpp"_L1) || fileName.endsWith(".h"_L1)) {
            rule(u"\\b[A-Za-z_]\\w*(?=\\()"_s, Name);
            rule(u"\\b(?:class|struct|namespace|public|private|protected|const|constexpr|static"
                 "|inline|explicit|override|return|if|else|for|while|using|auto|bool|int|double"
                 "|void|nullptr|true|false|this|const_cast|static_cast)\\b"_s,
                 Keyword);
            rule(u"\\b(?:Q[A-Z]\\w*|Qt|std)\\b"_s, Constant);
            rule(u"\\b\\d+(?:\\.\\d+)?\\b"_s, Constant);
            rule(u"^\\s*#\\s*\\w+"_s, Keyword);
            rule(u"\"[^\"]*\"|<[A-Za-z/_.]+>(?=\\s*$)"_s, String);
            rule(u"//.*$"_s, Comment, true);
        } else if (fileName.endsWith(".txt"_L1)) {
            rule(u"^\\s*\\w+(?=\\()"_s, Command);
            rule(u"\\b(?:PUBLIC|PRIVATE|INTERFACE|ALIAS|FILE_SET|HEADERS|BASE_DIRS|FILES|VERSION"
                 "|LANGUAGES|REQUIRED|COMPONENTS|ON|OFF|CXX)\\b"_s,
                 Constant);
            rule(u"\\$\\{[^}]*\\}"_s, Keyword);
            rule(u"\"[^\"]*\""_s, String);
            rule(u"#.*$"_s, Comment, true);
        } else if (fileName.endsWith(".md"_L1)) {
            rule(u"^#+ .*$"_s, Constant);
            rule(u"`[^`]*`"_s, String);
            rule(u"^\\s*- "_s, Keyword);
        }
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const Rule &rule : std::as_const(m_rules)) {
            auto matches = rule.pattern.globalMatch(text);
            while (matches.hasNext()) {
                const QRegularExpressionMatch match = matches.next();
                setFormat(int(match.capturedStart()), int(match.capturedLength()), rule.format);
            }
        }
    }

private:
    struct Rule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<Rule> m_rules;
};

/// A text editor with line numbers down its left and a miniature of the whole
/// text down its right.
class CodeEditor : public QPlainTextEdit
{
public:
    static constexpr int MinimapWidth = 84;

    CodeEditor(const QString &fileName, const QString &text, QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
    {
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPixelSize(14);
        setFont(font);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setFrameShape(QFrame::NoFrame);
        setTabStopDistance(4 * fontMetrics().horizontalAdvance(u' '));
        new Highlighter(fileName, document());
        setPlainText(text);
        document()->setModified(false);

        m_gutter = new Strip(this, [this](QPainter &p, const QRect &rect) { paintGutter(p, rect); });
        m_minimap = new Strip(this, [this](QPainter &p, const QRect &rect) { paintMinimap(p, rect); });
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { layoutStrips(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this] {
            m_gutter->update();
            m_minimap->update();
        });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { markCurrentLine(); });
        layoutStrips();
        markCurrentLine();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        layoutStrips();
    }

private:
    class Strip : public QWidget
    {
    public:
        Strip(QWidget *parent, std::function<void(QPainter &, const QRect &)> paint)
            : QWidget(parent)
            , m_paint(std::move(paint))
        {
        }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            m_paint(painter, rect());
        }

    private:
        std::function<void(QPainter &, const QRect &)> m_paint;
    };

    [[nodiscard]] int gutterWidth() const
    {
        const int digits = qMax(3, int(QString::number(blockCount()).size()));
        return 26 + digits * fontMetrics().horizontalAdvance(u'9') + 22;
    }

    void layoutStrips()
    {
        setViewportMargins(gutterWidth(), 0, MinimapWidth, 0);
        const QRect inside = contentsRect();
        const QRect view = viewport()->geometry();
        m_gutter->setGeometry(inside.left(), inside.top(), gutterWidth(), inside.height());
        m_minimap->setGeometry(view.right() + 1, inside.top(), MinimapWidth, inside.height());
    }

    void markCurrentLine()
    {
        QTextEdit::ExtraSelection line;
        line.format.setBackground(QColor(255, 255, 255, 10));
        line.format.setProperty(QTextFormat::FullWidthSelection, true);
        line.cursor = textCursor();
        line.cursor.clearSelection();
        setExtraSelections({line});
    }

    void paintGutter(QPainter &painter, const QRect &rect)
    {
        painter.setFont(font());
        const int current = textCursor().blockNumber();
        QTextBlock block = firstVisibleBlock();
        qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
        while (block.isValid() && top <= rect.bottom()) {
            const qreal height = blockBoundingRect(block).height();
            if (block.isVisible() && top + height >= rect.top()) {
                painter.setPen(QColor(block.blockNumber() == current ? Text : 0xff6e7681));
                painter.drawText(QRectF(0, top, rect.width() - 22, height),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(block.blockNumber() + 1));
            }
            top += height;
            block = block.next();
        }
    }

    // Every run of characters as a short dash, three pixels to a line.
    void paintMinimap(QPainter &painter, const QRect &rect)
    {
        constexpr int Row = 3;
        constexpr qreal Column = 1.1;
        const int first = firstVisibleBlock().blockNumber();
        const int visible = viewport()->height() / qMax(1, fontMetrics().lineSpacing());
        painter.fillRect(QRect(0, first * Row, rect.width(), qMin(visible, blockCount()) * Row),
                         QColor(121, 121, 121, 38));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(204, 204, 204, 105));
        int row = 0;
        for (QTextBlock block = document()->begin(); block.isValid() && row * Row < rect.height();
             block = block.next(), ++row) {
            const QString text = block.text();
            int start = -1;
            for (int i = 0; i <= text.size(); ++i) {
                const bool ink = i < text.size() && !text.at(i).isSpace();
                if (ink && start < 0) {
                    start = i;
                } else if (!ink && start >= 0) {
                    painter.drawRect(QRectF(8 + start * Column, row * Row, (i - start) * Column, 2));
                    start = -1;
                }
            }
        }
    }

    Strip *m_gutter;
    Strip *m_minimap;
};

/// An editor under the path of its file, as one panel's content.
inline QWidget *makeEditorPane(const File &file, CodeEditor **editor = nullptr)
{
    auto *pane = new QWidget;
    auto *crumbs = new QLabel(QString(file.path).replace(u'/', u"  \u203a  "_s), pane);
    crumbs->setObjectName(u"breadcrumbs"_s);
    auto *edit = new CodeEditor(file.path, file.text, pane);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(crumbs);
    layout->addWidget(edit, 1);
    pane->setFocusProxy(edit);
    if (editor)
        *editor = edit;
    return pane;
}

/// What is behind the editors, and shows once the last one is closed.
class EditorArea : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(Editor));
        painter.setRenderHint(QPainter::Antialiasing);
        const QPoint middle = rect().center();
        painter.save();
        painter.translate(middle.x() - 56, middle.y() - 130);
        painter.scale(7, 7);
        drawGlyph(painter, Glyph::Logo, QColor(0xff2b2b2b));
        painter.restore();
        painter.setPen(QColor(TextMuted));
        const QStringList hints{u"Go to File      Ctrl+P"_s, u"Show All Commands      Ctrl+Shift+P"_s,
                                u"Toggle Side Bar      Ctrl+B"_s, u"Toggle Panel      Ctrl+J"_s};
        for (int i = 0; i < hints.size(); ++i) {
            painter.drawText(QRect(0, middle.y() + 10 + i * 26, width(), 26), Qt::AlignHCenter,
                             hints.at(i));
        }
    }
};

// --- Side bar views ------------------------------------------------------------------

inline QLabel *sectionHeader(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(u"sectionHeader"_s);
    return label;
}

inline QWidget *column(std::initializer_list<QWidget *> widgets, int stretchLast = 1,
                       const QMargins &margins = {})
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(margins);
    layout->setSpacing(6);
    for (QWidget *widget : widgets) {
        widget->setParent(page);
        layout->addWidget(widget, widget == *(widgets.end() - 1) ? stretchLast : 0);
    }
    if (stretchLast == 0)
        layout->addStretch(1);
    return page;
}

inline QLineEdit *viewInput(const QString &placeholder)
{
    auto *input = new QLineEdit;
    input->setObjectName(u"viewInput"_s);
    input->setPlaceholderText(placeholder);
    return input;
}

inline QListWidget *plainList(const QStringList &rows, const QList<QIcon> &icons = {})
{
    auto *list = new QListWidget;
    for (int i = 0; i < rows.size(); ++i) {
        auto *item = new QListWidgetItem(icons.value(i), rows.at(i), list);
        item->setSizeHint(QSize(0, 22));
    }
    return list;
}

/// The tree of the project's files. `open` is called with a file's path, and
/// whether it is only being looked at (one click) or opened to stay (two).
inline QWidget *makeExplorer(const std::function<void(const QString &, bool)> &open)
{
    auto *tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    tree->setIndentation(12);
    tree->setIconSize(QSize(16, 16));
    tree->setRootIsDecorated(true);
    tree->setExpandsOnDoubleClick(false);

    QHash<QString, QTreeWidgetItem *> folders;
    const auto folder = [&](const QString &path) {
        QTreeWidgetItem *parent = nullptr;
        QString soFar;
        for (const QString &part : path.split(u'/', Qt::SkipEmptyParts)) {
            soFar += u'/' + part;
            QTreeWidgetItem *&item = folders[soFar];
            if (!item) {
                item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
                item->setText(0, part);
                item->setSizeHint(0, QSize(0, 22));
                item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            }
            parent = item;
        }
        return parent;
    };
    for (const char *path : {"docs", "examples", "include/Docking", "src/core", "src/widgets", "tests"})
        folder(QString::fromLatin1(path));
    for (const File &file : projectFiles()) {
        const qsizetype slash = file.path.lastIndexOf(u'/');
        QTreeWidgetItem *parent = slash < 0 ? nullptr : folder(file.path.left(slash));
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        item->setText(0, file.path.mid(slash + 1));
        item->setIcon(0, fileIcon(file.path));
        item->setData(0, Qt::UserRole, file.path);
        item->setSizeHint(0, QSize(0, 22));
    }
    folders.value(u"/src"_s)->setExpanded(true);
    folders.value(u"/src/core"_s)->setExpanded(true);

    QObject::connect(tree, &QTreeWidget::itemClicked, tree, [open](QTreeWidgetItem *item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty())
            item->setExpanded(!item->isExpanded());
        else
            open(path, true);
    });
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, tree, [open](QTreeWidgetItem *item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty())
            open(path, false);
    });
    return column({sectionHeader(u"DOCKING"_s, nullptr), tree});
}

inline QWidget *makeSearch()
{
    auto *hint = new QLabel(u"Nothing searched for yet."_s);
    hint->setObjectName(u"hint"_s);
    hint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return column({viewInput(u"Search"_s), viewInput(u"Replace"_s), hint}, 1, QMargins(12, 4, 12, 8));
}

inline QWidget *makeSourceControl()
{
    auto *commit = new QPushButton(u"\u2713  Commit"_s);
    commit->setObjectName(u"primary"_s);
    QStringList names;
    QList<QIcon> icons;
    for (const char *name : {"Layout.cpp", "Layout.h", "CMakeLists.txt"}) {
        names << QString::fromLatin1(name);
        icons << fileIcon(names.constLast());
    }
    QWidget *top = column({viewInput(u"Message (Ctrl+Enter to commit)"_s), commit}, 0,
                          QMargins(12, 4, 12, 4));
    top->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    return column({top, sectionHeader(u"CHANGES"_s, nullptr), plainList(names, icons)});
}

inline QWidget *makeRunAndDebug()
{
    auto *run = new QPushButton(u"Run and Debug"_s);
    run->setObjectName(u"primary"_s);
    auto *hint = new QLabel(u"Nothing is run from here: this view is only here to be shown, "
                            "put away and brought back."_s);
    hint->setObjectName(u"hint"_s);
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return column({run, hint}, 1, QMargins(12, 6, 12, 8));
}

inline QWidget *makeExtensions()
{
    QWidget *top = column({viewInput(u"Search Extensions"_s)}, 0, QMargins(12, 4, 12, 4));
    top->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    return column({top, sectionHeader(u"INSTALLED"_s, nullptr),
                   plainList({u"C++ Tools"_s, u"CMake Support"_s, u"Markdown Preview"_s})});
}

// --- Panel and secondary side bar views ------------------------------------------------

inline QWidget *makeMessage(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(u"hint"_s);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    label->setWordWrap(true);
    return column({label}, 1, QMargins(20, 6, 20, 8));
}

inline QWidget *makeConsole(const QString &text)
{
    auto *console = new QPlainTextEdit(text);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(13);
    console->setFont(font);
    console->setFrameShape(QFrame::NoFrame);
    console->setLineWrapMode(QPlainTextEdit::NoWrap);
    console->document()->setDocumentMargin(0);
    return column({console}, 1, QMargins(20, 4, 0, 0));
}

inline QWidget *makeChat()
{
    auto *mark = new QLabel;
    mark->setPixmap(pixmap(Glyph::Sparkle, IconMuted, 40));
    mark->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    auto *title = new QLabel(u"Ask about your code"_s);
    title->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->addWidget(mark, 1);
    layout->addWidget(title, 1);
    layout->addWidget(viewInput(u"Ask a question\u2026"_s));
    return page;
}

// --- The bars along the window's sides ---------------------------------------------------

/// The column of icons at the far left: one for each view of the side bar.
class ActivityBar : public QWidget
{
public:
    explicit ActivityBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(u"activityBar"_s);
        setAttribute(Qt::WA_StyledBackground);
        setFixedWidth(49);
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(0, 2, 1, 4);
        m_layout->setSpacing(0);
        m_layout->addStretch(1);
    }

    /// Above the gap when `view`, below it otherwise.
    QToolButton *addButton(Glyph glyph, const QString &toolTip, bool view)
    {
        auto *button = new QToolButton(this);
        button->setIcon(icon(glyph, IconMuted, glyph, 0xffd7d7d7, 24));
        button->setIconSize(QSize(24, 24));
        button->setCheckable(view);
        button->setToolTip(toolTip);
        button->setFocusPolicy(Qt::NoFocus);
        m_layout->insertWidget(view ? m_views++ : m_layout->count(), button);
        return button;
    }

private:
    QVBoxLayout *m_layout;
    int m_views = 0;
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
        setFixedHeight(23);
        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(0, 1, 8, 0);
        m_layout->setSpacing(0);
        m_layout->addStretch(1);
    }

    QToolButton *add(const QString &text, bool right, const QIcon &icon = {})
    {
        auto *item = new QToolButton(this);
        item->setText(text);
        item->setIcon(icon);
        item->setIconSize(QSize(14, 14));
        item->setToolButtonStyle(icon.isNull() ? Qt::ToolButtonTextOnly
                                               : text.isEmpty() ? Qt::ToolButtonIconOnly
                                                                : Qt::ToolButtonTextBesideIcon);
        item->setFocusPolicy(Qt::NoFocus);
        m_layout->insertWidget(right ? m_layout->count() : m_left++, item);
        return item;
    }

private:
    QHBoxLayout *m_layout;
    int m_left = 0;
};

} // namespace VsStyle
