// SPDX-License-Identifier: MIT
#include <QFlexDock/Global.h>

#include "core/LayoutModel.h"

#include <QtTest/QtTest>

using namespace QFlexDock;

class tst_Smoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void version()
    {
        QCOMPARE(versionString(), QStringLiteral("0.1.0"));
    }

    void nodeIdsAreUnique()
    {
        const NodeId a = NodeId::create();
        const NodeId b = NodeId::create();
        QVERIFY(!a.isNull());
        QVERIFY(a != b);
        QVERIFY(NodeId{}.isNull());
    }

    void resultConvertsToBool()
    {
        QVERIFY(DockResult::success());
        const DockResult failed = DockResult::failure(DockError::UnknownPanel, QStringLiteral("x"));
        QVERIFY(!failed);
        QCOMPARE(failed.error(), DockError::UnknownPanel);
    }
};

QTEST_APPLESS_MAIN(tst_Smoke)
#include "tst_smoke.moc"
