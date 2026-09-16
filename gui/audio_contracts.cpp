// SPDX-License-Identifier: MIT
#include "audio_contracts_p.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
using namespace AudioLimits;

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

void skipJsonWhitespace(const QByteArray &payload, qsizetype *offset) {
  while (*offset < payload.size()) {
    const char byte = payload.at(*offset);
    if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n')
      break;
    ++*offset;
  }
}

int jsonHexDigit(char byte) {
  if (byte >= '0' && byte <= '9')
    return byte - '0';
  if (byte >= 'a' && byte <= 'f')
    return byte - 'a' + 10;
  if (byte >= 'A' && byte <= 'F')
    return byte - 'A' + 10;
  return -1;
}

bool jsonCodeUnit(const QByteArray &payload, qsizetype offset,
                  unsigned int *value) {
  if (!value || offset < 0 || payload.size() - offset < 4)
    return false;
  unsigned int decoded = 0;
  for (qsizetype index = 0; index < 4; ++index) {
    const int digit = jsonHexDigit(payload.at(offset + index));
    if (digit < 0)
      return false;
    decoded = (decoded << 4U) | static_cast<unsigned int>(digit);
  }
  *value = decoded;
  return true;
}

bool scanJsonString(const QByteArray &payload, qsizetype *offset,
                    QByteArray *token = nullptr) {
  if (!offset || *offset < 0 || *offset >= payload.size() ||
      payload.at(*offset) != '"')
    return false;
  const qsizetype beginning = (*offset)++;
  while (*offset < payload.size()) {
    const unsigned char byte =
        static_cast<unsigned char>(payload.at((*offset)++));
    if (byte == '\\') {
      if (*offset >= payload.size())
        return false;
      const char escape = payload.at((*offset)++);
      if (escape == 'u') {
        unsigned int codeUnit = 0;
        if (!jsonCodeUnit(payload, *offset, &codeUnit) || codeUnit == 0)
          return false;
        *offset += 4;
        if (codeUnit >= 0xd800U && codeUnit <= 0xdbffU) {
          if (payload.size() - *offset < 6 || payload.at(*offset) != '\\' ||
              payload.at(*offset + 1) != 'u')
            return false;
          unsigned int lowSurrogate = 0;
          if (!jsonCodeUnit(payload, *offset + 2, &lowSurrogate) ||
              lowSurrogate < 0xdc00U || lowSurrogate > 0xdfffU)
            return false;
          *offset += 6;
        } else if (codeUnit >= 0xdc00U && codeUnit <= 0xdfffU) {
          return false;
        }
      } else if (!QByteArrayLiteral("\"\\/bfnrt").contains(escape)) {
        return false;
      }
    } else if (byte == '"') {
      if (token)
        *token = payload.mid(beginning, *offset - beginning);
      return true;
    } else if (byte < 0x20U) {
      return false;
    }
  }
  return false;
}

bool decodeJsonKey(const QByteArray &token, QString *key) {
  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(QByteArrayLiteral("[") + token + ']', &error);
  if (error.error != QJsonParseError::NoError || !document.isArray() ||
      document.array().size() != 1 || !document.array().at(0).isString())
    return false;
  *key = document.array().at(0).toString();
  return true;
}

bool scanJsonValue(const QByteArray &payload, qsizetype *offset, int depth,
                   qsizetype *documentKeyCount);

bool scanJsonObject(const QByteArray &payload, qsizetype *offset, int depth,
                    qsizetype *documentKeyCount) {
  constexpr qsizetype kMaximumObjectKeys = 4096;
  constexpr qsizetype kMaximumDocumentKeys = 16384;
  if (*offset >= payload.size() || payload.at(*offset) != '{')
    return false;
  ++*offset;
  skipJsonWhitespace(payload, offset);
  if (*offset < payload.size() && payload.at(*offset) == '}') {
    ++*offset;
    return true;
  }
  QSet<QString> keys;
  for (;;) {
    if (keys.size() >= kMaximumObjectKeys || !documentKeyCount ||
        *documentKeyCount >= kMaximumDocumentKeys)
      return false;
    QByteArray token;
    if (!scanJsonString(payload, offset, &token))
      return false;
    QString key;
    if (!decodeJsonKey(token, &key) || keys.contains(key))
      return false;
    keys.insert(key);
    ++*documentKeyCount;
    skipJsonWhitespace(payload, offset);
    if (*offset >= payload.size() || payload.at(*offset) != ':')
      return false;
    ++*offset;
    if (!scanJsonValue(payload, offset, depth + 1, documentKeyCount))
      return false;
    skipJsonWhitespace(payload, offset);
    if (*offset >= payload.size())
      return false;
    const char separator = payload.at((*offset)++);
    if (separator == '}')
      return true;
    if (separator != ',')
      return false;
    skipJsonWhitespace(payload, offset);
  }
}

bool scanJsonArray(const QByteArray &payload, qsizetype *offset, int depth,
                   qsizetype *documentKeyCount) {
  if (*offset >= payload.size() || payload.at(*offset) != '[')
    return false;
  ++*offset;
  skipJsonWhitespace(payload, offset);
  if (*offset < payload.size() && payload.at(*offset) == ']') {
    ++*offset;
    return true;
  }
  for (;;) {
    if (!scanJsonValue(payload, offset, depth + 1, documentKeyCount))
      return false;
    skipJsonWhitespace(payload, offset);
    if (*offset >= payload.size())
      return false;
    const char separator = payload.at((*offset)++);
    if (separator == ']')
      return true;
    if (separator != ',')
      return false;
    skipJsonWhitespace(payload, offset);
  }
}

bool scanJsonLiteral(const QByteArray &payload, qsizetype *offset,
                     const QByteArray &literal) {
  if (!offset || *offset < 0 || payload.size() - *offset < literal.size() ||
      payload.mid(*offset, literal.size()) != literal)
    return false;
  *offset += literal.size();
  return true;
}

bool scanJsonNumber(const QByteArray &payload, qsizetype *offset) {
  if (!offset || *offset < 0)
    return false;
  qsizetype cursor = *offset;
  if (cursor < payload.size() && payload.at(cursor) == '-')
    ++cursor;
  if (cursor >= payload.size())
    return false;
  if (payload.at(cursor) == '0') {
    ++cursor;
    if (cursor < payload.size() && payload.at(cursor) >= '0' &&
        payload.at(cursor) <= '9')
      return false;
  } else if (payload.at(cursor) >= '1' && payload.at(cursor) <= '9') {
    do {
      ++cursor;
    } while (cursor < payload.size() && payload.at(cursor) >= '0' &&
             payload.at(cursor) <= '9');
  } else {
    return false;
  }
  if (cursor < payload.size() && payload.at(cursor) == '.') {
    ++cursor;
    const qsizetype fractional = cursor;
    while (cursor < payload.size() && payload.at(cursor) >= '0' &&
           payload.at(cursor) <= '9')
      ++cursor;
    if (cursor == fractional)
      return false;
  }
  if (cursor < payload.size() &&
      (payload.at(cursor) == 'e' || payload.at(cursor) == 'E')) {
    ++cursor;
    if (cursor < payload.size() &&
        (payload.at(cursor) == '+' || payload.at(cursor) == '-'))
      ++cursor;
    const qsizetype exponent = cursor;
    while (cursor < payload.size() && payload.at(cursor) >= '0' &&
           payload.at(cursor) <= '9')
      ++cursor;
    if (cursor == exponent)
      return false;
  }
  *offset = cursor;
  return true;
}

bool scanJsonValue(const QByteArray &payload, qsizetype *offset, int depth,
                   qsizetype *documentKeyCount) {
  if (!offset || !documentKeyCount || depth > 64)
    return false;
  skipJsonWhitespace(payload, offset);
  if (*offset >= payload.size())
    return false;
  const char first = payload.at(*offset);
  if (first == '{')
    return scanJsonObject(payload, offset, depth, documentKeyCount);
  if (first == '[')
    return scanJsonArray(payload, offset, depth, documentKeyCount);
  if (first == '"')
    return scanJsonString(payload, offset);
  if (first == 't')
    return scanJsonLiteral(payload, offset, QByteArrayLiteral("true"));
  if (first == 'f')
    return scanJsonLiteral(payload, offset, QByteArrayLiteral("false"));
  if (first == 'n')
    return scanJsonLiteral(payload, offset, QByteArrayLiteral("null"));
  if (first == '-' || (first >= '0' && first <= '9'))
    return scanJsonNumber(payload, offset);
  return false;
}

bool jsonObjectKeysAreUnique(const QByteArray &payload) {
  qsizetype offset = 0;
  qsizetype documentKeyCount = 0;
  if (!scanJsonValue(payload, &offset, 0, &documentKeyCount))
    return false;
  skipJsonWhitespace(payload, &offset);
  return offset == payload.size();
}

bool oneJsonObject(QByteArray payload, qsizetype maximumBytes,
                   QJsonObject *object, QString *errorId) {
  if (!object || payload.isEmpty() || payload.size() > maximumBytes ||
      payload.contains('\0') || QString::fromUtf8(payload).toUtf8() != payload)
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (payload.endsWith('\n'))
    payload.chop(1);
  if (payload.isEmpty() || payload.contains('\n') || payload.contains('\r'))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (!jsonObjectKeysAreUnique(payload))
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

const QRegularExpression &profileTokenExpression() {
  static const QRegularExpression value(
      QStringLiteral("^profile-[0-9a-f]{16}$"));
  return value;
}

const QRegularExpression &portTokenExpression() {
  static const QRegularExpression value(QStringLiteral("^port-[0-9a-f]{16}$"));
  return value;
}

const QRegularExpression &selectionCohortExpression() {
  static const QRegularExpression value(
      QStringLiteral("^selection-[0-9a-f]{16}$"));
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

const QSet<QString> &selectionRefusedReasonIds() {
  static const QSet<QString> values = {
      QStringLiteral("audio-unavailable"),
      QStringLiteral("target-vanished"),
      QStringLiteral("active-selection-unavailable"),
      QStringLiteral("selection-unavailable"),
      QStringLiteral("original-selection-mismatch"),
      QStringLiteral("selection-cohort-changed"),
      QStringLiteral("target-identity-changed"),
  };
  return values;
}

const QSet<QString> &selectionFailedReasonIds() {
  static const QSet<QString> values = {
      QStringLiteral("target-vanished"),
      QStringLiteral("target-identity-changed"),
      QStringLiteral("mutation-timeout"),
      QStringLiteral("mutation-failed"),
      QStringLiteral("verification-failed"),
      QStringLiteral("verification-unavailable"),
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
  QSet<QString> activeProfiles;
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
        !boundedText(object.value(QStringLiteral("activeProfile")), 31,
                     &profile) ||
        (!profile.isEmpty() &&
         (!tokenMatches(profile, profileTokenExpression()) ||
          activeProfiles.contains(profile))))
      return false;
    seen.insert(id);
    if (!profile.isEmpty())
      activeProfiles.insert(profile);
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

bool selectionToken(const QString &selection, const QString &value) {
  if (selection == QStringLiteral("profile"))
    return tokenMatches(value, profileTokenExpression());
  if (selection == QStringLiteral("port"))
    return tokenMatches(value, portTokenExpression());
  return false;
}

bool selectionTargetToken(const QString &selection, const QString &targetType,
                          const QString &value) {
  if (selection == QStringLiteral("profile"))
    return targetType == QStringLiteral("card") &&
           tokenMatches(value, cardTokenExpression());
  return selection == QStringLiteral("port") &&
         endpointToken(targetType, value);
}

bool decodeSelectionOptions(const QJsonValue &value, const QString &selection,
                            QVariantList *items, QSet<QString> *identities,
                            QHash<QString, QString> *labels,
                            bool *hasSelectable) {
  if (!value.isArray() || !items || !identities || !labels || !hasSelectable)
    return false;
  const QJsonArray array = value.toArray();
  if (array.size() > kMaximumSelectionOptions)
    return false;
  QVariantList decoded;
  decoded.reserve(array.size());
  QSet<QString> localIdentities;
  for (const QJsonValue entry : array) {
    if (!entry.isObject())
      return false;
    const QJsonObject object = entry.toObject();
    if (!exactKeys(object, {"id", "label", "availability"}))
      return false;
    QString id;
    QString label;
    QString availability;
    if (!boundedText(object.value(QStringLiteral("id")), 31, &id, false) ||
        !selectionToken(selection, id) || identities->contains(id) ||
        localIdentities.contains(id) ||
        !boundedText(object.value(QStringLiteral("label")), kMaximumLabelBytes,
                     &label) ||
        !boundedText(object.value(QStringLiteral("availability")), 15,
                     &availability, false) ||
        !QStringList({QStringLiteral("available"), QStringLiteral("unknown"),
                      QStringLiteral("unavailable")})
             .contains(availability))
      return false;
    localIdentities.insert(id);
    identities->insert(id);
    labels->insert(id, label);
    *hasSelectable |= availability != QStringLiteral("unavailable");
    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("label"), label);
    item.insert(QStringLiteral("availability"), availability);
    decoded.append(item);
  }
  *items = std::move(decoded);
  return true;
}

QHash<QString, QVariantMap>
selectionBaseTargets(const AudioPresentationSnapshot &snapshot,
                     const QString &selection) {
  QHash<QString, QVariantMap> targets;
  if (selection == QStringLiteral("profile")) {
    for (const QVariant &entry : snapshot.cards) {
      const QVariantMap item = entry.toMap();
      targets.insert(item.value(QStringLiteral("id")).toString(), item);
    }
    return targets;
  }
  for (const QVariantList *list : {&snapshot.outputs, &snapshot.inputs}) {
    for (const QVariant &entry : *list) {
      const QVariantMap item = entry.toMap();
      targets.insert(item.value(QStringLiteral("id")).toString(), item);
    }
  }
  return targets;
}

bool decodeSelectionTargets(const QJsonValue &value, const QString &selection,
                            const AudioPresentationSnapshot &snapshot,
                            QVariantList *items, QSet<QString> *optionIds,
                            bool *anyMutationAvailable) {
  if (!value.isArray() || !items || !optionIds || !anyMutationAvailable)
    return false;
  const QJsonArray array = value.toArray();
  const int maximum = selection == QStringLiteral("profile")
                          ? kMaximumCards
                          : 2 * kMaximumEndpoints;
  if (array.size() > maximum)
    return false;
  const QHash<QString, QVariantMap> base =
      selectionBaseTargets(snapshot, selection);
  if (array.size() != base.size())
    return false;
  QVariantList decoded;
  QSet<QString> targetIds;
  decoded.reserve(array.size());
  for (const QJsonValue entry : array) {
    if (!entry.isObject())
      return false;
    const QJsonObject object = entry.toObject();
    const bool profile = selection == QStringLiteral("profile");
    if ((profile && !exactKeys(object, {"id", "label", "activeProfile",
                                        "activeProfileLabel",
                                        "mutationAvailable", "profiles"})) ||
        (!profile &&
         !exactKeys(object, {"id", "direction", "label", "activePort",
                             "activePortLabel", "mutationAvailable", "ports"})))
      return false;
    QString id;
    QString direction =
        profile ? QStringLiteral("card")
                : object.value(QStringLiteral("direction")).toString();
    QString label;
    if (!boundedText(object.value(QStringLiteral("id")), 31, &id, false) ||
        !selectionTargetToken(selection, direction, id) ||
        targetIds.contains(id) || !base.contains(id) ||
        !boundedText(object.value(QStringLiteral("label")), kMaximumLabelBytes,
                     &label) ||
        !object.value(QStringLiteral("mutationAvailable")).isBool())
      return false;
    targetIds.insert(id);

    const QString activeKey = profile ? QStringLiteral("activeProfile")
                                      : QStringLiteral("activePort");
    const QString activeLabelKey = profile
                                       ? QStringLiteral("activeProfileLabel")
                                       : QStringLiteral("activePortLabel");
    const QString optionsKey =
        profile ? QStringLiteral("profiles") : QStringLiteral("ports");
    QString active;
    QString activeLabel;
    const bool hasActive = !object.value(activeKey).isNull();
    if ((hasActive &&
         (!boundedText(object.value(activeKey), 31, &active, false) ||
          !selectionToken(selection, active) ||
          !boundedText(object.value(activeLabelKey), kMaximumLabelBytes,
                       &activeLabel))) ||
        (!hasActive && !object.value(activeLabelKey).isNull()))
      return false;

    QVariantList options;
    QHash<QString, QString> optionLabels;
    bool hasSelectable = false;
    if (!decodeSelectionOptions(object.value(optionsKey), selection, &options,
                                optionIds, &optionLabels, &hasSelectable) ||
        (hasActive && (!optionLabels.contains(active) ||
                       optionLabels.value(active) != activeLabel)))
      return false;
    const bool expectedMutation = hasActive && hasSelectable;
    const bool mutationAvailable =
        object.value(QStringLiteral("mutationAvailable")).toBool();
    if (mutationAvailable != expectedMutation)
      return false;
    if (profile &&
        base.value(id).value(QStringLiteral("activeProfile")).toString() !=
            active)
      return false;
    *anyMutationAvailable |= mutationAvailable;

    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("label"), label);
    if (!profile)
      item.insert(QStringLiteral("direction"), direction);
    item.insert(activeKey, hasActive ? QVariant(active) : QVariant());
    item.insert(activeLabelKey, hasActive ? QVariant(activeLabel) : QVariant());
    item.insert(QStringLiteral("mutationAvailable"), mutationAvailable);
    item.insert(optionsKey, options);
    decoded.append(item);
  }
  if (targetIds.size() != base.size())
    return false;
  *items = std::move(decoded);
  return true;
}

bool selectionPlanShape(const AudioSelectionPlan &plan) {
  return selectionTargetToken(plan.selection, plan.targetType, plan.target) &&
         selectionToken(plan.selection, plan.originalSelection) &&
         selectionToken(plan.selection, plan.requestedSelection) &&
         QStringList({QStringLiteral("available"), QStringLiteral("unknown")})
             .contains(plan.requestedAvailability);
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
          QStringLiteral("synapse.settings.audio-inventory/v2") ||
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

bool decodeProfilePortInventory(const QByteArray &payload,
                                AudioPresentationSnapshot *snapshot,
                                QString *errorId) {
  if (!snapshot)
    return fail(errorId, QStringLiteral("contract-invalid"));
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumInventoryBytes, &object, errorId))
    return false;
  if (!exactKeys(object,
                 {"schema", "stateAuthority", "available", "reason",
                  "mutationAvailable", "cards", "endpoints", "hardwareReadback",
                  "hardwareExactRollback", "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-profile-port-inventory/v1") ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      !object.value(QStringLiteral("available")).isBool() ||
      !object.value(QStringLiteral("mutationAvailable")).isBool() ||
      !object.value(QStringLiteral("hardwareReadback")).isBool() ||
      object.value(QStringLiteral("hardwareReadback")).toBool() ||
      !object.value(QStringLiteral("hardwareExactRollback")).isBool() ||
      object.value(QStringLiteral("hardwareExactRollback")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));

  const bool available = object.value(QStringLiteral("available")).toBool();
  const bool declaredMutation =
      object.value(QStringLiteral("mutationAvailable")).toBool();
  if (!available) {
    QString reason;
    if (!boundedText(object.value(QStringLiteral("reason")), 32, &reason,
                     false) ||
        !QStringList({QStringLiteral("unavailable"), QStringLiteral("timeout"),
                      QStringLiteral("invalid-response")})
             .contains(reason) ||
        declaredMutation || !object.value(QStringLiteral("cards")).isArray() ||
        !object.value(QStringLiteral("cards")).toArray().isEmpty() ||
        !object.value(QStringLiteral("endpoints")).isArray() ||
        !object.value(QStringLiteral("endpoints")).toArray().isEmpty())
      return fail(errorId, QStringLiteral("contract-invalid"));
    snapshot->profilePortAvailable = false;
    snapshot->profilePortMutationAvailable = false;
    snapshot->profilePortReason = reason;
    snapshot->profileCards.clear();
    snapshot->portEndpoints.clear();
    return true;
  }
  if (!snapshot->available || !object.value(QStringLiteral("reason")).isNull())
    return fail(errorId, QStringLiteral("contract-invalid"));

  QSet<QString> profileIds;
  QSet<QString> portIds;
  QVariantList cards;
  QVariantList endpoints;
  bool anyMutation = false;
  if (!decodeSelectionTargets(object.value(QStringLiteral("cards")),
                              QStringLiteral("profile"), *snapshot, &cards,
                              &profileIds, &anyMutation) ||
      profileIds.size() > kMaximumProfileOptions ||
      !decodeSelectionTargets(object.value(QStringLiteral("endpoints")),
                              QStringLiteral("port"), *snapshot, &endpoints,
                              &portIds, &anyMutation) ||
      portIds.size() > kMaximumPortOptions || declaredMutation != anyMutation)
    return fail(errorId, QStringLiteral("contract-invalid"));
  snapshot->profilePortAvailable = true;
  snapshot->profilePortMutationAvailable = declaredMutation;
  snapshot->profilePortReason.clear();
  snapshot->profileCards = std::move(cards);
  snapshot->portEndpoints = std::move(endpoints);
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
  if (!oneJsonObject(payload, kMaximumGoxlrStatusBytes, &object, errorId) ||
      !exactKeys(object,
                 {"schema", "status", "reason", "providerActive",
                  "presenceKnown", "devicePresent", "deviceCount", "devices",
                  "stateAuthority", "hardwareReadback", "hardwareExactRollback",
                  "mutationAvailable", "inspectionReadOnly", "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-goxlr-status/v2") ||
      !object.value(QStringLiteral("providerActive")).isBool() ||
      !object.value(QStringLiteral("presenceKnown")).isBool() ||
      !object.value(QStringLiteral("devicePresent")).isBool() ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("provider-profile-model") ||
      !object.value(QStringLiteral("hardwareReadback")).isBool() ||
      object.value(QStringLiteral("hardwareReadback")).toBool() ||
      !object.value(QStringLiteral("hardwareExactRollback")).isBool() ||
      object.value(QStringLiteral("hardwareExactRollback")).toBool() ||
      !object.value(QStringLiteral("mutationAvailable")).isBool() ||
      !object.value(QStringLiteral("inspectionReadOnly")).isBool() ||
      !object.value(QStringLiteral("inspectionReadOnly")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));

  QString status;
  QString reason;
  if (!boundedText(object.value(QStringLiteral("status")), 15, &status,
                   false) ||
      !QStringList({QStringLiteral("Ready"), QStringLiteral("Inactive"),
                    QStringLiteral("Unavailable"), QStringLiteral("Failed")})
           .contains(status))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (object.value(QStringLiteral("reason")).isNull()) {
    reason.clear();
  } else if (!boundedText(object.value(QStringLiteral("reason")), 47, &reason,
                          false)) {
    return fail(errorId, QStringLiteral("contract-invalid"));
  }

  const bool providerActive =
      object.value(QStringLiteral("providerActive")).toBool();
  const bool presenceKnown =
      object.value(QStringLiteral("presenceKnown")).toBool();
  const bool devicePresent =
      object.value(QStringLiteral("devicePresent")).toBool();
  const bool declaredMutation =
      object.value(QStringLiteral("mutationAvailable")).toBool();
  qint64 declaredCount = 0;
  if (!exactInteger(object.value(QStringLiteral("deviceCount")), 0, 1,
                    &declaredCount) ||
      !object.value(QStringLiteral("devices")).isArray() ||
      (!presenceKnown && devicePresent))
    return fail(errorId, QStringLiteral("contract-invalid"));
  const QJsonArray devices = object.value(QStringLiteral("devices")).toArray();
  if (devices.size() != declaredCount ||
      (!providerActive && declaredCount != 0))
    return fail(errorId, QStringLiteral("contract-invalid"));

  QVariantList decoded;
  bool anyControl = false;
  if (declaredCount == 1) {
    if (!devices.at(0).isObject())
      return fail(errorId, QStringLiteral("contract-invalid"));
    const QJsonObject device = devices.at(0).toObject();
    if (!exactKeys(device,
                   {"model", "profileModelReady", "systemOutputSupported",
                    "controlAvailable", "faders", "cough", "outputs"}) ||
        !device.value(QStringLiteral("profileModelReady")).isBool() ||
        !device.value(QStringLiteral("systemOutputSupported")).isBool() ||
        !device.value(QStringLiteral("controlAvailable")).isBool() ||
        !device.value(QStringLiteral("faders")).isArray() ||
        !device.value(QStringLiteral("cough")).isObject() ||
        !device.value(QStringLiteral("outputs")).isObject())
      return fail(errorId, QStringLiteral("contract-invalid"));
    QString model;
    if (!boundedText(device.value(QStringLiteral("model")), 15, &model,
                     false) ||
        (model != QStringLiteral("GoXLR Mini") &&
         model != QStringLiteral("GoXLR")))
      return fail(errorId, QStringLiteral("contract-invalid"));
    const bool profileReady =
        device.value(QStringLiteral("profileModelReady")).toBool();

    static const QSet<QString> channels = {
        QStringLiteral("Mic"),        QStringLiteral("LineIn"),
        QStringLiteral("Console"),    QStringLiteral("System"),
        QStringLiteral("Game"),       QStringLiteral("Chat"),
        QStringLiteral("Sample"),     QStringLiteral("Music"),
        QStringLiteral("Headphones"), QStringLiteral("MicMonitor"),
        QStringLiteral("LineOut")};
    const QJsonArray faders = device.value(QStringLiteral("faders")).toArray();
    if (faders.size() != 4)
      return fail(errorId, QStringLiteral("contract-invalid"));
    QVariantList projectedFaders;
    projectedFaders.reserve(4);
    for (int index = 0; index < 4; ++index) {
      if (!faders.at(index).isObject())
        return fail(errorId, QStringLiteral("contract-invalid"));
      const QJsonObject fader = faders.at(index).toObject();
      if (!exactKeys(fader, {"fader", "channel", "volume", "muted",
                             "volumeAvailable", "muteAvailable"}) ||
          fader.value(QStringLiteral("fader")).toString() !=
              QString(QChar(u'A' + index)) ||
          !channels.contains(
              fader.value(QStringLiteral("channel")).toString()) ||
          !fader.value(QStringLiteral("muted")).isBool() ||
          !fader.value(QStringLiteral("volumeAvailable")).isBool() ||
          !fader.value(QStringLiteral("muteAvailable")).isBool())
        return fail(errorId, QStringLiteral("contract-invalid"));
      qint64 volume = 0;
      if (!exactInteger(fader.value(QStringLiteral("volume")), 0, 255, &volume))
        return fail(errorId, QStringLiteral("contract-invalid"));
      const bool volumeAvailable =
          fader.value(QStringLiteral("volumeAvailable")).toBool();
      const bool muteAvailable =
          fader.value(QStringLiteral("muteAvailable")).toBool();
      if ((volumeAvailable || muteAvailable) && !profileReady)
        return fail(errorId, QStringLiteral("contract-invalid"));
      anyControl |= volumeAvailable || muteAvailable;
      QVariantMap item;
      item.insert(QStringLiteral("fader"),
                  fader.value(QStringLiteral("fader")).toString());
      item.insert(QStringLiteral("channel"),
                  fader.value(QStringLiteral("channel")).toString());
      item.insert(QStringLiteral("volume"), static_cast<int>(volume));
      item.insert(QStringLiteral("muted"),
                  fader.value(QStringLiteral("muted")).toBool());
      item.insert(QStringLiteral("volumeAvailable"), volumeAvailable);
      item.insert(QStringLiteral("muteAvailable"), muteAvailable);
      projectedFaders.append(item);
    }

    const QJsonObject cough = device.value(QStringLiteral("cough")).toObject();
    if (!exactKeys(cough, {"mode", "muted", "available"}) ||
        !cough.value(QStringLiteral("muted")).isBool() ||
        !cough.value(QStringLiteral("available")).isBool())
      return fail(errorId, QStringLiteral("contract-invalid"));
    const QString coughMode = cough.value(QStringLiteral("mode")).toString();
    const bool coughAvailable =
        cough.value(QStringLiteral("available")).toBool();
    if ((coughMode != QStringLiteral("Toggle") &&
         coughMode != QStringLiteral("Hold")) ||
        (coughMode == QStringLiteral("Hold") && coughAvailable) ||
        (coughAvailable && !profileReady))
      return fail(errorId, QStringLiteral("contract-invalid"));
    anyControl |= coughAvailable;

    const QJsonObject outputs =
        device.value(QStringLiteral("outputs")).toObject();
    if (!exactKeys(outputs,
                   {"headphonesVolume", "lineOutVolume", "monitoredOutput",
                    "headphonesAvailable", "lineOutAvailable"}) ||
        !outputs.value(QStringLiteral("headphonesAvailable")).isBool() ||
        !outputs.value(QStringLiteral("lineOutAvailable")).isBool())
      return fail(errorId, QStringLiteral("contract-invalid"));
    qint64 headphonesVolume = 0;
    qint64 lineOutVolume = 0;
    if (!exactInteger(outputs.value(QStringLiteral("headphonesVolume")), 0, 255,
                      &headphonesVolume) ||
        !exactInteger(outputs.value(QStringLiteral("lineOutVolume")), 0, 255,
                      &lineOutVolume))
      return fail(errorId, QStringLiteral("contract-invalid"));
    static const QSet<QString> monitoredOutputs = {
        QStringLiteral("Headphones"), QStringLiteral("BroadcastMix"),
        QStringLiteral("ChatMic"),    QStringLiteral("Sampler"),
        QStringLiteral("LineOut"),    QStringLiteral("StreamMix2")};
    const QString monitoredOutput =
        outputs.value(QStringLiteral("monitoredOutput")).toString();
    const bool headphonesAvailable =
        outputs.value(QStringLiteral("headphonesAvailable")).toBool();
    const bool lineOutAvailable =
        outputs.value(QStringLiteral("lineOutAvailable")).toBool();
    if (!monitoredOutputs.contains(monitoredOutput) ||
        ((headphonesAvailable || lineOutAvailable) && !profileReady))
      return fail(errorId, QStringLiteral("contract-invalid"));
    anyControl |= headphonesAvailable || lineOutAvailable;

    const bool declaredControl =
        device.value(QStringLiteral("controlAvailable")).toBool();
    if (declaredControl != anyControl || !devicePresent)
      return fail(errorId, QStringLiteral("contract-invalid"));
    QVariantMap projected;
    projected.insert(QStringLiteral("model"), model);
    projected.insert(QStringLiteral("profileModelReady"), profileReady);
    projected.insert(
        QStringLiteral("systemOutputSupported"),
        device.value(QStringLiteral("systemOutputSupported")).toBool());
    projected.insert(QStringLiteral("controlAvailable"), declaredControl);
    projected.insert(QStringLiteral("faders"), projectedFaders);
    QVariantMap projectedCough;
    projectedCough.insert(QStringLiteral("mode"), coughMode);
    projectedCough.insert(QStringLiteral("muted"),
                          cough.value(QStringLiteral("muted")).toBool());
    projectedCough.insert(QStringLiteral("available"), coughAvailable);
    projected.insert(QStringLiteral("cough"), projectedCough);
    QVariantMap projectedOutputs;
    projectedOutputs.insert(QStringLiteral("headphonesVolume"),
                            static_cast<int>(headphonesVolume));
    projectedOutputs.insert(QStringLiteral("lineOutVolume"),
                            static_cast<int>(lineOutVolume));
    projectedOutputs.insert(QStringLiteral("monitoredOutput"), monitoredOutput);
    projectedOutputs.insert(QStringLiteral("headphonesAvailable"),
                            headphonesAvailable);
    projectedOutputs.insert(QStringLiteral("lineOutAvailable"),
                            lineOutAvailable);
    projected.insert(QStringLiteral("outputs"), projectedOutputs);
    decoded.append(projected);
  }

  const bool validReady = status == QStringLiteral("Ready") && providerActive &&
                          reason.isEmpty() && presenceKnown &&
                          devicePresent == (declaredCount == 1);
  const bool validInactive = status == QStringLiteral("Inactive") &&
                             !providerActive &&
                             reason == QStringLiteral("provider-inactive");
  const bool validUnavailable = status == QStringLiteral("Unavailable") &&
                                !providerActive &&
                                reason == QStringLiteral("adapter-unavailable");
  const bool validFailed = status == QStringLiteral("Failed") &&
                           !providerActive &&
                           goxlrFailedReasonIds().contains(reason);
  if ((!validReady && !validInactive && !validUnavailable && !validFailed) ||
      declaredMutation != anyControl)
    return fail(errorId, QStringLiteral("contract-invalid"));

  snapshot->goxlrStatus = status;
  snapshot->goxlrReason = reason;
  snapshot->goxlrProviderActive = providerActive;
  snapshot->goxlrPresenceKnown = presenceKnown;
  snapshot->goxlrDevicePresent = devicePresent;
  snapshot->goxlrMutationAvailable = declaredMutation;
  snapshot->goxlrTruncated = false;
  snapshot->goxlrDevices = std::move(decoded);
  return true;
}

bool decodeGoxlrControlPlan(const QByteArray &payload,
                            const QString &expectedControl,
                            int expectedOriginal, int expectedRequested,
                            QString *cohort, QString *errorId) {
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId) ||
      !exactKeys(object, {"schema", "status", "control", "originalValue",
                          "requestedValue", "cohort", "requiresAcknowledgement",
                          "stateAuthority", "hardwareReadback",
                          "hardwareExactRollback", "applied", "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-goxlr-control-plan/v1") ||
      object.value(QStringLiteral("status")).toString() !=
          QStringLiteral("Planned") ||
      object.value(QStringLiteral("control")).toString() != expectedControl ||
      object.value(QStringLiteral("requiresAcknowledgement")).toString() !=
          QStringLiteral("synapse-settings/audio-goxlr-popup/v1") ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("provider-profile-model") ||
      !object.value(QStringLiteral("hardwareReadback")).isBool() ||
      object.value(QStringLiteral("hardwareReadback")).toBool() ||
      !object.value(QStringLiteral("hardwareExactRollback")).isBool() ||
      object.value(QStringLiteral("hardwareExactRollback")).toBool() ||
      !object.value(QStringLiteral("applied")).isBool() ||
      object.value(QStringLiteral("applied")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));
  const bool mute = expectedControl.contains(QStringLiteral("mute"));
  const int maximum = mute ? 1 : 255;
  qint64 original = 0;
  qint64 requested = 0;
  QString decodedCohort;
  static const QRegularExpression cohortExpression(
      QStringLiteral("^[0-9a-f]{16}$"));
  if (!exactInteger(object.value(QStringLiteral("originalValue")), 0, maximum,
                    &original) ||
      !exactInteger(object.value(QStringLiteral("requestedValue")), 0, maximum,
                    &requested) ||
      original != expectedOriginal || requested != expectedRequested ||
      !boundedText(object.value(QStringLiteral("cohort")), 16, &decodedCohort,
                   false) ||
      !tokenMatches(decodedCohort, cohortExpression))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (cohort)
    *cohort = decodedCohort;
  return true;
}

bool decodeGoxlrControlReceipt(const QByteArray &payload,
                               const QString &expectedControl,
                               int expectedOriginal, int expectedRequested,
                               QString *status, int *observed, bool *changed,
                               bool *rollbackAttempted, bool *rollbackSucceeded,
                               QString *errorId) {
  QJsonObject object;
  if (!oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId) ||
      !exactKeys(object,
                 {"schema", "status", "control", "originalValue",
                  "requestedValue", "observedValue", "changed",
                  "rollbackAttempted", "rollbackSucceeded", "stateAuthority",
                  "hardwareReadback", "hardwareExactRollback",
                  "playbackStarted", "captureStarted", "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-goxlr-control-receipt/v1") ||
      object.value(QStringLiteral("control")).toString() != expectedControl ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("provider-profile-model") ||
      !object.value(QStringLiteral("changed")).isBool() ||
      !object.value(QStringLiteral("rollbackAttempted")).isBool() ||
      !object.value(QStringLiteral("hardwareReadback")).isBool() ||
      object.value(QStringLiteral("hardwareReadback")).toBool() ||
      !object.value(QStringLiteral("hardwareExactRollback")).isBool() ||
      object.value(QStringLiteral("hardwareExactRollback")).toBool() ||
      !object.value(QStringLiteral("playbackStarted")).isBool() ||
      object.value(QStringLiteral("playbackStarted")).toBool() ||
      !object.value(QStringLiteral("captureStarted")).isBool() ||
      object.value(QStringLiteral("captureStarted")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));
  const QString decodedStatus =
      object.value(QStringLiteral("status")).toString();
  if (!QStringList({QStringLiteral("Applied"), QStringLiteral("AlreadyApplied"),
                    QStringLiteral("Refused"), QStringLiteral("Drifted"),
                    QStringLiteral("RolledBack"),
                    QStringLiteral("RollbackFailed")})
           .contains(decodedStatus))
    return fail(errorId, QStringLiteral("contract-invalid"));
  const bool mute = expectedControl.contains(QStringLiteral("mute"));
  const int maximum = mute ? 1 : 255;
  qint64 original = 0;
  qint64 requested = 0;
  qint64 decodedObserved = 0;
  if (!exactInteger(object.value(QStringLiteral("originalValue")), 0, maximum,
                    &original) ||
      !exactInteger(object.value(QStringLiteral("requestedValue")), 0, maximum,
                    &requested) ||
      !exactInteger(object.value(QStringLiteral("observedValue")), 0, maximum,
                    &decodedObserved) ||
      original != expectedOriginal || requested != expectedRequested)
    return fail(errorId, QStringLiteral("contract-invalid"));
  const bool attempted =
      object.value(QStringLiteral("rollbackAttempted")).toBool();
  const QJsonValue succeededValue =
      object.value(QStringLiteral("rollbackSucceeded"));
  if ((!attempted && !succeededValue.isNull()) ||
      (attempted && !succeededValue.isBool()))
    return fail(errorId, QStringLiteral("contract-invalid"));
  const bool succeeded = attempted && succeededValue.toBool();
  const bool decodedChanged = object.value(QStringLiteral("changed")).toBool();
  if ((decodedStatus == QStringLiteral("Applied") &&
       (decodedObserved != expectedRequested || attempted)) ||
      (decodedStatus == QStringLiteral("AlreadyApplied") &&
       (expectedOriginal != expectedRequested ||
        decodedObserved != expectedOriginal || decodedChanged || attempted)) ||
      (decodedStatus == QStringLiteral("Refused") &&
       (decodedObserved != expectedOriginal || decodedChanged || attempted)) ||
      (decodedStatus == QStringLiteral("RolledBack") &&
       (!attempted || !succeeded || decodedObserved != expectedOriginal ||
        decodedChanged)) ||
      (decodedStatus == QStringLiteral("RollbackFailed") &&
       (!attempted || succeeded)) ||
      (decodedStatus == QStringLiteral("Drifted") && attempted))
    return fail(errorId, QStringLiteral("contract-invalid"));
  if (status)
    *status = decodedStatus;
  if (observed)
    *observed = static_cast<int>(decodedObserved);
  if (changed)
    *changed = decodedChanged;
  if (rollbackAttempted)
    *rollbackAttempted = attempted;
  if (rollbackSucceeded)
    *rollbackSucceeded = succeeded;
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

bool decodeSelectionPlan(const QByteArray &payload,
                         const AudioSelectionPlan &expected,
                         AudioSelectionPlan *decoded, QString *errorId) {
  QJsonObject object;
  if (!decoded || !selectionPlanShape(expected) ||
      !oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId))
    return fail(errorId, QStringLiteral("contract-invalid"));
  QString targetLabel;
  QString originalLabel;
  QString requestedLabel;
  QString requestedAvailability;
  QString cohort;
  const bool expectedChanged =
      expected.originalSelection != expected.requestedSelection;
  const bool profile = expected.selection == QStringLiteral("profile");
  if (!exactKeys(object, {"schema",
                          "status",
                          "selection",
                          "target",
                          "targetType",
                          "targetLabel",
                          "originalSelection",
                          "originalLabel",
                          "requestedSelection",
                          "requestedLabel",
                          "requestedAvailability",
                          "cohort",
                          "changed",
                          "stateAuthority",
                          "requiresAcknowledgement",
                          "singleTarget",
                          "postflightRequired",
                          "rollbackOnUnverified",
                          "graphMayChange",
                          "signalPathMayChange",
                          "playbackStarted",
                          "captureStarted",
                          "defaultChanged",
                          "policyChanged",
                          "audibilityVerified",
                          "hardwareReadback",
                          "hardwareExactRollback",
                          "applied",
                          "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-profile-port-plan/v1") ||
      object.value(QStringLiteral("status")).toString() !=
          QStringLiteral("Planned") ||
      object.value(QStringLiteral("selection")).toString() !=
          expected.selection ||
      object.value(QStringLiteral("target")).toString() != expected.target ||
      object.value(QStringLiteral("targetType")).toString() !=
          expected.targetType ||
      !boundedText(object.value(QStringLiteral("targetLabel")),
                   kMaximumLabelBytes, &targetLabel) ||
      object.value(QStringLiteral("originalSelection")).toString() !=
          expected.originalSelection ||
      !boundedText(object.value(QStringLiteral("originalLabel")),
                   kMaximumLabelBytes, &originalLabel) ||
      object.value(QStringLiteral("requestedSelection")).toString() !=
          expected.requestedSelection ||
      !boundedText(object.value(QStringLiteral("requestedLabel")),
                   kMaximumLabelBytes, &requestedLabel) ||
      !boundedText(object.value(QStringLiteral("requestedAvailability")), 15,
                   &requestedAvailability, false) ||
      !QStringList({QStringLiteral("available"), QStringLiteral("unknown")})
           .contains(requestedAvailability) ||
      !boundedText(object.value(QStringLiteral("cohort")), 31, &cohort,
                   false) ||
      !tokenMatches(cohort, selectionCohortExpression()) ||
      !object.value(QStringLiteral("changed")).isBool() ||
      object.value(QStringLiteral("changed")).toBool() != expectedChanged ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      object.value(QStringLiteral("requiresAcknowledgement")).toString() !=
          QString::fromLatin1(kSelectionAcknowledgement) ||
      !object.value(QStringLiteral("singleTarget")).isBool() ||
      !object.value(QStringLiteral("singleTarget")).toBool() ||
      !object.value(QStringLiteral("postflightRequired")).isBool() ||
      !object.value(QStringLiteral("postflightRequired")).toBool() ||
      !object.value(QStringLiteral("rollbackOnUnverified")).isBool() ||
      !object.value(QStringLiteral("rollbackOnUnverified")).toBool() ||
      !object.value(QStringLiteral("graphMayChange")).isBool() ||
      object.value(QStringLiteral("graphMayChange")).toBool() != profile ||
      !object.value(QStringLiteral("signalPathMayChange")).isBool() ||
      !object.value(QStringLiteral("signalPathMayChange")).toBool() ||
      !object.value(QStringLiteral("playbackStarted")).isBool() ||
      object.value(QStringLiteral("playbackStarted")).toBool() ||
      !object.value(QStringLiteral("captureStarted")).isBool() ||
      object.value(QStringLiteral("captureStarted")).toBool() ||
      !object.value(QStringLiteral("defaultChanged")).isBool() ||
      object.value(QStringLiteral("defaultChanged")).toBool() ||
      !object.value(QStringLiteral("policyChanged")).isBool() ||
      object.value(QStringLiteral("policyChanged")).toBool() ||
      !object.value(QStringLiteral("audibilityVerified")).isBool() ||
      object.value(QStringLiteral("audibilityVerified")).toBool() ||
      !object.value(QStringLiteral("hardwareReadback")).isBool() ||
      object.value(QStringLiteral("hardwareReadback")).toBool() ||
      !object.value(QStringLiteral("hardwareExactRollback")).isBool() ||
      object.value(QStringLiteral("hardwareExactRollback")).toBool() ||
      !object.value(QStringLiteral("applied")).isBool() ||
      object.value(QStringLiteral("applied")).toBool() ||
      !object.value(QStringLiteral("bounded")).isBool() ||
      !object.value(QStringLiteral("bounded")).toBool())
    return fail(errorId, QStringLiteral("contract-invalid"));
  *decoded = expected;
  decoded->targetLabel = targetLabel;
  decoded->originalLabel = originalLabel;
  decoded->requestedLabel = requestedLabel;
  decoded->requestedAvailability = requestedAvailability;
  decoded->cohort = cohort;
  decoded->changed = expectedChanged;
  return true;
}

bool decodeSelectionReceipt(const QByteArray &payload,
                            const AudioSelectionPlan &expected, QString *status,
                            QString *reason, bool *changed,
                            bool *rollbackAttempted, bool *rollbackVerified,
                            QString *errorId) {
  QJsonObject object;
  if (!selectionPlanShape(expected) ||
      expected.changed !=
          (expected.originalSelection != expected.requestedSelection) ||
      expected.cohort.isEmpty() ||
      !tokenMatches(expected.cohort, selectionCohortExpression()) ||
      !oneJsonObject(payload, kMaximumReceiptBytes, &object, errorId))
    return fail(errorId, QStringLiteral("contract-invalid"));
  const bool profile = expected.selection == QStringLiteral("profile");
  if (!exactKeys(object, {"schema",
                          "status",
                          "reason",
                          "selection",
                          "target",
                          "targetType",
                          "originalSelection",
                          "requestedSelection",
                          "changed",
                          "mutationAttempted",
                          "verified",
                          "rollbackAttempted",
                          "rollbackVerified",
                          "profileChanged",
                          "portChanged",
                          "stateAuthority",
                          "requiresAcknowledgement",
                          "singleTarget",
                          "graphMayChange",
                          "signalPathMayChange",
                          "playbackStarted",
                          "captureStarted",
                          "defaultChanged",
                          "policyChanged",
                          "audibilityVerified",
                          "hardwareReadback",
                          "hardwareExactRollback",
                          "bounded"}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.settings.audio-profile-port-receipt/v1") ||
      object.value(QStringLiteral("selection")).toString() !=
          expected.selection ||
      object.value(QStringLiteral("target")).toString() != expected.target ||
      object.value(QStringLiteral("targetType")).toString() !=
          expected.targetType ||
      object.value(QStringLiteral("originalSelection")).toString() !=
          expected.originalSelection ||
      object.value(QStringLiteral("requestedSelection")).toString() !=
          expected.requestedSelection ||
      !object.value(QStringLiteral("changed")).isBool() ||
      !object.value(QStringLiteral("mutationAttempted")).isBool() ||
      !object.value(QStringLiteral("verified")).isBool() ||
      !object.value(QStringLiteral("rollbackAttempted")).isBool() ||
      !object.value(QStringLiteral("rollbackVerified")).isBool() ||
      !object.value(QStringLiteral("profileChanged")).isBool() ||
      !object.value(QStringLiteral("portChanged")).isBool() ||
      object.value(QStringLiteral("stateAuthority")).toString() !=
          QStringLiteral("pipewire-pulse-model") ||
      object.value(QStringLiteral("requiresAcknowledgement")).toString() !=
          QString::fromLatin1(kSelectionAcknowledgement) ||
      !object.value(QStringLiteral("singleTarget")).isBool() ||
      !object.value(QStringLiteral("singleTarget")).toBool() ||
      !object.value(QStringLiteral("graphMayChange")).isBool() ||
      object.value(QStringLiteral("graphMayChange")).toBool() != profile ||
      !object.value(QStringLiteral("signalPathMayChange")).isBool() ||
      !object.value(QStringLiteral("signalPathMayChange")).toBool() ||
      !object.value(QStringLiteral("playbackStarted")).isBool() ||
      object.value(QStringLiteral("playbackStarted")).toBool() ||
      !object.value(QStringLiteral("captureStarted")).isBool() ||
      object.value(QStringLiteral("captureStarted")).toBool() ||
      !object.value(QStringLiteral("defaultChanged")).isBool() ||
      object.value(QStringLiteral("defaultChanged")).toBool() ||
      !object.value(QStringLiteral("policyChanged")).isBool() ||
      object.value(QStringLiteral("policyChanged")).toBool() ||
      !object.value(QStringLiteral("audibilityVerified")).isBool() ||
      object.value(QStringLiteral("audibilityVerified")).toBool() ||
      !object.value(QStringLiteral("hardwareReadback")).isBool() ||
      object.value(QStringLiteral("hardwareReadback")).toBool() ||
      !object.value(QStringLiteral("hardwareExactRollback")).isBool() ||
      object.value(QStringLiteral("hardwareExactRollback")).toBool() ||
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
  const bool profileChanged =
      object.value(QStringLiteral("profileChanged")).toBool();
  const bool portChanged = object.value(QStringLiteral("portChanged")).toBool();
  const bool expectedChanged = expected.changed;
  const bool validApplied =
      decodedStatus == QStringLiteral("Applied") && decodedReason.isEmpty() &&
      expectedChanged && valueChanged && mutationAttempted && valueVerified &&
      !attempted && !restored && profileChanged == profile &&
      portChanged == !profile;
  const bool validAlready = decodedStatus == QStringLiteral("AlreadySet") &&
                            decodedReason.isEmpty() && !expectedChanged &&
                            !valueChanged && !mutationAttempted &&
                            valueVerified && !attempted && !restored &&
                            !profileChanged && !portChanged;
  const bool validRefused =
      decodedStatus == QStringLiteral("Refused") &&
      selectionRefusedReasonIds().contains(decodedReason) && !valueChanged &&
      !mutationAttempted && !valueVerified && !attempted && !restored &&
      !profileChanged && !portChanged;
  const bool rollbackReason =
      decodedReason == QStringLiteral("mutation-timeout") ||
      decodedReason == QStringLiteral("mutation-failed") ||
      decodedReason == QStringLiteral("verification-failed");
  const bool validFailed =
      decodedStatus == QStringLiteral("Failed") && expectedChanged &&
      selectionFailedReasonIds().contains(decodedReason) && !valueChanged &&
      mutationAttempted && !valueVerified && !profileChanged && !portChanged &&
      (!restored || attempted) &&
      (!attempted || restored ||
       decodedReason == QStringLiteral("rollback-failed")) &&
      (!restored || rollbackReason) &&
      (decodedReason != QStringLiteral("rollback-failed") ||
       (attempted && !restored));
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

bool validEndpointId(const QString &direction, const QString &id) {
  return endpointToken(direction, id);
}
bool validStreamId(const QString &id) {
  return tokenMatches(id, streamTokenExpression());
}
bool validRuleId(const QString &id) {
  return tokenMatches(id, ruleTokenExpression());
}
QString controlTargetType(const QString &id) {
  return audioControlTargetType(id);
}
bool validSelectionId(const QString &selection, const QString &id) {
  return selectionToken(selection, id);
}
bool validSelectionTarget(const QString &selection, const QString &type,
                          const QString &id) {
  return selectionTargetToken(selection, type, id);
}
bool validSelectionPlan(const AudioSelectionPlan &plan) {
  return selectionPlanShape(plan);
}
bool validSelectionCohort(const QString &cohort) {
  return tokenMatches(cohort, selectionCohortExpression());
}

} // namespace AudioContracts
