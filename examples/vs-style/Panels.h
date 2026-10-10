// SPDX-License-Identifier: MIT
#pragma once

#include "Style.h"

#include <QtGui/QFontDatabase>
#include <QtGui/QTextBlock>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QTreeWidget>

// What is in the panes. All of it is for show: the look of a tool window or
// a document, with nothing behind it. None of them paints a background, so
// that the pane they are in shows through with its round corners.
namespace VisualStudio {

inline QToolButton *paneButton(QWidget *parent, Glyph glyph, const QString &toolTip,
                               QRgb rgb = Text)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(glyph, rgb));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(toolTip);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

/// A row of small buttons at the top of a tool window.
inline QHBoxLayout *paneBar(QWidget *parent, QVBoxLayout *into)
{
    auto *bar = new QWidget(parent);
    bar->setObjectName(u"paneBar"_s);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 0, 6, 2);
    layout->setSpacing(2);
    into->addWidget(bar);
    return layout;
}

/// Text with line numbers before it and a line of status below.
class TextEditor : public QWidget
{
public:
    explicit TextEditor(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_edit = new Edit(this);
        m_edit->setPlainText(text);
        m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(font.pointSizeF() + 1);
        m_edit->setFont(font);

        auto *status = new QWidget(this);
        status->setObjectName(u"editorStatus"_s);
        status->setAttribute(Qt::WA_StyledBackground);
        auto *row = new QHBoxLayout(status);
        row->setContentsMargins(6, 1, 6, 1);
        row->setSpacing(2);
        auto *zoom = new QToolButton(status);
        zoom->setText(u"100 %"_s);
        zoom->setFocusPolicy(Qt::NoFocus);
        row->addWidget(zoom);
        auto *state = new QLabel(status);
        state->setPixmap(pixmap(Glyph::Check, Green, 14));
        row->addWidget(state);
        row->addWidget(new QLabel(u"No issues found"_s, status));
        row->addStretch(1);
        m_position = new QLabel(status);
        row->addWidget(m_position);
        for (const QString &setting : {u"TABS"_s, u"CRLF"_s, u"UTF-8"_s})
            row->addWidget(new QLabel(setting, status));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 2, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_edit, 1);
        layout->addWidget(status);
        setFocusProxy(m_edit);

        connect(m_edit, &QPlainTextEdit::cursorPositionChanged, this, [this] { showPosition(); });
        showPosition();
    }

private:
    /// A plain text edit with a gutter: the number of each line, and a mark
    /// beside the one the cursor is in.
    class Edit : public QPlainTextEdit
    {
    public:
        explicit Edit(QWidget *parent)
            : QPlainTextEdit(parent)
        {
            m_gutter = new Gutter(this);
            setViewportMargins(GutterWidth, 0, 0, 0);
            connect(this, &QPlainTextEdit::updateRequest, m_gutter,
                    [this] { m_gutter->update(); });
            connect(this, &QPlainTextEdit::cursorPositionChanged, m_gutter,
                    [this] { m_gutter->update(); });
        }

    protected:
        void resizeEvent(QResizeEvent *event) override
        {
            QPlainTextEdit::resizeEvent(event);
            const QRect c = contentsRect();
            m_gutter->setGeometry(c.left(), c.top(), GutterWidth, c.height());
        }

    private:
        static constexpr int GutterWidth = 56;

        class Gutter : public QWidget
        {
        public:
            explicit Gutter(Edit *edit)
                : QWidget(edit)
                , m_edit(edit)
            {
            }

        protected:
            void paintEvent(QPaintEvent *) override { m_edit->paintGutter(this); }

        private:
            Edit *m_edit;
        };

        void paintGutter(QWidget *gutter)
        {
            QPainter painter(gutter);
            painter.setFont(font());
            const int current = textCursor().blockNumber();
            for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
                const QRectF box = blockBoundingGeometry(block).translated(contentOffset());
                if (box.top() > gutter->height())
                    break;
                if (!block.isVisible())
                    continue;
                if (block.blockNumber() == current)
                    painter.fillRect(QRectF(2, box.top(), 3, box.height()), QColor(Accent));
                painter.setPen(QColor(block.blockNumber() == current ? Text : IconMuted));
                painter.drawText(QRectF(0, box.top(), GutterWidth - 14, box.height()),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(block.blockNumber() + 1));
            }
        }

        Gutter *m_gutter;
    };

    void showPosition()
    {
        const QTextCursor cursor = m_edit->textCursor();
        m_position->setText(u"Ln: %1   Ch: %2"_s.arg(cursor.blockNumber() + 1)
                                                .arg(cursor.positionInBlock() + 1));
    }

    Edit *m_edit;
    QLabel *m_position;
};

/// The tree of a solution, under its buttons and a search box.
inline QWidget *makeSolutionExplorer(const QStringList &files)
{
    auto *view = new QWidget;
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(4);

    QHBoxLayout *bar = paneBar(view, layout);
    bar->addWidget(paneButton(view, Glyph::Sync, u"Switch Views"_s));
    bar->addWidget(paneButton(view, Glyph::Refresh, u"Refresh"_s));
    bar->addWidget(paneButton(view, Glyph::Collapse, u"Collapse All"_s, IconMuted));
    bar->addSpacing(6);
    bar->addWidget(paneButton(view, Glyph::Wrench, u"Properties"_s));
    QToolButton *preview = paneButton(view, Glyph::Split, u"Preview Selected Items"_s);
    preview->setCheckable(true);
    preview->setChecked(true);
    bar->addWidget(preview);
    bar->addStretch(1);
    bar->addWidget(paneButton(view, Glyph::Ellipsis, u"More"_s));

    auto *search = new QLineEdit(view);
    search->setObjectName(u"paneSearch"_s);
    search->setPlaceholderText(u"Search Solution Explorer (Ctrl+;)"_s);
    search->addAction(icon(Glyph::Search), QLineEdit::TrailingPosition);
    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(6, 0, 6, 0);
    searchRow->addWidget(search);
    layout->addLayout(searchRow);

    auto *tree = new QTreeWidget(view);
    tree->setHeaderHidden(true);
    tree->setIndentation(16);
    auto *solution = new QTreeWidgetItem(tree, {u"Solution 'Docking' (1 of 1 project)"_s});
    solution->setIcon(0, icon(Glyph::Solution, Accent));
    auto *project = new QTreeWidgetItem(solution, {u"Docking"_s});
    project->setIcon(0, icon(Glyph::Folder, Blue));
    for (const QString &name : {u"Dependencies"_s, u"Properties"_s})
        (new QTreeWidgetItem(project, {name}))->setIcon(0, icon(Glyph::Folder, TextMuted));
    for (const QString &file : files)
        (new QTreeWidgetItem(project, {file}))->setIcon(0, icon(Glyph::File, TextMuted));
    tree->expandAll();
    layout->addWidget(tree, 1);
    return view;
}

/// What a tool window says when there is no repository.
inline QWidget *makeGitChanges()
{
    auto *view = new QWidget;
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(10);
    const auto text = [view, layout](const QString &line) {
        auto *label = new QLabel(line, view);
        label->setWordWrap(true);
        layout->addWidget(label);
    };
    const auto button = [view, layout](Glyph glyph, QRgb rgb, const QString &title) {
        auto *b = new QPushButton(icon(glyph, rgb), title, view);
        b->setObjectName(u"paneButton"_s);
        b->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(b, 0, Qt::AlignLeft);
    };
    text(u"Git version control isn't being used yet."_s);
    text(u"Initialize, back up, and share your repository."_s);
    button(Glyph::Plus, Green, u"Create Git Repository..."_s);
    text(u"Get code from an online repository such as GitHub or Azure DevOps."_s);
    button(Glyph::Repository, Text, u"Clone Repository..."_s);
    text(u"Use the Git menu to open an existing local repository."_s);
    auto *link = new QLabel(u"<u>Learn about using Git in Visual Studio.</u>"_s, view);
    link->setObjectName(u"link"_s);
    layout->addWidget(link);
    layout->addStretch(1);
    return view;
}

/// A chat with nothing said in it yet.
inline QWidget *makeChat()
{
    auto *view = new QWidget;
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(6);

    QHBoxLayout *bar = paneBar(view, layout);
    bar->addWidget(new QLabel(u"New thread"_s, view));
    bar->addStretch(1);
    bar->addWidget(paneButton(view, Glyph::History, u"History"_s));
    bar->addWidget(paneButton(view, Glyph::NewChat, u"New Thread"_s));
    bar->addWidget(paneButton(view, Glyph::Ellipsis, u"More"_s));

    layout->addStretch(2);
    auto *picture = new QLabel(view);
    picture->setPixmap(pixmap(Glyph::Chat, TextMuted, 48));
    picture->setAlignment(Qt::AlignCenter);
    layout->addWidget(picture);
    auto *heading = new QLabel(u"Copilot Chat"_s, view);
    heading->setObjectName(u"heading"_s);
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    auto *hint = new QLabel(u"AI responses may be inaccurate; check them.\n"
                            "Use # for references, / for commands and @ for agents."_s, view);
    hint->setObjectName(u"hint"_s);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch(5);

    for (const QString &suggestion : {u"Create a new project"_s,
                                      u"Suggest ideas for projects I could build in C#."_s,
                                      u"What can you help me with in Visual Studio?"_s}) {
        auto *chip = new QPushButton(suggestion, view);
        chip->setObjectName(u"chip"_s);
        chip->setFocusPolicy(Qt::NoFocus);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(8, 0, 8, 0);
        row->addStretch(1);
        row->addWidget(chip);
        layout->addLayout(row);
    }

    auto *input = new QFrame(view);
    input->setObjectName(u"chatInput"_s);
    auto *inputLayout = new QVBoxLayout(input);
    inputLayout->setContentsMargins(6, 6, 6, 6);
    inputLayout->setSpacing(2);
    auto *attach = paneButton(input, Glyph::Plus, u"Add Context"_s, Green);
    inputLayout->addWidget(attach, 0, Qt::AlignLeft);
    auto *edit = new QPlainTextEdit(input);
    edit->setPlaceholderText(u"Ask Copilot"_s);
    edit->setFixedHeight(40);
    inputLayout->addWidget(edit);
    auto *bottom = new QHBoxLayout;
    auto *model = new QPushButton(u"Add a model"_s, input);
    model->setObjectName(u"chip"_s);
    model->setFocusPolicy(Qt::NoFocus);
    bottom->addWidget(model);
    bottom->addStretch(1);
    bottom->addWidget(paneButton(input, Glyph::Send, u"Send"_s, IconMuted));
    inputLayout->addLayout(bottom);
    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(8, 4, 8, 0);
    inputRow->addWidget(input);
    layout->addLayout(inputRow);
    view->setFocusProxy(edit);
    return view;
}

/// What a build wrote, under a box to choose whose output is shown.
inline QWidget *makeOutput()
{
    auto *view = new QWidget;
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(4);
    QHBoxLayout *bar = paneBar(view, layout);
    bar->addWidget(new QLabel(u"Show output from:"_s, view));
    auto *source = new QComboBox(view);
    source->setObjectName(u"paneCombo"_s);
    source->addItems({u"Build"_s, u"Debug"_s, u"Source Control - Git"_s});
    source->setMinimumWidth(220);
    bar->addWidget(source);
    bar->addStretch(1);
    auto *text = new QPlainTextEdit(view);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(u"Build started at 10:42...\n"
                       "1>------ Build started: Project: Docking, Configuration: Debug ------\n"
                       "1>Docking -> bin\\Debug\\Docking.dll\n"
                       "========== Build: 1 succeeded, 0 failed, 0 up-to-date, 0 skipped ==========\n"
                       "========== Build completed and took 01.4 seconds =========="_s);
    layout->addWidget(text, 1);
    view->setFocusProxy(text);
    return view;
}

/// A table of what is wrong with the code.
inline QWidget *makeErrorList()
{
    auto *view = new QWidget;
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(4);
    QHBoxLayout *bar = paneBar(view, layout);
    const auto count = [view, bar](Glyph glyph, QRgb rgb, const QString &text) {
        QToolButton *b = paneButton(view, glyph, text, rgb);
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        b->setChecked(true);
        bar->addWidget(b);
    };
    count(Glyph::Error, 0xfff48771, u"0 Errors"_s);
    count(Glyph::Warning, 0xffcca700, u"2 Warnings"_s);
    count(Glyph::Info, Blue, u"1 Message"_s);
    bar->addStretch(1);

    auto *table = new QTreeWidget(view);
    table->setRootIsDecorated(false);
    table->setHeaderLabels({u"Code"_s, u"Description"_s, u"Project"_s, u"File"_s, u"Line"_s});
    const auto row = [table](Glyph glyph, QRgb rgb, const QStringList &cells) {
        (new QTreeWidgetItem(table, cells))->setIcon(0, icon(glyph, rgb));
    };
    row(Glyph::Warning, 0xffcca700, {u"CS0168"_s, u"The variable 'area' is declared but never used"_s,
                                     u"Docking"_s, u"Program.cs"_s, u"12"_s});
    row(Glyph::Warning, 0xffcca700, {u"CS8618"_s, u"Non-nullable field 'panel' must contain a "
                                                  "non-null value when exiting constructor"_s,
                                     u"Docking"_s, u"Workspace.cs"_s, u"7"_s});
    row(Glyph::Info, Blue, {u"IDE0005"_s, u"Using directive is unnecessary"_s, u"Docking"_s,
                            u"Program.cs"_s, u"2"_s});
    table->header()->setStretchLastSection(false);
    table->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    return view;
}

} // namespace VisualStudio
