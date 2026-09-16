// SPDX-License-Identifier: MIT
#ifndef SYNAPSE_SETTINGS_GUI_AUDIO_ADAPTER_H
#define SYNAPSE_SETTINGS_GUI_AUDIO_ADAPTER_H

#include "audio_contracts_p.h"

class AudioCommand;
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <functional>

class AudioAdapter final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool audioBusy READ audioBusy NOTIFY audioStateChanged)
  Q_PROPERTY(
      bool audioSnapshotReady READ audioSnapshotReady NOTIFY audioModelsChanged)
  Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY audioModelsChanged)
  Q_PROPERTY(QString audioReason READ audioReason NOTIFY audioModelsChanged)
  Q_PROPERTY(
      QVariantList audioOutputs READ audioOutputs NOTIFY audioModelsChanged)
  Q_PROPERTY(
      QVariantList audioInputs READ audioInputs NOTIFY audioModelsChanged)
  Q_PROPERTY(
      QVariantList audioStreams READ audioStreams NOTIFY audioModelsChanged)
  Q_PROPERTY(QVariantList audioCards READ audioCards NOTIFY audioModelsChanged)
  Q_PROPERTY(bool audioProfilePortAvailable READ audioProfilePortAvailable
                 NOTIFY audioModelsChanged)
  Q_PROPERTY(bool audioProfilePortMutationAvailable READ
                 audioProfilePortMutationAvailable NOTIFY audioModelsChanged)
  Q_PROPERTY(QString audioProfilePortReason READ audioProfilePortReason NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(QVariantList audioProfileCards READ audioProfileCards NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(QVariantList audioPortEndpoints READ audioPortEndpoints NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(QVariantList audioRouteRules READ audioRouteRules NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(bool audioRouteBrokerAvailable READ audioRouteBrokerAvailable
                 NOTIFY audioModelsChanged)
  Q_PROPERTY(bool audioRouteBrokerActive READ audioRouteBrokerActive NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(QString audioRouteBrokerReason READ audioRouteBrokerReason NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(bool audioRouteEnforcementAvailable READ
                 audioRouteEnforcementAvailable NOTIFY audioModelsChanged)
  Q_PROPERTY(
      QString audioGoxlrStatus READ audioGoxlrStatus NOTIFY audioModelsChanged)
  Q_PROPERTY(
      QString audioGoxlrReason READ audioGoxlrReason NOTIFY audioModelsChanged)
  Q_PROPERTY(bool audioGoxlrProviderActive READ audioGoxlrProviderActive NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(bool audioGoxlrPresenceKnown READ audioGoxlrPresenceKnown NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(bool audioGoxlrDevicePresent READ audioGoxlrDevicePresent NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(bool audioGoxlrMutationAvailable READ audioGoxlrMutationAvailable
                 NOTIFY audioModelsChanged)
  Q_PROPERTY(bool audioGoxlrTruncated READ audioGoxlrTruncated NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(QVariantList audioGoxlrDevices READ audioGoxlrDevices NOTIFY
                 audioModelsChanged)
  Q_PROPERTY(QVariantList audioProcessChoices READ audioProcessChoices NOTIFY
                 audioProcessChoiceChanged)
  Q_PROPERTY(bool audioProcessChoiceOpen READ audioProcessChoiceOpen NOTIFY
                 audioProcessChoiceChanged)
  Q_PROPERTY(bool audioSelectionConfirmationOpen READ
                 audioSelectionConfirmationOpen NOTIFY audioSelectionChanged)
  Q_PROPERTY(QString audioSelectionKind READ audioSelectionKind NOTIFY
                 audioSelectionChanged)
  Q_PROPERTY(QString audioSelectionTargetLabel READ audioSelectionTargetLabel
                 NOTIFY audioSelectionChanged)
  Q_PROPERTY(QString audioSelectionOriginalLabel READ
                 audioSelectionOriginalLabel NOTIFY audioSelectionChanged)
  Q_PROPERTY(QString audioSelectionRequestedLabel READ
                 audioSelectionRequestedLabel NOTIFY audioSelectionChanged)
  Q_PROPERTY(QString audioStatusId READ audioStatusId NOTIFY
                 audioOperationStateChanged)
  Q_PROPERTY(
      QString audioErrorId READ audioErrorId NOTIFY audioOperationStateChanged)

public:
  using PathChooser = std::function<QString(const QString &matchType)>;

  explicit AudioAdapter(QObject *parent = nullptr);
  explicit AudioAdapter(QString backendPath, QObject *parent = nullptr);
  AudioAdapter(QString backendPath, PathChooser pathChooser,
               int commandTimeoutMilliseconds, QObject *parent = nullptr);
  ~AudioAdapter() override;

  bool audioBusy() const;
  bool audioSnapshotReady() const;
  bool audioAvailable() const;
  QString audioReason() const;
  QVariantList audioOutputs() const;
  QVariantList audioInputs() const;
  QVariantList audioStreams() const;
  QVariantList audioCards() const;
  bool audioProfilePortAvailable() const;
  bool audioProfilePortMutationAvailable() const;
  QString audioProfilePortReason() const;
  QVariantList audioProfileCards() const;
  QVariantList audioPortEndpoints() const;
  QVariantList audioRouteRules() const;
  bool audioRouteBrokerAvailable() const;
  bool audioRouteBrokerActive() const;
  QString audioRouteBrokerReason() const;
  bool audioRouteEnforcementAvailable() const;
  QString audioGoxlrStatus() const;
  QString audioGoxlrReason() const;
  bool audioGoxlrProviderActive() const;
  bool audioGoxlrPresenceKnown() const;
  bool audioGoxlrDevicePresent() const;
  bool audioGoxlrMutationAvailable() const;
  bool audioGoxlrTruncated() const;
  QVariantList audioGoxlrDevices() const;
  QVariantList audioProcessChoices() const;
  bool audioProcessChoiceOpen() const;
  bool audioSelectionConfirmationOpen() const;
  QString audioSelectionKind() const;
  QString audioSelectionTargetLabel() const;
  QString audioSelectionOriginalLabel() const;
  QString audioSelectionRequestedLabel() const;
  QString audioStatusId() const;
  QString audioErrorId() const;

  Q_INVOKABLE bool loadAudio();
  Q_INVOKABLE bool setAudioDefault(const QString &direction,
                                   const QString &deviceId);
  Q_INVOKABLE bool moveAudioStream(const QString &streamId,
                                   const QString &originalDeviceId,
                                   const QString &requestedDeviceId);
  Q_INVOKABLE bool setAudioVolume(const QString &targetId, int percent);
  Q_INVOKABLE bool setAudioMuted(const QString &targetId, bool muted);
  Q_INVOKABLE bool setAudioGoxlrFaderVolume(int faderIndex, int value);
  Q_INVOKABLE bool setAudioGoxlrFaderMuted(int faderIndex, bool muted);
  Q_INVOKABLE bool setAudioGoxlrCoughMuted(bool muted);
  Q_INVOKABLE bool setAudioGoxlrHeadphonesVolume(int value);
  Q_INVOKABLE bool setAudioGoxlrLineOutVolume(int value);
  Q_INVOKABLE bool openGoxlrMixer();
  Q_INVOKABLE void deactivateAudio();
  Q_INVOKABLE bool planAudioProfile(const QString &cardId,
                                    const QString &profileId);
  Q_INVOKABLE bool planAudioPort(const QString &direction,
                                 const QString &deviceId,
                                 const QString &portId);
  Q_INVOKABLE bool confirmAudioSelection();
  Q_INVOKABLE void cancelAudioSelection();
  Q_INVOKABLE bool chooseAudioProcessRule(const QString &direction,
                                          const QString &deviceId);
  Q_INVOKABLE bool confirmAudioProcessRule(const QString &streamId);
  Q_INVOKABLE void cancelAudioProcessRule();
  Q_INVOKABLE bool chooseAudioExecutableRule(const QString &direction,
                                             const QString &deviceId);
  Q_INVOKABLE bool chooseAudioDirectoryRule(const QString &direction,
                                            const QString &deviceId);
  Q_INVOKABLE bool removeAudioRouteRule(const QString &ruleId);
  Q_INVOKABLE void clearAudioMessage();

signals:
  void audioStateChanged();
  void audioModelsChanged();
  void audioProcessChoiceChanged();
  void audioSelectionChanged();
  void audioOperationStateChanged();
  void audioProcessChoiceRequested();
  void audioSelectionConfirmationRequested();
  void audioLoaded(bool success);
  void audioOperationFinished(const QString &statusId);

private:
  using Command = AudioCommand;

  bool startCommand(
      const QStringList &arguments, int outputLimit,
      std::function<void(int, const QByteArray &, const QString &)> callback,
      int timeoutScale = 1);
  void startInventoryLoad(const QString &successStatusId,
                          const QString &operationErrorId = QString());
  void startProfilePortLoad(AudioPresentationSnapshot snapshot,
                            const QString &successStatusId,
                            const QString &operationErrorId);
  void startPolicyLoad(AudioPresentationSnapshot snapshot,
                       const QString &successStatusId,
                       const QString &operationErrorId);
  void startBrokerStatusLoad(AudioPresentationSnapshot snapshot,
                             const QString &successStatusId,
                             const QString &operationErrorId);
  void startGoxlrStatusLoad(AudioPresentationSnapshot snapshot,
                            const QString &successStatusId,
                            const QString &operationErrorId);
  void publishSnapshot(AudioPresentationSnapshot snapshot,
                       const QString &successStatusId,
                       const QString &operationErrorId);
  void failLoad(const QString &errorId);
  void failOperation(const QString &errorId);
  void setBusy(bool busy);
  void setMessage(const QString &statusId, const QString &errorId);
  bool validDirectionDevice(const QString &direction,
                            const QString &deviceId) const;
  QVariantMap audioStream(const QString &streamId) const;
  bool validStreamMove(const QString &streamId, const QString &originalDeviceId,
                       const QString &requestedDeviceId) const;
  QVariantMap audioControlTarget(const QString &targetId) const;
  bool startAudioControl(const QString &targetId, const QString &control,
                         const QVariant &requestedValue);
  bool startAudioGoxlrControl(const QString &control, int requestedValue);
  bool audioGoxlrControlState(const QString &control, int *originalValue) const;
  QVariantMap audioSelectionTarget(const QString &selection,
                                   const QString &targetType,
                                   const QString &targetId) const;
  QVariantMap audioSelectionOption(const QVariantMap &target,
                                   const QString &selection,
                                   const QString &selectionId) const;
  bool startAudioSelectionPlan(const QString &selection,
                               const QString &targetType,
                               const QString &targetId,
                               const QString &selectionId);
  void clearAudioSelection();
  bool validRule(const QString &ruleId) const;
  QVariantMap routeRule(const QString &ruleId) const;
  bool startRouteRule(const QString &action, const QStringList &arguments,
                      const QString &expectedDevice,
                      const QString &expectedRule);
  bool choosePathRule(const QString &matchType, const QString &direction,
                      const QString &deviceId);
  QString defaultChoosePath(const QString &matchType);
  void clearProcessChoice();

  QString backendPath_;
  PathChooser pathChooser_;
  Command *command_ = nullptr;
  Command *goxlrLaunchCommand_ = nullptr; // independent finite presentation request
  AudioPresentationSnapshot snapshot_;
  QVariantList processChoices_;
  QString pendingProcessDirection_;
  QString pendingProcessDevice_;
  AudioSelectionPlan pendingSelection_;
  QString statusId_;
  QString errorId_;
  int commandTimeoutMilliseconds_ = 15000;
  bool busy_ = false;
  bool snapshotReady_ = false;
  bool processChoiceOpen_ = false;
  bool selectionConfirmationOpen_ = false;
};

#endif
