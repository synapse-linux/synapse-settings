// SPDX-License-Identifier: MIT
#pragma once
// Private typed wire contracts. No QObject lifecycle or command execution.
#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVariantList>

namespace AudioLimits {
inline constexpr qsizetype kMaximumInventoryBytes = qsizetype{1024} * 1024;
inline constexpr qsizetype kMaximumPolicyBytes = qsizetype{128} * 1024;
inline constexpr qsizetype kMaximumBrokerStatusBytes = 4096;
inline constexpr qsizetype kMaximumGoxlrStatusBytes = qsizetype{16} * 1024;
inline constexpr qsizetype kMaximumReceiptBytes = qsizetype{64} * 1024;
inline constexpr int kMaximumLabelBytes = 255;
inline constexpr int kMaximumPathBytes = 4095;
inline constexpr int kMaximumEndpoints = 64;
inline constexpr int kMaximumStreams = 128;
inline constexpr int kMaximumCards = 32;
inline constexpr int kMaximumSelectionOptions = 64;
inline constexpr int kMaximumProfileOptions = 512;
inline constexpr int kMaximumPortOptions = 512;
inline constexpr int kMaximumRules = 128;
inline constexpr int kMaximumCommandTimeoutMilliseconds = 120000;
inline constexpr int kDefaultApplyTimeoutScale = 3;
inline constexpr int kStreamMoveApplyTimeoutScale = 5;
inline constexpr int kControlApplyTimeoutScale = 6;
inline constexpr int kSelectionApplyTimeoutScale = 3;

inline constexpr char kDefaultAcknowledgement[] =
    "synapse-settings/audio-default/v1";
inline constexpr char kRouteAcknowledgement[] =
    "synapse-settings/audio-route-policy/v1";
inline constexpr char kStreamMoveAcknowledgement[] =
    "synapse-settings/audio-existing-stream-move/v1";
inline constexpr char kControlAcknowledgement[] =
    "synapse-settings/audio-control/v1";
inline constexpr char kSelectionAcknowledgement[] =
    "synapse-settings/audio-profile-port/v1";
inline constexpr int kSafeVolumeMaximumPercent = 100;

} // namespace AudioLimits

struct AudioPresentationSnapshot {
  bool available = false;
  bool mutationAvailable = false;
  QString reason;
  QVariantList outputs;
  QVariantList inputs;
  QVariantList streams;
  QVariantList cards;
  bool profilePortAvailable = false;
  bool profilePortMutationAvailable = false;
  QString profilePortReason;
  QVariantList profileCards;
  QVariantList portEndpoints;
  QVariantList routeRules;
  bool routeBrokerAvailable = false;
  bool routeBrokerActive = false;
  QString routeBrokerReason;
  bool routeEnforcementAvailable = false;
  QString goxlrStatus;
  QString goxlrReason;
  bool goxlrProviderActive = false;
  bool goxlrPresenceKnown = false;
  bool goxlrDevicePresent = false;
  bool goxlrMutationAvailable = false;
  bool goxlrTruncated = false;
  QVariantList goxlrDevices;
};

struct AudioSelectionPlan {
  QString selection;
  QString target;
  QString targetType;
  QString targetLabel;
  QString originalSelection;
  QString originalLabel;
  QString requestedSelection;
  QString requestedLabel;
  QString requestedAvailability;
  QString cohort;
  bool changed = false;
};

namespace AudioContracts {

bool decodeInventory(const QByteArray &payload,
                     AudioPresentationSnapshot *snapshot, QString *errorId);
bool decodeProfilePortInventory(const QByteArray &payload,
                                AudioPresentationSnapshot *snapshot,
                                QString *errorId);
bool decodePolicy(const QByteArray &payload,
                  AudioPresentationSnapshot *snapshot, QString *errorId);
bool decodeBrokerStatus(const QByteArray &payload,
                        AudioPresentationSnapshot *snapshot, QString *errorId);
bool decodeGoxlrStatus(const QByteArray &payload,
                       AudioPresentationSnapshot *snapshot, QString *errorId);
bool decodeGoxlrControlPlan(const QByteArray &payload,
                            const QString &expectedControl,
                            int expectedOriginal, int expectedRequested,
                            QString *cohort, QString *errorId);
bool decodeGoxlrControlReceipt(const QByteArray &payload,
                               const QString &expectedControl,
                               int expectedOriginal, int expectedRequested,
                               QString *status, int *observed, bool *changed,
                               bool *rollbackAttempted, bool *rollbackSucceeded,
                               QString *errorId);
bool decodeStreamMovePlan(const QByteArray &payload,
                          const QString &expectedStream,
                          const QString &expectedOriginalDevice,
                          const QString &expectedRequestedDevice,
                          QString *cohort, bool *changed, QString *errorId);
bool decodeStreamMoveReceipt(const QByteArray &payload,
                             const QString &expectedStream,
                             const QString &expectedOriginalDevice,
                             const QString &expectedRequestedDevice,
                             QString *status, QString *reason, bool *changed,
                             bool *rollbackAttempted, bool *rollbackVerified,
                             QString *errorId);
bool decodeDefaultPlan(const QByteArray &payload,
                       const QString &expectedDirection,
                       const QString &expectedDevice, bool *changed,
                       QString *errorId);
bool decodeDefaultReceipt(const QByteArray &payload,
                          const QString &expectedDirection,
                          const QString &expectedDevice, bool *changed,
                          QString *errorId);
bool decodeControlPlan(const QByteArray &payload, const QString &expectedTarget,
                       const QString &expectedControl,
                       const QVariant &expectedOriginalValue,
                       const QVariant &expectedRequestedValue, QString *cohort,
                       bool *changed, QString *errorId);
bool decodeControlReceipt(const QByteArray &payload,
                          const QString &expectedTarget,
                          const QString &expectedControl,
                          const QVariant &expectedOriginalValue,
                          const QVariant &expectedRequestedValue,
                          QString *status, QString *reason, bool *changed,
                          bool *rollbackAttempted, bool *rollbackVerified,
                          QString *errorId);
bool decodeRouteReceipt(const QByteArray &payload,
                        const QString &expectedAction,
                        const QString &expectedDevice,
                        const QString &expectedRule, QString *resultRule,
                        bool *changed, QString *errorId);
bool decodeSelectionPlan(const QByteArray &payload,
                         const AudioSelectionPlan &expected,
                         AudioSelectionPlan *decoded, QString *errorId);
bool decodeSelectionReceipt(const QByteArray &payload,
                            const AudioSelectionPlan &expected, QString *status,
                            QString *reason, bool *changed,
                            bool *rollbackAttempted, bool *rollbackVerified,
                            QString *errorId);

// Internal semantic validation used by the coordinator; no raw regex API.
bool validEndpointId(const QString &direction, const QString &id);
bool validStreamId(const QString &id);
bool validRuleId(const QString &id);
QString controlTargetType(const QString &id);
bool validSelectionId(const QString &selection, const QString &id);
bool validSelectionTarget(const QString &selection, const QString &type,
                          const QString &id);
bool validSelectionPlan(const AudioSelectionPlan &plan);
bool validSelectionCohort(const QString &cohort);

} // namespace AudioContracts
