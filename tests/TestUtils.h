// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/DockManager.h>
#include <QFlexDock/DockPanel.h>
#include <QFlexDock/DockWorkspace.h>

#include "core/DockManager_p.h"
#include "widgets/DockAreaWidget.h"
#include "widgets/DockTabGroup.h"

#include <QtCore/QDir>
#include <QtTest/QtTest>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>

namespace TestUtils {

using namespace QFlexDock;

inline QString p(const char *s)
{
    return QString::fromLatin1(s);
}

/// Compact structure of a tree, e.g. "H(a|b, V(c, d))". The active tab of a
/// group is not shown.
inline QString describe(const LayoutNode &node)
{
    if (node.isTabs())
        return node.panels.join(QLatin1Char('|'));
    QStringList parts;
    for (const auto &child : node.children)
        parts << describe(child);
    return QLatin1String(node.orientation == Qt::Horizontal ? "H(" : "V(")
        + parts.join(QLatin1String(", ")) + QLatin1Char(')');
}

inline QString describe(const LayoutTree &tree)
{
    return tree.root() ? describe(*tree.root()) : QStringLiteral("<empty>");
}

inline QString describe(const DockWorkspace *workspace)
{
    return describe(workspace->layoutTree());
}

inline DockManagerPrivate *priv(DockManager &manager)
{
    return DockManagerPrivate::get(&manager);
}

inline DockAreaWidget *areaOf(DockWorkspace *workspace)
{
    return DockManagerPrivate::get(workspace)->area;
}

/// Saves a screenshot of `widget` when QFLEXDOCK_TEST_GRABS names a directory.
/// For looking at what the tests see; never part of an assertion.
inline void grab(QWidget *widget, const QString &name)
{
    const QString dir = qEnvironmentVariable("QFLEXDOCK_TEST_GRABS");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    widget->grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

/// Whether a client can place its windows and learn where they are. Wayland
/// leaves both to the compositor: geometry().topLeft() is not meaningful there
/// and positions are neither saved usefully nor restored.
inline bool windowPositionsWork()
{
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

/// Two main windows with a workspace each, sharing one manager, plus `count`
/// registered label panels named a, b, c...
struct TwoWindows
{
    explicit TwoWindows(int count = 6)
    {
        a = manager.createWorkspace(QStringLiteral("A"));
        b = manager.createWorkspace(QStringLiteral("B"));
        windowA.setCentralWidget(a);
        windowB.setCentralWidget(b);
        for (int i = 0; i < count; ++i) {
            const QString id(QChar(QLatin1Char(char('a' + i))));
            auto *label = new QLabel(id.toUpper());
            label->setAlignment(Qt::AlignCenter);
            label->setFocusPolicy(Qt::StrongFocus);
            widgets.insert(id, label);
            manager.registerPanel(id, label);
        }
        windowA.resize(900, 600);
        windowB.resize(700, 500);
    }

    void show()
    {
        windowA.show();
        windowB.show();
        QVERIFY(QTest::qWaitForWindowExposed(&windowA));
        QVERIFY(QTest::qWaitForWindowExposed(&windowB));
    }

    // The manager is declared first, so it is destroyed last.
    DockManager manager;
    QMainWindow windowA;
    QMainWindow windowB;
    DockWorkspace *a = nullptr;
    DockWorkspace *b = nullptr;
    QHash<QString, QWidget *> widgets;
};

} // namespace TestUtils
