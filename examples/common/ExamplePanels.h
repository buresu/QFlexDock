// SPDX-License-Identifier: MIT
#pragma once

#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QTreeWidget>

// Small stand-in contents shared by the examples.
namespace ExamplePanels {

inline QWidget *makeViewport(const QString &text)
{
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumSize(160, 120);
    label->setFrameShape(QFrame::StyledPanel);
    return label;
}

inline QWidget *makeTree()
{
    auto *tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    auto *root = new QTreeWidgetItem(tree, {QStringLiteral("Scene")});
    for (const char *name : {"Camera", "Light", "Mesh", "Empty"})
        new QTreeWidgetItem(root, {QString::fromLatin1(name)});
    tree->expandAll();
    tree->setMinimumWidth(120);
    return tree;
}

inline QWidget *makeList(int count)
{
    auto *list = new QListWidget;
    for (int i = 1; i <= count; ++i)
        list->addItem(QStringLiteral("Item %1").arg(i));
    return list;
}

inline QWidget *makeLog()
{
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    // The panel around it is frame enough. (The Windows 11 style would draw
    // the frame of a text box: a line along the bottom that lights up.)
    log->setFrameShape(QFrame::NoFrame);
    log->setPlainText(QStringLiteral("Ready.\nDrag a tab to rearrange the layout."));
    log->setMinimumHeight(60);
    return log;
}

} // namespace ExamplePanels
