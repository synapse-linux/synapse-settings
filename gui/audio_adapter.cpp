// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_adapter.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr qsizetype kMaximumInventoryBytes = qsizetype{1024} * 1024;
constexpr qsizetype kMaximumPolicyBytes = qsizetype{128} * 1024;
constexpr qsizetype kMaximumBrokerStatusBytes = 4096;
constexpr qsizetype kMaximumGoxlrStatusBytes = qsizetype{16} * 1024;
constexpr qsizetype kMaximumReceiptBytes = qsizetype{64} * 1024;
constexpr qsizetype kMaximumErrorBytes = qsizetype{16} * 1024;
constexpr int kMaximumLabelBytes = 255;
constexpr int kMaximumPathBytes = 4095;
constexpr int kMaximumEndpoints = 64;
constexpr int kMaximumStreams = 128;
constexpr int kMaximumCards = 32;
constexpr int kMaximumRules = 128;
constexpr int kMaximumGoxlrDevices = 8;

constexpr char kDefaultAcknowledgement[] = "synapse-settings/audio-default/v1";
constexpr char kRouteAcknowledgement[] =
    "synapse-settings/audio-route-policy/v1";
constexpr char kStreamMoveAcknowledgement[] =
    "synapse-settings/audio-existing-stream-move/v1";
constexpr char kControlAcknowledgement[] = "synapse-settings/audio-control/v1";
constexpr int kSafeVolumeMaximumPercent = 100;

QString defaultBackendPath() {
#ifdef SYNAPSE_SETTINGS_GUI_TEST_HOOKS
  const QByteArray overridePath = qgetenv("SYNAPSE_SETTINGS_TEST_BACKEND");
  if (!overridePath.isEmpty() && overridePath.size() < 4096 &&
      !overridePath.contains('\0')) {
    const QFileInfo candidate(QString::fromUtf8(overridePath));
    if (candidate.isAbsolute() && candidate.isFile() &&
        candidate.isExecutable())
      return candidate.canonicalFilePath();
  }
#endif
  return QStringLiteral("/usr/bin/synapse-settings");
}

bool fail(QString *errorId, const QString &value) {
  if (errorId)
    *errorId = value;
  return false;
}

bool exactKeys(const QJsonObject &object,
               std::initializer_list<const char *> keys) {
  QSet<QString> expected;
  expected.reserve(static_cast<qsizetype>(keys.size()));
  for (const char *key : keys)
    expected.insert(QString::fromLatin1(key));
  const QStringList actualKeys = object.keys();
  return QSet<QString>(actualKeys.cbegin(), actualKeys.cend()) == expected;
}

bool oneJsonObject(QByteArray payload, qsizetype maximumBytes,
                   QJsonObject *object, QString *errorId) {
  if (!object || payload.isEmpty() || payload.size() > maximumBytes ||
      payload.contains('\0'))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (payload.endsWith('\n'))
    payload.chop(1);
  if (payload.isEmpty() || payload.contains('\n') || payload.contains('\r'))
    return fail(errorId, QStringLiteral("contract-invalid"));
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return fail(errorId, QStringLiteral("contract-invalid"));
  *object = document.object();
  return true;
}

bool boundedText(const QJsonValue &value, int maximumBytes, QString *output,
                 bool allowEmpty = true) {
  if (!value.isString())
    return false;
  const QString text = value.toString();
  if ((!allowEmpty && text.isEmpty()) || text.contains(QChar::Null) ||
      text.toUtf8().size() > maximumBytes)
    return false;
  for (qsizetype index = 0; index < text.size(); ++index) {
    const QChar character = text.at(index);
    if (character.isHighSurrogate()) {
      if (index + 1 >= text.size() || !text.at(index + 1).isLowSurrogate())
        return false;
      ++index;
    } else if (character.isLowSurrogate() || character.unicode() < 0x20 ||
               character.unicode() == 0x7f) {
      return false;
    }
  }
  if (output)
    *output = text;
  return true;
}

bool exactInteger(const QJsonValue &value, qint64 minimum, qint64 maximum,
                  qint64 *output = nullptr) {
  if (!value.isDouble())
    return false;
  const double number = value.toDouble();
  if (!std::isfinite(number) || std::floor(number) != number ||
      number < static_cast<double>(minimum) ||
      number > static_cast<double>(maximum))
    return false;
  if (output)
    *output = static_cast<qint64>(number);
  return true;
}

bool tokenMatches(const QString &value, const QRegularExpression &expression) {
  return expression.match(value).hasMatch();
}

const QRegularExpression &outputTokenExpression() {
  static const QRegularExpression value(
      QStringLiteral("^output-[0-9a-f]{16}$"));
  return value;
}

const QRegularExpression &inputTokenExpression() {
  static const QRegularExpression value(QStringLiteral("^input-[0-9a-f]{16}$"));
  return value;
}

const QRegularExpression &cardTokenExpression() {
  static const QRegularExpression value(QStringLiteral("^card-[0-9a-f]{16}$"));
  return value;
}

const QRegularExpression &ruleTokenExpression() {
  static const QRegularExpression value(QStringLiteral("^rule-[0-9]{4}$"));
  return value;
}

const QRegularExpression &streamTokenExpression() {
  static const QRegularExpression value(
      QStringLiteral("^(playback|recording)-(0|[1-9][0-9]{0,9})$"));
  return value;
}

const QRegularExpression &streamMoveCohortExpression() {
  static const QRegularExpression value(QStringLiteral("^move-[0-9a-f]{16}$"));
  return value;
}

const QRegularExpression &controlCohortExpression() {
  static const QRegularExpression value(
      QStringLiteral("^control-[0-9a-f]{16}$"));
  return value;
}

const QRegularExpression &goxlrDeviceExpression() {
  static const QRegularExpression value(QStringLiteral("^goxlr-[1-8]$"));
  return value;
}

QString audioControlTargetType(const QString &target) {
  if (tokenMatches(target, outputTokenExpression()))
    return QStringLiteral("output");
  if (tokenMatches(target, inputTokenExpression()))
    return QStringLiteral("input");
  const QRegularExpressionMatch match = streamTokenExpression().match(target);
  bool indexOk = false;
  const qlonglong index = match.captured(2).toLongLong(&indexOk);
  if (!match.hasMatch() || !indexOk || index < 0 || index > 2147483647LL)
    return {};
  return match.captured(1);
}

bool controlValueMatches(const QJsonValue &value, const QString &control,
                         const QVariant &expected, bool original) {
  if (control == QStringLiteral("volume")) {
    if (expected.metaType().id() != QMetaType::Int)
      return false;
    qint64 decoded = 0;
    const int maximum = original ? 999 : kSafeVolumeMaximumPercent;
    return exactInteger(value, 0, maximum, &decoded) &&
           decoded == expected.toInt();
  }
  return control == QStringLiteral("mute") &&
         expected.metaType().id() == QMetaType::Bool && value.isBool() &&
         value.toBool() == expected.toBool();
}

const QSet<QString> &controlRefusedReasonIds() {
  static const QSet<QString> values = {
      QStringLiteral("audio-unavailable"),
      QStringLiteral("target-vanished"),
      QStringLiteral("process-unavailable"),
      QStringLiteral("original-value-mismatch"),
      QStringLiteral("control-cohort-changed"),
  };
  return values;
}

const QSet<QString> &controlFailedReasonIds() {
  static const QSet<QString> values = {
      QStringLiteral("target-vanished"),
      QStringLiteral("mutation-timeout"),
      QStringLiteral("mutation-failed"),
      QStringLiteral("verification-failed"),
      QStringLiteral("verification-unavailable"),
      QStringLiteral("target-identity-changed"),
      QStringLiteral("rollback-failed"),
  };
  return values;
}

const QSet<QString> &streamMoveRefusedReasonIds() {
  static const QSet<QString> values = {
      QStringLiteral("audio-unavailable"),
      QStringLiteral("stream-vanished"),
      QStringLiteral("process-unavailable"),
      QStringLiteral("target-unavailable"),
      QStringLiteral("current-target-unavailable"),
      QStringLiteral("original-target-mismatch"),
      QStringLiteral("stream-cohort-changed"),
      QStringLiteral("stream-identity-changed"),
  };
  return values;
}

const QSet<QString> &streamMoveFailedReasonIds() {
  static const QSet<QString> values = {
      QStringLiteral("stream-vanished"),
      QStringLiteral("move-timeout"),
      QStringLiteral("move-failed"),
      QStringLiteral("verification-failed"),
      QStringLiteral("verification-unavailable"),
      QStringLiteral("stream-identity-changed"),
  };
  return values;
}

const QSet<QString> &brokerReasonIds() {
  static const QSet<QString> values = {
      QStringLiteral("broker-not-running"),
      QStringLiteral("audio-unavailable"),
      QStringLiteral("unavailable"),
      QStringLiteral("timeout"),
      QStringLiteral("invalid-response"),
      QStringLiteral("stream-limit"),
      QStringLiteral("policy-unavailable"),
      QStringLiteral("subscriber-unavailable"),
      QStringLiteral("subscriber-ended"),
      QStringLiteral("invalid-subscriber-event"),
      QStringLiteral("broker-internal-error"),
      QStringLiteral("runtime-unavailable"),
      QStringLiteral("runtime-state-invalid"),
      QStringLiteral("broker-already-running"),
  };
  return values;
}

const QSet<QString> &goxlrFailedReasonIds() {
  static const QSet<QString> values = {
      QStringLiteral("timeout"),
      QStringLiteral("response-too-large"),
      QStringLiteral("invalid-response"),
      QStringLiteral("status-unavailable"),
  };
  return values;
}

bool endpointToken(const QString &direction, const QString &device) {
  if (direction == QStringLiteral("output"))
    return tokenMatches(device, outputTokenExpression());
  if (direction == QStringLiteral("input"))
    return tokenMatches(device, inputTokenExpression());
  return false;
}

bool validAbsoluteChooserPath(const QString &path) {
  if (path.isEmpty() || !QFileInfo(path).isAbsolute() ||
      path.contains(QChar::Null) || path.toUtf8().size() > kMaximumPathBytes)
    return false;
  for (qsizetype index = 0; index < path.size(); ++index) {
    const QChar character = path.at(index);
    if (character.isHighSurrogate()) {
      if (index + 1 >= path.size() || !path.at(index + 1).isLowSurrogate())
        return false;
      ++index;
    } else if (character.isLowSurrogate() || character.unicode() < 0x20 ||
               character.unicode() == 0x7f) {
      return false;
    }
  }
  return true;
}

bool decodeEndpointArray(const QJsonValue &value, const QString &direction,
                         QVariantList *items, QSet<QString> *identities) {
  if (!value.isArray() || !items || !identities)
    return false;
  const QJsonArray array = value.toArray();
  if (array.size() > kMaximumEndpoints)
    return false;
  QVariantList decoded;
  QSet<QString> seen;
  int defaults = 0;
  decoded.reserve(array.size());
  for (const auto entry : array) {
    if (!entry.isObject())
      return false;
    const QJsonObject object = entry.toObject();
    if (!exactKeys(object,
                   {"id", "label", "default", "volumePercent", "muted"}))
      return false;
    QString id;
    QString label;
    qint64 volume = 0;
    if (!boundedText(object.value(QStringLiteral("id")), 31, &id, false) ||
        !endpointToken(direction, id) || seen.contains(id) ||
        !boundedText(object.value(QStringLiteral("label")), kMaximumLabelBytes,
                     &label) ||
        !object.value(QStringLiteral("default")).isBool() ||
        !exactInteger(object.value(QStringLiteral("volumePercent")), 0, 999,
                      &volume) ||
        !object.value(QStringLiteral("muted")).isBool())
      return false;
    const bool isDefault = object.value(QStringLiteral("default")).toBool();
    defaults += isDefault ? 1 : 0;
    if (defaults > 1)
      return false;
    seen.insert(id);
    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("label"), label);
    item.insert(QStringLiteral("default"), isDefault);
    item.insert(QStringLiteral("volumePercent"), static_cast<int>(volume));
    item.insert(QStringLiteral("muted"),
                object.value(QStringLiteral("muted")).toBool());
    item.insert(QStringLiteral("levelControlAvailable"), true);
    decoded.append(item);
  }
  *items = std::move(decoded);
  *identities = std::move(seen);
  return true;
}

bool decodeStreams(const QJsonValue &value, const QSet<QString> &outputs,
                   const QSet<QString> &inputs, QVariantList *items) {
  if (!value.isArray() || !items)
    return false;
  const QJsonArray array = value.toArray();
  if (array.size() > kMaximumStreams)
    return false;
  QVariantList decoded;
  QSet<QString> seen;
  decoded.reserve(array.size());
  for (const auto entry : array) {
    if (!entry.isObject())
      return false;
    const QJsonObject object = entry.toObject();
    if (!exactKeys(object, {"id", "direction", "label", "target",
                            "volumePercent", "muted", "processRuleAvailable"}))
      return false;
    QString id;
    QString direction;
    QString label;
    QString target;
    qint64 volume = 0;
    if (!boundedText(object.value(QStringLiteral("id")), 31, &id, false) ||
        !tokenMatches(id, streamTokenExpression()) || seen.contains(id) ||
        !boundedText(object.value(QStringLiteral("direction")), 16, &direction,
                     false) ||
        !boundedText(object.value(QStringLiteral("label")), kMaximumLabelBytes,
                     &label) ||
        !boundedText(object.value(QStringLiteral("target")), 31, &target,
                     false) ||
        !exactInteger(object.value(QStringLiteral("volumePercent")), 0, 999,
                      &volume) ||
        !object.value(QStringLiteral("muted")).isBool() ||
        !object.value(QStringLiteral("processRuleAvailable")).isBool())
      return false;
    const QRegularExpressionMatch streamMatch =
        streamTokenExpression().match(id);
    bool indexOk = false;
    const qlonglong streamIndex = streamMatch.captured(2).toLongLong(&indexOk);
    if (!indexOk || streamIndex < 0 || streamIndex > 2147483647LL ||
        (direction != QStringLiteral("playback") &&
         direction != QStringLiteral("recording")) ||
        streamMatch.captured(1) != direction)
      return false;
    const bool targetValid =
        target == QStringLiteral("unavailable") ||
        (direction == QStringLiteral("playback") && outputs.contains(target)) ||
        (direction == QStringLiteral("recording") && inputs.contains(target));
    if (!targetValid)
      return false;
    seen.insert(id);
    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("direction"), direction);
    item.insert(QStringLiteral("label"), label);
    item.insert(QStringLiteral("target"), target);
    item.insert(QStringLiteral("volumePercent"), static_cast<int>(volume));
    item.insert(QStringLiteral("muted"),
                object.value(QStringLiteral("muted")).toBool());
    const bool processRuleAvailable =
        object.value(QStringLiteral("processRuleAvailable")).toBool();
    item.insert(QStringLiteral("processRuleAvailable"), processRuleAvailable);
    item.insert(QStringLiteral("moveAvailable"),
                processRuleAvailable &&
                    target != QStringLiteral("unavailable"));
    item.insert(QStringLiteral("levelControlAvailable"), processRuleAvailable);
    decoded.append(item);
  }
  *items = std::move(decoded);
  return true;
}

bool decodeCards(const QJsonValue &value, QVariantList *items) {
  if (!value.isArray() || !items)
    return false;
  const QJsonArray array = value.toArray();
  if (array.size() > kMaximumCards)
    return false;
  QVariantList decoded;
  QSet<QString> seen;
  decoded.reserve(array.size());
  for (const auto entry : array) {
    if (!entry.isObject())
      return false;
    const QJsonObject object = entry.toObject();
    if (!exactKeys(object, {"id", "label", "activeProfile"}))
      return false;
    QString id;
    QString label;
    QString profile;
    if (!boundedText(object.value(QStringLiteral("id")), 31, &id, false) ||
        !tokenMatches(id, cardTokenExpression()) || seen.contains(id) ||
        !boundedText(object.value(QStringLiteral("label")), kMaximumLabelBytes,
                     &label) ||
        !boundedText(object.value(QStringLiteral("activeProfile")),
                     kMaximumLabelBytes, &profile))
      return false;
    seen.insert(id);
    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("label"), label);
    item.insert(QStringLiteral("activeProfile"), profile);
    decoded.append(item);
  }
  *items = std::move(decoded);
  return true;
}

QHash<QString, QString>
endpointLabels(const AudioPresentationSnapshot &snapshot) {
  QHash<QString, QString> labels;
  for (const QVariant &entry : snapshot.outputs) {
    const QVariantMap item = entry.toMap();
    labels.insert(item.value(QStringLiteral("id")).toString(),
                  item.value(QStringLiteral("label")).toString());
  }
  for (const QVariant &entry : snapshot.inputs) {
    const QVariantMap item = entry.toMap();
    labels.insert(item.value(QStringLiteral("id")).toString(),
                  item.value(QStringLiteral("label")).toString());
  }
  return labels;
}

bool decodeDefaultCommon(const QByteArray &payload, const QString &schema,
                         const QString &status, bool applied,
                         const QString &expectedDirection,
                         const QString &expectedDevice, bool *changed,
                         QString *errorId) {
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId))
    return false;
  if (!exactKeys(object,
                 {"schema", "status", "direction", "device", "label", "changed",
                  "stateAuthority", "requiresAcknowledgement", "applied"}) ||
      object.value(QStringLiteral("schema")).toString() != schema ||
      object.value(QStringLiteral("status")).toString() != status ||
      object.value(QStringLiteral("direction")).toString() !=
          expectedDirection ||
      object.value(QStringLiteral("device")).toString() != expectedDevice ||
      !endpointToken(expectedDirection, expectedDevice) ||
      !boundedText(object.value(QStringLiteral("label")), kMaximumLabelBytes,
                   nullptr) ||
      !object.value(QStringLiteral("changed")).isBool() ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      object.value(QStringLiteral("requiresAcknowledgement")).toString() !=
          QString::fromLatin1(kDefaultAcknowledgement) ||
      !object.value(QStringLiteral("applied")).isBool() ||
      object.value(QStringLiteral("applied")).toBool() != applied)
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (changed)
    *changed = object.value(QStringLiteral("changed")).toBool();
  return true;
}

} // namespace

namespace AudioContracts {

bool decodeInventory(const QByteArray &payload,
                     AudioPresentationSnapshot *snapshot, QString *errorId) {
  if (!snapshot)
    return fail(errorId, QStringLiteral("contract-invalid"));
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumInventoryBytes, &object, errorId))
    return false;
  if (!exactKeys(object, {"schema", "stateAuthority", "available", "reason",
                          "mutationAvailable", "outputs", "inputs", "streams",
                          "cards", "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-inventory/v1") ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      !object.value(QStringLiteral("available")).isBool() ||
      !object.value(QStringLiteral("mutationAvailable")).isBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));

  AudioPresentationSnapshot decoded;
  decoded.available = object.value(QStringLiteral("available")).toBool();
  decoded.mutationAvailable =
      object.value(QStringLiteral("mutationAvailable")).toBool();
  if (decoded.available) {
    if (!object.value(QStringLiteral("reason")).isNull() ||
        !decoded.mutationAvailable)
      return fail(errorId, QStringLiteral("contract-invalid"));
  } else {
    if (!boundedText(object.value(QStringLiteral("reason")), 64,
                     &decoded.reason, false) ||
        decoded.mutationAvailable)
      return fail(errorId, QStringLiteral("contract-invalid"));
  }

  QSet<QString> outputs;
  QSet<QString> inputs;
  if (!decodeEndpointArray(object.value(QStringLiteral("outputs")),
                           QStringLiteral("output"), &decoded.outputs,
                           &outputs) ||
      !decodeEndpointArray(object.value(QStringLiteral("inputs")),
                           QStringLiteral("input"), &decoded.inputs, &inputs) ||
      !decodeStreams(object.value(QStringLiteral("streams")), outputs, inputs,
                     &decoded.streams) ||
      !decodeCards(object.value(QStringLiteral("cards")), &decoded.cards))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (!decoded.available &&
      (!decoded.outputs.isEmpty() || !decoded.inputs.isEmpty() ||
       !decoded.streams.isEmpty() || !decoded.cards.isEmpty()))
    return fail(errorId, QStringLiteral("contract-invalid"));
  *snapshot = std::move(decoded);
  return true;
}

bool decodePolicy(const QByteArray &payload,
                  AudioPresentationSnapshot *snapshot, QString *errorId) {
  if (!snapshot)
    return fail(errorId, QStringLiteral("contract-invalid"));
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumPolicyBytes, &object, errorId))
    return false;
  if (!exactKeys(object, {"schema", "generation", "rules", "present",
                          "systemDefaultFallback", "enforcementAvailable",
                          "enforcementReason", "persistentPidRules",
                          "existingStreamMigration"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-route-policy-view/v1") ||
      !exactInteger(object.value(QStringLiteral("generation")), 0,
                    4294967295LL) ||
      !object.value(QStringLiteral("rules")).isArray() ||
      !object.value(QStringLiteral("present")).isBool() ||
      !object.value(QStringLiteral("systemDefaultFallback")).isBool() ||
      !object.value(QStringLiteral("systemDefaultFallback")).toBool() ||
      !object.value(QStringLiteral("enforcementAvailable")).isBool() ||
      object.value(QStringLiteral("enforcementAvailable")).toBool() ||
      object.value(QStringLiteral("enforcementReason")).toString() !=
          QStringLiteral("audio-route-broker-not-integrated") ||
      !object.value(QStringLiteral("persistentPidRules")).isBool() ||
      object.value(QStringLiteral("persistentPidRules")).toBool() ||
      !object.value(QStringLiteral("existingStreamMigration")).isBool() ||
      object.value(QStringLiteral("existingStreamMigration")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));

  const QJsonArray rules = object.value(QStringLiteral("rules")).toArray();
  const bool present = object.value(QStringLiteral("present")).toBool();
  qint64 generation = 0;
  (void)exactInteger(object.value(QStringLiteral("generation")), 0,
                     4294967295LL, &generation);
  if (rules.size() > kMaximumRules ||
      (!present && (generation != 0 || !rules.isEmpty())))
    return fail(errorId, QStringLiteral("contract-invalid"));

  const QHash<QString, QString> labels = endpointLabels(*snapshot);
  QVariantList decoded;
  QSet<QString> identities;
  QSet<QString> semanticKeys;
  decoded.reserve(rules.size());
  for (const auto entry : rules) {
    if (!entry.isObject())
      return fail(errorId, QStringLiteral("contract-invalid"));
    const QJsonObject rule = entry.toObject();
    if (!exactKeys(rule, {"id", "match", "direction", "device", "enabled"}) ||
        !rule.value(QStringLiteral("match")).isObject() ||
        !rule.value(QStringLiteral("enabled")).isBool())
      return fail(errorId, QStringLiteral("contract-invalid"));
    const QJsonObject match = rule.value(QStringLiteral("match")).toObject();
    if (!exactKeys(match, {"type", "displayPath"}))
      return fail(errorId, QStringLiteral("contract-invalid"));
    QString id;
    QString type;
    QString displayPath;
    QString direction;
    QString device;
    if (!boundedText(rule.value(QStringLiteral("id")), 23, &id, false) ||
        !tokenMatches(id, ruleTokenExpression()) || identities.contains(id) ||
        !boundedText(match.value(QStringLiteral("type")), 15, &type, false) ||
        (type != QStringLiteral("executable") &&
         type != QStringLiteral("directory")) ||
        !boundedText(match.value(QStringLiteral("displayPath")),
                     kMaximumPathBytes, &displayPath, false) ||
        !boundedText(rule.value(QStringLiteral("direction")), 7, &direction,
                     false) ||
        !boundedText(rule.value(QStringLiteral("device")), 31, &device,
                     false) ||
        !endpointToken(direction, device))
      return fail(errorId, QStringLiteral("contract-invalid"));
    const QString semanticKey =
        type + QChar::Null + displayPath + QChar::Null + direction;
    if (semanticKeys.contains(semanticKey))
      return fail(errorId, QStringLiteral("contract-invalid"));
    identities.insert(id);
    semanticKeys.insert(semanticKey);
    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("matchType"), type);
    item.insert(QStringLiteral("displayPath"), displayPath);
    item.insert(QStringLiteral("direction"), direction);
    item.insert(QStringLiteral("device"), device);
    item.insert(QStringLiteral("enabled"),
                rule.value(QStringLiteral("enabled")).toBool());
    item.insert(QStringLiteral("deviceAvailable"), labels.contains(device));
    item.insert(QStringLiteral("deviceLabel"), labels.value(device));
    decoded.append(item);
  }
  snapshot->routeRules = std::move(decoded);
  snapshot->routeEnforcementAvailable = false;
  return true;
}

bool decodeBrokerStatus(const QByteArray &payload,
                        AudioPresentationSnapshot *snapshot, QString *errorId) {
  if (!snapshot)
    return fail(errorId, QStringLiteral("contract-invalid"));
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumBrokerStatusBytes, &object, errorId))
    return false;
  if (!exactKeys(object,
                 {"schema", "status", "mode", "stateAuthority", "capable",
                  "active", "enforcementAvailable", "reason",
                  "policyGeneration", "baselineStreams", "persistentPidRules",
                  "existingStreamMigration", "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-route-broker-status/v1") ||
      object.value(QStringLiteral("mode")).toString() !=
          QStringLiteral("new-streams-only") ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      !object.value(QStringLiteral("capable")).isBool() ||
      !object.value(QStringLiteral("capable")).toBool() ||
      !object.value(QStringLiteral("active")).isBool() ||
      !object.value(QStringLiteral("enforcementAvailable")).isBool() ||
      !exactInteger(object.value(QStringLiteral("policyGeneration")), 0,
                    4294967295LL) ||
      !exactInteger(object.value(QStringLiteral("baselineStreams")), 0,
                    kMaximumStreams) ||
      !object.value(QStringLiteral("persistentPidRules")).isBool() ||
      object.value(QStringLiteral("persistentPidRules")).toBool() ||
      !object.value(QStringLiteral("existingStreamMigration")).isBool() ||
      object.value(QStringLiteral("existingStreamMigration")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));

  QString status;
  if (!boundedText(object.value(QStringLiteral("status")), 15, &status,
                   false) ||
      (status != QStringLiteral("Ready") &&
       status != QStringLiteral("Unavailable")))
    return fail(errorId, QStringLiteral("contract-invalid"));
  QString reason;
  if (object.value(QStringLiteral("reason")).isNull()) {
    reason.clear();
  } else if (!boundedText(object.value(QStringLiteral("reason")), 47, &reason,
                          false) ||
             !brokerReasonIds().contains(reason)) {
    return fail(errorId, QStringLiteral("contract-invalid"));
  }

  const bool active = object.value(QStringLiteral("active")).toBool();
  const bool enforcement =
      object.value(QStringLiteral("enforcementAvailable")).toBool();
  const bool ready = status == QStringLiteral("Ready");
  const bool validReady =
      ready && ((active && enforcement && reason.isEmpty()) ||
                (!active && !enforcement &&
                 reason == QStringLiteral("broker-not-running")));
  const bool validUnavailable = !ready && !active && !enforcement &&
                                !reason.isEmpty() &&
                                reason != QStringLiteral("broker-not-running");
  if (!validReady && !validUnavailable)
    return fail(errorId, QStringLiteral("contract-invalid"));

  snapshot->routeBrokerAvailable = ready;
  snapshot->routeBrokerActive = active;
  snapshot->routeBrokerReason = reason;
  snapshot->routeEnforcementAvailable = enforcement;
  return true;
}

bool decodeGoxlrStatus(const QByteArray &payload,
                       AudioPresentationSnapshot *snapshot, QString *errorId) {
  if (!snapshot)
    return fail(errorId, QStringLiteral("contract-invalid"));
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumGoxlrStatusBytes, &object, errorId))
    return false;
  if (!exactKeys(object,
                 {"schema", "status", "reason", "providerActive", "deviceCount",
                  "truncated", "devices", "stateAuthority", "hardwareReadback",
                  "hardwareExactRollback", "mutationAvailable", "readOnly",
                  "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-goxlr-status/v1") ||
      !object.value(QStringLiteral("providerActive")).isBool() ||
      !object.value(QStringLiteral("truncated")).isBool() ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("provider-profile-model") ||
      !object.value(QStringLiteral("hardwareReadback")).isBool() ||
      object.value(QStringLiteral("hardwareReadback")).toBool() ||
      !object.value(QStringLiteral("hardwareExactRollback")).isBool() ||
      object.value(QStringLiteral("hardwareExactRollback")).toBool() ||
      !object.value(QStringLiteral("mutationAvailable")).isBool() ||
      object.value(QStringLiteral("mutationAvailable")).toBool() ||
      !object.value(QStringLiteral("readOnly")).isBool() ||
      !object.value(QStringLiteral("readOnly")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));

  QString status;
  if (!boundedText(object.value(QStringLiteral("status")), 15, &status,
                   false) ||
      (status != QStringLiteral("Ready") &&
       status != QStringLiteral("Inactive") &&
       status != QStringLiteral("Unavailable") &&
       status != QStringLiteral("Failed")))
    return fail(errorId, QStringLiteral("contract-invalid"));
  QString reason;
  if (object.value(QStringLiteral("reason")).isNull()) {
    reason.clear();
  } else if (!boundedText(object.value(QStringLiteral("reason")), 47, &reason,
                          false)) {
    return fail(errorId, QStringLiteral("contract-invalid"));
  }

  qint64 declaredCount = 0;
  if (!exactInteger(object.value(QStringLiteral("deviceCount")), 0,
                    kMaximumGoxlrDevices, &declaredCount) ||
      !object.value(QStringLiteral("devices")).isArray())
    return fail(errorId, QStringLiteral("contract-invalid"));
  const QJsonArray devices = object.value(QStringLiteral("devices")).toArray();
  if (devices.size() != declaredCount || devices.size() > kMaximumGoxlrDevices)
    return fail(errorId, QStringLiteral("contract-invalid"));

  QVariantList decoded;
  QSet<QString> identities;
  decoded.reserve(devices.size());
  for (const QJsonValue &entry : devices) {
    if (!entry.isObject())
      return fail(errorId, QStringLiteral("contract-invalid"));
    const QJsonObject device = entry.toObject();
    if (!exactKeys(device, {"id", "model", "systemOutputSupported",
                            "controlAvailable"}))
      return fail(errorId, QStringLiteral("contract-invalid"));
    QString id;
    QString model;
    if (!boundedText(device.value(QStringLiteral("id")), 15, &id, false) ||
        !tokenMatches(id, goxlrDeviceExpression()) || identities.contains(id) ||
        !boundedText(device.value(QStringLiteral("model")), 15, &model,
                     false) ||
        (model != QStringLiteral("GoXLR Mini") &&
         model != QStringLiteral("GoXLR") &&
         model != QStringLiteral("Unknown")) ||
        !device.value(QStringLiteral("systemOutputSupported")).isBool() ||
        !device.value(QStringLiteral("controlAvailable")).isBool() ||
        device.value(QStringLiteral("controlAvailable")).toBool())
      return fail(errorId, QStringLiteral("contract-invalid"));
    identities.insert(id);
    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("model"), model);
    item.insert(QStringLiteral("systemOutputSupported"),
                device.value(QStringLiteral("systemOutputSupported")).toBool());
    decoded.append(item);
  }
  std::sort(decoded.begin(), decoded.end(),
            [](const QVariant &left, const QVariant &right) {
              return left.toMap().value(QStringLiteral("id")).toString() <
                     right.toMap().value(QStringLiteral("id")).toString();
            });
  for (QVariant &entry : decoded) {
    QVariantMap projected = entry.toMap();
    projected.remove(QStringLiteral("id"));
    entry = projected;
  }

  const bool active = object.value(QStringLiteral("providerActive")).toBool();
  const bool truncated = object.value(QStringLiteral("truncated")).toBool();
  const bool ready = status == QStringLiteral("Ready");
  const bool validReady = ready && active && reason.isEmpty() &&
                          (!truncated || declaredCount == kMaximumGoxlrDevices);
  const bool validUnavailable = status == QStringLiteral("Unavailable") &&
                                !active &&
                                reason == QStringLiteral("adapter-unavailable");
  const bool validInactive = status == QStringLiteral("Inactive") && !active &&
                             reason == QStringLiteral("provider-inactive");
  const bool validFailed = status == QStringLiteral("Failed") && !active &&
                           goxlrFailedReasonIds().contains(reason);
  if ((!validReady && !validUnavailable && !validInactive && !validFailed) ||
      (!ready && (declaredCount != 0 || !devices.isEmpty() || truncated)))
    return fail(errorId, QStringLiteral("contract-invalid"));

  snapshot->goxlrStatus = status;
  snapshot->goxlrReason = reason;
  snapshot->goxlrProviderActive = active;
  snapshot->goxlrTruncated = truncated;
  snapshot->goxlrDevices = std::move(decoded);
  return true;
}

bool decodeStreamMovePlan(const QByteArray &payload,
                          const QString &expectedStream,
                          const QString &expectedOriginalDevice,
                          const QString &expectedRequestedDevice,
                          QString *cohort, bool *changed, QString *errorId) {
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId))
    return false;
  const QRegularExpressionMatch streamMatch =
      streamTokenExpression().match(expectedStream);
  bool indexOk = false;
  const qlonglong streamIndex = streamMatch.captured(2).toLongLong(&indexOk);
  const QString direction =
      streamMatch.captured(1) == QStringLiteral("playback")
          ? QStringLiteral("output")
          : QStringLiteral("input");
  QString decodedCohort;
  if (!streamMatch.hasMatch() || !indexOk || streamIndex < 0 ||
      streamIndex > 2147483647LL ||
      !exactKeys(object,
                 {"schema", "status", "stream", "direction", "originalDevice",
                  "requestedDevice", "cohort", "changed", "stateAuthority",
                  "requiresAcknowledgement", "singleStream",
                  "postflightRequired", "rollbackOnUnverified", "applied",
                  "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral(
              "synapse.settings.audio-existing-stream-move-plan/v1") ||
      object.value(QStringLiteral("status")).toString() !=
          QStringLiteral("Planned") ||
      object.value(QStringLiteral("stream")).toString() != expectedStream ||
      object.value(QStringLiteral("direction")).toString() != direction ||
      object.value(QStringLiteral("originalDevice")).toString() !=
          expectedOriginalDevice ||
      object.value(QStringLiteral("requestedDevice")).toString() !=
          expectedRequestedDevice ||
      !endpointToken(direction, expectedOriginalDevice) ||
      !endpointToken(direction, expectedRequestedDevice) ||
      !boundedText(object.value(QStringLiteral("cohort")), 21, &decodedCohort,
                   false) ||
      !tokenMatches(decodedCohort, streamMoveCohortExpression()) ||
      !object.value(QStringLiteral("changed")).isBool() ||
      object.value(QStringLiteral("changed")).toBool() !=
          (expectedOriginalDevice != expectedRequestedDevice) ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      object.value(QStringLiteral("requiresAcknowledgement")).toString() !=
          QString::fromLatin1(kStreamMoveAcknowledgement) ||
      !object.value(QStringLiteral("singleStream")).isBool() ||
      !object.value(QStringLiteral("singleStream")).toBool() ||
      !object.value(QStringLiteral("postflightRequired")).isBool() ||
      !object.value(QStringLiteral("postflightRequired")).toBool() ||
      !object.value(QStringLiteral("rollbackOnUnverified")).isBool() ||
      !object.value(QStringLiteral("rollbackOnUnverified")).toBool() ||
      !object.value(QStringLiteral("applied")).isBool() ||
      object.value(QStringLiteral("applied")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (cohort)
    *cohort = decodedCohort;
  if (changed)
    *changed = object.value(QStringLiteral("changed")).toBool();
  return true;
}

bool decodeStreamMoveReceipt(const QByteArray &payload,
                             const QString &expectedStream,
                             const QString &expectedOriginalDevice,
                             const QString &expectedRequestedDevice,
                             QString *status, QString *reason, bool *changed,
                             bool *rollbackAttempted, bool *rollbackVerified,
                             QString *errorId) {
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId))
    return false;
  const QRegularExpressionMatch streamMatch =
      streamTokenExpression().match(expectedStream);
  bool indexOk = false;
  const qlonglong streamIndex = streamMatch.captured(2).toLongLong(&indexOk);
  const QString direction =
      streamMatch.captured(1) == QStringLiteral("playback")
          ? QStringLiteral("output")
          : QStringLiteral("input");
  if (!streamMatch.hasMatch() || !indexOk || streamIndex < 0 ||
      streamIndex > 2147483647LL ||
      !exactKeys(object,
                 {"schema", "status", "reason", "stream", "direction",
                  "originalDevice", "requestedDevice", "changed", "moveApplied",
                  "verified", "rollbackAttempted", "rollbackVerified",
                  "stateAuthority", "requiresAcknowledgement", "singleStream",
                  "policyApplied", "persistentRuleCreated",
                  "existingStreamMovement", "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral(
              "synapse.settings.audio-existing-stream-move-receipt/v1") ||
      object.value(QStringLiteral("stream")).toString() != expectedStream ||
      object.value(QStringLiteral("direction")).toString() != direction ||
      object.value(QStringLiteral("originalDevice")).toString() !=
          expectedOriginalDevice ||
      object.value(QStringLiteral("requestedDevice")).toString() !=
          expectedRequestedDevice ||
      !endpointToken(direction, expectedOriginalDevice) ||
      !endpointToken(direction, expectedRequestedDevice) ||
      !object.value(QStringLiteral("changed")).isBool() ||
      !object.value(QStringLiteral("moveApplied")).isBool() ||
      !object.value(QStringLiteral("verified")).isBool() ||
      !object.value(QStringLiteral("rollbackAttempted")).isBool() ||
      !object.value(QStringLiteral("rollbackVerified")).isBool() ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      object.value(QStringLiteral("requiresAcknowledgement")).toString() !=
          QString::fromLatin1(kStreamMoveAcknowledgement) ||
      !object.value(QStringLiteral("singleStream")).isBool() ||
      !object.value(QStringLiteral("singleStream")).toBool() ||
      !object.value(QStringLiteral("policyApplied")).isBool() ||
      object.value(QStringLiteral("policyApplied")).toBool() ||
      !object.value(QStringLiteral("persistentRuleCreated")).isBool() ||
      object.value(QStringLiteral("persistentRuleCreated")).toBool() ||
      !object.value(QStringLiteral("existingStreamMovement")).isBool() ||
      !object.value(QStringLiteral("existingStreamMovement")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));

  const QString decodedStatus =
      object.value(QStringLiteral("status")).toString();
  if (!QStringList({QStringLiteral("Applied"), QStringLiteral("AlreadyRouted"),
                    QStringLiteral("Refused"), QStringLiteral("Failed")})
           .contains(decodedStatus))
    return fail(errorId, QStringLiteral("contract-invalid"));
  QString decodedReason;
  if (object.value(QStringLiteral("reason")).isNull()) {
    decodedReason.clear();
  } else if (!boundedText(object.value(QStringLiteral("reason")), 47,
                          &decodedReason, false)) {
    return fail(errorId, QStringLiteral("contract-invalid"));
  }
  const bool valueChanged = object.value(QStringLiteral("changed")).toBool();
  const bool moveApplied = object.value(QStringLiteral("moveApplied")).toBool();
  const bool valueVerified = object.value(QStringLiteral("verified")).toBool();
  const bool attempted =
      object.value(QStringLiteral("rollbackAttempted")).toBool();
  const bool restored =
      object.value(QStringLiteral("rollbackVerified")).toBool();
  const bool validApplied =
      decodedStatus == QStringLiteral("Applied") && decodedReason.isEmpty() &&
      expectedOriginalDevice != expectedRequestedDevice && valueChanged &&
      moveApplied && valueVerified && !attempted && !restored;
  const bool validAlready = decodedStatus == QStringLiteral("AlreadyRouted") &&
                            decodedReason.isEmpty() &&
                            expectedOriginalDevice == expectedRequestedDevice &&
                            !valueChanged && !moveApplied && valueVerified &&
                            !attempted && !restored;
  const bool validRefused =
      decodedStatus == QStringLiteral("Refused") &&
      streamMoveRefusedReasonIds().contains(decodedReason) && !valueChanged &&
      !moveApplied && !valueVerified && !attempted && !restored;
  const bool validFailed =
      decodedStatus == QStringLiteral("Failed") &&
      expectedOriginalDevice != expectedRequestedDevice &&
      streamMoveFailedReasonIds().contains(decodedReason) && !valueChanged &&
      !moveApplied && !valueVerified && (!restored || attempted) &&
      (!attempted || decodedReason == QStringLiteral("move-timeout") ||
       decodedReason == QStringLiteral("move-failed"));
  if (!validApplied && !validAlready && !validRefused && !validFailed)
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (status)
    *status = decodedStatus;
  if (reason)
    *reason = decodedReason;
  if (changed)
    *changed = valueChanged;
  if (rollbackAttempted)
    *rollbackAttempted = attempted;
  if (rollbackVerified)
    *rollbackVerified = restored;
  return true;
}

bool decodeDefaultPlan(const QByteArray &payload,
                       const QString &expectedDirection,
                       const QString &expectedDevice, bool *changed,
                       QString *errorId) {
  return decodeDefaultCommon(
      payload, QStringLiteral("synapse.settings.audio-default-plan/v1"),
      QStringLiteral("Planned"), false, expectedDirection, expectedDevice,
      changed, errorId);
}

bool decodeDefaultReceipt(const QByteArray &payload,
                          const QString &expectedDirection,
                          const QString &expectedDevice, bool *changed,
                          QString *errorId) {
  return decodeDefaultCommon(
      payload, QStringLiteral("synapse.settings.audio-default-receipt/v1"),
      QStringLiteral("Applied"), true, expectedDirection, expectedDevice,
      changed, errorId);
}

bool decodeControlPlan(const QByteArray &payload, const QString &expectedTarget,
                       const QString &expectedControl,
                       const QVariant &expectedOriginalValue,
                       const QVariant &expectedRequestedValue, QString *cohort,
                       bool *changed, QString *errorId) {
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId))
    return false;
  const QString targetType = audioControlTargetType(expectedTarget);
  QString decodedCohort;
  const bool expectedChanged = expectedOriginalValue != expectedRequestedValue;
  if (targetType.isEmpty() ||
      (expectedControl != QStringLiteral("volume") &&
       expectedControl != QStringLiteral("mute")) ||
      !exactKeys(object, {"schema",
                          "status",
                          "target",
                          "targetType",
                          "control",
                          "originalValue",
                          "requestedValue",
                          "cohort",
                          "changed",
                          "stateAuthority",
                          "requiresAcknowledgement",
                          "singleTarget",
                          "safeVolumeMaximumPercent",
                          "postflightRequired",
                          "rollbackOnUnverified",
                          "playbackStarted",
                          "captureStarted",
                          "profileChanged",
                          "routingChanged",
                          "applied",
                          "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-control-plan/v1") ||
      object.value(QStringLiteral("status")).toString() !=
          QStringLiteral("Planned") ||
      object.value(QStringLiteral("target")).toString() != expectedTarget ||
      object.value(QStringLiteral("targetType")).toString() != targetType ||
      object.value(QStringLiteral("control")).toString() != expectedControl ||
      !controlValueMatches(object.value(QStringLiteral("originalValue")),
                           expectedControl, expectedOriginalValue, true) ||
      !controlValueMatches(object.value(QStringLiteral("requestedValue")),
                           expectedControl, expectedRequestedValue, false) ||
      !boundedText(object.value(QStringLiteral("cohort")), 31, &decodedCohort,
                   false) ||
      !tokenMatches(decodedCohort, controlCohortExpression()) ||
      !object.value(QStringLiteral("changed")).isBool() ||
      object.value(QStringLiteral("changed")).toBool() != expectedChanged ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      object.value(QStringLiteral("requiresAcknowledgement")).toString() !=
          QString::fromLatin1(kControlAcknowledgement) ||
      !object.value(QStringLiteral("singleTarget")).isBool() ||
      !object.value(QStringLiteral("singleTarget")).toBool() ||
      !exactInteger(object.value(QStringLiteral("safeVolumeMaximumPercent")),
                    kSafeVolumeMaximumPercent, kSafeVolumeMaximumPercent) ||
      !object.value(QStringLiteral("postflightRequired")).isBool() ||
      !object.value(QStringLiteral("postflightRequired")).toBool() ||
      !object.value(QStringLiteral("rollbackOnUnverified")).isBool() ||
      !object.value(QStringLiteral("rollbackOnUnverified")).toBool() ||
      !object.value(QStringLiteral("playbackStarted")).isBool() ||
      object.value(QStringLiteral("playbackStarted")).toBool() ||
      !object.value(QStringLiteral("captureStarted")).isBool() ||
      object.value(QStringLiteral("captureStarted")).toBool() ||
      !object.value(QStringLiteral("profileChanged")).isBool() ||
      object.value(QStringLiteral("profileChanged")).toBool() ||
      !object.value(QStringLiteral("routingChanged")).isBool() ||
      object.value(QStringLiteral("routingChanged")).toBool() ||
      !object.value(QStringLiteral("applied")).isBool() ||
      object.value(QStringLiteral("applied")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (cohort)
    *cohort = decodedCohort;
  if (changed)
    *changed = expectedChanged;
  return true;
}

bool decodeControlReceipt(const QByteArray &payload,
                          const QString &expectedTarget,
                          const QString &expectedControl,
                          const QVariant &expectedOriginalValue,
                          const QVariant &expectedRequestedValue,
                          QString *status, QString *reason, bool *changed,
                          bool *rollbackAttempted, bool *rollbackVerified,
                          QString *errorId) {
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId))
    return false;
  const QString targetType = audioControlTargetType(expectedTarget);
  const bool expectedChanged = expectedOriginalValue != expectedRequestedValue;
  if (targetType.isEmpty() ||
      (expectedControl != QStringLiteral("volume") &&
       expectedControl != QStringLiteral("mute")) ||
      !exactKeys(object, {"schema",
                          "status",
                          "reason",
                          "target",
                          "targetType",
                          "control",
                          "originalValue",
                          "requestedValue",
                          "changed",
                          "mutationAttempted",
                          "verified",
                          "rollbackAttempted",
                          "rollbackVerified",
                          "stateAuthority",
                          "requiresAcknowledgement",
                          "singleTarget",
                          "safeVolumeMaximumPercent",
                          "playbackStarted",
                          "captureStarted",
                          "profileChanged",
                          "routingChanged",
                          "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-control-receipt/v1") ||
      object.value(QStringLiteral("target")).toString() != expectedTarget ||
      object.value(QStringLiteral("targetType")).toString() != targetType ||
      object.value(QStringLiteral("control")).toString() != expectedControl ||
      !controlValueMatches(object.value(QStringLiteral("originalValue")),
                           expectedControl, expectedOriginalValue, true) ||
      !controlValueMatches(object.value(QStringLiteral("requestedValue")),
                           expectedControl, expectedRequestedValue, false) ||
      !object.value(QStringLiteral("changed")).isBool() ||
      !object.value(QStringLiteral("mutationAttempted")).isBool() ||
      !object.value(QStringLiteral("verified")).isBool() ||
      !object.value(QStringLiteral("rollbackAttempted")).isBool() ||
      !object.value(QStringLiteral("rollbackVerified")).isBool() ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      object.value(QStringLiteral("requiresAcknowledgement")).toString() !=
          QString::fromLatin1(kControlAcknowledgement) ||
      !object.value(QStringLiteral("singleTarget")).isBool() ||
      !object.value(QStringLiteral("singleTarget")).toBool() ||
      !exactInteger(object.value(QStringLiteral("safeVolumeMaximumPercent")),
                    kSafeVolumeMaximumPercent, kSafeVolumeMaximumPercent) ||
      !object.value(QStringLiteral("playbackStarted")).isBool() ||
      object.value(QStringLiteral("playbackStarted")).toBool() ||
      !object.value(QStringLiteral("captureStarted")).isBool() ||
      object.value(QStringLiteral("captureStarted")).toBool() ||
      !object.value(QStringLiteral("profileChanged")).isBool() ||
      object.value(QStringLiteral("profileChanged")).toBool() ||
      !object.value(QStringLiteral("routingChanged")).isBool() ||
      object.value(QStringLiteral("routingChanged")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));

  const QString decodedStatus =
      object.value(QStringLiteral("status")).toString();
  QString decodedReason;
  if (object.value(QStringLiteral("reason")).isNull()) {
    decodedReason.clear();
  } else if (!boundedText(object.value(QStringLiteral("reason")), 47,
                          &decodedReason, false)) {
    return fail(errorId, QStringLiteral("contract-invalid"));
  }
  const bool valueChanged = object.value(QStringLiteral("changed")).toBool();
  const bool mutationAttempted =
      object.value(QStringLiteral("mutationAttempted")).toBool();
  const bool valueVerified = object.value(QStringLiteral("verified")).toBool();
  const bool attempted =
      object.value(QStringLiteral("rollbackAttempted")).toBool();
  const bool restored =
      object.value(QStringLiteral("rollbackVerified")).toBool();
  const bool validApplied = decodedStatus == QStringLiteral("Applied") &&
                            decodedReason.isEmpty() && expectedChanged &&
                            valueChanged && mutationAttempted &&
                            valueVerified && !attempted && !restored;
  const bool validAlready = decodedStatus == QStringLiteral("AlreadySet") &&
                            decodedReason.isEmpty() && !expectedChanged &&
                            !valueChanged && !mutationAttempted &&
                            valueVerified && !attempted && !restored;
  const bool validRefused = decodedStatus == QStringLiteral("Refused") &&
                            controlRefusedReasonIds().contains(decodedReason) &&
                            !valueChanged && !mutationAttempted &&
                            !valueVerified && !attempted && !restored;
  const bool rollbackReason =
      decodedReason == QStringLiteral("mutation-timeout") ||
      decodedReason == QStringLiteral("mutation-failed") ||
      decodedReason == QStringLiteral("verification-failed") ||
      decodedReason == QStringLiteral("rollback-failed");
  const bool validFailed =
      decodedStatus == QStringLiteral("Failed") && expectedChanged &&
      controlFailedReasonIds().contains(decodedReason) && !valueChanged &&
      mutationAttempted && !valueVerified && (!restored || attempted) &&
      (!attempted || rollbackReason) &&
      (decodedReason != QStringLiteral("rollback-failed") || attempted) &&
      (!restored || decodedReason != QStringLiteral("rollback-failed")) &&
      (!attempted || restored ||
       decodedReason == QStringLiteral("rollback-failed"));
  if (!validApplied && !validAlready && !validRefused && !validFailed)
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (status)
    *status = decodedStatus;
  if (reason)
    *reason = decodedReason;
  if (changed)
    *changed = valueChanged;
  if (rollbackAttempted)
    *rollbackAttempted = attempted;
  if (rollbackVerified)
    *rollbackVerified = restored;
  return true;
}

bool decodeRouteReceipt(const QByteArray &payload,
                        const QString &expectedAction,
                        const QString &expectedDevice,
                        const QString &expectedRule, QString *resultRule,
                        bool *changed, QString *errorId) {
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId))
    return false;
  if (!exactKeys(object, {"schema", "status", "action", "generation", "changed",
                          "device", "rule", "policyApplied", "routingApplied",
                          "enforcementAvailable", "enforcementReason"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-route-policy-receipt/v1") ||
      object.value(QStringLiteral("status")).toString() !=
          QStringLiteral("Applied") ||
      object.value(QStringLiteral("action")).toString() != expectedAction ||
      !QStringList({QStringLiteral("set-rule"),
                    QStringLiteral("set-process-rule"),
                    QStringLiteral("remove-rule")})
           .contains(expectedAction) ||
      !exactInteger(object.value(QStringLiteral("generation")), 0,
                    4294967295LL) ||
      !object.value(QStringLiteral("changed")).isBool() ||
      !object.value(QStringLiteral("policyApplied")).isBool() ||
      !object.value(QStringLiteral("policyApplied")).toBool() ||
      !object.value(QStringLiteral("routingApplied")).isBool() ||
      object.value(QStringLiteral("routingApplied")).toBool() ||
      !object.value(QStringLiteral("enforcementAvailable")).isBool() ||
      object.value(QStringLiteral("enforcementAvailable")).toBool() ||
      object.value(QStringLiteral("enforcementReason")).toString() !=
          QStringLiteral("audio-route-broker-not-integrated"))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (!object.value(QStringLiteral("device")).isString() ||
      object.value(QStringLiteral("device")).toString() != expectedDevice)
    return fail(errorId, QStringLiteral("contract-invalid"));
  const QString device = object.value(QStringLiteral("device")).toString();
  if (!tokenMatches(device, outputTokenExpression()) &&
      !tokenMatches(device, inputTokenExpression()))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (!object.value(QStringLiteral("rule")).isString())
    return fail(errorId, QStringLiteral("contract-invalid"));
  const QString rule = object.value(QStringLiteral("rule")).toString();
  if (!tokenMatches(rule, ruleTokenExpression()) ||
      (!expectedRule.isEmpty() && rule != expectedRule))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (resultRule)
    *resultRule = rule;
  if (changed)
    *changed = object.value(QStringLiteral("changed")).toBool();
  return true;
}

} // namespace AudioContracts

class AudioAdapter::Command final : public QObject {
public:
  using Callback =
      std::function<void(int, const QByteArray &, const QString &)>;

  Command(const QString &program, const QStringList &arguments, int outputLimit,
          int timeoutMilliseconds, Callback callback, QObject *parent)
      : QObject(parent), outputLimit_(outputLimit),
        callback_(std::move(callback)) {
    timer_.setSingleShot(true);
    process_.setProgram(program);
    process_.setArguments(arguments);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
    process_.setProcessEnvironment(environment);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this]() {
      output_.append(process_.readAllStandardOutput());
      if (output_.size() > outputLimit_)
        complete(-1, QStringLiteral("output-too-large"));
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this]() {
      const QByteArray available = process_.readAllStandardError();
      if (errorOutput_.size() < kMaximumErrorBytes)
        errorOutput_.append(
            available.left(kMaximumErrorBytes - errorOutput_.size()));
    });
    connect(&process_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
              if (error == QProcess::FailedToStart)
                complete(-1, QStringLiteral("start-failed"));
            });
    connect(&process_,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
              output_.append(process_.readAllStandardOutput());
              if (output_.size() > outputLimit_) {
                complete(-1, QStringLiteral("output-too-large"));
              } else if (status != QProcess::NormalExit) {
                complete(-1, QStringLiteral("process-crashed"));
              } else {
                complete(exitCode, QString());
              }
            });
    connect(&timer_, &QTimer::timeout, this,
            [this]() { complete(-1, QStringLiteral("timeout")); });
    timer_.start(timeoutMilliseconds);
    process_.start();
  }

  ~Command() override {
    finished_ = true;
    timer_.stop();
    QObject::disconnect(&process_, nullptr, this, nullptr);
    if (process_.state() != QProcess::NotRunning) {
      process_.kill();
      (void)process_.waitForFinished(1000);
    }
  }

  void cancel() { complete(-1, QStringLiteral("cancelled")); }

private:
  void complete(int exitCode, const QString &errorId) {
    if (finished_)
      return;
    finished_ = true;
    timer_.stop();
    if (process_.state() != QProcess::NotRunning && !errorId.isEmpty())
      process_.kill();
    Callback callback = std::move(callback_);
    callback(exitCode, output_, errorId);
    deleteLater();
  }

  QProcess process_;
  QTimer timer_;
  QByteArray output_;
  QByteArray errorOutput_;
  qsizetype outputLimit_ = 0;
  Callback callback_;
  bool finished_ = false;
};

AudioAdapter::AudioAdapter(QObject *parent)
    : AudioAdapter(defaultBackendPath(), parent) {}

AudioAdapter::AudioAdapter(QString backendPath, QObject *parent)
    : AudioAdapter(std::move(backendPath), PathChooser(), 15000, parent) {}

AudioAdapter::AudioAdapter(QString backendPath, PathChooser pathChooser,
                           int commandTimeoutMilliseconds, QObject *parent)
    : QObject(parent), backendPath_(std::move(backendPath)),
      pathChooser_(std::move(pathChooser)),
      commandTimeoutMilliseconds_(commandTimeoutMilliseconds) {
  if (!pathChooser_)
    pathChooser_ = [this](const QString &matchType) {
      return defaultChoosePath(matchType);
    };
  if (commandTimeoutMilliseconds_ < 100 || commandTimeoutMilliseconds_ > 60000)
    commandTimeoutMilliseconds_ = 15000;
}

AudioAdapter::~AudioAdapter() {
  if (command_) {
    Command *running = command_;
    command_ = nullptr;
    delete running;
  }
}

bool AudioAdapter::audioBusy() const { return busy_; }
bool AudioAdapter::audioSnapshotReady() const { return snapshotReady_; }
bool AudioAdapter::audioAvailable() const { return snapshot_.available; }
QString AudioAdapter::audioReason() const { return snapshot_.reason; }
QVariantList AudioAdapter::audioOutputs() const { return snapshot_.outputs; }
QVariantList AudioAdapter::audioInputs() const { return snapshot_.inputs; }
QVariantList AudioAdapter::audioStreams() const { return snapshot_.streams; }
QVariantList AudioAdapter::audioCards() const { return snapshot_.cards; }
QVariantList AudioAdapter::audioRouteRules() const {
  return snapshot_.routeRules;
}
bool AudioAdapter::audioRouteBrokerAvailable() const {
  return snapshot_.routeBrokerAvailable;
}
bool AudioAdapter::audioRouteBrokerActive() const {
  return snapshot_.routeBrokerActive;
}
QString AudioAdapter::audioRouteBrokerReason() const {
  return snapshot_.routeBrokerReason;
}
bool AudioAdapter::audioRouteEnforcementAvailable() const {
  return snapshot_.routeEnforcementAvailable;
}
QString AudioAdapter::audioGoxlrStatus() const { return snapshot_.goxlrStatus; }
QString AudioAdapter::audioGoxlrReason() const { return snapshot_.goxlrReason; }
bool AudioAdapter::audioGoxlrProviderActive() const {
  return snapshot_.goxlrProviderActive;
}
bool AudioAdapter::audioGoxlrTruncated() const {
  return snapshot_.goxlrTruncated;
}
QVariantList AudioAdapter::audioGoxlrDevices() const {
  return snapshot_.goxlrDevices;
}
QVariantList AudioAdapter::audioProcessChoices() const {
  return processChoices_;
}
bool AudioAdapter::audioProcessChoiceOpen() const { return processChoiceOpen_; }
QString AudioAdapter::audioStatusId() const { return statusId_; }
QString AudioAdapter::audioErrorId() const { return errorId_; }

bool AudioAdapter::startCommand(
    const QStringList &arguments, int outputLimit,
    std::function<void(int, const QByteArray &, const QString &)> callback) {
  if (command_)
    return false;
  const QFileInfo backend(backendPath_);
  if (!backend.isAbsolute() || !backend.isFile() || !backend.isExecutable())
    return false;
  command_ = new Command(
      backend.absoluteFilePath(), arguments, outputLimit,
      commandTimeoutMilliseconds_,
      [this, callback = std::move(callback)](
          int exitCode, const QByteArray &payload, const QString &errorId) {
        command_ = nullptr;
        callback(exitCode, payload, errorId);
      },
      this);
  return true;
}

void AudioAdapter::setBusy(bool busy) {
  if (busy_ == busy)
    return;
  busy_ = busy;
  emit audioStateChanged();
}

void AudioAdapter::setMessage(const QString &statusId, const QString &errorId) {
  if (statusId_ == statusId && errorId_ == errorId)
    return;
  statusId_ = statusId;
  errorId_ = errorId;
  emit audioOperationStateChanged();
}

bool AudioAdapter::loadAudio() {
  if (busy_)
    return false;
  clearProcessChoice();
  setMessage(QString(), QString());
  setBusy(true);
  startInventoryLoad(QStringLiteral("audio-loaded"));
  return true;
}

void AudioAdapter::startInventoryLoad(const QString &successStatusId,
                                      const QString &operationErrorId) {
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("inventory"),
       QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumInventoryBytes),
      [this, successStatusId, operationErrorId](int exitCode,
                                                const QByteArray &payload,
                                                const QString &commandError) {
        if (!commandError.isEmpty() || exitCode != 0) {
          failLoad(commandError.isEmpty() ? QStringLiteral("backend-failed")
                                          : commandError);
          return;
        }
        AudioPresentationSnapshot snapshot;
        QString errorId;
        if (!AudioContracts::decodeInventory(payload, &snapshot, &errorId)) {
          failLoad(errorId);
          return;
        }
        startPolicyLoad(std::move(snapshot), successStatusId, operationErrorId);
      });
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::startPolicyLoad(AudioPresentationSnapshot snapshot,
                                   const QString &successStatusId,
                                   const QString &operationErrorId) {
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("policy"),
       QStringLiteral("show"), QStringLiteral("--format"),
       QStringLiteral("json")},
      static_cast<int>(kMaximumPolicyBytes),
      [this, snapshot = std::move(snapshot), successStatusId,
       operationErrorId](int exitCode, const QByteArray &payload,
                         const QString &commandError) mutable {
        if (!commandError.isEmpty() || exitCode != 0) {
          failLoad(commandError.isEmpty() ? QStringLiteral("policy-unavailable")
                                          : commandError);
          return;
        }
        QString errorId;
        if (!AudioContracts::decodePolicy(payload, &snapshot, &errorId)) {
          failLoad(errorId);
          return;
        }
        startBrokerStatusLoad(std::move(snapshot), successStatusId,
                              operationErrorId);
      });
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::startBrokerStatusLoad(AudioPresentationSnapshot snapshot,
                                         const QString &successStatusId,
                                         const QString &operationErrorId) {
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("broker-status"),
       QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumBrokerStatusBytes),
      [this, snapshot = std::move(snapshot), successStatusId,
       operationErrorId](int exitCode, const QByteArray &payload,
                         const QString &commandError) mutable {
        if (!commandError.isEmpty() || exitCode != 0) {
          failLoad(commandError.isEmpty()
                       ? QStringLiteral("broker-status-unavailable")
                       : commandError);
          return;
        }
        QString errorId;
        if (!AudioContracts::decodeBrokerStatus(payload, &snapshot, &errorId)) {
          failLoad(errorId);
          return;
        }
        startGoxlrStatusLoad(std::move(snapshot), successStatusId,
                             operationErrorId);
      });
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::startGoxlrStatusLoad(AudioPresentationSnapshot snapshot,
                                        const QString &successStatusId,
                                        const QString &operationErrorId) {
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("goxlr-status"),
       QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumGoxlrStatusBytes),
      [this, snapshot = std::move(snapshot), successStatusId,
       operationErrorId](int exitCode, const QByteArray &payload,
                         const QString &commandError) mutable {
        if (!commandError.isEmpty() || exitCode != 0) {
          failLoad(commandError.isEmpty()
                       ? QStringLiteral("goxlr-status-unavailable")
                       : commandError);
          return;
        }
        QString errorId;
        if (!AudioContracts::decodeGoxlrStatus(payload, &snapshot, &errorId)) {
          failLoad(errorId);
          return;
        }
        publishSnapshot(std::move(snapshot), successStatusId, operationErrorId);
      });
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::publishSnapshot(AudioPresentationSnapshot snapshot,
                                   const QString &successStatusId,
                                   const QString &operationErrorId) {
  snapshot_ = std::move(snapshot);
  snapshotReady_ = true;
  emit audioModelsChanged();
  setBusy(false);
  setMessage(successStatusId, operationErrorId);
  emit audioLoaded(true);
  if (!successStatusId.isEmpty() || !operationErrorId.isEmpty())
    emit audioOperationFinished(successStatusId);
}

void AudioAdapter::failLoad(const QString &errorId) {
  snapshot_ = AudioPresentationSnapshot();
  snapshot_.reason = errorId;
  snapshotReady_ = false;
  emit audioModelsChanged();
  setBusy(false);
  setMessage(QString(), errorId);
  emit audioLoaded(false);
}

void AudioAdapter::failOperation(const QString &errorId) {
  setBusy(false);
  setMessage(QString(), errorId);
  emit audioOperationFinished(QString());
}

bool AudioAdapter::validDirectionDevice(const QString &direction,
                                        const QString &deviceId) const {
  if (!snapshot_.available || !snapshot_.mutationAvailable ||
      !endpointToken(direction, deviceId))
    return false;
  const QVariantList &items = direction == QStringLiteral("output")
                                  ? snapshot_.outputs
                                  : snapshot_.inputs;
  for (const QVariant &entry : items) {
    if (entry.toMap().value(QStringLiteral("id")).toString() == deviceId)
      return true;
  }
  return false;
}

QVariantMap AudioAdapter::audioStream(const QString &streamId) const {
  if (!tokenMatches(streamId, streamTokenExpression()))
    return {};
  for (const QVariant &entry : snapshot_.streams) {
    const QVariantMap stream = entry.toMap();
    if (stream.value(QStringLiteral("id")).toString() == streamId)
      return stream;
  }
  return {};
}

bool AudioAdapter::validStreamMove(const QString &streamId,
                                   const QString &originalDeviceId,
                                   const QString &requestedDeviceId) const {
  if (!snapshot_.available || !snapshot_.mutationAvailable)
    return false;
  const QVariantMap stream = audioStream(streamId);
  if (stream.isEmpty() ||
      !stream.value(QStringLiteral("moveAvailable")).toBool() ||
      stream.value(QStringLiteral("target")).toString() != originalDeviceId)
    return false;
  const QString direction =
      stream.value(QStringLiteral("direction")).toString() ==
              QStringLiteral("playback")
          ? QStringLiteral("output")
          : QStringLiteral("input");
  return validDirectionDevice(direction, originalDeviceId) &&
         validDirectionDevice(direction, requestedDeviceId);
}

QVariantMap AudioAdapter::audioControlTarget(const QString &targetId) const {
  if (!snapshot_.available || !snapshot_.mutationAvailable ||
      audioControlTargetType(targetId).isEmpty())
    return {};
  for (const QVariantList *items :
       {&snapshot_.outputs, &snapshot_.inputs, &snapshot_.streams}) {
    for (const QVariant &entry : *items) {
      const QVariantMap item = entry.toMap();
      if (item.value(QStringLiteral("id")).toString() == targetId &&
          item.value(QStringLiteral("levelControlAvailable")).toBool())
        return item;
    }
  }
  return {};
}

bool AudioAdapter::startAudioControl(const QString &targetId,
                                     const QString &control,
                                     const QVariant &requestedValue) {
  const QVariantMap target = audioControlTarget(targetId);
  const bool volume = control == QStringLiteral("volume");
  const bool mute = control == QStringLiteral("mute");
  if (busy_ || target.isEmpty() || (!volume && !mute) ||
      (volume && (requestedValue.metaType().id() != QMetaType::Int ||
                  requestedValue.toInt() < 0 ||
                  requestedValue.toInt() > kSafeVolumeMaximumPercent)) ||
      (mute && requestedValue.metaType().id() != QMetaType::Bool)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  const QVariant originalValue =
      volume ? target.value(QStringLiteral("volumePercent"))
             : target.value(QStringLiteral("muted"));
  if ((volume && originalValue.metaType().id() != QMetaType::Int) ||
      (mute && originalValue.metaType().id() != QMetaType::Bool)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  const QString planAction =
      volume ? QStringLiteral("plan-volume") : QStringLiteral("plan-mute");
  const QString applyAction =
      volume ? QStringLiteral("set-volume") : QStringLiteral("set-mute");
  const QString requestedFlag =
      volume ? QStringLiteral("--percent") : QStringLiteral("--muted");
  const QString originalFlag = volume ? QStringLiteral("--from-percent")
                                      : QStringLiteral("--from-muted");
  const auto serializedValue = [volume](const QVariant &value) {
    return volume           ? QString::number(value.toInt())
           : value.toBool() ? QStringLiteral("true")
                            : QStringLiteral("false");
  };
  const QString requestedText = serializedValue(requestedValue);
  const QString originalText = serializedValue(originalValue);

  clearProcessChoice();
  setMessage(QString(), QString());
  setBusy(true);
  const bool started = startCommand(
      {QStringLiteral("audio"), planAction, QStringLiteral("--target"),
       targetId, requestedFlag, requestedText, QStringLiteral("--format"),
       QStringLiteral("json")},
      static_cast<int>(kMaximumReceiptBytes),
      [this, targetId, control, originalValue, requestedValue, applyAction,
       requestedFlag, requestedText, originalFlag,
       originalText](int exitCode, const QByteArray &payload,
                     const QString &commandError) {
        if (!commandError.isEmpty() || exitCode != 0) {
          failOperation(commandError.isEmpty()
                            ? QStringLiteral("audio-control-plan-failed")
                            : commandError);
          return;
        }
        QString cohort;
        QString errorId;
        if (!AudioContracts::decodeControlPlan(payload, targetId, control,
                                               originalValue, requestedValue,
                                               &cohort, nullptr, &errorId)) {
          failOperation(errorId);
          return;
        }
        const bool applyStarted = startCommand(
            {QStringLiteral("audio"), applyAction, QStringLiteral("--target"),
             targetId, originalFlag, originalText, requestedFlag, requestedText,
             QStringLiteral("--cohort"), cohort, QStringLiteral("--ack"),
             QString::fromLatin1(kControlAcknowledgement),
             QStringLiteral("--format"), QStringLiteral("json")},
            static_cast<int>(kMaximumReceiptBytes),
            [this, targetId, control, originalValue,
             requestedValue](int applyExitCode, const QByteArray &applyPayload,
                             const QString &applyError) {
              if (!applyError.isEmpty()) {
                startInventoryLoad(QString(), applyError);
                return;
              }
              QString receiptStatus;
              QString receiptReason;
              bool receiptChanged = false;
              bool rollbackAttempted = false;
              bool rollbackVerified = false;
              QString receiptError;
              if (!AudioContracts::decodeControlReceipt(
                      applyPayload, targetId, control, originalValue,
                      requestedValue, &receiptStatus, &receiptReason,
                      &receiptChanged, &rollbackAttempted, &rollbackVerified,
                      &receiptError)) {
                startInventoryLoad(QString(), receiptError);
                return;
              }
              const bool success =
                  receiptStatus == QStringLiteral("Applied") ||
                  receiptStatus == QStringLiteral("AlreadySet");
              if ((success && applyExitCode != 0) ||
                  (!success && applyExitCode != 1)) {
                startInventoryLoad(QString(),
                                   QStringLiteral("contract-invalid"));
                return;
              }
              if (success) {
                const QString status =
                    control == QStringLiteral("volume")
                        ? receiptChanged
                              ? QStringLiteral("audio-volume-applied")
                              : QStringLiteral("audio-volume-unchanged")
                    : receiptChanged ? QStringLiteral("audio-mute-applied")
                                     : QStringLiteral("audio-mute-unchanged");
                startInventoryLoad(status);
                return;
              }
              QString operationError =
                  receiptStatus == QStringLiteral("Refused")
                      ? QStringLiteral("audio-control-refused")
                      : QStringLiteral("audio-control-failed");
              if (rollbackAttempted && rollbackVerified)
                operationError = QStringLiteral("audio-control-restored");
              startInventoryLoad(QString(), operationError);
            });
        if (!applyStarted)
          failOperation(QStringLiteral("backend-unavailable"));
      });
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

bool AudioAdapter::setAudioVolume(const QString &targetId, int percent) {
  return startAudioControl(targetId, QStringLiteral("volume"),
                           QVariant(percent));
}

bool AudioAdapter::setAudioMuted(const QString &targetId, bool muted) {
  return startAudioControl(targetId, QStringLiteral("mute"), QVariant(muted));
}

bool AudioAdapter::setAudioDefault(const QString &direction,
                                   const QString &deviceId) {
  if (busy_ || !validDirectionDevice(direction, deviceId)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  clearProcessChoice();
  setMessage(QString(), QString());
  setBusy(true);
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("plan-default"),
       QStringLiteral("--direction"), direction, QStringLiteral("--device"),
       deviceId, QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumReceiptBytes),
      [this, direction, deviceId](int exitCode, const QByteArray &payload,
                                  const QString &commandError) {
        if (!commandError.isEmpty() || exitCode != 0) {
          failOperation(commandError.isEmpty()
                            ? QStringLiteral("default-plan-failed")
                            : commandError);
          return;
        }
        bool planChanged = false;
        QString errorId;
        if (!AudioContracts::decodeDefaultPlan(payload, direction, deviceId,
                                               &planChanged, &errorId)) {
          failOperation(errorId);
          return;
        }
        if (!planChanged) {
          startInventoryLoad(QStringLiteral("audio-default-unchanged"));
          return;
        }
        const bool applyStarted = startCommand(
            {QStringLiteral("audio"), QStringLiteral("set-default"),
             QStringLiteral("--direction"), direction,
             QStringLiteral("--device"), deviceId, QStringLiteral("--ack"),
             QString::fromLatin1(kDefaultAcknowledgement),
             QStringLiteral("--format"), QStringLiteral("json")},
            static_cast<int>(kMaximumReceiptBytes),
            [this, direction, deviceId](int applyExitCode,
                                        const QByteArray &applyPayload,
                                        const QString &applyError) {
              if (!applyError.isEmpty() || applyExitCode != 0) {
                failOperation(applyError.isEmpty()
                                  ? QStringLiteral("default-apply-failed")
                                  : applyError);
                return;
              }
              bool changed = false;
              QString receiptError;
              if (!AudioContracts::decodeDefaultReceipt(applyPayload, direction,
                                                        deviceId, &changed,
                                                        &receiptError)) {
                failOperation(receiptError);
                return;
              }
              startInventoryLoad(
                  changed ? QStringLiteral("audio-default-applied")
                          : QStringLiteral("audio-default-unchanged"));
            });
        if (!applyStarted)
          failOperation(QStringLiteral("backend-unavailable"));
      });
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

bool AudioAdapter::moveAudioStream(const QString &streamId,
                                   const QString &originalDeviceId,
                                   const QString &requestedDeviceId) {
  if (busy_ ||
      !validStreamMove(streamId, originalDeviceId, requestedDeviceId)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  clearProcessChoice();
  setMessage(QString(), QString());
  setBusy(true);
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("plan-stream-move"),
       QStringLiteral("--stream"), streamId, QStringLiteral("--device"),
       requestedDeviceId, QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumReceiptBytes),
      [this, streamId, originalDeviceId,
       requestedDeviceId](int exitCode, const QByteArray &payload,
                          const QString &commandError) {
        if (!commandError.isEmpty() || exitCode != 0) {
          failOperation(commandError.isEmpty()
                            ? QStringLiteral("audio-stream-plan-failed")
                            : commandError);
          return;
        }
        QString cohort;
        bool planChanged = false;
        QString errorId;
        if (!AudioContracts::decodeStreamMovePlan(
                payload, streamId, originalDeviceId, requestedDeviceId, &cohort,
                &planChanged, &errorId)) {
          failOperation(errorId);
          return;
        }
        if (!planChanged) {
          startInventoryLoad(QStringLiteral("audio-stream-unchanged"));
          return;
        }
        const bool applyStarted = startCommand(
            {QStringLiteral("audio"), QStringLiteral("move-stream"),
             QStringLiteral("--stream"), streamId,
             QStringLiteral("--from-device"), originalDeviceId,
             QStringLiteral("--device"), requestedDeviceId,
             QStringLiteral("--cohort"), cohort, QStringLiteral("--ack"),
             QString::fromLatin1(kStreamMoveAcknowledgement),
             QStringLiteral("--format"), QStringLiteral("json")},
            static_cast<int>(kMaximumReceiptBytes),
            [this, streamId, originalDeviceId, requestedDeviceId](
                int applyExitCode, const QByteArray &applyPayload,
                const QString &applyError) {
              if (!applyError.isEmpty()) {
                startInventoryLoad(QString(), applyError);
                return;
              }
              QString receiptStatus;
              QString receiptReason;
              bool receiptChanged = false;
              bool rollbackAttempted = false;
              bool rollbackVerified = false;
              QString receiptError;
              if (!AudioContracts::decodeStreamMoveReceipt(
                      applyPayload, streamId, originalDeviceId,
                      requestedDeviceId, &receiptStatus, &receiptReason,
                      &receiptChanged, &rollbackAttempted, &rollbackVerified,
                      &receiptError)) {
                startInventoryLoad(QString(), receiptError);
                return;
              }
              const bool success =
                  receiptStatus == QStringLiteral("Applied") ||
                  receiptStatus == QStringLiteral("AlreadyRouted");
              if ((success && applyExitCode != 0) ||
                  (!success && applyExitCode != 1)) {
                startInventoryLoad(QString(),
                                   QStringLiteral("contract-invalid"));
                return;
              }
              if (success) {
                startInventoryLoad(
                    receiptChanged ? QStringLiteral("audio-stream-moved")
                                   : QStringLiteral("audio-stream-unchanged"));
                return;
              }
              QString operationError =
                  receiptStatus == QStringLiteral("Refused")
                      ? QStringLiteral("audio-stream-move-refused")
                      : QStringLiteral("audio-stream-move-failed");
              if (rollbackAttempted && rollbackVerified)
                operationError = QStringLiteral("audio-stream-move-restored");
              startInventoryLoad(QString(), operationError);
            });
        if (!applyStarted)
          failOperation(QStringLiteral("backend-unavailable"));
      });
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

bool AudioAdapter::chooseAudioProcessRule(const QString &direction,
                                          const QString &deviceId) {
  if (busy_ || !validDirectionDevice(direction, deviceId)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  const QString streamDirection = direction == QStringLiteral("output")
                                      ? QStringLiteral("playback")
                                      : QStringLiteral("recording");
  QVariantList choices;
  for (const QVariant &entry : snapshot_.streams) {
    const QVariantMap stream = entry.toMap();
    if (stream.value(QStringLiteral("direction")).toString() !=
            streamDirection ||
        !stream.value(QStringLiteral("processRuleAvailable")).toBool())
      continue;
    QVariantMap choice;
    choice.insert(QStringLiteral("id"), stream.value(QStringLiteral("id")));
    choice.insert(QStringLiteral("label"),
                  stream.value(QStringLiteral("label")));
    choice.insert(QStringLiteral("direction"),
                  stream.value(QStringLiteral("direction")));
    choice.insert(QStringLiteral("target"),
                  stream.value(QStringLiteral("target")));
    choices.append(choice);
  }
  if (choices.isEmpty()) {
    setMessage(QString(), QStringLiteral("audio-process-unavailable"));
    return false;
  }
  processChoices_ = std::move(choices);
  pendingProcessDirection_ = direction;
  pendingProcessDevice_ = deviceId;
  processChoiceOpen_ = true;
  setMessage(QString(), QString());
  emit audioProcessChoiceChanged();
  emit audioProcessChoiceRequested();
  return true;
}

bool AudioAdapter::confirmAudioProcessRule(const QString &streamId) {
  if (busy_ || !processChoiceOpen_)
    return false;
  bool selected = false;
  for (const QVariant &entry : processChoices_) {
    if (entry.toMap().value(QStringLiteral("id")).toString() == streamId) {
      selected = true;
      break;
    }
  }
  if (!selected) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  const QString device = pendingProcessDevice_;
  clearProcessChoice();
  return startRouteRule(
      QStringLiteral("set-process-rule"),
      {QStringLiteral("audio"), QStringLiteral("policy"),
       QStringLiteral("set-process-rule"), QStringLiteral("--stream"), streamId,
       QStringLiteral("--device"), device, QStringLiteral("--ack"),
       QString::fromLatin1(kRouteAcknowledgement), QStringLiteral("--format"),
       QStringLiteral("json")},
      device, QString());
}

void AudioAdapter::cancelAudioProcessRule() { clearProcessChoice(); }

void AudioAdapter::clearProcessChoice() {
  if (!processChoiceOpen_ && processChoices_.isEmpty() &&
      pendingProcessDirection_.isEmpty() && pendingProcessDevice_.isEmpty())
    return;
  processChoiceOpen_ = false;
  processChoices_.clear();
  pendingProcessDirection_.clear();
  pendingProcessDevice_.clear();
  emit audioProcessChoiceChanged();
}

bool AudioAdapter::chooseAudioExecutableRule(const QString &direction,
                                             const QString &deviceId) {
  return choosePathRule(QStringLiteral("executable"), direction, deviceId);
}

bool AudioAdapter::chooseAudioDirectoryRule(const QString &direction,
                                            const QString &deviceId) {
  return choosePathRule(QStringLiteral("directory"), direction, deviceId);
}

bool AudioAdapter::choosePathRule(const QString &matchType,
                                  const QString &direction,
                                  const QString &deviceId) {
  if (busy_ || !validDirectionDevice(direction, deviceId)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  const QString path = pathChooser_(matchType);
  if (path.isEmpty())
    return false;
  if (!validAbsoluteChooserPath(path)) {
    setMessage(QString(), QStringLiteral("path-invalid"));
    return false;
  }
  return startRouteRule(
      QStringLiteral("set-rule"),
      {QStringLiteral("audio"), QStringLiteral("policy"),
       QStringLiteral("set-rule"), QStringLiteral("--match"), matchType,
       QStringLiteral("--path"), path, QStringLiteral("--direction"), direction,
       QStringLiteral("--device"), deviceId, QStringLiteral("--ack"),
       QString::fromLatin1(kRouteAcknowledgement), QStringLiteral("--format"),
       QStringLiteral("json")},
      deviceId, QString());
}

QString AudioAdapter::defaultChoosePath(const QString &matchType) {
  if (!qobject_cast<QApplication *>(QCoreApplication::instance())) {
    setMessage(QString(), QStringLiteral("native-dialog-unavailable"));
    return {};
  }
  QFileDialog dialog;
  dialog.setDirectory(QDir::homePath());
  dialog.setOption(QFileDialog::DontUseCustomDirectoryIcons, true);
  if (matchType == QStringLiteral("directory")) {
    dialog.setWindowTitle(tr("Choose an application directory"));
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
  } else {
    dialog.setWindowTitle(tr("Choose an application executable"));
    dialog.setFileMode(QFileDialog::ExistingFile);
  }
  if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().size() != 1)
    return {};
  return dialog.selectedFiles().constFirst();
}

bool AudioAdapter::validRule(const QString &ruleId) const {
  if (!tokenMatches(ruleId, ruleTokenExpression()))
    return false;
  for (const QVariant &entry : snapshot_.routeRules) {
    if (entry.toMap().value(QStringLiteral("id")).toString() == ruleId)
      return true;
  }
  return false;
}

QVariantMap AudioAdapter::routeRule(const QString &ruleId) const {
  for (const QVariant &entry : snapshot_.routeRules) {
    const QVariantMap rule = entry.toMap();
    if (rule.value(QStringLiteral("id")).toString() == ruleId)
      return rule;
  }
  return {};
}

bool AudioAdapter::removeAudioRouteRule(const QString &ruleId) {
  if (busy_ || !validRule(ruleId)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  const QVariantMap rule = routeRule(ruleId);
  const QString device = rule.value(QStringLiteral("device")).toString();
  return startRouteRule(
      QStringLiteral("remove-rule"),
      {QStringLiteral("audio"), QStringLiteral("policy"),
       QStringLiteral("remove-rule"), QStringLiteral("--rule"), ruleId,
       QStringLiteral("--ack"), QString::fromLatin1(kRouteAcknowledgement),
       QStringLiteral("--format"), QStringLiteral("json")},
      device, ruleId);
}

bool AudioAdapter::startRouteRule(const QString &action,
                                  const QStringList &arguments,
                                  const QString &expectedDevice,
                                  const QString &expectedRule) {
  if (busy_)
    return false;
  clearProcessChoice();
  setMessage(QString(), QString());
  setBusy(true);
  const bool started = startCommand(
      arguments, static_cast<int>(kMaximumReceiptBytes),
      [this, action, expectedDevice,
       expectedRule](int exitCode, const QByteArray &payload,
                     const QString &commandError) {
        if (!commandError.isEmpty() || exitCode != 0) {
          failOperation(commandError.isEmpty()
                            ? QStringLiteral("route-policy-failed")
                            : commandError);
          return;
        }
        QString resultRule;
        bool changed = false;
        QString errorId;
        if (!AudioContracts::decodeRouteReceipt(payload, action, expectedDevice,
                                                expectedRule, &resultRule,
                                                &changed, &errorId)) {
          failOperation(errorId);
          return;
        }
        QString status = QStringLiteral("audio-route-rule-unchanged");
        if (action == QStringLiteral("remove-rule"))
          status = QStringLiteral("audio-route-rule-removed");
        else if (changed)
          status = QStringLiteral("audio-route-rule-saved");
        startInventoryLoad(status);
      });
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

void AudioAdapter::clearAudioMessage() { setMessage(QString(), QString()); }
