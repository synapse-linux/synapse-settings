// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_adapter.h"

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

#include <cmath>
#include <utility>

namespace {

constexpr qsizetype kMaximumInventoryBytes = qsizetype{1024} * 1024;
constexpr qsizetype kMaximumPolicyBytes = qsizetype{128} * 1024;
constexpr qsizetype kMaximumReceiptBytes = qsizetype{64} * 1024;
constexpr qsizetype kMaximumErrorBytes = qsizetype{16} * 1024;
constexpr int kMaximumLabelBytes = 255;
constexpr int kMaximumPathBytes = 4095;
constexpr int kMaximumEndpoints = 64;
constexpr int kMaximumStreams = 128;
constexpr int kMaximumCards = 32;
constexpr int kMaximumRules = 128;

constexpr char kDefaultAcknowledgement[] = "synapse-settings/audio-default/v1";
constexpr char kRouteAcknowledgement[] =
    "synapse-settings/audio-route-policy/v1";

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
    item.insert(QStringLiteral("processRuleAvailable"),
                object.value(QStringLiteral("processRuleAvailable")).toBool());
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
bool AudioAdapter::audioAvailable() const { return snapshot_.available; }
QString AudioAdapter::audioReason() const { return snapshot_.reason; }
QVariantList AudioAdapter::audioOutputs() const { return snapshot_.outputs; }
QVariantList AudioAdapter::audioInputs() const { return snapshot_.inputs; }
QVariantList AudioAdapter::audioStreams() const { return snapshot_.streams; }
QVariantList AudioAdapter::audioCards() const { return snapshot_.cards; }
QVariantList AudioAdapter::audioRouteRules() const {
  return snapshot_.routeRules;
}
bool AudioAdapter::audioRouteEnforcementAvailable() const {
  return snapshot_.routeEnforcementAvailable;
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

void AudioAdapter::startInventoryLoad(const QString &successStatusId) {
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("inventory"),
       QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumInventoryBytes),
      [this, successStatusId](int exitCode, const QByteArray &payload,
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
        startPolicyLoad(std::move(snapshot), successStatusId);
      });
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::startPolicyLoad(AudioPresentationSnapshot snapshot,
                                   const QString &successStatusId) {
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("policy"),
       QStringLiteral("show"), QStringLiteral("--format"),
       QStringLiteral("json")},
      static_cast<int>(kMaximumPolicyBytes),
      [this, snapshot = std::move(snapshot),
       successStatusId](int exitCode, const QByteArray &payload,
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
        publishSnapshot(std::move(snapshot), successStatusId);
      });
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::publishSnapshot(AudioPresentationSnapshot snapshot,
                                   const QString &successStatusId) {
  snapshot_ = std::move(snapshot);
  emit audioModelsChanged();
  setBusy(false);
  setMessage(successStatusId, QString());
  emit audioLoaded(true);
  if (!successStatusId.isEmpty())
    emit audioOperationFinished(successStatusId);
}

void AudioAdapter::failLoad(const QString &errorId) {
  snapshot_ = AudioPresentationSnapshot();
  snapshot_.reason = errorId;
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
