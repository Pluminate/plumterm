#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include "CoreBridge.h"

// Exercises the C ABI through CoreBridge (ownership, JSON round-trips, null handling).
class CoreBridgeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        qputenv("TP_DATA_DIR", m_dir.path().toUtf8());
    }
    void version() { QVERIFY(!CoreBridge::version().isEmpty()); }
    void settingsDefaultsAndMerge() {
        QCOMPARE(CoreBridge::settings().toObject()["runShortcut"].toString(), QString("CommandOrControl+S"));
        QVERIFY(CoreBridge::settings().toObject()["confirmOnQuit"].toBool());
        QVERIFY(CoreBridge::setSettings(R"({"confirmOnQuit":false})"));
        QVERIFY(!CoreBridge::settings().toObject()["confirmOnQuit"].toBool());
        QVERIFY(!CoreBridge::setSettings("[1]"));
    }
    void sessionRoundTrip() {
        QVERIFY(CoreBridge::loadSession().isEmpty());
        QVERIFY(CoreBridge::saveSession(R"([{"title":"é tab"}])"));
        QVERIFY(CoreBridge::loadSession().contains("é tab"));
        QVERIFY(!CoreBridge::saveSession("{bad"));
    }
    void safety() {
        QVERIFY(CoreBridge::verifyCommand("ls -la"));
        QVERIFY(!CoreBridge::verifyCommand("   "));
        QVERIFY(CoreBridge::isDangerous("git status"));
    }
    void gitOutsideRepo() {
        QTemporaryDir d;
        QVERIFY(CoreBridge::git::status(d.path()).isNull());
        QVERIFY(CoreBridge::git::fileDiff(d.path(), "x").isNull());
        QVERIFY(CoreBridge::git::changedFiles(d.path()).toArray().isEmpty());
    }
private:
    QTemporaryDir m_dir;
};
QTEST_APPLESS_MAIN(CoreBridgeTest)
#include "corebridge_test.moc"
