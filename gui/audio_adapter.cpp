// SPDX-License-Identifier: MIT
#include "audio_adapter.h"
#include "audio_command_p.h"
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <algorithm>
#include <utility>

namespace {
using namespace AudioLimits;

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

} // namespace

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
  delete goxlrLaunchCommand_;
  goxlrLaunchCommand_ = nullptr;
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
bool AudioAdapter::audioProfilePortAvailable() const {
  return snapshot_.profilePortAvailable;
}
bool AudioAdapter::audioProfilePortMutationAvailable() const {
  return snapshot_.profilePortMutationAvailable;
}
QString AudioAdapter::audioProfilePortReason() const {
  return snapshot_.profilePortReason;
}
QVariantList AudioAdapter::audioProfileCards() const {
  return snapshot_.profileCards;
}
QVariantList AudioAdapter::audioPortEndpoints() const {
  return snapshot_.portEndpoints;
}
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
bool AudioAdapter::audioGoxlrPresenceKnown() const {
  return snapshot_.goxlrPresenceKnown;
}
bool AudioAdapter::audioGoxlrDevicePresent() const {
  return snapshot_.goxlrDevicePresent;
}
bool AudioAdapter::audioGoxlrMutationAvailable() const {
  return snapshot_.goxlrMutationAvailable;
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
bool AudioAdapter::audioSelectionConfirmationOpen() const {
  return selectionConfirmationOpen_;
}
QString AudioAdapter::audioSelectionKind() const {
  return pendingSelection_.selection;
}
QString AudioAdapter::audioSelectionTargetLabel() const {
  return pendingSelection_.targetLabel;
}
QString AudioAdapter::audioSelectionOriginalLabel() const {
  return pendingSelection_.originalLabel;
}
QString AudioAdapter::audioSelectionRequestedLabel() const {
  return pendingSelection_.requestedLabel;
}
QString AudioAdapter::audioStatusId() const { return statusId_; }
QString AudioAdapter::audioErrorId() const { return errorId_; }

bool AudioAdapter::startCommand(
    const QStringList &arguments, int outputLimit,
    std::function<void(int, const QByteArray &, const QString &)> callback,
    int timeoutScale) {
  if (command_ || timeoutScale < 1)
    return false;
  const QFileInfo backend(backendPath_);
  if (!backend.isAbsolute() || !backend.isFile() || !backend.isExecutable())
    return false;
  const qint64 scaledTimeout =
      static_cast<qint64>(commandTimeoutMilliseconds_) * timeoutScale;
  const int timeoutMilliseconds = static_cast<int>(
      std::min<qint64>(scaledTimeout, kMaximumCommandTimeoutMilliseconds));
  QPointer<AudioAdapter> guard(this);
  command_ = new Command(
      backend.absoluteFilePath(), arguments, outputLimit, timeoutMilliseconds,
      [guard, callback = std::move(callback)](
          int exitCode, const QByteArray &payload, const QString &errorId) {
        if (!guard)
          return;
        guard->command_ = nullptr;
        callback(exitCode, payload, errorId);
      },
      this);
  Command *command = command_;
  command->start();
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
  QPointer<AudioAdapter> guard(this);
  clearProcessChoice();
  if (!guard)
    return false;
  clearAudioSelection();
  if (!guard)
    return false;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  setBusy(true);
  if (!guard)
    return false;
  startInventoryLoad(QStringLiteral("audio-loaded"));
  return true;
}

void AudioAdapter::startInventoryLoad(const QString &successStatusId,
                                      const QString &operationErrorId) {
  QPointer<AudioAdapter> guard(this);
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("inventory"),
       QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumInventoryBytes),
      [guard, successStatusId, operationErrorId](int exitCode,
                                                 const QByteArray &payload,
                                                 const QString &commandError) {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failLoad(commandError.isEmpty()
                              ? QStringLiteral("backend-failed")
                              : commandError);
          return;
        }
        AudioPresentationSnapshot snapshot;
        QString errorId;
        if (!AudioContracts::decodeInventory(payload, &snapshot, &errorId)) {
          guard->failLoad(errorId);
          return;
        }
        guard->startProfilePortLoad(std::move(snapshot), successStatusId,
                                    operationErrorId);
      });
  if (!guard)
    return;
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::startProfilePortLoad(AudioPresentationSnapshot snapshot,
                                        const QString &successStatusId,
                                        const QString &operationErrorId) {
  QPointer<AudioAdapter> guard(this);
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("profile-port-inventory"),
       QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumInventoryBytes),
      [guard, snapshot = std::move(snapshot), successStatusId,
       operationErrorId](int exitCode, const QByteArray &payload,
                         const QString &commandError) mutable {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failLoad(
              commandError.isEmpty()
                  ? QStringLiteral("profile-port-inventory-unavailable")
                  : commandError);
          return;
        }
        QString errorId;
        if (!AudioContracts::decodeProfilePortInventory(payload, &snapshot,
                                                        &errorId)) {
          guard->failLoad(errorId);
          return;
        }
        guard->startPolicyLoad(std::move(snapshot), successStatusId,
                               operationErrorId);
      });
  if (!guard)
    return;
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::startPolicyLoad(AudioPresentationSnapshot snapshot,
                                   const QString &successStatusId,
                                   const QString &operationErrorId) {
  QPointer<AudioAdapter> guard(this);
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("policy"),
       QStringLiteral("show"), QStringLiteral("--format"),
       QStringLiteral("json")},
      static_cast<int>(kMaximumPolicyBytes),
      [guard, snapshot = std::move(snapshot), successStatusId,
       operationErrorId](int exitCode, const QByteArray &payload,
                         const QString &commandError) mutable {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failLoad(commandError.isEmpty()
                              ? QStringLiteral("policy-unavailable")
                              : commandError);
          return;
        }
        QString errorId;
        if (!AudioContracts::decodePolicy(payload, &snapshot, &errorId)) {
          guard->failLoad(errorId);
          return;
        }
        guard->startBrokerStatusLoad(std::move(snapshot), successStatusId,
                                     operationErrorId);
      });
  if (!guard)
    return;
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::startBrokerStatusLoad(AudioPresentationSnapshot snapshot,
                                         const QString &successStatusId,
                                         const QString &operationErrorId) {
  QPointer<AudioAdapter> guard(this);
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("broker-status"),
       QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumBrokerStatusBytes),
      [guard, snapshot = std::move(snapshot), successStatusId,
       operationErrorId](int exitCode, const QByteArray &payload,
                         const QString &commandError) mutable {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failLoad(commandError.isEmpty()
                              ? QStringLiteral("broker-status-unavailable")
                              : commandError);
          return;
        }
        QString errorId;
        if (!AudioContracts::decodeBrokerStatus(payload, &snapshot, &errorId)) {
          guard->failLoad(errorId);
          return;
        }
        guard->startGoxlrStatusLoad(std::move(snapshot), successStatusId,
                                    operationErrorId);
      });
  if (!guard)
    return;
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::startGoxlrStatusLoad(AudioPresentationSnapshot snapshot,
                                        const QString &successStatusId,
                                        const QString &operationErrorId) {
  QPointer<AudioAdapter> guard(this);
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("goxlr-status"),
       QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumGoxlrStatusBytes),
      [guard, snapshot = std::move(snapshot), successStatusId,
       operationErrorId](int exitCode, const QByteArray &payload,
                         const QString &commandError) mutable {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failLoad(commandError.isEmpty()
                              ? QStringLiteral("goxlr-status-unavailable")
                              : commandError);
          return;
        }
        QString errorId;
        if (!AudioContracts::decodeGoxlrStatus(payload, &snapshot, &errorId)) {
          guard->failLoad(errorId);
          return;
        }
        guard->publishSnapshot(std::move(snapshot), successStatusId,
                               operationErrorId);
      });
  if (!guard)
    return;
  if (!started)
    failLoad(QStringLiteral("backend-unavailable"));
}

void AudioAdapter::publishSnapshot(AudioPresentationSnapshot snapshot,
                                   const QString &successStatusId,
                                   const QString &operationErrorId) {
  QPointer<AudioAdapter> guard(this);
  snapshot_ = std::move(snapshot);
  snapshotReady_ = true;
  emit audioModelsChanged();
  if (!guard)
    return;
  setBusy(false);
  if (!guard)
    return;
  setMessage(successStatusId, operationErrorId);
  if (!guard)
    return;
  emit audioLoaded(true);
  if (!guard)
    return;
  if (!successStatusId.isEmpty() || !operationErrorId.isEmpty())
    emit audioOperationFinished(successStatusId);
}

void AudioAdapter::failLoad(const QString &errorId) {
  QPointer<AudioAdapter> guard(this);
  snapshot_ = AudioPresentationSnapshot();
  snapshot_.reason = errorId;
  snapshotReady_ = false;
  emit audioModelsChanged();
  if (!guard)
    return;
  setBusy(false);
  if (!guard)
    return;
  setMessage(QString(), errorId);
  if (!guard)
    return;
  emit audioLoaded(false);
}

void AudioAdapter::failOperation(const QString &errorId) {
  QPointer<AudioAdapter> guard(this);
  setBusy(false);
  if (!guard)
    return;
  setMessage(QString(), errorId);
  if (!guard)
    return;
  emit audioOperationFinished(QString());
}

bool AudioAdapter::validDirectionDevice(const QString &direction,
                                        const QString &deviceId) const {
  if (!snapshot_.available || !snapshot_.mutationAvailable ||
      !AudioContracts::validEndpointId(direction, deviceId))
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
  if (!AudioContracts::validStreamId(streamId))
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
      AudioContracts::controlTargetType(targetId).isEmpty())
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
  if (busy_ || selectionConfirmationOpen_ || target.isEmpty() ||
      (!volume && !mute) ||
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

  QPointer<AudioAdapter> guard(this);
  clearProcessChoice();
  if (!guard)
    return false;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  setBusy(true);
  if (!guard)
    return false;
  const bool started = startCommand(
      {QStringLiteral("audio"), planAction, QStringLiteral("--target"),
       targetId, requestedFlag, requestedText, QStringLiteral("--format"),
       QStringLiteral("json")},
      static_cast<int>(kMaximumReceiptBytes),
      [guard, targetId, control, originalValue, requestedValue, applyAction,
       requestedFlag, requestedText, originalFlag,
       originalText](int exitCode, const QByteArray &payload,
                     const QString &commandError) {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failOperation(commandError.isEmpty()
                                   ? QStringLiteral("audio-control-plan-failed")
                                   : commandError);
          return;
        }
        QString cohort;
        QString errorId;
        if (!AudioContracts::decodeControlPlan(payload, targetId, control,
                                               originalValue, requestedValue,
                                               &cohort, nullptr, &errorId)) {
          guard->failOperation(errorId);
          return;
        }
        const bool applyStarted = guard->startCommand(
            {QStringLiteral("audio"), applyAction, QStringLiteral("--target"),
             targetId, originalFlag, originalText, requestedFlag, requestedText,
             QStringLiteral("--cohort"), cohort, QStringLiteral("--ack"),
             QString::fromLatin1(kControlAcknowledgement),
             QStringLiteral("--format"), QStringLiteral("json")},
            static_cast<int>(kMaximumReceiptBytes),
            [guard, targetId, control, originalValue,
             requestedValue](int applyExitCode, const QByteArray &applyPayload,
                             const QString &applyError) {
              if (!guard)
                return;
              if (!applyError.isEmpty()) {
                guard->startInventoryLoad(QString(), applyError);
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
                guard->startInventoryLoad(QString(), receiptError);
                return;
              }
              const bool success =
                  receiptStatus == QStringLiteral("Applied") ||
                  receiptStatus == QStringLiteral("AlreadySet");
              if ((success && applyExitCode != 0) ||
                  (!success && applyExitCode != 1)) {
                guard->startInventoryLoad(QString(),
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
                guard->startInventoryLoad(status);
                return;
              }
              QString operationError =
                  receiptStatus == QStringLiteral("Refused")
                      ? QStringLiteral("audio-control-refused")
                      : QStringLiteral("audio-control-failed");
              if (rollbackAttempted && rollbackVerified)
                operationError = QStringLiteral("audio-control-restored");
              guard->startInventoryLoad(QString(), operationError);
            },
            kControlApplyTimeoutScale);
        if (!guard)
          return;
        if (!applyStarted)
          guard->failOperation(QStringLiteral("backend-unavailable"));
      });
  if (!guard)
    return started;
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

bool AudioAdapter::audioGoxlrControlState(const QString &control,
                                          int *originalValue) const {
  if (!originalValue || !snapshotReady_ ||
      snapshot_.goxlrStatus != QStringLiteral("Ready") ||
      !snapshot_.goxlrProviderActive || !snapshot_.goxlrDevicePresent ||
      !snapshot_.goxlrMutationAvailable || snapshot_.goxlrDevices.size() != 1)
    return false;
  const QVariantMap device = snapshot_.goxlrDevices.constFirst().toMap();
  const QVariantList faders = device.value(QStringLiteral("faders")).toList();
  const QStringList letters = {QStringLiteral("a"), QStringLiteral("b"),
                               QStringLiteral("c"), QStringLiteral("d")};
  for (int index = 0; index < letters.size(); ++index) {
    if (control == QStringLiteral("fader-") + letters.at(index) +
                       QStringLiteral("-volume")) {
      if (index >= faders.size() ||
          !faders.at(index).toMap()
               .value(QStringLiteral("volumeAvailable"))
               .toBool())
        return false;
      *originalValue =
          faders.at(index).toMap().value(QStringLiteral("volume")).toInt();
      return *originalValue >= 0 && *originalValue <= 255;
    }
    if (control == QStringLiteral("fader-") + letters.at(index) +
                       QStringLiteral("-mute")) {
      if (index >= faders.size() ||
          !faders.at(index).toMap()
               .value(QStringLiteral("muteAvailable"))
               .toBool())
        return false;
      *originalValue =
          faders.at(index).toMap().value(QStringLiteral("muted")).toBool()
              ? 1
              : 0;
      return true;
    }
  }
  if (control == QStringLiteral("cough-mute")) {
    const QVariantMap cough = device.value(QStringLiteral("cough")).toMap();
    if (!cough.value(QStringLiteral("available")).toBool())
      return false;
    *originalValue = cough.value(QStringLiteral("muted")).toBool() ? 1 : 0;
    return true;
  }
  const QVariantMap outputs = device.value(QStringLiteral("outputs")).toMap();
  if (control == QStringLiteral("headphones-volume")) {
    if (!outputs.value(QStringLiteral("headphonesAvailable")).toBool())
      return false;
    *originalValue =
        outputs.value(QStringLiteral("headphonesVolume")).toInt();
  } else if (control == QStringLiteral("line-out-volume")) {
    if (!outputs.value(QStringLiteral("lineOutAvailable")).toBool())
      return false;
    *originalValue = outputs.value(QStringLiteral("lineOutVolume")).toInt();
  } else {
    return false;
  }
  return *originalValue >= 0 && *originalValue <= 255;
}

bool AudioAdapter::startAudioGoxlrControl(const QString &control,
                                          int requestedValue) {
  int originalValue = 0;
  const bool mute = control.contains(QStringLiteral("mute"));
  if (busy_ || selectionConfirmationOpen_ || requestedValue < 0 ||
      requestedValue > (mute ? 1 : 255) ||
      !audioGoxlrControlState(control, &originalValue)) {
    setMessage(QString(), QStringLiteral("audio-goxlr-selection-invalid"));
    return false;
  }
  const QString requestedText = QString::number(requestedValue);
  const QString originalText = QString::number(originalValue);
  QPointer<AudioAdapter> guard(this);
  clearProcessChoice();
  if (!guard)
    return false;
  clearAudioSelection();
  if (!guard)
    return false;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  setBusy(true);
  if (!guard)
    return false;
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("plan-goxlr-control"),
       QStringLiteral("--control"), control, QStringLiteral("--value"),
       requestedText, QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumReceiptBytes),
      [guard, control, originalValue, requestedValue, originalText,
       requestedText](int exitCode, const QByteArray &payload,
                      const QString &commandError) {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->startInventoryLoad(
              QString(),
              commandError.isEmpty()
                  ? QStringLiteral("audio-goxlr-control-plan-failed")
                  : commandError);
          return;
        }
        QString cohort;
        QString errorId;
        if (!AudioContracts::decodeGoxlrControlPlan(
                payload, control, originalValue, requestedValue, &cohort,
                &errorId)) {
          guard->startInventoryLoad(QString(), errorId);
          return;
        }
        const bool applyStarted = guard->startCommand(
            {QStringLiteral("audio"), QStringLiteral("set-goxlr-control"),
             QStringLiteral("--control"), control, QStringLiteral("--value"),
             requestedText, QStringLiteral("--original"), originalText,
             QStringLiteral("--cohort"), cohort, QStringLiteral("--ack"),
             QStringLiteral("synapse-settings/audio-goxlr-popup/v1"),
             QStringLiteral("--format"), QStringLiteral("json")},
            static_cast<int>(kMaximumReceiptBytes),
            [guard, control, originalValue,
             requestedValue](int applyExitCode, const QByteArray &applyPayload,
                             const QString &applyError) {
              if (!guard)
                return;
              if (!applyError.isEmpty()) {
                guard->startInventoryLoad(QString(), applyError);
                return;
              }
              QString receiptStatus;
              int observed = 0;
              bool changed = false;
              bool rollbackAttempted = false;
              bool rollbackSucceeded = false;
              QString receiptError;
              if (!AudioContracts::decodeGoxlrControlReceipt(
                      applyPayload, control, originalValue, requestedValue,
                      &receiptStatus, &observed, &changed, &rollbackAttempted,
                      &rollbackSucceeded, &receiptError)) {
                guard->startInventoryLoad(QString(), receiptError);
                return;
              }
              const bool success =
                  receiptStatus == QStringLiteral("Applied") ||
                  receiptStatus == QStringLiteral("AlreadyApplied");
              if ((success && applyExitCode != 0) ||
                  (!success && applyExitCode != 1)) {
                guard->startInventoryLoad(
                    QString(), QStringLiteral("contract-invalid"));
                return;
              }
              if (success) {
                guard->startInventoryLoad(
                    changed ? QStringLiteral("audio-goxlr-control-applied")
                            : QStringLiteral("audio-goxlr-control-unchanged"));
                return;
              }
              QString operationError =
                  receiptStatus == QStringLiteral("Refused")
                      ? QStringLiteral("audio-goxlr-control-refused")
                      : QStringLiteral("audio-goxlr-control-failed");
              if (rollbackAttempted && rollbackSucceeded)
                operationError =
                    QStringLiteral("audio-goxlr-control-restored");
              guard->startInventoryLoad(QString(), operationError);
            },
            kControlApplyTimeoutScale);
        if (!guard)
          return;
        if (!applyStarted)
          guard->failOperation(QStringLiteral("backend-unavailable"));
      });
  if (!guard)
    return started;
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

bool AudioAdapter::setAudioGoxlrFaderVolume(int faderIndex, int value) {
  static const char *const controls[] = {
      "fader-a-volume", "fader-b-volume", "fader-c-volume",
      "fader-d-volume"};
  if (faderIndex < 0 || faderIndex >= 4) {
    setMessage(QString(), QStringLiteral("audio-goxlr-selection-invalid"));
    return false;
  }
  return startAudioGoxlrControl(QString::fromLatin1(controls[faderIndex]),
                                value);
}

bool AudioAdapter::setAudioGoxlrFaderMuted(int faderIndex, bool muted) {
  static const char *const controls[] = {"fader-a-mute", "fader-b-mute",
                                         "fader-c-mute", "fader-d-mute"};
  if (faderIndex < 0 || faderIndex >= 4) {
    setMessage(QString(), QStringLiteral("audio-goxlr-selection-invalid"));
    return false;
  }
  return startAudioGoxlrControl(QString::fromLatin1(controls[faderIndex]),
                                muted ? 1 : 0);
}

bool AudioAdapter::setAudioGoxlrCoughMuted(bool muted) {
  return startAudioGoxlrControl(QStringLiteral("cough-mute"), muted ? 1 : 0);
}

bool AudioAdapter::setAudioGoxlrHeadphonesVolume(int value) {
  return startAudioGoxlrControl(QStringLiteral("headphones-volume"), value);
}

bool AudioAdapter::setAudioGoxlrLineOutVolume(int value) {
  return startAudioGoxlrControl(QStringLiteral("line-out-volume"), value);
}

bool AudioAdapter::openGoxlrMixer() {
  if (goxlrLaunchCommand_ || busy_ || processChoiceOpen_ ||
      selectionConfirmationOpen_)
    return false;
  QString program = QStringLiteral("/usr/bin/synapse-goxlr-gui");
#ifdef SYNAPSE_SETTINGS_GUI_TEST_HOOKS
  const QString fixture =
      qEnvironmentVariable("SYNAPSE_SETTINGS_GOXLR_APP_FIXTURE");
  if (!fixture.isEmpty())
    program = fixture;
#endif
  const QFileInfo executable(program);
  if (!executable.isAbsolute() || !executable.isFile() ||
      executable.isSymLink() || !executable.isExecutable()) {
    setMessage(QString(), QStringLiteral("audio-goxlr-mixer-unavailable"));
    return false;
  }
  QPointer<AudioAdapter> guard(this);
  goxlrLaunchCommand_ = new Command(
      program, {QStringLiteral("--open-or-activate")}, 512, 5000,
      [guard](int code, const QByteArray &payload, const QString &error) {
        if (!guard)
          return;
        guard->goxlrLaunchCommand_ = nullptr;
        // v1 is a canonical finite receipt, not arbitrary backend JSON. No
        // permissive parsing, duplicate keys, extra records or exit-0 shortcut.
        const bool confirmed = code == 0 && error.isEmpty() && payload ==
            QByteArrayLiteral("{\"schema\":\"synapse.goxlr.gui-activation/v1\",\"result\":\"activation-requested\",\"sceneReady\":true,\"focusConfirmed\":false}\n");
        guard->setMessage(confirmed ? QStringLiteral("audio-goxlr-mixer-opened") : QString(),
                          confirmed ? QString() : QStringLiteral("audio-goxlr-mixer-unavailable"));
      }, this, Command::Environment::GuiActivation);
  // Install the pending owner before publishing: synchronous reentry cannot
  // launch a duplicate. Returning true means initiation, not GUI readiness.
  setMessage(QString(), QString());
  if (!guard)
    return false;
  goxlrLaunchCommand_->start();
  return true;
}

void AudioAdapter::deactivateAudio() {
  QPointer<AudioAdapter> guard(this);
  if (command_) {
    Command *running = command_;
    command_ = nullptr;
    delete running;
  }
  clearProcessChoice();
  if (!guard)
    return;
  clearAudioSelection();
  if (!guard)
    return;
  snapshot_ = AudioPresentationSnapshot();
  snapshotReady_ = false;
  busy_ = false;
  statusId_.clear();
  errorId_.clear();
  emit audioModelsChanged();
  if (!guard)
    return;
  emit audioStateChanged();
  if (!guard)
    return;
  emit audioOperationStateChanged();
}

QVariantMap AudioAdapter::audioSelectionTarget(const QString &selection,
                                               const QString &targetType,
                                               const QString &targetId) const {
  if (!snapshot_.profilePortAvailable ||
      !snapshot_.profilePortMutationAvailable ||
      !AudioContracts::validSelectionTarget(selection, targetType, targetId))
    return {};
  const QVariantList &targets = selection == QStringLiteral("profile")
                                    ? snapshot_.profileCards
                                    : snapshot_.portEndpoints;
  for (const QVariant &entry : targets) {
    const QVariantMap target = entry.toMap();
    if (target.value(QStringLiteral("id")).toString() == targetId &&
        (selection == QStringLiteral("profile") ||
         target.value(QStringLiteral("direction")).toString() == targetType) &&
        target.value(QStringLiteral("mutationAvailable")).toBool())
      return target;
  }
  return {};
}

QVariantMap
AudioAdapter::audioSelectionOption(const QVariantMap &target,
                                   const QString &selection,
                                   const QString &selectionId) const {
  if (!AudioContracts::validSelectionId(selection, selectionId))
    return {};
  const QString optionsKey = selection == QStringLiteral("profile")
                                 ? QStringLiteral("profiles")
                                 : QStringLiteral("ports");
  for (const QVariant &entry : target.value(optionsKey).toList()) {
    const QVariantMap option = entry.toMap();
    if (option.value(QStringLiteral("id")).toString() == selectionId &&
        option.value(QStringLiteral("availability")).toString() !=
            QStringLiteral("unavailable"))
      return option;
  }
  return {};
}

bool AudioAdapter::startAudioSelectionPlan(const QString &selection,
                                           const QString &targetType,
                                           const QString &targetId,
                                           const QString &selectionId) {
  const QVariantMap target =
      audioSelectionTarget(selection, targetType, targetId);
  const QVariantMap requested =
      audioSelectionOption(target, selection, selectionId);
  const QString activeKey = selection == QStringLiteral("profile")
                                ? QStringLiteral("activeProfile")
                                : QStringLiteral("activePort");
  const QString activeLabelKey = selection == QStringLiteral("profile")
                                     ? QStringLiteral("activeProfileLabel")
                                     : QStringLiteral("activePortLabel");
  const QString original = target.value(activeKey).toString();
  const QString originalLabel = target.value(activeLabelKey).toString();
  if (busy_ || selectionConfirmationOpen_ || target.isEmpty() ||
      requested.isEmpty() || !AudioContracts::validSelectionId(selection, original)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }

  AudioSelectionPlan expected;
  expected.selection = selection;
  expected.target = targetId;
  expected.targetType = targetType;
  expected.targetLabel = target.value(QStringLiteral("label")).toString();
  expected.originalSelection = original;
  expected.originalLabel = originalLabel;
  expected.requestedSelection = selectionId;
  expected.requestedLabel = requested.value(QStringLiteral("label")).toString();
  expected.requestedAvailability =
      requested.value(QStringLiteral("availability")).toString();
  if (!AudioContracts::validSelectionPlan(expected)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }

  QStringList arguments = {QStringLiteral("audio")};
  if (selection == QStringLiteral("profile")) {
    arguments << QStringLiteral("plan-profile") << QStringLiteral("--card")
              << targetId << QStringLiteral("--profile") << selectionId;
  } else {
    arguments << QStringLiteral("plan-port") << QStringLiteral("--direction")
              << targetType << QStringLiteral("--device") << targetId
              << QStringLiteral("--port") << selectionId;
  }
  arguments << QStringLiteral("--format") << QStringLiteral("json");
  QPointer<AudioAdapter> guard(this);
  clearProcessChoice();
  if (!guard)
    return false;
  clearAudioSelection();
  if (!guard)
    return false;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  setBusy(true);
  if (!guard)
    return false;
  const bool started = startCommand(
      arguments, static_cast<int>(kMaximumReceiptBytes),
      [guard, expected](int exitCode, const QByteArray &payload,
                        const QString &commandError) {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failOperation(
              commandError.isEmpty()
                  ? QStringLiteral("audio-selection-plan-failed")
                  : commandError);
          return;
        }
        AudioSelectionPlan decoded;
        QString errorId;
        if (!AudioContracts::decodeSelectionPlan(payload, expected, &decoded,
                                                 &errorId)) {
          guard->failOperation(errorId);
          return;
        }
        guard->pendingSelection_ = std::move(decoded);
        guard->selectionConfirmationOpen_ = true;
        guard->setBusy(false);
        if (!guard)
          return;
        guard->setMessage(
            QStringLiteral("audio-selection-confirmation-required"), QString());
        if (!guard)
          return;
        emit guard->audioSelectionChanged();
        if (!guard)
          return;
        emit guard->audioSelectionConfirmationRequested();
      });
  if (!guard)
    return started;
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

bool AudioAdapter::planAudioProfile(const QString &cardId,
                                    const QString &profileId) {
  return startAudioSelectionPlan(QStringLiteral("profile"),
                                 QStringLiteral("card"), cardId, profileId);
}

bool AudioAdapter::planAudioPort(const QString &direction,
                                 const QString &deviceId,
                                 const QString &portId) {
  if (direction != QStringLiteral("output") &&
      direction != QStringLiteral("input")) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  return startAudioSelectionPlan(QStringLiteral("port"), direction, deviceId,
                                 portId);
}

bool AudioAdapter::confirmAudioSelection() {
  if (busy_ || !selectionConfirmationOpen_ ||
      !AudioContracts::validSelectionPlan(pendingSelection_) ||
      !AudioContracts::validSelectionCohort(pendingSelection_.cohort)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  const AudioSelectionPlan expected = pendingSelection_;
  QStringList arguments = {QStringLiteral("audio")};
  if (expected.selection == QStringLiteral("profile")) {
    arguments << QStringLiteral("set-profile") << QStringLiteral("--card")
              << expected.target << QStringLiteral("--from-profile")
              << expected.originalSelection << QStringLiteral("--profile")
              << expected.requestedSelection;
  } else {
    arguments << QStringLiteral("set-port") << QStringLiteral("--direction")
              << expected.targetType << QStringLiteral("--device")
              << expected.target << QStringLiteral("--from-port")
              << expected.originalSelection << QStringLiteral("--port")
              << expected.requestedSelection;
  }
  arguments << QStringLiteral("--cohort") << expected.cohort
            << QStringLiteral("--ack")
            << QString::fromLatin1(kSelectionAcknowledgement)
            << QStringLiteral("--format") << QStringLiteral("json");
  QPointer<AudioAdapter> guard(this);
  clearAudioSelection();
  if (!guard)
    return false;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  setBusy(true);
  if (!guard)
    return false;
  const bool started = startCommand(
      arguments, static_cast<int>(kMaximumReceiptBytes),
      [guard, expected](int exitCode, const QByteArray &payload,
                        const QString &commandError) {
        if (!guard)
          return;
        if (!commandError.isEmpty()) {
          guard->startInventoryLoad(QString(), commandError);
          return;
        }
        QString receiptStatus;
        QString receiptReason;
        bool receiptChanged = false;
        bool rollbackAttempted = false;
        bool rollbackVerified = false;
        QString errorId;
        if (!AudioContracts::decodeSelectionReceipt(
                payload, expected, &receiptStatus, &receiptReason,
                &receiptChanged, &rollbackAttempted, &rollbackVerified,
                &errorId)) {
          guard->startInventoryLoad(QString(), errorId);
          return;
        }
        const bool success = receiptStatus == QStringLiteral("Applied") ||
                             receiptStatus == QStringLiteral("AlreadySet");
        if ((success && exitCode != 0) || (!success && exitCode != 1)) {
          guard->startInventoryLoad(QString(),
                                    QStringLiteral("contract-invalid"));
          return;
        }
        if (success) {
          const QString status =
              expected.selection == QStringLiteral("profile")
                  ? receiptChanged ? QStringLiteral("audio-profile-applied")
                                   : QStringLiteral("audio-profile-unchanged")
              : receiptChanged ? QStringLiteral("audio-port-applied")
                               : QStringLiteral("audio-port-unchanged");
          guard->startInventoryLoad(status);
          return;
        }
        QString operationError = receiptStatus == QStringLiteral("Refused")
                                     ? QStringLiteral("audio-selection-refused")
                                     : QStringLiteral("audio-selection-failed");
        if (rollbackAttempted && rollbackVerified)
          operationError = QStringLiteral("audio-selection-restored");
        guard->startInventoryLoad(QString(), operationError);
      },
      kSelectionApplyTimeoutScale);
  if (!guard)
    return started;
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

void AudioAdapter::cancelAudioSelection() {
  if (!selectionConfirmationOpen_)
    return;
  QPointer<AudioAdapter> guard(this);
  clearAudioSelection();
  if (guard)
    setMessage(QString(), QString());
}

void AudioAdapter::clearAudioSelection() {
  if (!selectionConfirmationOpen_ && pendingSelection_.selection.isEmpty())
    return;
  selectionConfirmationOpen_ = false;
  pendingSelection_ = AudioSelectionPlan();
  emit audioSelectionChanged();
}

bool AudioAdapter::setAudioDefault(const QString &direction,
                                   const QString &deviceId) {
  if (busy_ || selectionConfirmationOpen_ ||
      !validDirectionDevice(direction, deviceId)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  QPointer<AudioAdapter> guard(this);
  clearProcessChoice();
  if (!guard)
    return false;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  setBusy(true);
  if (!guard)
    return false;
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("plan-default"),
       QStringLiteral("--direction"), direction, QStringLiteral("--device"),
       deviceId, QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumReceiptBytes),
      [guard, direction, deviceId](int exitCode, const QByteArray &payload,
                                   const QString &commandError) {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failOperation(commandError.isEmpty()
                                   ? QStringLiteral("default-plan-failed")
                                   : commandError);
          return;
        }
        bool planChanged = false;
        QString errorId;
        if (!AudioContracts::decodeDefaultPlan(payload, direction, deviceId,
                                               &planChanged, &errorId)) {
          guard->failOperation(errorId);
          return;
        }
        if (!planChanged) {
          guard->startInventoryLoad(QStringLiteral("audio-default-unchanged"));
          return;
        }
        const bool applyStarted = guard->startCommand(
            {QStringLiteral("audio"), QStringLiteral("set-default"),
             QStringLiteral("--direction"), direction,
             QStringLiteral("--device"), deviceId, QStringLiteral("--ack"),
             QString::fromLatin1(kDefaultAcknowledgement),
             QStringLiteral("--format"), QStringLiteral("json")},
            static_cast<int>(kMaximumReceiptBytes),
            [guard, direction, deviceId](int applyExitCode,
                                         const QByteArray &applyPayload,
                                         const QString &applyError) {
              if (!guard)
                return;
              if (!applyError.isEmpty() || applyExitCode != 0) {
                guard->failOperation(
                    applyError.isEmpty()
                        ? QStringLiteral("default-apply-failed")
                        : applyError);
                return;
              }
              bool changed = false;
              QString receiptError;
              if (!AudioContracts::decodeDefaultReceipt(applyPayload, direction,
                                                        deviceId, &changed,
                                                        &receiptError)) {
                guard->failOperation(receiptError);
                return;
              }
              guard->startInventoryLoad(
                  changed ? QStringLiteral("audio-default-applied")
                          : QStringLiteral("audio-default-unchanged"));
            },
            kDefaultApplyTimeoutScale);
        if (!guard)
          return;
        if (!applyStarted)
          guard->failOperation(QStringLiteral("backend-unavailable"));
      });
  if (!guard)
    return started;
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

bool AudioAdapter::moveAudioStream(const QString &streamId,
                                   const QString &originalDeviceId,
                                   const QString &requestedDeviceId) {
  if (busy_ || selectionConfirmationOpen_ ||
      !validStreamMove(streamId, originalDeviceId, requestedDeviceId)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  QPointer<AudioAdapter> guard(this);
  clearProcessChoice();
  if (!guard)
    return false;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  setBusy(true);
  if (!guard)
    return false;
  const bool started = startCommand(
      {QStringLiteral("audio"), QStringLiteral("plan-stream-move"),
       QStringLiteral("--stream"), streamId, QStringLiteral("--device"),
       requestedDeviceId, QStringLiteral("--format"), QStringLiteral("json")},
      static_cast<int>(kMaximumReceiptBytes),
      [guard, streamId, originalDeviceId,
       requestedDeviceId](int exitCode, const QByteArray &payload,
                          const QString &commandError) {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failOperation(commandError.isEmpty()
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
          guard->failOperation(errorId);
          return;
        }
        if (!planChanged) {
          guard->startInventoryLoad(QStringLiteral("audio-stream-unchanged"));
          return;
        }
        const bool applyStarted = guard->startCommand(
            {QStringLiteral("audio"), QStringLiteral("move-stream"),
             QStringLiteral("--stream"), streamId,
             QStringLiteral("--from-device"), originalDeviceId,
             QStringLiteral("--device"), requestedDeviceId,
             QStringLiteral("--cohort"), cohort, QStringLiteral("--ack"),
             QString::fromLatin1(kStreamMoveAcknowledgement),
             QStringLiteral("--format"), QStringLiteral("json")},
            static_cast<int>(kMaximumReceiptBytes),
            [guard, streamId, originalDeviceId, requestedDeviceId](
                int applyExitCode, const QByteArray &applyPayload,
                const QString &applyError) {
              if (!guard)
                return;
              if (!applyError.isEmpty()) {
                guard->startInventoryLoad(QString(), applyError);
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
                guard->startInventoryLoad(QString(), receiptError);
                return;
              }
              const bool success =
                  receiptStatus == QStringLiteral("Applied") ||
                  receiptStatus == QStringLiteral("AlreadyRouted");
              if ((success && applyExitCode != 0) ||
                  (!success && applyExitCode != 1)) {
                guard->startInventoryLoad(QString(),
                                          QStringLiteral("contract-invalid"));
                return;
              }
              if (success) {
                guard->startInventoryLoad(
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
              guard->startInventoryLoad(QString(), operationError);
            },
            kStreamMoveApplyTimeoutScale);
        if (!guard)
          return;
        if (!applyStarted)
          guard->failOperation(QStringLiteral("backend-unavailable"));
      });
  if (!guard)
    return started;
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

bool AudioAdapter::chooseAudioProcessRule(const QString &direction,
                                          const QString &deviceId) {
  if (busy_ || selectionConfirmationOpen_ ||
      !validDirectionDevice(direction, deviceId)) {
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
  QPointer<AudioAdapter> guard(this);
  processChoices_ = std::move(choices);
  pendingProcessDirection_ = direction;
  pendingProcessDevice_ = deviceId;
  processChoiceOpen_ = true;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  emit audioProcessChoiceChanged();
  if (!guard)
    return false;
  emit audioProcessChoiceRequested();
  return true;
}

bool AudioAdapter::confirmAudioProcessRule(const QString &streamId) {
  if (busy_ || selectionConfirmationOpen_ || !processChoiceOpen_)
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
  QPointer<AudioAdapter> guard(this);
  clearProcessChoice();
  if (!guard)
    return false;
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
  if (busy_ || selectionConfirmationOpen_ ||
      !validDirectionDevice(direction, deviceId)) {
    setMessage(QString(), QStringLiteral("selection-invalid"));
    return false;
  }
  const PathChooser chooser = pathChooser_;
  QPointer<AudioAdapter> guard(this);
  const QString path = chooser(matchType);
  if (!guard || path.isEmpty())
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
  if (!AudioContracts::validRuleId(ruleId))
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
  if (busy_ || selectionConfirmationOpen_ || !validRule(ruleId)) {
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
  if (busy_ || selectionConfirmationOpen_)
    return false;
  QPointer<AudioAdapter> guard(this);
  clearProcessChoice();
  if (!guard)
    return false;
  setMessage(QString(), QString());
  if (!guard)
    return false;
  setBusy(true);
  if (!guard)
    return false;
  const bool started = startCommand(
      arguments, static_cast<int>(kMaximumReceiptBytes),
      [guard, action, expectedDevice,
       expectedRule](int exitCode, const QByteArray &payload,
                     const QString &commandError) {
        if (!guard)
          return;
        if (!commandError.isEmpty() || exitCode != 0) {
          guard->failOperation(commandError.isEmpty()
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
          guard->failOperation(errorId);
          return;
        }
        QString status = QStringLiteral("audio-route-rule-unchanged");
        if (action == QStringLiteral("remove-rule"))
          status = QStringLiteral("audio-route-rule-removed");
        else if (changed)
          status = QStringLiteral("audio-route-rule-saved");
        guard->startInventoryLoad(status);
      });
  if (!guard)
    return started;
  if (!started)
    failOperation(QStringLiteral("backend-unavailable"));
  return started;
}

void AudioAdapter::clearAudioMessage() { setMessage(QString(), QString()); }
