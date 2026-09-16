# JSON contracts

All contracts are UTF-8, locale-independent and versioned. Unknown input-policy
fields fail closed. At the raw PipeWire-Pulse boundary, C11 validates strict JSON
syntax, UTF-8, decoded-key uniqueness, document completion, 64-level nesting,
4096 keys per object and 16384 keys per document before `json-c` decoding. Raw
or escaped NUL, malformed escapes or surrogates, invalid literals/numbers,
trailing commas/data and the first byte beyond 1 MiB fail closed; exact limits
and valid surrogate pairs are accepted. Before Qt JSON decoding, the adapter
independently enforces the same strict lexical grammar, decoded-key uniqueness,
UTF-8 round trip, completion, nesting and key-count boundaries.

| Schema | Purpose |
|---|---|
| `synapse.settings.sections/v2` | Lazy Settings section inventory |
| `synapse.settings.audio-inventory/v2` | Bounded outputs, inputs, streams and cards with opaque active-profile tokens |
| `synapse.settings.audio-profile-port-inventory/v1` | Bounded card-profile and physical endpoint-port choices |
| `synapse.settings.audio-profile-port-plan/v1` | Read-only one-target profile or port plan |
| `synapse.settings.audio-profile-port-receipt/v1` | Verified profile/port selection or compensation result |
| `synapse.settings.audio-default-plan/v1` | Read-only default-device plan |
| `synapse.settings.audio-default-receipt/v1` | Verified default-device transaction |
| `synapse.settings.audio-control-plan/v1` | Read-only one-target volume or mute plan |
| `synapse.settings.audio-control-receipt/v1` | Verified one-target control or compensation result |
| `synapse.settings.audio-existing-stream-move-plan/v1` | Read-only plan bound to one active-stream cohort |
| `synapse.settings.audio-existing-stream-move-receipt/v1` | Explicit one-stream move, postflight and rollback result |
| `synapse.settings.audio-route-policy/v1` | Private canonical persisted policy |
| `synapse.settings.audio-route-policy-view/v1` | Redacted presentation model |
| `synapse.settings.audio-route-policy-receipt/v1` | Atomic policy mutation receipt |
| `synapse.settings.audio-route-resolution/v1` | Deterministic per-executable resolution |
| `synapse.settings.audio-route-broker-status/v1` | Read-only capability or active new-stream broker status |
| `synapse.settings.audio-route-broker-receipt/v1` | Verified per-new-stream enforcement result |
| `synapse.settings.audio-goxlr-status/v2` | Read-only redacted GoXLR provider/presence and popup presentation state |
| `synapse.settings.audio-goxlr-control-plan/v1` | Read-only one-control GoXLR provider-model plan |
| `synapse.settings.audio-goxlr-control-receipt/v1` | Verified, refused, drifted or compensated GoXLR provider-model result |

The policy receipt distinguishes `policyApplied` from `routingApplied`. Alpha 8
still sets the former true and the latter false because a policy write never
proves a broker move. The resolution contract returns one
of `exact-executable`, `directory-prefix`, or `system-default` and separately
reports whether the selected endpoint is currently available.

Opaque Audio IDs are scoped by direction (`output-…` or `input-…`). Cards are
`card-…`; base inventory v2 carries an opaque owner-scoped `profile-…` active
selection rather than a raw backend profile name. Stream IDs are bounded
`playback-N` or `recording-N` tokens and must be unique within one inventory
cohort. Raw backend node and choice names are never public contract fields.
Backend indexes must be exact nonnegative JSON integers through `INT_MAX` and
unique within their raw inventories. Invalid nonempty default sink/source raw
identities clear the complete inventory; an empty string means no named default.
Present mute/volume/property fields retain exact types, volume channels are
integers through `UINT32_MAX` with checked aggregation, and present application
or media labels are bounded strings. Missing optional fields retain only their
defined fallbacks; malformed process identity disables process-bound rule
selection.

The profile/port inventory is independent from base inventory and fixes
`stateAuthority=pipewire-pulse-model`, `hardwareReadback=false` and
`hardwareExactRollback=false`. It carries at most 32 cards, 128 endpoints, 64
choices per target, 512 profiles and 512 ports. A choice has one owner-scoped
`profile-…` or `port-…` identity, bounded label and availability of `available`,
`unknown` or `unavailable`. A missing backend label remains an empty presentation
string rather than falling back to a raw identity. Unknown remains selectable;
unavailable never appears in an accepted plan. The raw 64-input limit applies
before monitor filtering. Every excluded monitor must still validate its raw
identity, index, metadata, label, choices, availability, active selection and
identity collisions; its choices consume the global port budget. Omitted
`profiles` or `ports` fields mean no advertised choices. When present,
PulseAudio 17 card `profiles` must be an object keyed by the raw profile name,
with an optional boolean `available`; endpoint `ports` must be an array of
objects with a bounded `name` and optional C-locale `availability` string equal
to `available`, `availability unknown` or `not available`. These backend values
normalize to the public three-state availability above. A present collection or
availability value cannot be null. Null active selections require null active
labels and disable mutation for that target. An unavailable inventory has a
bounded reason, no targets and no mutation availability.

The profile/port plan fixes `status=Planned`, `singleTarget=true`,
`postflightRequired=true`, `rollbackOnUnverified=true`, `applied=false`, and the
exact acknowledgement `synapse-settings/audio-profile-port/v1`. Its
`selection-…` cohort binds the private target name/index, target kind, exact
original/requested owner-scoped choices and requested availability. A profile
uses a card and profile tokens with `graphMayChange=true`; a port uses a matching
output/input and port tokens with `graphMayChange=false`. Both set
`signalPathMayChange=true` while playback, capture, default-device changes,
policy changes, audibility verification, hardware readback and exact hardware
rollback remain false.

The receipt is `Applied`, `AlreadySet`, `Refused` or `Failed`. It separately
reports requested-state verification and compensation attempt/verification.
Only a verified changed profile or port may set its corresponding change flag.
Refusal cannot claim a mutation; failure cannot claim a requested change;
`rollback-failed` requires an actual unverified compensation attempt. A verified
compensation is permitted only for mutation timeout/failure or requested-state
verification failure, only after the first postflight observed the requested
selection, and only while a fresh same-identity read still observes it. An
immediate or intervening third selection is never overwritten.
Target/direction/choice shapes and change-kind invariants remain exact.

The control plan fixes `status=Planned`, `singleTarget=true`,
`safeVolumeMaximumPercent=100`, `postflightRequired=true`,
`rollbackOnUnverified=true`, and `applied=false`. Its `control-…` cohort binds
private target identity, target kind, backend index, control kind and exact
original/requested values. Volume is an integer; mute is a boolean. Requested
volume is 0–100 while original volume may be 0–999 solely to permit exact
rollback. A receipt is `Applied`, `AlreadySet`, `Refused` or `Failed` and
separately reports mutation attempt, verified requested result, compensation
attempt and verified compensation. `rollback-failed` requires an actual,
unverified compensation attempt; a verified compensation is limited to the
original mutation/verification failure reasons. `changed` means a verified
requested result, not a claim that failed compensation left state unchanged.
All control contracts fix playback, capture, profile and routing side effects
false.

The existing-stream plan fixes `status=Planned`, `singleStream=true`,
`postflightRequired=true`, `rollbackOnUnverified=true`, `applied=false`, and the
exact acknowledgement identifier. Its `move-…` value is an opaque bounded cohort
binding the private stream index, process instance, executable and exact current
and requested raw endpoints. The receipt is one of `Applied`, `AlreadyRouted`,
`Refused`, or `Failed`; cross-field invariants prevent any refused or failed
receipt from claiming `changed`, `moveApplied` or `verified`. A verified rollback
requires `rollbackAttempted=true`. Every receipt fixes `policyApplied=false`,
`persistentRuleCreated=false`, and `existingStreamMovement=true`.

The broker status distinguishes source capability, process activity and actual
enforcement availability. The broker serves the same contract for one fixed
`status-v1` request over its owner-private, status-only AF_UNIX channel. Absence,
stale sockets, unsafe ownership or modes, timeout, response limits and malformed
responses map to the schema's bounded identifiers such as `broker-not-running`,
`runtime-unavailable`, `runtime-state-invalid`, `timeout` and `invalid-response`,
with `active=false` and `enforcementAvailable=false`. Its event receipt is restricted to
`new-streams-only`; `routingApplied=true` requires `status=Applied`,
`changed=true`, and verified post-state. Failed compensation and unverified
command success cannot claim routing. Both broker contracts fix
`existingStreamMigration=false` and contain no PID, path, raw endpoint or event
line.

The GoXLR status v2 contract has exactly fourteen top-level fields. `status` is
`Ready`, `Inactive`, `Unavailable` or `Failed`. Only Ready permits
`providerActive=true` or a device; Ready has `reason=null`, known presence and
no more than one device. `presenceKnown` and `devicePresent` are independent
from provider activity so an inactive provider may still report an attached
GoXLR after a separate read-only inventory probe. Non-Ready states expose no
control device. Reasons are bounded to adapter absence, provider inactivity,
timeout, oversized response, invalid response or local status unavailability.

A projected device has exactly `model`, `profileModelReady`,
`systemOutputSupported`, `controlAvailable`, four ordered faders, cough and
outputs. Each fader carries its letter, assigned channel, 0–255 volume,
two-state mute and independent volume/mute availability. Cough carries
`Toggle` or `Hold`, mute and availability; availability must be false for Hold.
Outputs carry 0–255 headphones/Line Out values, a bounded monitored-output enum
and independent availability. A capability can be true only for a profile-ready
device. Top-level `mutationAvailable` is true exactly when at least one
independent control is available. The contract always fixes
`stateAuthority=provider-profile-model`, `hardwareReadback=false`,
`hardwareExactRollback=false`, `inspectionReadOnly=true` and `bounded=true`.
Generation, provider token, profile name, serial, USB bus/address/path and raw
routing never enter this projection.

The C11 bridge accepts only the complete upstream
`synapse.goxlr.provider-status/v3` contract. It validates exact fields, source
authority, one-device projection, generation, all eleven capability booleans,
fader assignments, mute/cough/output values and complete system-output fields
before discarding non-presentation state. If status is inactive or failed, it
may separately accept only `synapse.goxlr.inventory/v1` from fixed
`inventory --format json` argv to establish presence. Neither inspection starts
the provider.

The GoXLR control plan fixes `status=Planned`, one of eleven control IDs, exact
integer original/requested values, one lowercase 16-hex cohort,
`requiresAcknowledgement=synapse-settings/audio-goxlr-popup/v1`,
`stateAuthority=provider-profile-model`, and `applied=false`. Volume values are
0–255; mute values are 0 or 1. The cohort remains opaque to QML and binds the
provider device, model, generation, control, assigned channel, value kind and
exact values.

The control receipt is `Applied`, `AlreadyApplied`, `Refused`, `Drifted`,
`RolledBack` or `RollbackFailed`. It carries exact original, requested and fresh
observed values, `changed`, provider-model compensation attempt/result and
fixed no-readback/no-hardware-exact-rollback/no-playback/no-capture flags.
`Applied` and `AlreadyApplied` are the only successful CLI exits. `RolledBack`
means only verified provider-model restoration; it is not hardware restoration.
`RollbackFailed` and `Drifted` remain uncertain. Cross-field invariants prevent
refusal, drift or compensation from claiming a successful requested change.

The Qt adapter validates exact field sets, bounds, status coherence,
independent capability coherence and plan/receipt semantics before atomic
publication. It never forwards profile/port or GoXLR cohorts,
acknowledgements, provider IDs, generations, socket paths, raw contract objects
or transport text to QML.
