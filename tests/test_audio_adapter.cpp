// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_adapter.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

bool writeFile(const QString &path, const QByteArray &contents,
               QFile::Permissions permissions = QFile::ReadOwner |
                                                QFile::WriteOwner) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write(contents) != contents.size() || !file.flush())
    return false;
  file.close();
  return QFile::setPermissions(path, permissions);
}

class ScopedEnvironment final {
public:
  ScopedEnvironment() {
    const QList<QByteArray> names = {
        QByteArrayLiteral("HOME"), QByteArrayLiteral("XDG_CONFIG_HOME"),
        QByteArrayLiteral("SYNAPSE_PACTL"),
        QByteArrayLiteral("SYNAPSE_AUDIO_FIXTURES"),
        QByteArrayLiteral("SYNAPSE_AUDIO_ROUTE_POLICY")};
    for (const QByteArray &name : names) {
      names_.append(name);
      values_.append(qgetenv(name.constData()));
      present_.append(qEnvironmentVariableIsSet(name.constData()));
    }
  }

  ~ScopedEnvironment() {
    for (qsizetype index = 0; index < names_.size(); ++index) {
      if (present_.at(index))
        qputenv(names_.at(index).constData(), values_.at(index));
      else
        qunsetenv(names_.at(index).constData());
    }
  }

private:
  QList<QByteArray> names_;
  QList<QByteArray> values_;
  QList<bool> present_;
};

class AudioFixture final {
public:
  AudioFixture() {
    valid_ = directory_.isValid();
    if (!valid_)
      return;
    root_ = directory_.path();
    audio_ = root_ + QStringLiteral("/audio");
    bin_ = root_ + QStringLiteral("/bin");
    config_ = root_ + QStringLiteral("/config");
    applicationDirectory_ = root_ + QStringLiteral("/apps");
    valid_ = QDir().mkpath(audio_) && QDir().mkpath(bin_) &&
             QDir().mkpath(config_) && QDir().mkpath(applicationDirectory_);
    if (!valid_)
      return;
    executable_ = applicationDirectory_ + QStringLiteral("/player");
    valid_ = writeFile(executable_, QByteArrayLiteral("#!/bin/sh\nexit 0\n"),
                       QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (!valid_)
      return;

    const QByteArray pid =
        QByteArray::number(QCoreApplication::applicationPid());
    valid_ =
        writeFile(
            audio_ + QStringLiteral("/sinks.json"),
            QByteArrayLiteral(
                "[{\"index\":10,\"name\":\"sink.a\",\"description\":"
                "\"Integrated "
                "audio\",\"mute\":false,\"volume\":{\"left\":{\"value\":32768}}"
                "},{\"index\":11,\"name\":\"sink.b\",\"description\":\"USB "
                "headset\",\"mute\":false,\"volume\":{\"left\":{\"value\":"
                "65536}}}]\n")) &&
        writeFile(audio_ + QStringLiteral("/sources.json"),
                  QByteArrayLiteral(
                      "[{\"index\":20,\"name\":\"source.a\",\"description\":"
                      "\"Built-in microphone\",\"monitor_of_sink\":null,"
                      "\"monitor_source\":\"\",\"mute\":false,\"volume\":{"
                      "\"mono\":{\"value\":32768}}},{\"index\":21,\"name\":"
                      "\"source.b\",\"description\":\"USB microphone\","
                      "\"monitor_of_sink\":null,\"monitor_source\":\"\","
                      "\"mute\":false,\"volume\":{\"mono\":{\"value\":"
                      "65536}}},{\"index\":22,\"name\":\"sink.a.monitor\","
                      "\"description\":\"Monitor\",\"monitor_of_sink\":null,"
                      "\"monitor_source\":\"sink.a\",\"mute\":false,"
                      "\"volume\":{\"mono\":{\"value\":65536}}}]\n")) &&
        writeFile(
            audio_ + QStringLiteral("/sink-inputs.json"),
            QByteArrayLiteral(
                "[{\"index\":30,\"sink\":11,\"mute\":false,\"volume\":{"
                "\"left\":{\"value\":49152}},\"properties\":{\"application."
                "name\":\"Game\",\"application.process.id\":\"") +
                pid + QByteArrayLiteral("\"}}]\n")) &&
        writeFile(audio_ + QStringLiteral("/source-outputs.json"),
                  QByteArrayLiteral("[]\n")) &&
        writeFile(
            audio_ + QStringLiteral("/cards.json"),
            QByteArrayLiteral(
                "[{\"index\":40,\"name\":\"card.a\",\"description\":\"Primary "
                "audio card\",\"active_profile\":\"HiFi\"}]\n")) &&
        writeFile(audio_ + QStringLiteral("/default-sink"),
                  QByteArrayLiteral("sink.a\n")) &&
        writeFile(audio_ + QStringLiteral("/default-source"),
                  QByteArrayLiteral("source.a\n"));
    if (!valid_)
      return;

    pactl_ = bin_ + QStringLiteral("/pactl-fake");
    const QByteArray script = QByteArrayLiteral(
        "#!/bin/sh\n"
        "set -eu\n"
        "case \"${1-} ${2-} ${3-}\" in\n"
        "  '--format=json info ') printf "
        "'{\"default_sink_name\":\"%s\",\"default_source_name\":\"%s\"}\\n' "
        "\"$(cat \"$SYNAPSE_AUDIO_FIXTURES/default-sink\")\" \"$(cat "
        "\"$SYNAPSE_AUDIO_FIXTURES/default-source\")\" ;;\n"
        "  '--format=json list sinks') cat "
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.json\" ;;\n"
        "  '--format=json list sources') cat "
        "\"$SYNAPSE_AUDIO_FIXTURES/sources.json\" ;;\n"
        "  '--format=json list sink-inputs') cat "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json\" ;;\n"
        "  '--format=json list source-outputs') cat "
        "\"$SYNAPSE_AUDIO_FIXTURES/source-outputs.json\" ;;\n"
        "  '--format=json list cards') cat "
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.json\" ;;\n"
        "  'set-default-sink sink.b ') printf 'sink.b\\n' "
        ">\"$SYNAPSE_AUDIO_FIXTURES/default-sink\" ;;\n"
        "  'set-default-source source.b ') printf 'source.b\\n' "
        ">\"$SYNAPSE_AUDIO_FIXTURES/default-source\" ;;\n"
        "  *) exit 64 ;;\n"
        "esac\n");
    valid_ = writeFile(pactl_, script,
                       QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    policy_ = config_ + QStringLiteral("/audio-route-policy-v1.json");
  }

  bool valid() const { return valid_; }
  QString executable() const { return executable_; }
  QString applicationDirectory() const { return applicationDirectory_; }
  QString policy() const { return policy_; }

  void activate() const {
    qputenv("HOME", root_.toUtf8());
    qputenv("XDG_CONFIG_HOME", config_.toUtf8());
    qputenv("SYNAPSE_PACTL", pactl_.toUtf8());
    qputenv("SYNAPSE_AUDIO_FIXTURES", audio_.toUtf8());
    qputenv("SYNAPSE_AUDIO_ROUTE_POLICY", policy_.toUtf8());
  }

private:
  QTemporaryDir directory_;
  QString root_;
  QString audio_;
  QString bin_;
  QString config_;
  QString executable_;
  QString applicationDirectory_;
  QString pactl_;
  QString policy_;
  bool valid_ = false;
};

QString testBackend() {
  return QString::fromUtf8(qgetenv("SYNAPSE_SETTINGS_TEST_BACKEND"));
}

QByteArray unavailableInventory() {
  return QByteArrayLiteral(
      "{\"schema\":\"synapse.settings.audio-inventory/"
      "v1\",\"stateAuthority\":\"pipewire-pulse-model\",\"available\":false,"
      "\"reason\":\"unavailable\",\"mutationAvailable\":false,\"outputs\":[],"
      "\"inputs\":[],\"streams\":[],\"cards\":[],\"bounded\":true}\n");
}

} // namespace

class AudioAdapterTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QVERIFY2(QFileInfo(testBackend()).isExecutable(),
             "SYNAPSE_SETTINGS_TEST_BACKEND must name the fixture binary");
  }

  void contractDecodersRejectMalformedInput() {
    AudioPresentationSnapshot snapshot;
    QString error;
    QVERIFY(AudioContracts::decodeInventory(unavailableInventory(), &snapshot,
                                            &error));
    QVERIFY(!snapshot.available);

    QByteArray unknown = unavailableInventory();
    unknown.replace("\"bounded\":true", "\"unknown\":0,\"bounded\":true");
    QVERIFY(!AudioContracts::decodeInventory(unknown, &snapshot, &error));
    QCOMPARE(error, QStringLiteral("contract-invalid"));

    QByteArray duplicateStream = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-inventory/"
        "v1\",\"stateAuthority\":\"pipewire-pulse-model\",\"available\":true,"
        "\"reason\":null,\"mutationAvailable\":true,\"outputs\":[{\"id\":"
        "\"output-0123456789abcdef\",\"label\":\"Output\",\"default\":true,"
        "\"volumePercent\":50,\"muted\":false}],\"inputs\":[],\"streams\":[{"
        "\"id\":\"playback-1\",\"direction\":\"playback\",\"label\":\"One\","
        "\"target\":\"output-0123456789abcdef\",\"volumePercent\":50,\"muted\":"
        "false,\"processRuleAvailable\":true},{\"id\":\"playback-1\","
        "\"direction\":\"playback\",\"label\":\"Two\",\"target\":\"output-"
        "0123456789abcdef\",\"volumePercent\":50,\"muted\":false,"
        "\"processRuleAvailable\":true}],\"cards\":[],\"bounded\":true}\n");
    QVERIFY(
        !AudioContracts::decodeInventory(duplicateStream, &snapshot, &error));

    QByteArray oversized(kMaximumInventoryForTest(), 'x');
    QVERIFY(!AudioContracts::decodeInventory(oversized, &snapshot, &error));

    snapshot = AudioPresentationSnapshot();
    QVERIFY(AudioContracts::decodeInventory(unavailableInventory(), &snapshot,
                                            &error));
    const QByteArray falsePolicy = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-route-policy-view/"
        "v1\",\"generation\":0,\"rules\":[],\"present\":false,"
        "\"systemDefaultFallback\":true,\"enforcementAvailable\":true,"
        "\"enforcementReason\":\"audio-route-broker-not-integrated\","
        "\"persistentPidRules\":false,\"existingStreamMigration\":false}\n");
    QVERIFY(!AudioContracts::decodePolicy(falsePolicy, &snapshot, &error));

    bool changed = false;
    QString rule;
    const QByteArray falseReceipt = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-route-policy-receipt/"
        "v1\",\"status\":\"Applied\",\"action\":\"set-rule\",\"generation\":1,"
        "\"changed\":true,\"device\":\"output-0123456789abcdef\",\"rule\":"
        "\"rule-0001\",\"policyApplied\":true,\"routingApplied\":true,"
        "\"enforcementAvailable\":false,\"enforcementReason\":\"audio-route-"
        "broker-not-integrated\"}\n");
    QVERIFY(!AudioContracts::decodeRouteReceipt(
        falseReceipt, QStringLiteral("set-rule"),
        QStringLiteral("output-0123456789abcdef"), QString(), &rule, &changed,
        &error));
  }

  void loadAndGuardedMutations() {
    ScopedEnvironment restore;
    AudioFixture fixture;
    QVERIFY(fixture.valid());
    fixture.activate();
    AudioAdapter adapter(
        testBackend(),
        [&fixture](const QString &matchType) {
          return matchType == QStringLiteral("directory")
                     ? fixture.applicationDirectory()
                     : fixture.executable();
        },
        5000);

    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QCOMPARE(loaded.constFirst().constFirst().toBool(), true);
    QVERIFY(adapter.audioAvailable());
    QCOMPARE(adapter.audioOutputs().size(), 2);
    QCOMPARE(adapter.audioInputs().size(), 2);
    QCOMPARE(adapter.audioStreams().size(), 1);
    QCOMPARE(adapter.audioCards().size(), 1);
    QVERIFY(!adapter.audioRouteEnforcementAvailable());

    const QString output = adapter.audioOutputs()
                               .at(1)
                               .toMap()
                               .value(QStringLiteral("id"))
                               .toString();
    const QString input = adapter.audioInputs()
                              .at(1)
                              .toMap()
                              .value(QStringLiteral("id"))
                              .toString();
    QSignalSpy operations(&adapter, &AudioAdapter::audioOperationFinished);
    QVERIFY(adapter.setAudioDefault(QStringLiteral("output"), output));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioStatusId(),
                              QStringLiteral("audio-default-applied"), 15000);
    QCOMPARE(adapter.audioOutputs()
                 .at(1)
                 .toMap()
                 .value(QStringLiteral("default"))
                 .toBool(),
             true);

    QSignalSpy processChoice(&adapter,
                             &AudioAdapter::audioProcessChoiceRequested);
    QVERIFY(adapter.chooseAudioProcessRule(QStringLiteral("output"), output));
    QCOMPARE(processChoice.size(), 1);
    QVERIFY(adapter.audioProcessChoiceOpen());
    QCOMPARE(adapter.audioProcessChoices().size(), 1);
    const QString stream = adapter.audioProcessChoices()
                               .constFirst()
                               .toMap()
                               .value(QStringLiteral("id"))
                               .toString();
    QVERIFY(adapter.confirmAudioProcessRule(stream));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioStatusId(),
                              QStringLiteral("audio-route-rule-saved"), 15000);
    QCOMPARE(adapter.audioRouteRules().size(), 1);

    QVERIFY(
        adapter.chooseAudioExecutableRule(QStringLiteral("output"), output));
    QTRY_VERIFY_WITH_TIMEOUT(!adapter.audioBusy(), 15000);
    QCOMPARE(adapter.audioRouteRules().size(), 2);
    QVERIFY(adapter.chooseAudioDirectoryRule(QStringLiteral("input"), input));
    QTRY_VERIFY_WITH_TIMEOUT(!adapter.audioBusy(), 15000);
    QCOMPARE(adapter.audioRouteRules().size(), 3);

    const QString removeRule = adapter.audioRouteRules()
                                   .constFirst()
                                   .toMap()
                                   .value(QStringLiteral("id"))
                                   .toString();
    QVERIFY(adapter.removeAudioRouteRule(removeRule));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioStatusId(),
                              QStringLiteral("audio-route-rule-removed"),
                              15000);
    QCOMPARE(adapter.audioRouteRules().size(), 2);
    QVERIFY(operations.size() >= 4);

    const QFileInfo policy(fixture.policy());
    QVERIFY(policy.isFile());
    QCOMPARE(policy.permissions() &
                 (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                  QFile::ReadOther | QFile::WriteOther | QFile::ExeOther),
             QFile::Permissions());
    QFile policyFile(fixture.policy());
    QVERIFY(policyFile.open(QIODevice::ReadOnly));
    const QJsonDocument policyDocument =
        QJsonDocument::fromJson(policyFile.readAll());
    QVERIFY(policyDocument.isObject());
    const QByteArray serialized = policyDocument.toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains("\"pid\""));
    QVERIFY(!serialized.contains("processName"));
  }

  void invalidSelectionAndConcurrentLoadFailClosed() {
    ScopedEnvironment restore;
    AudioFixture fixture;
    QVERIFY(fixture.valid());
    fixture.activate();
    AudioAdapter adapter(
        testBackend(), [](const QString &) { return QString(); }, 5000);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QVERIFY(!adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    const QString input = adapter.audioInputs()
                              .constFirst()
                              .toMap()
                              .value(QStringLiteral("id"))
                              .toString();
    QVERIFY(!adapter.chooseAudioProcessRule(QStringLiteral("input"), input));
    QCOMPARE(adapter.audioErrorId(),
             QStringLiteral("audio-process-unavailable"));
    QVERIFY(!adapter.setAudioDefault(
        QStringLiteral("output"), QStringLiteral("output-0000000000000000")));
    QCOMPARE(adapter.audioErrorId(), QStringLiteral("selection-invalid"));
    QVERIFY(!adapter.removeAudioRouteRule(QStringLiteral("rule-9999")));
  }

  void commandOutputIsBounded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString backend =
        directory.filePath(QStringLiteral("oversized-backend"));
    QVERIFY(writeFile(backend,
                      QByteArrayLiteral("#!/bin/sh\ndd if=/dev/zero bs=1100000 "
                                        "count=1 2>/dev/null | tr '\\000' x\n"),
                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    AudioAdapter adapter(
        backend, [](const QString &) { return QString(); }, 3000);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    QCOMPARE(loaded.constFirst().constFirst().toBool(), false);
    QCOMPARE(adapter.audioErrorId(), QStringLiteral("output-too-large"));
    QVERIFY(!adapter.audioBusy());
  }

  void commandTimeoutIsBounded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString backend = directory.filePath(QStringLiteral("slow-backend"));
    QVERIFY(writeFile(backend, QByteArrayLiteral("#!/bin/sh\nsleep 5\n"),
                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    AudioAdapter adapter(
        backend, [](const QString &) { return QString(); }, 100);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 3000);
    QCOMPARE(loaded.constFirst().constFirst().toBool(), false);
    QCOMPARE(adapter.audioErrorId(), QStringLiteral("timeout"));
    QVERIFY(!adapter.audioBusy());
  }

private:
  static qsizetype kMaximumInventoryForTest() {
    return qsizetype{1024} * 1024 + 1;
  }
};

QTEST_MAIN(AudioAdapterTest)
#include "test_audio_adapter.moc"
