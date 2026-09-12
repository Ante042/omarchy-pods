#include <QTest>
#include <QFile>
#include <QTemporaryDir>
#include "../media/conversationvolume.hpp"
#include "../airpods_packets.h"

class TestConversationalAwareness : public QObject
{
    Q_OBJECT

private slots:
    void softwareFadeCanReverseAndReset()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray previousPath = qgetenv("PATH");
        const QByteArray previousLog = qgetenv("CONVERSATION_VOLUME_TEST_LOG");
        struct RestoreEnvironment {
            QByteArray path, log;
            ~RestoreEnvironment() {
                qputenv("PATH", path);
                if (log.isNull()) qunsetenv("CONVERSATION_VOLUME_TEST_LOG");
                else qputenv("CONVERSATION_VOLUME_TEST_LOG", log);
            }
        } restore{previousPath, previousLog};
        const QString logPath = directory.filePath("commands");
        QFile executable(directory.filePath("pw-cli"));
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$CONVERSATION_VOLUME_TEST_LOG\"\n");
        executable.close();
        QVERIFY(executable.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("PATH", directory.path().toUtf8() + ':' + previousPath);
        qputenv("CONVERSATION_VOLUME_TEST_LOG", logPath.toUtf8());
        auto commands = [&]() {
            QFile log(logPath);
            if (!log.open(QIODevice::ReadOnly)) return QByteArray();
            return log.readAll();
        };
        auto lastGain = [&]() {
            const auto lines = commands().trimmed().split('\n');
            const auto fields = lines.last().split(' ');
            return fields.size() >= 6 ? fields[5].toDouble() : -1.0;
        };

        ConversationVolume volume;
        const QString sink = QStringLiteral("bluez_output.test.1");
        volume.setSpeaking(true, sink);
        volume.setSpeaking(false, sink);
        QTest::qWait(80);
        QVERIFY(commands().isEmpty());

        volume.setSpeaking(true, sink);
        QTest::qWait(250);
        QVERIFY(lastGain() > 0.008 && lastGain() < 1.0);
        volume.setSpeaking(true, sink);
        QTRY_COMPARE_WITH_TIMEOUT(lastGain(), 0.008, 1200);
        QVERIFY(commands().count('\n') > 10);
        QVERIFY(!commands().contains("volumeRamp"));

        volume.setSpeaking(false, sink);
        QTest::qWait(150);
        const double risingGain = lastGain();
        QVERIFY(risingGain > 0.008 && risingGain < 1.0);
        volume.setSpeaking(true, sink);
        QTest::qWait(100);
        QVERIFY(lastGain() < risingGain);
        volume.reset();
        QCOMPARE(lastGain(), 1.0);
        const QByteArray afterReset = commands();
        QTest::qWait(700);
        QCOMPARE(commands(), afterReset);

        volume.setSpeaking(true, sink);
        QTest::qWait(100);
        QVERIFY(lastGain() < 1.0);
        const qsizetype beforeReplacement = commands().size();
        volume.setSpeaking(true, QStringLiteral("bluez_output.replacement.1"));
        QVERIFY(commands().mid(beforeReplacement).startsWith("set-param bluez_output.test.1 Props { volume: 1.000000 }\n"));
        volume.reset();
        QCOMPARE(lastGain(), 1.0);
    }

    void conversationKeepsVolumeLoweredUntilEnd()
    {
        // The 01 -> 02 speech sequence must not restore volume on its second packet.
        for (int level : {0x01, 0x02}) {
            const auto speaking = AirPodsPackets::ConversationalAwareness::parseSpeaking(
                AirPodsPackets::ConversationalAwareness::DATA_HEADER + static_cast<char>(level));
            QVERIFY(speaking.has_value());
            QVERIFY(speaking.value());
        }
        for (int level : {0x03, 0x04, 0x05, 0x07, 0x00, 0xff}) {
            QVERIFY(!AirPodsPackets::ConversationalAwareness::parseSpeaking(
                AirPodsPackets::ConversationalAwareness::DATA_HEADER + static_cast<char>(level)).has_value());
        }
        for (int level : {0x06, 0x08, 0x09}) {
            const auto speaking = AirPodsPackets::ConversationalAwareness::parseSpeaking(
                AirPodsPackets::ConversationalAwareness::DATA_HEADER + static_cast<char>(level));
            QVERIFY(speaking.has_value());
            QVERIFY(!speaking.value());
        }
    }

    void rejectsMalformedPackets()
    {
        const QByteArray packet = QByteArray::fromHex("040004004b0002000102");
        for (int size = 0; size < packet.size(); ++size)
            QVERIFY(!AirPodsPackets::ConversationalAwareness::parseSpeaking(packet.left(size)).has_value());
        QVERIFY(!AirPodsPackets::ConversationalAwareness::parseSpeaking(packet + '\0').has_value());
        QByteArray wrongHeader = packet;
        wrongHeader[4] = 0x09;
        QVERIFY(!AirPodsPackets::ConversationalAwareness::parseSpeaking(wrongHeader).has_value());
    }
};

QTEST_GUILESS_MAIN(TestConversationalAwareness)
#include "tst_conversationalawareness.moc"
