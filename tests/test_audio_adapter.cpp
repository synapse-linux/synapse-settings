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
        QByteArrayLiteral("HOME"),
        QByteArrayLiteral("XDG_CONFIG_HOME"),
        QByteArrayLiteral("XDG_RUNTIME_DIR"),
        QByteArrayLiteral("SYNAPSE_PACTL"),
        QByteArrayLiteral("SYNAPSE_AUDIO_FIXTURES"),
        QByteArrayLiteral("SYNAPSE_AUDIO_ROUTE_POLICY"),
        QByteArrayLiteral("SYNAPSE_AUDIO_MOVE_LOG"),
        QByteArrayLiteral("SYNAPSE_AUDIO_MOVE_MODE")};
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
    runtime_ = root_ + QStringLiteral("/runtime");
    applicationDirectory_ = root_ + QStringLiteral("/apps");
    valid_ =
        QDir().mkpath(audio_) && QDir().mkpath(bin_) &&
        QDir().mkpath(config_) && QDir().mkpath(runtime_) &&
        QDir().mkpath(applicationDirectory_) &&
        QFile::setPermissions(runtime_, QFile::ReadOwner | QFile::WriteOwner |
                                            QFile::ExeOwner);
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
        "  'move-sink-input 30 sink.a') "
        "[ -z \"${SYNAPSE_AUDIO_MOVE_LOG:-}\" ] || printf 'sink.a\\n' "
        ">>\"$SYNAPSE_AUDIO_MOVE_LOG\"; "
        "[ \"${SYNAPSE_AUDIO_MOVE_MODE:-success}\" != fail ] || exit 65; "
        "[ \"${SYNAPSE_AUDIO_MOVE_MODE:-success}\" != timeout ] || { "
        "sleep 5; exit 65; }; "
        "sed 's/\"sink\":11/\"sink\":10/' "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json\" >"
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.next\" && mv "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.next\" "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json\"; "
        "[ \"${SYNAPSE_AUDIO_MOVE_MODE:-success}\" != mutate-fail ] || "
        "exit 65 ;;\n"
        "  'move-sink-input 30 sink.b') "
        "[ -z \"${SYNAPSE_AUDIO_MOVE_LOG:-}\" ] || printf 'sink.b\\n' "
        ">>\"$SYNAPSE_AUDIO_MOVE_LOG\"; "
        "[ \"${SYNAPSE_AUDIO_MOVE_MODE:-success}\" != fail ] || exit 65; "
        "[ \"${SYNAPSE_AUDIO_MOVE_MODE:-success}\" != timeout ] || { "
        "sleep 5; exit 65; }; "
        "sed 's/\"sink\":10/\"sink\":11/' "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json\" >"
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.next\" && mv "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.next\" "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json\"; "
        "[ \"${SYNAPSE_AUDIO_MOVE_MODE:-success}\" != mutate-fail ] || "
        "exit 65 ;;\n"
        "  *) exit 64 ;;\n"
        "esac\n");
    valid_ = writeFile(pactl_, script,
                       QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    policy_ = config_ + QStringLiteral("/audio-route-policy-v1.json");
  }

  bool valid() const { return valid_; }
  QString executable() const { return executable_; }
  QString applicationDirectory() const { return applicationDirectory_; }
  QString moveLog() const { return root_ + QStringLiteral("/move.log"); }
  QString policy() const { return policy_; }

  void activate() const {
    qputenv("HOME", root_.toUtf8());
    qputenv("XDG_CONFIG_HOME", config_.toUtf8());
    qputenv("XDG_RUNTIME_DIR", runtime_.toUtf8());
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
  QString runtime_;
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

    const QByteArray activeBroker = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-route-broker-status/v1\","
        "\"status\":\"Ready\",\"mode\":\"new-streams-only\","
        "\"stateAuthority\":\"pipewire-pulse-model\",\"capable\":true,"
        "\"active\":true,\"enforcementAvailable\":true,\"reason\":null,"
        "\"policyGeneration\":7,\"baselineStreams\":2,"
        "\"persistentPidRules\":false,\"existingStreamMigration\":false,"
        "\"bounded\":true}\n");
    QVERIFY(
        AudioContracts::decodeBrokerStatus(activeBroker, &snapshot, &error));
    QVERIFY(snapshot.routeBrokerAvailable);
    QVERIFY(snapshot.routeBrokerActive);
    QVERIFY(snapshot.routeEnforcementAvailable);
    QVERIFY(snapshot.routeBrokerReason.isEmpty());
    QByteArray falseBroker = activeBroker;
    falseBroker.replace("\"reason\":null", "\"reason\":\"timeout\"");
    QVERIFY(
        !AudioContracts::decodeBrokerStatus(falseBroker, &snapshot, &error));
    QByteArray expandedBroker = activeBroker;
    expandedBroker.replace("\"bounded\":true",
                           "\"unknown\":0,\"bounded\":true");
    QVERIFY(
        !AudioContracts::decodeBrokerStatus(expandedBroker, &snapshot, &error));

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

    QString cohort;
    const QByteArray movePlan = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-existing-stream-move-plan/"
        "v1\",\"status\":\"Planned\",\"stream\":\"playback-30\","
        "\"direction\":\"output\",\"originalDevice\":\"output-"
        "0123456789abcdef\",\"requestedDevice\":\"output-fedcba9876543210\","
        "\"cohort\":\"move-0123456789abcdef\",\"changed\":true,"
        "\"stateAuthority\":\"pipewire-pulse-model\","
        "\"requiresAcknowledgement\":\"synapse-settings/"
        "audio-existing-stream-move/v1\",\"singleStream\":true,"
        "\"postflightRequired\":true,\"rollbackOnUnverified\":true,"
        "\"applied\":false,\"bounded\":true}\n");
    QVERIFY(AudioContracts::decodeStreamMovePlan(
        movePlan, QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &cohort, &changed, &error));
    QCOMPARE(cohort, QStringLiteral("move-0123456789abcdef"));
    QVERIFY(changed);
    QByteArray falseMovePlan = movePlan;
    falseMovePlan.replace("\"changed\":true", "\"changed\":false");
    QVERIFY(!AudioContracts::decodeStreamMovePlan(
        falseMovePlan, QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &cohort, &changed, &error));

    QString moveStatus;
    QString moveReason;
    bool rollbackAttempted = false;
    bool rollbackVerified = false;
    const QByteArray moveReceipt = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-existing-stream-move-receipt/"
        "v1\",\"status\":\"Applied\",\"reason\":null,\"stream\":"
        "\"playback-30\",\"direction\":\"output\",\"originalDevice\":"
        "\"output-0123456789abcdef\",\"requestedDevice\":"
        "\"output-fedcba9876543210\",\"changed\":true,\"moveApplied\":true,"
        "\"verified\":true,\"rollbackAttempted\":false,"
        "\"rollbackVerified\":false,\"stateAuthority\":"
        "\"pipewire-pulse-model\",\"requiresAcknowledgement\":"
        "\"synapse-settings/audio-existing-stream-move/v1\","
        "\"singleStream\":true,\"policyApplied\":false,"
        "\"persistentRuleCreated\":false,\"existingStreamMovement\":true,"
        "\"bounded\":true}\n");
    QVERIFY(AudioContracts::decodeStreamMoveReceipt(
        moveReceipt, QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &moveStatus, &moveReason,
        &changed, &rollbackAttempted, &rollbackVerified, &error));
    QCOMPARE(moveStatus, QStringLiteral("Applied"));
    QVERIFY(moveReason.isEmpty());
    QVERIFY(changed);
    QVERIFY(!rollbackAttempted && !rollbackVerified);
    QByteArray expandedMoveReceipt = moveReceipt;
    expandedMoveReceipt.replace("\"bounded\":true",
                                "\"unknown\":0,\"bounded\":true");
    QVERIFY(!AudioContracts::decodeStreamMoveReceipt(
        expandedMoveReceipt, QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &moveStatus, &moveReason,
        &changed, &rollbackAttempted, &rollbackVerified, &error));
    QByteArray falsePolicyMoveReceipt = moveReceipt;
    falsePolicyMoveReceipt.replace("\"policyApplied\":false",
                                   "\"policyApplied\":true");
    QVERIFY(!AudioContracts::decodeStreamMoveReceipt(
        falsePolicyMoveReceipt, QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &moveStatus, &moveReason,
        &changed, &rollbackAttempted, &rollbackVerified, &error));
    QByteArray rollbackReceipt = moveReceipt;
    rollbackReceipt.replace("\"status\":\"Applied\",\"reason\":null",
                            "\"status\":\"Failed\",\"reason\":\"move-failed\"");
    rollbackReceipt.replace("\"changed\":true,\"moveApplied\":true,"
                            "\"verified\":true,\"rollbackAttempted\":false,"
                            "\"rollbackVerified\":false",
                            "\"changed\":false,\"moveApplied\":false,"
                            "\"verified\":false,\"rollbackAttempted\":true,"
                            "\"rollbackVerified\":true");
    QVERIFY(AudioContracts::decodeStreamMoveReceipt(
        rollbackReceipt, QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &moveStatus, &moveReason,
        &changed, &rollbackAttempted, &rollbackVerified, &error));
    QCOMPARE(moveStatus, QStringLiteral("Failed"));
    QCOMPARE(moveReason, QStringLiteral("move-failed"));
    QVERIFY(rollbackAttempted && rollbackVerified);
    QByteArray falseRollbackReceipt = rollbackReceipt;
    falseRollbackReceipt.replace("\"rollbackAttempted\":true",
                                 "\"rollbackAttempted\":false");
    QVERIFY(!AudioContracts::decodeStreamMoveReceipt(
        falseRollbackReceipt, QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &moveStatus, &moveReason,
        &changed, &rollbackAttempted, &rollbackVerified, &error));

    QByteArray oversizedStreamPlan = movePlan;
    oversizedStreamPlan.replace("playback-30", "playback-2147483648");
    QVERIFY(!AudioContracts::decodeStreamMovePlan(
        oversizedStreamPlan, QStringLiteral("playback-2147483648"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &cohort, &changed, &error));

    const QJsonObject appliedObject =
        QJsonDocument::fromJson(moveReceipt).object();
    QJsonObject alreadyObject = appliedObject;
    alreadyObject.insert(QStringLiteral("status"),
                         QStringLiteral("AlreadyRouted"));
    alreadyObject.insert(QStringLiteral("reason"), QJsonValue::Null);
    alreadyObject.insert(QStringLiteral("requestedDevice"),
                         QStringLiteral("output-0123456789abcdef"));
    alreadyObject.insert(QStringLiteral("changed"), false);
    alreadyObject.insert(QStringLiteral("moveApplied"), false);
    alreadyObject.insert(QStringLiteral("verified"), true);
    QVERIFY(AudioContracts::decodeStreamMoveReceipt(
        QJsonDocument(alreadyObject).toJson(QJsonDocument::Compact),
        QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-0123456789abcdef"), &moveStatus, &moveReason,
        &changed, &rollbackAttempted, &rollbackVerified, &error));
    QCOMPARE(moveStatus, QStringLiteral("AlreadyRouted"));
    QVERIFY(!changed && moveReason.isEmpty());

    const QStringList refusedReasons = {
        QStringLiteral("audio-unavailable"),
        QStringLiteral("stream-vanished"),
        QStringLiteral("process-unavailable"),
        QStringLiteral("target-unavailable"),
        QStringLiteral("current-target-unavailable"),
        QStringLiteral("original-target-mismatch"),
        QStringLiteral("stream-cohort-changed"),
        QStringLiteral("stream-identity-changed"),
    };
    for (const QString &refusedReason : refusedReasons) {
      QJsonObject refusedObject = appliedObject;
      refusedObject.insert(QStringLiteral("status"), QStringLiteral("Refused"));
      refusedObject.insert(QStringLiteral("reason"), refusedReason);
      refusedObject.insert(QStringLiteral("changed"), false);
      refusedObject.insert(QStringLiteral("moveApplied"), false);
      refusedObject.insert(QStringLiteral("verified"), false);
      QVERIFY2(AudioContracts::decodeStreamMoveReceipt(
                   QJsonDocument(refusedObject).toJson(QJsonDocument::Compact),
                   QStringLiteral("playback-30"),
                   QStringLiteral("output-0123456789abcdef"),
                   QStringLiteral("output-fedcba9876543210"), &moveStatus,
                   &moveReason, &changed, &rollbackAttempted, &rollbackVerified,
                   &error),
               qPrintable(refusedReason));
      QCOMPARE(moveStatus, QStringLiteral("Refused"));
      QCOMPARE(moveReason, refusedReason);
    }

    const QStringList failedReasons = {
        QStringLiteral("stream-vanished"),
        QStringLiteral("move-timeout"),
        QStringLiteral("move-failed"),
        QStringLiteral("verification-failed"),
        QStringLiteral("verification-unavailable"),
        QStringLiteral("stream-identity-changed"),
    };
    for (const QString &failedReason : failedReasons) {
      QJsonObject failedObject = appliedObject;
      failedObject.insert(QStringLiteral("status"), QStringLiteral("Failed"));
      failedObject.insert(QStringLiteral("reason"), failedReason);
      failedObject.insert(QStringLiteral("changed"), false);
      failedObject.insert(QStringLiteral("moveApplied"), false);
      failedObject.insert(QStringLiteral("verified"), false);
      QVERIFY2(AudioContracts::decodeStreamMoveReceipt(
                   QJsonDocument(failedObject).toJson(QJsonDocument::Compact),
                   QStringLiteral("playback-30"),
                   QStringLiteral("output-0123456789abcdef"),
                   QStringLiteral("output-fedcba9876543210"), &moveStatus,
                   &moveReason, &changed, &rollbackAttempted, &rollbackVerified,
                   &error),
               qPrintable(failedReason));
      QCOMPARE(moveStatus, QStringLiteral("Failed"));
      QCOMPARE(moveReason, failedReason);
    }

    QJsonObject invalidRollbackReason = appliedObject;
    invalidRollbackReason.insert(QStringLiteral("status"),
                                 QStringLiteral("Failed"));
    invalidRollbackReason.insert(QStringLiteral("reason"),
                                 QStringLiteral("verification-failed"));
    invalidRollbackReason.insert(QStringLiteral("changed"), false);
    invalidRollbackReason.insert(QStringLiteral("moveApplied"), false);
    invalidRollbackReason.insert(QStringLiteral("verified"), false);
    invalidRollbackReason.insert(QStringLiteral("rollbackAttempted"), true);
    QVERIFY(!AudioContracts::decodeStreamMoveReceipt(
        QJsonDocument(invalidRollbackReason).toJson(QJsonDocument::Compact),
        QStringLiteral("playback-30"),
        QStringLiteral("output-0123456789abcdef"),
        QStringLiteral("output-fedcba9876543210"), &moveStatus, &moveReason,
        &changed, &rollbackAttempted, &rollbackVerified, &error));
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
    QVERIFY(adapter.audioRouteBrokerAvailable());
    QVERIFY(!adapter.audioRouteBrokerActive());
    QCOMPARE(adapter.audioRouteBrokerReason(),
             QStringLiteral("broker-not-running"));
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
    const QString integratedOutput = adapter.audioOutputs()
                                         .constFirst()
                                         .toMap()
                                         .value(QStringLiteral("id"))
                                         .toString();
    const QString activeStream = adapter.audioStreams()
                                     .constFirst()
                                     .toMap()
                                     .value(QStringLiteral("id"))
                                     .toString();
    QVERIFY(adapter.audioStreams()
                .constFirst()
                .toMap()
                .value(QStringLiteral("moveAvailable"))
                .toBool());
    QVERIFY(writeFile(fixture.moveLog(), QByteArray()));
    qputenv("SYNAPSE_AUDIO_MOVE_LOG", fixture.moveLog().toUtf8());
    QSignalSpy operations(&adapter, &AudioAdapter::audioOperationFinished);
    QVERIFY(adapter.moveAudioStream(activeStream, output, integratedOutput));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioStatusId(),
                              QStringLiteral("audio-stream-moved"), 15000);
    QCOMPARE(adapter.audioStreams()
                 .constFirst()
                 .toMap()
                 .value(QStringLiteral("target"))
                 .toString(),
             integratedOutput);
    QFile moveLog(fixture.moveLog());
    QVERIFY(moveLog.open(QIODevice::ReadOnly));
    QCOMPARE(moveLog.readAll(), QByteArrayLiteral("sink.a\n"));
    moveLog.close();

    QVERIFY(adapter.moveAudioStream(activeStream, integratedOutput,
                                    integratedOutput));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioStatusId(),
                              QStringLiteral("audio-stream-unchanged"), 15000);
    QVERIFY(moveLog.open(QIODevice::ReadOnly));
    QCOMPARE(moveLog.readAll(), QByteArrayLiteral("sink.a\n"));
    moveLog.close();

    qputenv("SYNAPSE_AUDIO_MOVE_MODE", QByteArrayLiteral("fail"));
    QVERIFY(adapter.moveAudioStream(activeStream, integratedOutput, output));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioErrorId(),
                              QStringLiteral("audio-stream-move-failed"),
                              15000);
    QVERIFY(adapter.audioStatusId().isEmpty());
    QCOMPARE(adapter.audioStreams()
                 .constFirst()
                 .toMap()
                 .value(QStringLiteral("target"))
                 .toString(),
             integratedOutput);
    QVERIFY(moveLog.open(QIODevice::ReadOnly));
    QCOMPARE(moveLog.readAll(), QByteArrayLiteral("sink.a\nsink.b\n"));
    moveLog.close();

    qputenv("SYNAPSE_AUDIO_MOVE_MODE", QByteArrayLiteral("mutate-fail"));
    QVERIFY(adapter.moveAudioStream(activeStream, integratedOutput, output));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioErrorId(),
                              QStringLiteral("audio-stream-move-restored"),
                              15000);
    QCOMPARE(adapter.audioStreams()
                 .constFirst()
                 .toMap()
                 .value(QStringLiteral("target"))
                 .toString(),
             integratedOutput);
    QVERIFY(moveLog.open(QIODevice::ReadOnly));
    QCOMPARE(moveLog.readAll(),
             QByteArrayLiteral("sink.a\nsink.b\nsink.b\nsink.a\n"));
    moveLog.close();
    qunsetenv("SYNAPSE_AUDIO_MOVE_MODE");
    QVERIFY(!QFileInfo::exists(fixture.policy()));

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
    QVERIFY(operations.size() >= 5);

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

  void applyTransportFailureRefreshesSnapshot() {
    ScopedEnvironment restore;
    AudioFixture fixture;
    QVERIFY(fixture.valid());
    fixture.activate();
    AudioAdapter adapter(
        testBackend(), [](const QString &) { return QString(); }, 500);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    const QString requested = adapter.audioOutputs()
                                  .constFirst()
                                  .toMap()
                                  .value(QStringLiteral("id"))
                                  .toString();
    const QString original = adapter.audioOutputs()
                                 .at(1)
                                 .toMap()
                                 .value(QStringLiteral("id"))
                                 .toString();
    const QString stream = adapter.audioStreams()
                               .constFirst()
                               .toMap()
                               .value(QStringLiteral("id"))
                               .toString();
    qputenv("SYNAPSE_AUDIO_MOVE_MODE", QByteArrayLiteral("timeout"));
    QVERIFY(adapter.moveAudioStream(stream, original, requested));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioErrorId(), QStringLiteral("timeout"),
                              10000);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 10000);
    QCOMPARE(loaded.constLast().constFirst().toBool(), true);
    QVERIFY(adapter.audioAvailable());
    QVERIFY(!adapter.audioBusy());
    QCOMPARE(adapter.audioStreams()
                 .constFirst()
                 .toMap()
                 .value(QStringLiteral("target"))
                 .toString(),
             original);
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
    QVERIFY(
        !adapter.moveAudioStream(QStringLiteral("playback-30"),
                                 QStringLiteral("output-0000000000000000"),
                                 QStringLiteral("output-0123456789abcdef")));
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
