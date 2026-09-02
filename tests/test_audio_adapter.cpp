// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_adapter.h"
#include "localization.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
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
        QByteArrayLiteral("SYNAPSE_GOXLR"),
        QByteArrayLiteral("SYNAPSE_SETTINGS_TEST_BACKEND"),
        QByteArrayLiteral("SYNAPSE_AUDIO_MOVE_LOG"),
        QByteArrayLiteral("SYNAPSE_AUDIO_MOVE_MODE"),
        QByteArrayLiteral("SYNAPSE_AUDIO_CONTROL_LOG"),
        QByteArrayLiteral("SYNAPSE_AUDIO_CONTROL_MODE"),
        QByteArrayLiteral("SYNAPSE_AUDIO_SELECTION_LOG"),
        QByteArrayLiteral("SYNAPSE_AUDIO_SELECTION_MODE"),
        QByteArrayLiteral("SYNAPSE_ADAPTER_REAL_BACKEND"),
        QByteArrayLiteral("SYNAPSE_ADAPTER_OVERRIDE_RESPONSE"),
        QByteArrayLiteral("SYNAPSE_ADAPTER_OVERRIDE_EXIT"),
        QByteArrayLiteral("SYNAPSE_ADAPTER_WRAPPER_LOG")};
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
                "\"Integrated audio\",\"mute\":false,\"volume\":{\"left\":"
                "{\"value\":32768}},\"ports\":[{\"name\":\"port.speaker\","
                "\"description\":\"Speakers\",\"availability\":\"available\"},"
                "{\"name\":\"port.headphones\",\"description\":\"Headphones\","
                "\"availability\":\"availability unknown\"}],\"active_port\":"
                "\"port.speaker\"},{\"index\":11,\"name\":\"sink.b\","
                "\"description\":\"USB headset\",\"mute\":false,\"volume\":{"
                "\"left\":{\"value\":65536}},\"ports\":[{\"name\":\"port.usb\","
                "\"description\":\"USB "
                "output\",\"availability\":\"available\"}],"
                "\"active_port\":\"port.usb\"}]\n")) &&
        writeFile(
            audio_ + QStringLiteral("/sources.json"),
            QByteArrayLiteral(
                "[{\"index\":20,\"name\":\"source.a\",\"description\":"
                "\"Built-in microphone\",\"monitor_of_sink\":null,"
                "\"monitor_source\":\"\",\"mute\":false,\"volume\":{"
                "\"mono\":{\"value\":32768}},\"ports\":[{\"name\":"
                "\"port.internal\",\"description\":\"Internal microphone\","
                "\"availability\":\"available\"},{\"name\":\"port.mic\","
                "\"description\":\"External microphone\",\"availability\":"
                "\"availability unknown\"}],\"active_port\":\"port.internal\"},"
                "{\"index\":21,\"name\":\"source.b\",\"description\":"
                "\"USB microphone\",\"monitor_of_sink\":null,"
                "\"monitor_source\":\"\",\"mute\":false,\"volume\":{"
                "\"mono\":{\"value\":65536}},\"ports\":[{\"name\":"
                "\"port.usb-mic\",\"description\":\"USB microphone\","
                "\"availability\":\"available\"}],\"active_port\":"
                "\"port.usb-mic\"},{\"index\":22,\"name\":\"sink.a.monitor\","
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
                "audio card\",\"profiles\":{\"profile.hifi\":{\"description\":"
                "\"High Fidelity\",\"available\":true},\"profile.pro\":{"
                "\"description\":\"Pro Audio\"}},\"active_profile\":"
                "\"profile.hifi\"}]\n")) &&
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
        "  'set-sink-volume sink.a 35%') "
        "[ -z \"${SYNAPSE_AUDIO_CONTROL_LOG:-}\" ] || printf "
        "'volume\\tsink.a\\t35%%\\n' >>\"$SYNAPSE_AUDIO_CONTROL_LOG\"; "
        "sed '0,/\"value\":32768/s//\"value\":22938/' "
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.json\" >"
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.next\" && mv "
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.next\" "
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.json\" ;;\n"
        "  'set-sink-input-mute 30 1') "
        "[ -z \"${SYNAPSE_AUDIO_CONTROL_LOG:-}\" ] || printf "
        "'mute\\t30\\t1\\n' >>\"$SYNAPSE_AUDIO_CONTROL_LOG\"; "
        "sed '0,/\"mute\":false/s//\"mute\":true/' "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json\" >"
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.next\" && mv "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.next\" "
        "\"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json\" ;;\n"
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
        "  'set-card-profile card.a profile.hifi') "
        "[ -z \"${SYNAPSE_AUDIO_SELECTION_LOG:-}\" ] || printf "
        "'profile\\tcard.a\\tprofile.hifi\\n' "
        ">>\"$SYNAPSE_AUDIO_SELECTION_LOG\"; "
        "sed 's/\"active_profile\":\"profile.pro\"/"
        "\"active_profile\":\"profile.hifi\"/' "
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.json\" >"
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.next\" && mv "
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.next\" "
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.json\"; "
        "[ \"${SYNAPSE_AUDIO_SELECTION_MODE:-success}\" != "
        "fail-after-mutate ] || exit 65 ;;\n"
        "  'set-card-profile card.a profile.pro') "
        "[ -z \"${SYNAPSE_AUDIO_SELECTION_LOG:-}\" ] || printf "
        "'profile\\tcard.a\\tprofile.pro\\n' "
        ">>\"$SYNAPSE_AUDIO_SELECTION_LOG\"; "
        "sed 's/\"active_profile\":\"profile.hifi\"/"
        "\"active_profile\":\"profile.pro\"/' "
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.json\" >"
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.next\" && mv "
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.next\" "
        "\"$SYNAPSE_AUDIO_FIXTURES/cards.json\" ;;\n"
        "  'set-sink-port sink.a port.headphones') "
        "[ -z \"${SYNAPSE_AUDIO_SELECTION_LOG:-}\" ] || printf "
        "'port\\tsink.a\\tport.headphones\\n' "
        ">>\"$SYNAPSE_AUDIO_SELECTION_LOG\"; "
        "sed 's/\"active_port\":\"port.speaker\"/"
        "\"active_port\":\"port.headphones\"/' "
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.json\" >"
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.next\" && mv "
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.next\" "
        "\"$SYNAPSE_AUDIO_FIXTURES/sinks.json\" ;;\n"
        "  'set-source-port source.a port.mic') "
        "[ -z \"${SYNAPSE_AUDIO_SELECTION_LOG:-}\" ] || printf "
        "'port\\tsource.a\\tport.mic\\n' >>\"$SYNAPSE_AUDIO_SELECTION_LOG\"; "
        "sed 's/\"active_port\":\"port.internal\"/"
        "\"active_port\":\"port.mic\"/' "
        "\"$SYNAPSE_AUDIO_FIXTURES/sources.json\" >"
        "\"$SYNAPSE_AUDIO_FIXTURES/sources.next\" && mv "
        "\"$SYNAPSE_AUDIO_FIXTURES/sources.next\" "
        "\"$SYNAPSE_AUDIO_FIXTURES/sources.json\" ;;\n"
        "  *) exit 64 ;;\n"
        "esac\n");
    valid_ = writeFile(pactl_, script,
                       QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (!valid_)
      return;
    goxlr_ = bin_ + QStringLiteral("/synapse-goxlr-fake");
    const QByteArray goxlrScript = QByteArrayLiteral(
        "#!/bin/sh\n"
        "set -eu\n"
        "[ \"${1-} ${2-} ${3-}\" = 'provider-status --format json' ] || "
        "exit 64\n"
        "printf '%s\\n' '{\"schema\":\"synapse.goxlr.provider-status/v2\","
        "\"deviceCount\":1,\"truncated\":false,\"devices\":[{\"id\":"
        "\"goxlr-1\",\"model\":\"GoXLR Mini\","
        "\"systemOutputSupported\":true,\"stateAuthority\":"
        "\"provider-profile-model\",\"systemOutput\":{"
        "\"routeToLineOut\":true,\"systemVolume\":254,"
        "\"lineOutVolume\":255,\"systemFader\":\"D\","
        "\"systemMuteState\":\"Unmuted\",\"lineOutMix\":\"A\","
        "\"submixEnabled\":false}}]}'\n");
    valid_ = writeFile(goxlr_, goxlrScript,
                       QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    policy_ = config_ + QStringLiteral("/audio-route-policy-v1.json");
  }

  bool valid() const { return valid_; }
  QString executable() const { return executable_; }
  QString applicationDirectory() const { return applicationDirectory_; }
  QString moveLog() const { return root_ + QStringLiteral("/move.log"); }
  QString controlLog() const { return root_ + QStringLiteral("/control.log"); }
  QString selectionLog() const {
    return root_ + QStringLiteral("/selection.log");
  }
  QString policy() const { return policy_; }

  void activate() const {
    qputenv("HOME", root_.toUtf8());
    qputenv("XDG_CONFIG_HOME", config_.toUtf8());
    qputenv("XDG_RUNTIME_DIR", runtime_.toUtf8());
    qputenv("SYNAPSE_PACTL", pactl_.toUtf8());
    qputenv("SYNAPSE_AUDIO_FIXTURES", audio_.toUtf8());
    qputenv("SYNAPSE_AUDIO_ROUTE_POLICY", policy_.toUtf8());
    qputenv("SYNAPSE_GOXLR", goxlr_.toUtf8());
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
  QString goxlr_;
  QString policy_;
  bool valid_ = false;
};

QString testBackend() {
  return QString::fromUtf8(qgetenv("SYNAPSE_SETTINGS_TEST_BACKEND"));
}

QByteArray unavailableInventory() {
  return QByteArrayLiteral(
      "{\"schema\":\"synapse.settings.audio-inventory/"
      "v2\",\"stateAuthority\":\"pipewire-pulse-model\",\"available\":false,"
      "\"reason\":\"unavailable\",\"mutationAvailable\":false,\"outputs\":[],"
      "\"inputs\":[],\"streams\":[],\"cards\":[],\"bounded\":true}\n");
}

QStringList requiredGuiLocales() {
  return {QStringLiteral("ar"),       QStringLiteral("as"),
          QStringLiteral("ast"),      QStringLiteral("az"),
          QStringLiteral("az_AZ"),    QStringLiteral("be"),
          QStringLiteral("bg"),       QStringLiteral("bn"),
          QStringLiteral("ca"),       QStringLiteral("ca@valencia"),
          QStringLiteral("cs_CZ"),    QStringLiteral("da"),
          QStringLiteral("de"),       QStringLiteral("el"),
          QStringLiteral("en_US"),    QStringLiteral("en_GB"),
          QStringLiteral("eo"),       QStringLiteral("es"),
          QStringLiteral("es_AR"),    QStringLiteral("es_MX"),
          QStringLiteral("et"),       QStringLiteral("eu"),
          QStringLiteral("fa"),       QStringLiteral("fi_FI"),
          QStringLiteral("fr"),       QStringLiteral("fur"),
          QStringLiteral("gl"),       QStringLiteral("he"),
          QStringLiteral("hi"),       QStringLiteral("hr"),
          QStringLiteral("hu"),       QStringLiteral("ia"),
          QStringLiteral("id"),       QStringLiteral("is"),
          QStringLiteral("it_IT"),    QStringLiteral("ja"),
          QStringLiteral("ka"),       QStringLiteral("ko"),
          QStringLiteral("lt"),       QStringLiteral("ml"),
          QStringLiteral("mr"),       QStringLiteral("nb"),
          QStringLiteral("nl"),       QStringLiteral("oc"),
          QStringLiteral("pl"),       QStringLiteral("pt_BR"),
          QStringLiteral("pt_PT"),    QStringLiteral("ro"),
          QStringLiteral("ru"),       QStringLiteral("si"),
          QStringLiteral("sk"),       QStringLiteral("sl"),
          QStringLiteral("sq"),       QStringLiteral("sr"),
          QStringLiteral("sr@latin"), QStringLiteral("sv"),
          QStringLiteral("tg"),       QStringLiteral("th"),
          QStringLiteral("tr_TR"),    QStringLiteral("uk"),
          QStringLiteral("uz"),       QStringLiteral("vi"),
          QStringLiteral("zh_CN"),    QStringLiteral("zh_TW")};
}

} // namespace

class AudioAdapterTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QVERIFY2(QFileInfo(testBackend()).isExecutable(),
             "SYNAPSE_SETTINGS_TEST_BACKEND must name the fixture binary");
  }

  void localizationUsesBoundedEnglishFallback() {
    SettingsLocalization localization;
    const QStringList locales = requiredGuiLocales();
    QCOMPARE(locales.size(), 64);
    for (const QString &locale : locales) {
      QVERIFY2(localization.initialize(locale), qPrintable(locale));
      QCOMPARE(localization.localeId(), locale);
    }

    QVERIFY(localization.initialize(QStringLiteral("it_IT.UTF-8@euro")));
    QCOMPARE(localization.localeId(), QStringLiteral("it_IT"));
    QCOMPARE(QCoreApplication::translate("Main", "Settings"),
             QStringLiteral("Impostazioni"));

    QVERIFY(localization.initialize(QStringLiteral("fr_FR")));
    QCOMPARE(localization.localeId(), QStringLiteral("fr"));
    QCOMPARE(QCoreApplication::translate("Main", "Settings"),
             QStringLiteral("Settings"));

    QVERIFY(localization.initialize(QStringLiteral("sr_RS.UTF-8@latin")));
    QCOMPARE(localization.localeId(), QStringLiteral("sr@latin"));
    QVERIFY(localization.initialize(QStringLiteral("ar")));
    QCOMPARE(QGuiApplication::layoutDirection(), Qt::RightToLeft);

    QString unsafeLocale = QStringLiteral("it_IT");
    unsafeLocale.append(QChar(0x1f));
    QVERIFY(localization.initialize(unsafeLocale));
    QCOMPARE(localization.localeId(), QStringLiteral("en_US"));
    QCOMPARE(QGuiApplication::layoutDirection(), Qt::LeftToRight);
    QVERIFY(localization.initialize(QStringLiteral("zh_HK")));
    QCOMPARE(localization.localeId(), QStringLiteral("en_US"));
  }

  void defaultQmlConstructorIsFixtureBoundInTestBuild() {
    ScopedEnvironment environment;
    QVERIFY(qputenv("SYNAPSE_SETTINGS_TEST_BACKEND",
                    QByteArrayLiteral("/usr/bin/false")));
    AudioAdapter adapter;
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(!adapter.audioSnapshotReady());
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
    QCOMPARE(loaded.constFirst().constFirst().toBool(), false);
    QVERIFY(!adapter.audioSnapshotReady());
    QCOMPARE(adapter.audioErrorId(), QStringLiteral("backend-failed"));
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

    QByteArray duplicateObjectKey = unavailableInventory();
    duplicateObjectKey.replace(
        "{\"schema\":", "{\"schema\":\"synapse.settings.audio-inventory/v2\","
                        "\"schema\":");
    QVERIFY(!AudioContracts::decodeInventory(duplicateObjectKey, &snapshot,
                                             &error));
    QByteArray escapedDuplicateObjectKey = unavailableInventory();
    escapedDuplicateObjectKey.replace(
        "{\"schema\":", "{\"schema\":\"synapse.settings.audio-inventory/v2\","
                        "\"\\u0073chema\":");
    QVERIFY(!AudioContracts::decodeInventory(escapedDuplicateObjectKey,
                                             &snapshot, &error));
    QByteArray trailingObject = unavailableInventory();
    trailingObject.append("{}\n");
    QVERIFY(
        !AudioContracts::decodeInventory(trailingObject, &snapshot, &error));
    QByteArray trailingComma = unavailableInventory();
    trailingComma.replace("\"bounded\":true}", "\"bounded\":true,}");
    QVERIFY(!AudioContracts::decodeInventory(trailingComma, &snapshot, &error));
    QByteArray invalidEscape = unavailableInventory();
    invalidEscape.replace("\"reason\":\"unavailable\"", "\"reason\":\"\\x20\"");
    QVERIFY(!AudioContracts::decodeInventory(invalidEscape, &snapshot, &error));
    QByteArray escapedNul = unavailableInventory();
    escapedNul.replace("\"reason\":\"unavailable\"", "\"reason\":\"\\u0000\"");
    QVERIFY(!AudioContracts::decodeInventory(escapedNul, &snapshot, &error));
    QByteArray unpairedSurrogate = unavailableInventory();
    unpairedSurrogate.replace("\"reason\":\"unavailable\"",
                              "\"reason\":\"\\ud83d\"");
    QVERIFY(
        !AudioContracts::decodeInventory(unpairedSurrogate, &snapshot, &error));
    QByteArray surrogatePair = unavailableInventory();
    surrogatePair.replace("\"reason\":\"unavailable\"",
                          "\"reason\":\"\\ud83d\\ude00\"");
    QVERIFY(AudioContracts::decodeInventory(surrogatePair, &snapshot, &error));
    QCOMPARE(snapshot.reason, QString::fromUtf8("😀"));
    QByteArray invalidLiteral = unavailableInventory();
    invalidLiteral.replace("\"available\":false", "\"available\":falsx");
    QVERIFY(
        !AudioContracts::decodeInventory(invalidLiteral, &snapshot, &error));
    QByteArray leadingZero = unavailableInventory();
    leadingZero.replace("\"available\":false", "\"available\":01");
    QVERIFY(!AudioContracts::decodeInventory(leadingZero, &snapshot, &error));
    QByteArray invalidUtf8 = unavailableInventory();
    const qsizetype invalidByte = invalidUtf8.indexOf("unavailable");
    QVERIFY(invalidByte >= 0);
    invalidUtf8[invalidByte] = static_cast<char>(0xff);
    QVERIFY(!AudioContracts::decodeInventory(invalidUtf8, &snapshot, &error));

    QByteArray duplicateStream = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-inventory/"
        "v2\",\"stateAuthority\":\"pipewire-pulse-model\",\"available\":true,"
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

    const QByteArray duplicateActiveProfile = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-inventory/v2\","
        "\"stateAuthority\":\"pipewire-pulse-model\",\"available\":true,"
        "\"reason\":null,\"mutationAvailable\":true,\"outputs\":[],"
        "\"inputs\":[],\"streams\":[],\"cards\":[{\"id\":"
        "\"card-0123456789abcdef\",\"label\":\"One\",\"activeProfile\":"
        "\"profile-0123456789abcdef\"},{\"id\":"
        "\"card-fedcba9876543210\",\"label\":\"Two\",\"activeProfile\":"
        "\"profile-0123456789abcdef\"}],\"bounded\":true}\n");
    QVERIFY(!AudioContracts::decodeInventory(duplicateActiveProfile, &snapshot,
                                             &error));

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

    const QByteArray goxlrReady = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-goxlr-status/v1\","
        "\"status\":\"Ready\",\"reason\":null,\"providerActive\":true,"
        "\"deviceCount\":2,\"truncated\":false,\"devices\":[{\"id\":"
        "\"goxlr-2\",\"model\":\"GoXLR\","
        "\"systemOutputSupported\":false,\"controlAvailable\":false},{"
        "\"id\":\"goxlr-1\",\"model\":\"GoXLR Mini\","
        "\"systemOutputSupported\":true,\"controlAvailable\":false}],"
        "\"stateAuthority\":\"provider-profile-model\","
        "\"hardwareReadback\":false,\"hardwareExactRollback\":false,"
        "\"mutationAvailable\":false,\"readOnly\":true,"
        "\"bounded\":true}\n");
    QVERIFY(AudioContracts::decodeGoxlrStatus(goxlrReady, &snapshot, &error));
    QCOMPARE(snapshot.goxlrStatus, QStringLiteral("Ready"));
    QVERIFY(snapshot.goxlrReason.isEmpty());
    QVERIFY(snapshot.goxlrProviderActive);
    QCOMPARE(snapshot.goxlrDevices.size(), 2);
    QCOMPARE(snapshot.goxlrDevices.constFirst()
                 .toMap()
                 .value(QStringLiteral("model"))
                 .toString(),
             QStringLiteral("GoXLR Mini"));
    QVERIFY(!snapshot.goxlrDevices.constFirst().toMap().contains(
        QStringLiteral("id")));
    QVERIFY(!snapshot.goxlrDevices.constFirst().toMap().contains(
        QStringLiteral("controlAvailable")));
    QByteArray falseGoxlr = goxlrReady;
    falseGoxlr.replace("\"hardwareReadback\":false",
                       "\"hardwareReadback\":true");
    QVERIFY(!AudioContracts::decodeGoxlrStatus(falseGoxlr, &snapshot, &error));
    falseGoxlr = goxlrReady;
    falseGoxlr.replace("\"reason\":null", "\"reason\":\"provider-inactive\"");
    QVERIFY(!AudioContracts::decodeGoxlrStatus(falseGoxlr, &snapshot, &error));
    falseGoxlr = goxlrReady;
    falseGoxlr.replace("\"deviceCount\":2", "\"deviceCount\":1");
    QVERIFY(!AudioContracts::decodeGoxlrStatus(falseGoxlr, &snapshot, &error));
    falseGoxlr = goxlrReady;
    falseGoxlr.replace("\"goxlr-2\"", "\"goxlr-1\"");
    QVERIFY(!AudioContracts::decodeGoxlrStatus(falseGoxlr, &snapshot, &error));
    falseGoxlr = goxlrReady;
    falseGoxlr.replace("\"truncated\":false", "\"truncated\":true");
    QVERIFY(!AudioContracts::decodeGoxlrStatus(falseGoxlr, &snapshot, &error));
    falseGoxlr = goxlrReady;
    falseGoxlr.replace("\"bounded\":true", "\"unknown\":0,\"bounded\":true");
    QVERIFY(!AudioContracts::decodeGoxlrStatus(falseGoxlr, &snapshot, &error));
    const QByteArray goxlrUnavailable = QByteArrayLiteral(
        "{\"schema\":\"synapse.settings.audio-goxlr-status/v1\","
        "\"status\":\"Unavailable\",\"reason\":\"adapter-unavailable\","
        "\"providerActive\":false,\"deviceCount\":0,"
        "\"truncated\":false,\"devices\":[],\"stateAuthority\":"
        "\"provider-profile-model\",\"hardwareReadback\":false,"
        "\"hardwareExactRollback\":false,\"mutationAvailable\":false,"
        "\"readOnly\":true,\"bounded\":true}\n");
    QVERIFY(
        AudioContracts::decodeGoxlrStatus(goxlrUnavailable, &snapshot, &error));
    QCOMPARE(snapshot.goxlrStatus, QStringLiteral("Unavailable"));
    QVERIFY(!snapshot.goxlrProviderActive);
    QVERIFY(snapshot.goxlrDevices.isEmpty());
    QByteArray goxlrInactive = goxlrUnavailable;
    goxlrInactive.replace("\"Unavailable\"", "\"Inactive\"");
    goxlrInactive.replace("\"adapter-unavailable\"", "\"provider-inactive\"");
    QVERIFY(
        AudioContracts::decodeGoxlrStatus(goxlrInactive, &snapshot, &error));
    QCOMPARE(snapshot.goxlrStatus, QStringLiteral("Inactive"));
    QByteArray goxlrFailed = goxlrUnavailable;
    goxlrFailed.replace("\"Unavailable\"", "\"Failed\"");
    goxlrFailed.replace("\"adapter-unavailable\"", "\"timeout\"");
    QVERIFY(AudioContracts::decodeGoxlrStatus(goxlrFailed, &snapshot, &error));
    QCOMPARE(snapshot.goxlrStatus, QStringLiteral("Failed"));
    goxlrFailed.replace("\"timeout\"", "\"adapter-unavailable\"");
    QVERIFY(!AudioContracts::decodeGoxlrStatus(goxlrFailed, &snapshot, &error));

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

    QJsonObject controlPlanObject{
        {QStringLiteral("schema"),
         QStringLiteral("synapse.settings.audio-control-plan/v1")},
        {QStringLiteral("status"), QStringLiteral("Planned")},
        {QStringLiteral("target"), QStringLiteral("output-0123456789abcdef")},
        {QStringLiteral("targetType"), QStringLiteral("output")},
        {QStringLiteral("control"), QStringLiteral("volume")},
        {QStringLiteral("originalValue"), 50},
        {QStringLiteral("requestedValue"), 40},
        {QStringLiteral("cohort"), QStringLiteral("control-0123456789abcdef")},
        {QStringLiteral("changed"), true},
        {QStringLiteral("stateAuthority"),
         QStringLiteral("pipewire-pulse-model")},
        {QStringLiteral("requiresAcknowledgement"),
         QStringLiteral("synapse-settings/audio-control/v1")},
        {QStringLiteral("singleTarget"), true},
        {QStringLiteral("safeVolumeMaximumPercent"), 100},
        {QStringLiteral("postflightRequired"), true},
        {QStringLiteral("rollbackOnUnverified"), true},
        {QStringLiteral("playbackStarted"), false},
        {QStringLiteral("captureStarted"), false},
        {QStringLiteral("profileChanged"), false},
        {QStringLiteral("routingChanged"), false},
        {QStringLiteral("applied"), false},
        {QStringLiteral("bounded"), true},
    };
    QVERIFY(AudioContracts::decodeControlPlan(
        QJsonDocument(controlPlanObject).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &cohort, &changed, &error));
    QCOMPARE(cohort, QStringLiteral("control-0123456789abcdef"));
    QVERIFY(changed);
    QJsonObject invalidControlPlan = controlPlanObject;
    invalidControlPlan.insert(QStringLiteral("requestedValue"), true);
    QVERIFY(!AudioContracts::decodeControlPlan(
        QJsonDocument(invalidControlPlan).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &cohort, &changed, &error));
    invalidControlPlan = controlPlanObject;
    invalidControlPlan.insert(QStringLiteral("unknown"), 0);
    QVERIFY(!AudioContracts::decodeControlPlan(
        QJsonDocument(invalidControlPlan).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &cohort, &changed, &error));
    QVERIFY(!AudioContracts::decodeControlPlan(
        QJsonDocument(controlPlanObject).toJson(QJsonDocument::Compact),
        QStringLiteral("sink.a"), QStringLiteral("volume"), QVariant(50),
        QVariant(40), &cohort, &changed, &error));

    QJsonObject mutePlanObject = controlPlanObject;
    mutePlanObject.insert(QStringLiteral("target"),
                          QStringLiteral("playback-30"));
    mutePlanObject.insert(QStringLiteral("targetType"),
                          QStringLiteral("playback"));
    mutePlanObject.insert(QStringLiteral("control"), QStringLiteral("mute"));
    mutePlanObject.insert(QStringLiteral("originalValue"), false);
    mutePlanObject.insert(QStringLiteral("requestedValue"), true);
    QVERIFY(AudioContracts::decodeControlPlan(
        QJsonDocument(mutePlanObject).toJson(QJsonDocument::Compact),
        QStringLiteral("playback-30"), QStringLiteral("mute"), QVariant(false),
        QVariant(true), &cohort, &changed, &error));
    QVERIFY(!AudioContracts::decodeControlPlan(
        QJsonDocument(mutePlanObject).toJson(QJsonDocument::Compact),
        QStringLiteral("playback-2147483648"), QStringLiteral("mute"),
        QVariant(false), QVariant(true), &cohort, &changed, &error));

    QJsonObject controlReceiptObject{
        {QStringLiteral("schema"),
         QStringLiteral("synapse.settings.audio-control-receipt/v1")},
        {QStringLiteral("status"), QStringLiteral("Applied")},
        {QStringLiteral("reason"), QJsonValue::Null},
        {QStringLiteral("target"), QStringLiteral("output-0123456789abcdef")},
        {QStringLiteral("targetType"), QStringLiteral("output")},
        {QStringLiteral("control"), QStringLiteral("volume")},
        {QStringLiteral("originalValue"), 50},
        {QStringLiteral("requestedValue"), 40},
        {QStringLiteral("changed"), true},
        {QStringLiteral("mutationAttempted"), true},
        {QStringLiteral("verified"), true},
        {QStringLiteral("rollbackAttempted"), false},
        {QStringLiteral("rollbackVerified"), false},
        {QStringLiteral("stateAuthority"),
         QStringLiteral("pipewire-pulse-model")},
        {QStringLiteral("requiresAcknowledgement"),
         QStringLiteral("synapse-settings/audio-control/v1")},
        {QStringLiteral("singleTarget"), true},
        {QStringLiteral("safeVolumeMaximumPercent"), 100},
        {QStringLiteral("playbackStarted"), false},
        {QStringLiteral("captureStarted"), false},
        {QStringLiteral("profileChanged"), false},
        {QStringLiteral("routingChanged"), false},
        {QStringLiteral("bounded"), true},
    };
    QString controlStatus;
    QString controlReason;
    QVERIFY(AudioContracts::decodeControlReceipt(
        QJsonDocument(controlReceiptObject).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &controlStatus, &controlReason, &changed,
        &rollbackAttempted, &rollbackVerified, &error));
    QCOMPARE(controlStatus, QStringLiteral("Applied"));
    QVERIFY(controlReason.isEmpty() && changed && !rollbackAttempted &&
            !rollbackVerified);

    QJsonObject alreadyControl = controlReceiptObject;
    alreadyControl.insert(QStringLiteral("status"),
                          QStringLiteral("AlreadySet"));
    alreadyControl.insert(QStringLiteral("requestedValue"), 50);
    alreadyControl.insert(QStringLiteral("changed"), false);
    alreadyControl.insert(QStringLiteral("mutationAttempted"), false);
    QVERIFY(AudioContracts::decodeControlReceipt(
        QJsonDocument(alreadyControl).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(50), &controlStatus, &controlReason, &changed,
        &rollbackAttempted, &rollbackVerified, &error));
    QCOMPARE(controlStatus, QStringLiteral("AlreadySet"));
    QVERIFY(controlReason.isEmpty() && !changed && !rollbackAttempted &&
            !rollbackVerified);

    const QStringList controlRefusedReasons = {
        QStringLiteral("audio-unavailable"),
        QStringLiteral("target-vanished"),
        QStringLiteral("process-unavailable"),
        QStringLiteral("original-value-mismatch"),
        QStringLiteral("control-cohort-changed"),
    };
    for (const QString &refusedReason : controlRefusedReasons) {
      QJsonObject refusedControl = controlReceiptObject;
      refusedControl.insert(QStringLiteral("status"),
                            QStringLiteral("Refused"));
      refusedControl.insert(QStringLiteral("reason"), refusedReason);
      refusedControl.insert(QStringLiteral("changed"), false);
      refusedControl.insert(QStringLiteral("mutationAttempted"), false);
      refusedControl.insert(QStringLiteral("verified"), false);
      QVERIFY2(AudioContracts::decodeControlReceipt(
                   QJsonDocument(refusedControl).toJson(QJsonDocument::Compact),
                   QStringLiteral("output-0123456789abcdef"),
                   QStringLiteral("volume"), QVariant(50), QVariant(40),
                   &controlStatus, &controlReason, &changed, &rollbackAttempted,
                   &rollbackVerified, &error),
               qPrintable(refusedReason));
      QCOMPARE(controlStatus, QStringLiteral("Refused"));
      QCOMPARE(controlReason, refusedReason);
    }

    const QStringList controlFailedReasons = {
        QStringLiteral("target-vanished"),
        QStringLiteral("mutation-timeout"),
        QStringLiteral("mutation-failed"),
        QStringLiteral("verification-failed"),
        QStringLiteral("verification-unavailable"),
        QStringLiteral("target-identity-changed"),
    };
    for (const QString &failedReason : controlFailedReasons) {
      QJsonObject failedControl = controlReceiptObject;
      failedControl.insert(QStringLiteral("status"), QStringLiteral("Failed"));
      failedControl.insert(QStringLiteral("reason"), failedReason);
      failedControl.insert(QStringLiteral("changed"), false);
      failedControl.insert(QStringLiteral("verified"), false);
      QVERIFY2(AudioContracts::decodeControlReceipt(
                   QJsonDocument(failedControl).toJson(QJsonDocument::Compact),
                   QStringLiteral("output-0123456789abcdef"),
                   QStringLiteral("volume"), QVariant(50), QVariant(40),
                   &controlStatus, &controlReason, &changed, &rollbackAttempted,
                   &rollbackVerified, &error),
               qPrintable(failedReason));
      QCOMPARE(controlStatus, QStringLiteral("Failed"));
      QCOMPARE(controlReason, failedReason);
    }

    QJsonObject restoredControl = controlReceiptObject;
    restoredControl.insert(QStringLiteral("status"), QStringLiteral("Failed"));
    restoredControl.insert(QStringLiteral("changed"), false);
    restoredControl.insert(QStringLiteral("verified"), false);
    restoredControl.insert(QStringLiteral("rollbackAttempted"), true);
    restoredControl.insert(QStringLiteral("rollbackVerified"), true);
    for (const QString &restoredReason :
         {QStringLiteral("mutation-timeout"), QStringLiteral("mutation-failed"),
          QStringLiteral("verification-failed")}) {
      restoredControl.insert(QStringLiteral("reason"), restoredReason);
      QVERIFY2(
          AudioContracts::decodeControlReceipt(
              QJsonDocument(restoredControl).toJson(QJsonDocument::Compact),
              QStringLiteral("output-0123456789abcdef"),
              QStringLiteral("volume"), QVariant(50), QVariant(40),
              &controlStatus, &controlReason, &changed, &rollbackAttempted,
              &rollbackVerified, &error),
          qPrintable(restoredReason));
      QCOMPARE(controlReason, restoredReason);
      QVERIFY(rollbackAttempted && rollbackVerified && !changed);
    }

    QJsonObject rollbackFailedControl = restoredControl;
    rollbackFailedControl.insert(QStringLiteral("reason"),
                                 QStringLiteral("rollback-failed"));
    rollbackFailedControl.insert(QStringLiteral("rollbackVerified"), false);
    QVERIFY(AudioContracts::decodeControlReceipt(
        QJsonDocument(rollbackFailedControl).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &controlStatus, &controlReason, &changed,
        &rollbackAttempted, &rollbackVerified, &error));
    QVERIFY(rollbackAttempted && !rollbackVerified);
    rollbackFailedControl.insert(QStringLiteral("rollbackAttempted"), false);
    QVERIFY(!AudioContracts::decodeControlReceipt(
        QJsonDocument(rollbackFailedControl).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &controlStatus, &controlReason, &changed,
        &rollbackAttempted, &rollbackVerified, &error));

    QJsonObject invalidControlReceipt = restoredControl;
    invalidControlReceipt.insert(QStringLiteral("reason"),
                                 QStringLiteral("mutation-failed"));
    invalidControlReceipt.insert(QStringLiteral("rollbackAttempted"), false);
    QVERIFY(!AudioContracts::decodeControlReceipt(
        QJsonDocument(invalidControlReceipt).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &controlStatus, &controlReason, &changed,
        &rollbackAttempted, &rollbackVerified, &error));
    invalidControlReceipt = controlReceiptObject;
    invalidControlReceipt.insert(QStringLiteral("playbackStarted"), true);
    QVERIFY(!AudioContracts::decodeControlReceipt(
        QJsonDocument(invalidControlReceipt).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &controlStatus, &controlReason, &changed,
        &rollbackAttempted, &rollbackVerified, &error));
    invalidControlReceipt = controlReceiptObject;
    invalidControlReceipt.insert(QStringLiteral("requestedValue"), 101);
    QVERIFY(!AudioContracts::decodeControlReceipt(
        QJsonDocument(invalidControlReceipt).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &controlStatus, &controlReason, &changed,
        &rollbackAttempted, &rollbackVerified, &error));
    invalidControlReceipt = controlReceiptObject;
    invalidControlReceipt.insert(QStringLiteral("targetType"),
                                 QStringLiteral("input"));
    QVERIFY(!AudioContracts::decodeControlReceipt(
        QJsonDocument(invalidControlReceipt).toJson(QJsonDocument::Compact),
        QStringLiteral("output-0123456789abcdef"), QStringLiteral("volume"),
        QVariant(50), QVariant(40), &controlStatus, &controlReason, &changed,
        &rollbackAttempted, &rollbackVerified, &error));
  }

  void missingGoxlrAdapterIsTypedAndNonFatal() {
    ScopedEnvironment restore;
    AudioFixture fixture;
    QVERIFY(fixture.valid());
    fixture.activate();
    QVERIFY(qputenv("SYNAPSE_GOXLR",
                    QByteArrayLiteral("/definitely/absent/synapse-goxlr")));
    AudioAdapter adapter(
        testBackend(), [](const QString &) { return QString(); }, 5000);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QCOMPARE(loaded.constFirst().constFirst().toBool(), true);
    QVERIFY(adapter.audioSnapshotReady());
    QVERIFY(adapter.audioAvailable());
    QCOMPARE(adapter.audioGoxlrStatus(), QStringLiteral("Unavailable"));
    QCOMPARE(adapter.audioGoxlrReason(), QStringLiteral("adapter-unavailable"));
    QVERIFY(!adapter.audioGoxlrProviderActive());
    QVERIFY(adapter.audioGoxlrDevices().isEmpty());
  }

  void profilePortContractsAndGuardedTransactions() {
    ScopedEnvironment restore;
    AudioFixture fixture;
    QVERIFY(fixture.valid());
    fixture.activate();
    QVERIFY(qputenv("SYNAPSE_AUDIO_SELECTION_LOG",
                    fixture.selectionLog().toUtf8()));

    AudioAdapter adapter(
        testBackend(), [](const QString &) { return QString(); }, 5000);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    QCOMPARE(loaded.constFirst().constFirst().toBool(), true);
    QVERIFY(adapter.audioSnapshotReady());
    QVERIFY(adapter.audioProfilePortAvailable());
    QVERIFY(adapter.audioProfilePortMutationAvailable());
    QVERIFY(adapter.audioProfilePortReason().isEmpty());
    QCOMPARE(adapter.audioProfileCards().size(), 1);
    QCOMPARE(adapter.audioPortEndpoints().size(), 4);

    const QByteArray projected =
        QJsonDocument::fromVariant(
            QVariantMap{
                {QStringLiteral("cards"), adapter.audioProfileCards()},
                {QStringLiteral("endpoints"), adapter.audioPortEndpoints()},
            })
            .toJson(QJsonDocument::Compact);
    QVERIFY(!projected.contains("card.a"));
    QVERIFY(!projected.contains("profile.hifi"));
    QVERIFY(!projected.contains("sink.a"));
    QVERIFY(!projected.contains("port.speaker"));
    QVERIFY(adapter.metaObject()->indexOfProperty("audioSelectionCohort") < 0);
    QVERIFY(adapter.metaObject()->indexOfProperty("audioSelectionAck") < 0);

    QVariantMap card = adapter.audioProfileCards().constFirst().toMap();
    QCOMPARE(card.value(QStringLiteral("activeProfileLabel")).toString(),
             QStringLiteral("High Fidelity"));
    QVariantMap proProfile;
    for (const QVariant &entry :
         card.value(QStringLiteral("profiles")).toList()) {
      const QVariantMap profile = entry.toMap();
      if (profile.value(QStringLiteral("label")).toString() ==
          QStringLiteral("Pro Audio"))
        proProfile = profile;
    }
    QVERIFY(!proProfile.isEmpty());
    QCOMPARE(proProfile.value(QStringLiteral("availability")).toString(),
             QStringLiteral("unknown"));

    QSignalSpy selectionRequested(
        &adapter, &AudioAdapter::audioSelectionConfirmationRequested);
    QVERIFY(adapter.planAudioProfile(
        card.value(QStringLiteral("id")).toString(),
        proProfile.value(QStringLiteral("id")).toString()));
    QVERIFY(adapter.audioBusy());
    QTRY_COMPARE_WITH_TIMEOUT(selectionRequested.size(), 1, 5000);
    QVERIFY(!adapter.audioBusy());
    QVERIFY(adapter.audioSelectionConfirmationOpen());
    QCOMPARE(adapter.audioSelectionKind(), QStringLiteral("profile"));
    QCOMPARE(adapter.audioSelectionTargetLabel(),
             QStringLiteral("Primary audio card"));
    QCOMPARE(adapter.audioSelectionOriginalLabel(),
             QStringLiteral("High Fidelity"));
    QCOMPARE(adapter.audioSelectionRequestedLabel(),
             QStringLiteral("Pro Audio"));
    QVERIFY(!QFileInfo::exists(fixture.selectionLog()));
    const QVariantMap output = adapter.audioOutputs().constFirst().toMap();
    QVERIFY(!adapter.setAudioDefault(
        QStringLiteral("output"),
        output.value(QStringLiteral("id")).toString()));
    QCOMPARE(adapter.audioErrorId(), QStringLiteral("selection-invalid"));

    QSignalSpy operation(&adapter, &AudioAdapter::audioOperationFinished);
    QVERIFY(adapter.confirmAudioSelection());
    QVERIFY(!adapter.audioSelectionConfirmationOpen());
    QTRY_VERIFY_WITH_TIMEOUT(!adapter.audioBusy(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(operation.size() >= 1, 10000);
    QCOMPARE(adapter.audioStatusId(), QStringLiteral("audio-profile-applied"));
    QCOMPARE(adapter.audioErrorId(), QString());
    QCOMPARE(QFile(fixture.selectionLog()).exists(), true);
    QFile selectionLog(fixture.selectionLog());
    QVERIFY(selectionLog.open(QIODevice::ReadOnly));
    QCOMPARE(selectionLog.readAll(),
             QByteArrayLiteral("profile\tcard.a\tprofile.pro\n"));
    selectionLog.close();
    card = adapter.audioProfileCards().constFirst().toMap();
    QCOMPARE(card.value(QStringLiteral("activeProfileLabel")).toString(),
             QStringLiteral("Pro Audio"));

    QVariantMap integratedOutput;
    QVariantMap headphones;
    for (const QVariant &entry : adapter.audioPortEndpoints()) {
      const QVariantMap endpoint = entry.toMap();
      if (endpoint.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Integrated audio"))
        continue;
      integratedOutput = endpoint;
      for (const QVariant &optionEntry :
           endpoint.value(QStringLiteral("ports")).toList()) {
        const QVariantMap option = optionEntry.toMap();
        if (option.value(QStringLiteral("label")).toString() ==
            QStringLiteral("Headphones"))
          headphones = option;
      }
    }
    QVERIFY(!integratedOutput.isEmpty() && !headphones.isEmpty());
    QVERIFY(adapter.planAudioPort(
        QStringLiteral("output"),
        integratedOutput.value(QStringLiteral("id")).toString(),
        headphones.value(QStringLiteral("id")).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(adapter.audioSelectionConfirmationOpen(), 5000);
    adapter.cancelAudioSelection();
    QVERIFY(!adapter.audioSelectionConfirmationOpen());
    QCOMPARE(adapter.audioStatusId(), QString());
    QCOMPARE(adapter.audioErrorId(), QString());
    selectionLog.setFileName(fixture.selectionLog());
    QVERIFY(selectionLog.open(QIODevice::ReadOnly));
    QCOMPARE(selectionLog.readAll(),
             QByteArrayLiteral("profile\tcard.a\tprofile.pro\n"));
    selectionLog.close();

    QVERIFY(adapter.planAudioPort(
        QStringLiteral("output"),
        integratedOutput.value(QStringLiteral("id")).toString(),
        headphones.value(QStringLiteral("id")).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(adapter.audioSelectionConfirmationOpen(), 5000);
    const int operationsBeforePort = operation.size();
    QVERIFY(adapter.confirmAudioSelection());
    QTRY_VERIFY_WITH_TIMEOUT(operation.size() > operationsBeforePort, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!adapter.audioBusy(), 10000);
    QCOMPARE(adapter.audioStatusId(), QStringLiteral("audio-port-applied"));
    selectionLog.setFileName(fixture.selectionLog());
    QVERIFY(selectionLog.open(QIODevice::ReadOnly));
    QCOMPARE(selectionLog.readAll(),
             QByteArrayLiteral("profile\tcard.a\tprofile.pro\n"
                               "port\tsink.a\tport.headphones\n"));
    selectionLog.close();

    QVERIFY(!adapter.planAudioProfile(
        QStringLiteral("card-not-a-token"),
        proProfile.value(QStringLiteral("id")).toString()));
    QVERIFY(!adapter.planAudioPort(
        QStringLiteral("input"),
        integratedOutput.value(QStringLiteral("id")).toString(),
        headphones.value(QStringLiteral("id")).toString()));

    auto runBackend = [](const QStringList &arguments, int *exitCode) {
      QProcess process;
      process.setProgram(testBackend());
      process.setArguments(arguments);
      process.start();
      if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        if (exitCode)
          *exitCode = -1;
        return QByteArray();
      }
      if (exitCode)
        *exitCode = process.exitCode();
      return process.readAllStandardOutput();
    };
    int exitCode = -1;
    const QByteArray inventoryPayload = runBackend(
        {QStringLiteral("audio"), QStringLiteral("profile-port-inventory"),
         QStringLiteral("--format"), QStringLiteral("json")},
        &exitCode);
    QCOMPARE(exitCode, 0);
    AudioPresentationSnapshot snapshot;
    snapshot.available = adapter.audioAvailable();
    snapshot.mutationAvailable = true;
    snapshot.outputs = adapter.audioOutputs();
    snapshot.inputs = adapter.audioInputs();
    snapshot.streams = adapter.audioStreams();
    snapshot.cards = adapter.audioCards();
    QString contractError;
    QVERIFY(AudioContracts::decodeProfilePortInventory(
        inventoryPayload, &snapshot, &contractError));
    QVERIFY(snapshot.profilePortAvailable);
    QJsonObject relabeledInventory =
        QJsonDocument::fromJson(inventoryPayload).object();
    QJsonArray relabeledCards =
        relabeledInventory.value(QStringLiteral("cards")).toArray();
    QJsonObject relabeledCard = relabeledCards.at(0).toObject();
    relabeledCard.insert(QStringLiteral("label"),
                         QStringLiteral("Presentation-only card label"));
    relabeledCards.replace(0, relabeledCard);
    relabeledInventory.insert(QStringLiteral("cards"), relabeledCards);
    QVERIFY(AudioContracts::decodeProfilePortInventory(
        QJsonDocument(relabeledInventory).toJson(QJsonDocument::Compact),
        &snapshot, &contractError));
    QCOMPARE(snapshot.profileCards.constFirst()
                 .toMap()
                 .value(QStringLiteral("label"))
                 .toString(),
             QStringLiteral("Presentation-only card label"));
    QVERIFY(AudioContracts::decodeProfilePortInventory(
        inventoryPayload, &snapshot, &contractError));

    QJsonObject invalidInventory =
        QJsonDocument::fromJson(inventoryPayload).object();
    invalidInventory.insert(QStringLiteral("rawName"),
                            QStringLiteral("card.a"));
    QVERIFY(!AudioContracts::decodeProfilePortInventory(
        QJsonDocument(invalidInventory).toJson(QJsonDocument::Compact),
        &snapshot, &contractError));
    invalidInventory = QJsonDocument::fromJson(inventoryPayload).object();
    QJsonArray invalidCards =
        invalidInventory.value(QStringLiteral("cards")).toArray();
    QJsonObject invalidCard = invalidCards.at(0).toObject();
    QJsonArray duplicateProfiles =
        invalidCard.value(QStringLiteral("profiles")).toArray();
    duplicateProfiles.append(duplicateProfiles.at(0));
    invalidCard.insert(QStringLiteral("profiles"), duplicateProfiles);
    invalidCards.replace(0, invalidCard);
    invalidInventory.insert(QStringLiteral("cards"), invalidCards);
    QVERIFY(!AudioContracts::decodeProfilePortInventory(
        QJsonDocument(invalidInventory).toJson(QJsonDocument::Compact),
        &snapshot, &contractError));

    auto profileBoundPayload = [](int cardCount, int finalOptionCount,
                                  AudioPresentationSnapshot *baseSnapshot) {
      *baseSnapshot = AudioPresentationSnapshot();
      baseSnapshot->available = true;
      baseSnapshot->mutationAvailable = true;
      QJsonArray cardsArray;
      quint64 optionIdentity = 1;
      for (int cardIndex = 0; cardIndex < cardCount; ++cardIndex) {
        const QString cardId = QStringLiteral("card-%1").arg(
            static_cast<qulonglong>(cardIndex + 1), 16, 16, QChar(u'0'));
        const QString cardLabel = QStringLiteral("Card %1").arg(cardIndex + 1);
        const int optionCount =
            cardIndex + 1 == cardCount ? finalOptionCount : 64;
        QJsonArray profiles;
        QString activeProfile;
        for (int optionIndex = 0; optionIndex < optionCount; ++optionIndex) {
          const QString profileId =
              QStringLiteral("profile-%1")
                  .arg(static_cast<qulonglong>(optionIdentity++), 16, 16,
                       QChar(u'0'));
          if (optionIndex == 0)
            activeProfile = profileId;
          profiles.append(QJsonObject{
              {QStringLiteral("id"), profileId},
              {QStringLiteral("label"),
               QStringLiteral("Profile %1").arg(optionIdentity)},
              {QStringLiteral("availability"), QStringLiteral("available")},
          });
        }
        baseSnapshot->cards.append(QVariantMap{
            {QStringLiteral("id"), cardId},
            {QStringLiteral("label"), cardLabel},
            {QStringLiteral("activeProfile"), activeProfile},
        });
        cardsArray.append(QJsonObject{
            {QStringLiteral("id"), cardId},
            {QStringLiteral("label"), cardLabel},
            {QStringLiteral("activeProfile"), activeProfile},
            {QStringLiteral("activeProfileLabel"),
             QStringLiteral("Profile %1")
                 .arg(optionIdentity - static_cast<quint64>(optionCount) + 1)},
            {QStringLiteral("mutationAvailable"), true},
            {QStringLiteral("profiles"), profiles},
        });
      }
      return QJsonDocument(
                 QJsonObject{
                     {QStringLiteral("schema"),
                      QStringLiteral(
                          "synapse.settings.audio-profile-port-inventory/v1")},
                     {QStringLiteral("stateAuthority"),
                      QStringLiteral("pipewire-pulse-model")},
                     {QStringLiteral("available"), true},
                     {QStringLiteral("reason"), QJsonValue::Null},
                     {QStringLiteral("mutationAvailable"), true},
                     {QStringLiteral("cards"), cardsArray},
                     {QStringLiteral("endpoints"), QJsonArray()},
                     {QStringLiteral("hardwareReadback"), false},
                     {QStringLiteral("hardwareExactRollback"), false},
                     {QStringLiteral("bounded"), true},
                 })
          .toJson(QJsonDocument::Compact);
    };
    AudioPresentationSnapshot maximumProfileSnapshot;
    const QByteArray maximumProfilePayload =
        profileBoundPayload(8, 64, &maximumProfileSnapshot);
    QVERIFY(AudioContracts::decodeProfilePortInventory(
        maximumProfilePayload, &maximumProfileSnapshot, &contractError));
    AudioPresentationSnapshot oversizedProfileSnapshot;
    const QByteArray oversizedProfilePayload =
        profileBoundPayload(9, 1, &oversizedProfileSnapshot);
    QVERIFY(!AudioContracts::decodeProfilePortInventory(
        oversizedProfilePayload, &oversizedProfileSnapshot, &contractError));

    card = adapter.audioProfileCards().constFirst().toMap();
    AudioSelectionPlan expected;
    expected.selection = QStringLiteral("profile");
    expected.target = card.value(QStringLiteral("id")).toString();
    expected.targetType = QStringLiteral("card");
    expected.targetLabel = card.value(QStringLiteral("label")).toString();
    expected.originalSelection =
        card.value(QStringLiteral("activeProfile")).toString();
    expected.originalLabel =
        card.value(QStringLiteral("activeProfileLabel")).toString();
    expected.requestedSelection = expected.originalSelection;
    expected.requestedLabel = expected.originalLabel;
    expected.requestedAvailability = QStringLiteral("unknown");
    const QByteArray planPayload =
        runBackend({QStringLiteral("audio"), QStringLiteral("plan-profile"),
                    QStringLiteral("--card"), expected.target,
                    QStringLiteral("--profile"), expected.requestedSelection,
                    QStringLiteral("--format"), QStringLiteral("json")},
                   &exitCode);
    QCOMPARE(exitCode, 0);
    AudioSelectionPlan decodedPlan;
    QVERIFY(AudioContracts::decodeSelectionPlan(planPayload, expected,
                                                &decodedPlan, &contractError));
    QVERIFY(!decodedPlan.changed && !decodedPlan.cohort.isEmpty());
    QJsonObject relabeledPlan = QJsonDocument::fromJson(planPayload).object();
    relabeledPlan.insert(QStringLiteral("targetLabel"),
                         QStringLiteral("Fresh target label"));
    relabeledPlan.insert(QStringLiteral("originalLabel"),
                         QStringLiteral("Fresh original label"));
    relabeledPlan.insert(QStringLiteral("requestedLabel"),
                         QStringLiteral("Fresh requested label"));
    relabeledPlan.insert(QStringLiteral("requestedAvailability"),
                         QStringLiteral("available"));
    QVERIFY(AudioContracts::decodeSelectionPlan(
        QJsonDocument(relabeledPlan).toJson(QJsonDocument::Compact), expected,
        &decodedPlan, &contractError));
    QCOMPARE(decodedPlan.targetLabel, QStringLiteral("Fresh target label"));
    QCOMPARE(decodedPlan.originalLabel, QStringLiteral("Fresh original label"));
    QCOMPARE(decodedPlan.requestedLabel,
             QStringLiteral("Fresh requested label"));
    QCOMPARE(decodedPlan.requestedAvailability, QStringLiteral("available"));
    QVERIFY(AudioContracts::decodeSelectionPlan(planPayload, expected,
                                                &decodedPlan, &contractError));
    QJsonObject invalidPlan = QJsonDocument::fromJson(planPayload).object();
    invalidPlan.insert(QStringLiteral("playbackStarted"), true);
    QVERIFY(!AudioContracts::decodeSelectionPlan(
        QJsonDocument(invalidPlan).toJson(QJsonDocument::Compact), expected,
        &decodedPlan, &contractError));
    invalidPlan = QJsonDocument::fromJson(planPayload).object();
    invalidPlan.insert(QStringLiteral("defaultChanged"), true);
    QVERIFY(!AudioContracts::decodeSelectionPlan(
        QJsonDocument(invalidPlan).toJson(QJsonDocument::Compact), expected,
        &decodedPlan, &contractError));

    const QByteArray receiptPayload =
        runBackend({QStringLiteral("audio"), QStringLiteral("set-profile"),
                    QStringLiteral("--card"), expected.target,
                    QStringLiteral("--from-profile"),
                    expected.originalSelection, QStringLiteral("--profile"),
                    expected.requestedSelection, QStringLiteral("--cohort"),
                    decodedPlan.cohort, QStringLiteral("--ack"),
                    QStringLiteral("synapse-settings/audio-profile-port/v1"),
                    QStringLiteral("--format"), QStringLiteral("json")},
                   &exitCode);
    QCOMPARE(exitCode, 0);
    QString receiptStatus;
    QString receiptReason;
    bool receiptChanged = true;
    bool rollbackAttempted = true;
    bool rollbackVerified = true;
    QVERIFY(AudioContracts::decodeSelectionReceipt(
        receiptPayload, decodedPlan, &receiptStatus, &receiptReason,
        &receiptChanged, &rollbackAttempted, &rollbackVerified,
        &contractError));
    QCOMPARE(receiptStatus, QStringLiteral("AlreadySet"));
    QVERIFY(receiptReason.isEmpty() && !receiptChanged && !rollbackAttempted &&
            !rollbackVerified);
    QJsonObject invalidReceipt =
        QJsonDocument::fromJson(receiptPayload).object();
    invalidReceipt.insert(QStringLiteral("hardwareReadback"), true);
    QVERIFY(!AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        decodedPlan, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    invalidReceipt = QJsonDocument::fromJson(receiptPayload).object();
    invalidReceipt.insert(QStringLiteral("defaultChanged"), true);
    QVERIFY(!AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        decodedPlan, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    for (const QString &field :
         {QStringLiteral("changed"), QStringLiteral("mutationAttempted"),
          QStringLiteral("rollbackAttempted"),
          QStringLiteral("profileChanged")}) {
      invalidReceipt = QJsonDocument::fromJson(receiptPayload).object();
      invalidReceipt.insert(field, true);
      QVERIFY(!AudioContracts::decodeSelectionReceipt(
          QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
          decodedPlan, &receiptStatus, &receiptReason, &receiptChanged,
          &rollbackAttempted, &rollbackVerified, &contractError));
    }
    invalidReceipt = QJsonDocument::fromJson(receiptPayload).object();
    invalidReceipt.insert(QStringLiteral("verified"), false);
    QVERIFY(!AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        decodedPlan, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));

    AudioSelectionPlan changedExpected = decodedPlan;
    changedExpected.requestedSelection =
        changedExpected.originalSelection ==
                QStringLiteral("profile-0000000000000001")
            ? QStringLiteral("profile-0000000000000002")
            : QStringLiteral("profile-0000000000000001");
    changedExpected.changed = true;
    QJsonObject appliedReceipt =
        QJsonDocument::fromJson(receiptPayload).object();
    appliedReceipt.insert(QStringLiteral("status"), QStringLiteral("Applied"));
    appliedReceipt.insert(QStringLiteral("requestedSelection"),
                          changedExpected.requestedSelection);
    appliedReceipt.insert(QStringLiteral("changed"), true);
    appliedReceipt.insert(QStringLiteral("mutationAttempted"), true);
    appliedReceipt.insert(QStringLiteral("profileChanged"), true);
    QVERIFY(AudioContracts::decodeSelectionReceipt(
        QJsonDocument(appliedReceipt).toJson(QJsonDocument::Compact),
        changedExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    QCOMPARE(receiptStatus, QStringLiteral("Applied"));
    QVERIFY(receiptChanged && !rollbackAttempted && !rollbackVerified);
    AudioSelectionPlan inconsistentExpected = changedExpected;
    inconsistentExpected.changed = false;
    QVERIFY(!AudioContracts::decodeSelectionReceipt(
        QJsonDocument(appliedReceipt).toJson(QJsonDocument::Compact),
        inconsistentExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));

    QJsonObject failedReceipt = appliedReceipt;
    failedReceipt.insert(QStringLiteral("status"), QStringLiteral("Failed"));
    failedReceipt.insert(QStringLiteral("reason"),
                         QStringLiteral("mutation-failed"));
    failedReceipt.insert(QStringLiteral("changed"), false);
    failedReceipt.insert(QStringLiteral("verified"), false);
    failedReceipt.insert(QStringLiteral("profileChanged"), false);
    QVERIFY(AudioContracts::decodeSelectionReceipt(
        QJsonDocument(failedReceipt).toJson(QJsonDocument::Compact),
        changedExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    QCOMPARE(receiptStatus, QStringLiteral("Failed"));
    QCOMPARE(receiptReason, QStringLiteral("mutation-failed"));
    QVERIFY(!receiptChanged && !rollbackAttempted && !rollbackVerified);

    invalidReceipt = failedReceipt;
    invalidReceipt.insert(QStringLiteral("mutationAttempted"), false);
    QVERIFY(!AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        changedExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    invalidReceipt = failedReceipt;
    invalidReceipt.insert(QStringLiteral("rollbackVerified"), true);
    QVERIFY(!AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        changedExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    invalidReceipt = failedReceipt;
    invalidReceipt.insert(QStringLiteral("rollbackAttempted"), true);
    QVERIFY(!AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        changedExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    invalidReceipt.insert(QStringLiteral("reason"),
                          QStringLiteral("rollback-failed"));
    QVERIFY(AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        changedExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    QVERIFY(rollbackAttempted && !rollbackVerified);
    invalidReceipt = failedReceipt;
    invalidReceipt.insert(QStringLiteral("rollbackAttempted"), true);
    invalidReceipt.insert(QStringLiteral("rollbackVerified"), true);
    QVERIFY(AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        changedExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));
    QVERIFY(rollbackAttempted && rollbackVerified);
    invalidReceipt.insert(QStringLiteral("reason"),
                          QStringLiteral("verification-unavailable"));
    QVERIFY(!AudioContracts::decodeSelectionReceipt(
        QJsonDocument(invalidReceipt).toJson(QJsonDocument::Compact),
        changedExpected, &receiptStatus, &receiptReason, &receiptChanged,
        &rollbackAttempted, &rollbackVerified, &contractError));

    QVariantMap hifiProfile;
    for (const QVariant &entry :
         card.value(QStringLiteral("profiles")).toList()) {
      const QVariantMap profile = entry.toMap();
      if (profile.value(QStringLiteral("label")).toString() ==
          QStringLiteral("High Fidelity"))
        hifiProfile = profile;
    }
    QVERIFY(!hifiProfile.isEmpty());
    QVERIFY(qputenv("SYNAPSE_AUDIO_SELECTION_MODE",
                    QByteArrayLiteral("fail-after-mutate")));
    QVERIFY(adapter.planAudioProfile(
        card.value(QStringLiteral("id")).toString(),
        hifiProfile.value(QStringLiteral("id")).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(adapter.audioSelectionConfirmationOpen(), 5000);
    const int operationsBeforeRestoredFailure = operation.size();
    QVERIFY(adapter.confirmAudioSelection());
    QTRY_VERIFY_WITH_TIMEOUT(operation.size() > operationsBeforeRestoredFailure,
                             10000);
    QTRY_VERIFY_WITH_TIMEOUT(!adapter.audioBusy(), 10000);
    QCOMPARE(adapter.audioStatusId(), QString());
    QCOMPARE(adapter.audioErrorId(),
             QStringLiteral("audio-selection-restored"));
    card = adapter.audioProfileCards().constFirst().toMap();
    QCOMPARE(card.value(QStringLiteral("activeProfileLabel")).toString(),
             QStringLiteral("Pro Audio"));
    QVERIFY(qunsetenv("SYNAPSE_AUDIO_SELECTION_MODE"));
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
    QCOMPARE(adapter.audioGoxlrStatus(), QStringLiteral("Ready"));
    QVERIFY(adapter.audioGoxlrReason().isEmpty());
    QVERIFY(adapter.audioGoxlrProviderActive());
    QVERIFY(!adapter.audioGoxlrTruncated());
    QCOMPARE(adapter.audioGoxlrDevices().size(), 1);
    QCOMPARE(adapter.audioGoxlrDevices()
                 .constFirst()
                 .toMap()
                 .value(QStringLiteral("model"))
                 .toString(),
             QStringLiteral("GoXLR Mini"));
    QVERIFY(!adapter.audioGoxlrDevices().constFirst().toMap().contains(
        QStringLiteral("id")));

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
    QVERIFY(adapter.audioOutputs()
                .constFirst()
                .toMap()
                .value(QStringLiteral("levelControlAvailable"))
                .toBool());
    QVERIFY(adapter.audioStreams()
                .constFirst()
                .toMap()
                .value(QStringLiteral("levelControlAvailable"))
                .toBool());

    QVERIFY(writeFile(fixture.controlLog(), QByteArray()));
    qputenv("SYNAPSE_AUDIO_CONTROL_LOG", fixture.controlLog().toUtf8());
    QVERIFY(adapter.setAudioVolume(integratedOutput, 35));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioStatusId(),
                              QStringLiteral("audio-volume-applied"), 15000);
    QCOMPARE(adapter.audioOutputs()
                 .constFirst()
                 .toMap()
                 .value(QStringLiteral("volumePercent"))
                 .toInt(),
             35);
    QFile controlLog(fixture.controlLog());
    QVERIFY(controlLog.open(QIODevice::ReadOnly));
    QCOMPARE(controlLog.readAll(), QByteArrayLiteral("volume\tsink.a\t35%\n"));
    controlLog.close();

    QVERIFY(adapter.setAudioVolume(integratedOutput, 35));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioStatusId(),
                              QStringLiteral("audio-volume-unchanged"), 15000);
    QVERIFY(controlLog.open(QIODevice::ReadOnly));
    QCOMPARE(controlLog.readAll(), QByteArrayLiteral("volume\tsink.a\t35%\n"));
    controlLog.close();

    QVERIFY(adapter.setAudioMuted(activeStream, true));
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioStatusId(),
                              QStringLiteral("audio-mute-applied"), 15000);
    QVERIFY(adapter.audioStreams()
                .constFirst()
                .toMap()
                .value(QStringLiteral("muted"))
                .toBool());
    QVERIFY(controlLog.open(QIODevice::ReadOnly));
    QCOMPARE(controlLog.readAll(),
             QByteArrayLiteral("volume\tsink.a\t35%\nmute\t30\t1\n"));
    controlLog.close();
    QVERIFY(!adapter.setAudioVolume(integratedOutput, 101));
    QCOMPARE(adapter.audioErrorId(), QStringLiteral("selection-invalid"));

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

  void controlAndSelectionReceiptExitCodeConsistency() {
    ScopedEnvironment restore;
    AudioFixture fixture;
    QTemporaryDir wrapperDirectory;
    QVERIFY(fixture.valid());
    QVERIFY(wrapperDirectory.isValid());
    fixture.activate();

    const QString wrapper =
        wrapperDirectory.path() + QStringLiteral("/backend");
    const QString response =
        wrapperDirectory.path() + QStringLiteral("/response.json");
    const QString log = wrapperDirectory.path() + QStringLiteral("/argv.log");
    const QByteArray wrapperScript = QByteArrayLiteral(
        "#!/bin/sh\n"
        "set -eu\n"
        "printf '%s\\n' \"$*\" >>\"$SYNAPSE_ADAPTER_WRAPPER_LOG\"\n"
        "case \"${1-} ${2-}\" in\n"
        "  'audio set-volume'|'audio set-profile')\n"
        "    if [ -n \"${SYNAPSE_ADAPTER_OVERRIDE_RESPONSE:-}\" ]; then\n"
        "      cat \"$SYNAPSE_ADAPTER_OVERRIDE_RESPONSE\"\n"
        "      exit \"$SYNAPSE_ADAPTER_OVERRIDE_EXIT\"\n"
        "    fi ;;\n"
        "esac\n"
        "exec \"$SYNAPSE_ADAPTER_REAL_BACKEND\" \"$@\"\n");
    QVERIFY(writeFile(wrapper, wrapperScript,
                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QVERIFY(writeFile(log, QByteArray()));
    qputenv("SYNAPSE_ADAPTER_REAL_BACKEND", testBackend().toUtf8());
    qputenv("SYNAPSE_ADAPTER_WRAPPER_LOG", log.toUtf8());

    AudioAdapter adapter(
        wrapper, [](const QString &) { return QString(); }, 5000);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
    const QString target = adapter.audioOutputs()
                               .constFirst()
                               .toMap()
                               .value(QStringLiteral("id"))
                               .toString();

    const auto receipt = [&target](const QString &status, const QString &reason,
                                   bool changed, bool mutationAttempted,
                                   bool verified, bool rollbackAttempted,
                                   bool rollbackVerified) {
      QJsonObject value{
          {QStringLiteral("schema"),
           QStringLiteral("synapse.settings.audio-control-receipt/v1")},
          {QStringLiteral("status"), status},
          {QStringLiteral("reason"), reason.isEmpty()
                                         ? QJsonValue(QJsonValue::Null)
                                         : QJsonValue(reason)},
          {QStringLiteral("target"), target},
          {QStringLiteral("targetType"), QStringLiteral("output")},
          {QStringLiteral("control"), QStringLiteral("volume")},
          {QStringLiteral("originalValue"), 50},
          {QStringLiteral("requestedValue"), 40},
          {QStringLiteral("changed"), changed},
          {QStringLiteral("mutationAttempted"), mutationAttempted},
          {QStringLiteral("verified"), verified},
          {QStringLiteral("rollbackAttempted"), rollbackAttempted},
          {QStringLiteral("rollbackVerified"), rollbackVerified},
          {QStringLiteral("stateAuthority"),
           QStringLiteral("pipewire-pulse-model")},
          {QStringLiteral("requiresAcknowledgement"),
           QStringLiteral("synapse-settings/audio-control/v1")},
          {QStringLiteral("singleTarget"), true},
          {QStringLiteral("safeVolumeMaximumPercent"), 100},
          {QStringLiteral("playbackStarted"), false},
          {QStringLiteral("captureStarted"), false},
          {QStringLiteral("profileChanged"), false},
          {QStringLiteral("routingChanged"), false},
          {QStringLiteral("bounded"), true},
      };
      return QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    };
    const auto applyOverride = [&](const QByteArray &payload, int exitCode,
                                   const QString &expectedError) {
      QVERIFY(writeFile(response, payload));
      qputenv("SYNAPSE_ADAPTER_OVERRIDE_RESPONSE", response.toUtf8());
      qputenv("SYNAPSE_ADAPTER_OVERRIDE_EXIT", QByteArray::number(exitCode));
      QVERIFY(adapter.setAudioVolume(target, 40));
      QTRY_COMPARE_WITH_TIMEOUT(adapter.audioErrorId(), expectedError, 15000);
      QVERIFY(!adapter.audioBusy());
      QCOMPARE(adapter.audioOutputs()
                   .constFirst()
                   .toMap()
                   .value(QStringLiteral("volumePercent"))
                   .toInt(),
               50);
    };

    applyOverride(receipt(QStringLiteral("Applied"), QString(), true, true,
                          true, false, false),
                  1, QStringLiteral("contract-invalid"));
    applyOverride(receipt(QStringLiteral("Refused"),
                          QStringLiteral("control-cohort-changed"), false,
                          false, false, false, false),
                  1, QStringLiteral("audio-control-refused"));
    applyOverride(receipt(QStringLiteral("Failed"),
                          QStringLiteral("mutation-failed"), false, true, false,
                          true, true),
                  1, QStringLiteral("audio-control-restored"));
    applyOverride(receipt(QStringLiteral("Failed"),
                          QStringLiteral("mutation-failed"), false, true, false,
                          false, false),
                  0, QStringLiteral("contract-invalid"));

    const QVariantMap card = adapter.audioProfileCards().constFirst().toMap();
    QVariantMap requestedProfile;
    for (const QVariant &entry :
         card.value(QStringLiteral("profiles")).toList()) {
      const QVariantMap profile = entry.toMap();
      if (profile.value(QStringLiteral("label")).toString() ==
          QStringLiteral("Pro Audio"))
        requestedProfile = profile;
    }
    QVERIFY(!requestedProfile.isEmpty());
    const QString cardId = card.value(QStringLiteral("id")).toString();
    const QString originalProfile =
        card.value(QStringLiteral("activeProfile")).toString();
    const QString requestedProfileId =
        requestedProfile.value(QStringLiteral("id")).toString();
    const auto selectionReceipt = [&](const QString &status,
                                      const QString &reason, bool changed,
                                      bool mutationAttempted, bool verified,
                                      bool rollbackAttempted,
                                      bool rollbackVerified) {
      QJsonObject value{
          {QStringLiteral("schema"),
           QStringLiteral("synapse.settings.audio-profile-port-receipt/v1")},
          {QStringLiteral("status"), status},
          {QStringLiteral("reason"), reason.isEmpty()
                                         ? QJsonValue(QJsonValue::Null)
                                         : QJsonValue(reason)},
          {QStringLiteral("selection"), QStringLiteral("profile")},
          {QStringLiteral("target"), cardId},
          {QStringLiteral("targetType"), QStringLiteral("card")},
          {QStringLiteral("originalSelection"), originalProfile},
          {QStringLiteral("requestedSelection"), requestedProfileId},
          {QStringLiteral("changed"), changed},
          {QStringLiteral("mutationAttempted"), mutationAttempted},
          {QStringLiteral("verified"), verified},
          {QStringLiteral("rollbackAttempted"), rollbackAttempted},
          {QStringLiteral("rollbackVerified"), rollbackVerified},
          {QStringLiteral("profileChanged"), changed},
          {QStringLiteral("portChanged"), false},
          {QStringLiteral("stateAuthority"),
           QStringLiteral("pipewire-pulse-model")},
          {QStringLiteral("requiresAcknowledgement"),
           QStringLiteral("synapse-settings/audio-profile-port/v1")},
          {QStringLiteral("singleTarget"), true},
          {QStringLiteral("graphMayChange"), true},
          {QStringLiteral("signalPathMayChange"), true},
          {QStringLiteral("playbackStarted"), false},
          {QStringLiteral("captureStarted"), false},
          {QStringLiteral("defaultChanged"), false},
          {QStringLiteral("policyChanged"), false},
          {QStringLiteral("audibilityVerified"), false},
          {QStringLiteral("hardwareReadback"), false},
          {QStringLiteral("hardwareExactRollback"), false},
          {QStringLiteral("bounded"), true},
      };
      return QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    };
    const auto applySelectionOverride = [&](const QByteArray &payload,
                                            int exitCode,
                                            const QString &expectedError) {
      QVERIFY(writeFile(response, payload));
      qputenv("SYNAPSE_ADAPTER_OVERRIDE_RESPONSE", response.toUtf8());
      qputenv("SYNAPSE_ADAPTER_OVERRIDE_EXIT", QByteArray::number(exitCode));
      QVERIFY(adapter.planAudioProfile(cardId, requestedProfileId));
      QTRY_VERIFY_WITH_TIMEOUT(adapter.audioSelectionConfirmationOpen(), 5000);
      QVERIFY(adapter.confirmAudioSelection());
      QTRY_COMPARE_WITH_TIMEOUT(adapter.audioErrorId(), expectedError, 15000);
      QVERIFY(!adapter.audioBusy());
      QCOMPARE(adapter.audioProfileCards()
                   .constFirst()
                   .toMap()
                   .value(QStringLiteral("activeProfile"))
                   .toString(),
               originalProfile);
    };
    applySelectionOverride(selectionReceipt(QStringLiteral("Applied"),
                                            QString(), true, true, true, false,
                                            false),
                           1, QStringLiteral("contract-invalid"));
    applySelectionOverride(
        selectionReceipt(QStringLiteral("Refused"),
                         QStringLiteral("selection-cohort-changed"), false,
                         false, false, false, false),
        1, QStringLiteral("audio-selection-refused"));
    applySelectionOverride(selectionReceipt(QStringLiteral("Failed"),
                                            QStringLiteral("mutation-failed"),
                                            false, true, false, true, true),
                           1, QStringLiteral("audio-selection-restored"));
    applySelectionOverride(selectionReceipt(QStringLiteral("Failed"),
                                            QStringLiteral("mutation-failed"),
                                            false, true, false, false, false),
                           0, QStringLiteral("contract-invalid"));

    QFile argvLog(log);
    QVERIFY(argvLog.open(QIODevice::ReadOnly));
    int controlApplyCount = 0;
    int selectionApplyCount = 0;
    for (const QByteArray &line : argvLog.readAll().split('\n')) {
      if (line.startsWith("audio set-volume "))
        controlApplyCount++;
      if (line.startsWith("audio set-profile "))
        selectionApplyCount++;
    }
    QCOMPARE(controlApplyCount, 4);
    QCOMPARE(selectionApplyCount, 4);
  }

  void applyDeadlineAllowsBoundedInnerTimeoutAndRefreshesSnapshot() {
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
    QTRY_COMPARE_WITH_TIMEOUT(adapter.audioErrorId(),
                              QStringLiteral("audio-stream-move-failed"),
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

  void completionCallbackMayDestroyAdapter() {
    ScopedEnvironment restore;
    AudioFixture fixture;
    QVERIFY(fixture.valid());
    fixture.activate();

    {
      QPointer<AudioAdapter> adapter = new AudioAdapter(
          testBackend(), [](const QString &) { return QString(); }, 5000);
      connect(adapter.data(), &AudioAdapter::audioStateChanged, this,
              [&adapter]() { delete adapter.data(); });
      QVERIFY(!adapter->loadAudio());
      QVERIFY(adapter.isNull());
    }

    {
      QPointer<AudioAdapter> adapter = new AudioAdapter(
          testBackend(), [](const QString &) { return QString(); }, 5000);
      QVERIFY(!adapter->planAudioPort(QStringLiteral("invalid"),
                                      QStringLiteral("device"),
                                      QStringLiteral("port")));
      connect(adapter.data(), &AudioAdapter::audioOperationStateChanged, this,
              [&adapter]() { delete adapter.data(); });
      QVERIFY(!adapter->loadAudio());
      QVERIFY(adapter.isNull());
    }

    {
      QPointer<AudioAdapter> adapter = new AudioAdapter(
          testBackend(), [](const QString &) { return QString(); }, 5000);
      connect(adapter.data(), &AudioAdapter::audioModelsChanged, this,
              [&adapter]() { delete adapter.data(); });
      QVERIFY(adapter->loadAudio());
      QTRY_VERIFY_WITH_TIMEOUT(adapter.isNull(), 10000);
    }

    {
      QPointer<AudioAdapter> adapter = new AudioAdapter(
          testBackend(), [](const QString &) { return QString(); }, 5000);
      connect(adapter.data(), &AudioAdapter::audioLoaded, this,
              [&adapter](bool) { delete adapter.data(); });
      QVERIFY(adapter->loadAudio());
      QTRY_VERIFY_WITH_TIMEOUT(adapter.isNull(), 10000);
    }

    {
      QPointer<AudioAdapter> adapter = new AudioAdapter(
          testBackend(), [](const QString &) { return QString(); }, 5000);
      QSignalSpy loaded(adapter.data(), &AudioAdapter::audioLoaded);
      QVERIFY(adapter->loadAudio());
      QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
      const QVariantMap card =
          adapter->audioProfileCards().constFirst().toMap();
      const QString active =
          card.value(QStringLiteral("activeProfile")).toString();
      QString requested;
      for (const QVariant &entry :
           card.value(QStringLiteral("profiles")).toList()) {
        const QVariantMap profile = entry.toMap();
        if (profile.value(QStringLiteral("id")).toString() != active &&
            profile.value(QStringLiteral("availability")).toString() !=
                QStringLiteral("unavailable")) {
          requested = profile.value(QStringLiteral("id")).toString();
          break;
        }
      }
      QVERIFY(!requested.isEmpty());
      connect(adapter.data(), &AudioAdapter::audioSelectionChanged, this,
              [&adapter]() { delete adapter.data(); });
      QVERIFY(adapter->planAudioProfile(
          card.value(QStringLiteral("id")).toString(), requested));
      QTRY_VERIFY_WITH_TIMEOUT(adapter.isNull(), 5000);
    }

    {
      QPointer<AudioAdapter> adapter = new AudioAdapter(
          testBackend(), [](const QString &) { return QString(); }, 5000);
      QSignalSpy loaded(adapter.data(), &AudioAdapter::audioLoaded);
      QVERIFY(adapter->loadAudio());
      QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 10000);
      const QString output = adapter->audioOutputs()
                                 .constFirst()
                                 .toMap()
                                 .value(QStringLiteral("id"))
                                 .toString();
      connect(adapter.data(), &AudioAdapter::audioProcessChoiceChanged, this,
              [&adapter]() { delete adapter.data(); });
      QVERIFY(
          !adapter->chooseAudioProcessRule(QStringLiteral("output"), output));
      QVERIFY(adapter.isNull());
    }
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

  void commandFailureKillsProcessGroup() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString backend =
        directory.filePath(QStringLiteral("failing-backend"));
    const QString survivor =
        directory.filePath(QStringLiteral("failure-descendant-survived"));
    const QByteArray script =
        QByteArrayLiteral("#!/bin/sh\n(sleep 1; : > '") + survivor.toUtf8() +
        QByteArrayLiteral("') </dev/null >/dev/null 2>&1 &\nexit 65\n");
    QVERIFY(writeFile(backend, script,
                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    AudioAdapter adapter(
        backend, [](const QString &) { return QString(); }, 3000);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 3000);
    QCOMPARE(loaded.constFirst().constFirst().toBool(), false);
    QCOMPARE(adapter.audioErrorId(), QStringLiteral("backend-failed"));
    QVERIFY(!adapter.audioBusy());
    QTest::qWait(1200);
    QVERIFY(!QFileInfo::exists(survivor));
  }

  void commandTimeoutIsBounded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString backend = directory.filePath(QStringLiteral("slow-backend"));
    const QString survivor =
        directory.filePath(QStringLiteral("descendant-survived"));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\n(sleep 1; : > '") +
                              survivor.toUtf8() +
                              QByteArrayLiteral("') &\nwait\n");
    QVERIFY(writeFile(backend, script,
                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    AudioAdapter adapter(
        backend, [](const QString &) { return QString(); }, 100);
    QSignalSpy loaded(&adapter, &AudioAdapter::audioLoaded);
    QVERIFY(adapter.loadAudio());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 3000);
    QCOMPARE(loaded.constFirst().constFirst().toBool(), false);
    QCOMPARE(adapter.audioErrorId(), QStringLiteral("timeout"));
    QVERIFY(!adapter.audioBusy());
    QTest::qWait(1200);
    QVERIFY(!QFileInfo::exists(survivor));
  }

private:
  static qsizetype kMaximumInventoryForTest() {
    return qsizetype{1024} * 1024 + 1;
  }
};

QTEST_MAIN(AudioAdapterTest)
#include "test_audio_adapter.moc"
