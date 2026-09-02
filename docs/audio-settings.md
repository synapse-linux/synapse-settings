# Audio Settings scope

## Alpha 3 boundary

The C11 backend provides:

- bounded outputs, physical inputs, streams and cards; Pulse monitor sources are
  excluded from the default-input selector;
- stable opaque endpoint identities;
- default output/input planning;
- exact-acknowledgement, fixed-argv default mutation;
- post-write verification and deterministic unavailability;
- private application route policy for exact executables and directory prefixes;
- independent output and input rules;
- deterministic policy resolution.

The exact default acknowledgement is `synapse-settings/audio-default/v1`. The
application-policy acknowledgement is `synapse-settings/audio-route-policy/v1`.
Both constants are owned by the trusted adapter/core boundary and never reach
QML.

The Alpha 3 Qt adapter strictly decodes these contracts, publishes bounded
QVariant projections, serializes GUI operations, owns native path choosers and
performs a complete refresh after every accepted receipt. The standalone host
uses only Qt Quick feature QML, so the Audio section does not depend on
Quickshell-specific types.

## Alpha 4 new-stream broker

The first-party broker is a separate C11 executable. It starts the fixed Pulse
subscriber, captures the current stream cohort as a baseline, and enforces only
later unseen `new` events. Startup and restart therefore cannot move existing
streams. Policy updates and hotplug affect the resolution of a later new stream,
but never trigger reconciliation of an already active stream.

Each event is bounded and parsed into an opaque stream token. The broker reloads
the same-UID process start time and executable, policy generation, current
endpoint and target endpoint before moving. It verifies the same identity and
target afterwards. Timeout, endpoint disappearance, PID reuse, stream reuse,
malformed events and verification failure fail closed. Receipts redact all raw
process and PipeWire metadata. Existing-stream migration is explicitly false.

`--probe` is read-only. `--foreground` is intended only for the separately gated
systemd user service. Test event limits and executable overrides are absent from
the production binary.

## Alpha 5 read-only runtime status

The foreground broker serves one fixed `status-v1` request over an owner-mode-
0600 AF_UNIX socket in `$XDG_RUNTIME_DIR/synapse`, which must itself be an owner-
mode-0700 real directory. It verifies same-UID peers and exposes no operation for
starting, stopping, configuring or mutating Audio. Request and response size,
connect/read/write duration and accepted-client work are bounded. A mode-0600
lock prevents concurrent brokers from replacing one another's sockets.

`synapse-settings audio broker-status` is the C11 client and decoder. It returns
the same versioned contract for a valid live broker and deterministic inactive
or unavailable status for absence, stale sockets, crashes, timeouts, unsafe
ownership or modes, malformed responses and oversized frames. These failures
never set `active` or `enforcementAvailable`.

The Qt adapter queries this status only after a complete inventory and policy
cohort. It publishes `available`, `active`, `enforcementAvailable` and a bounded
reason identifier. QML maps those typed values to presentation text and receives
no PID, executable path, raw endpoint, socket path, IPC frame, argv, environment
or acknowledgement.

## Alpha 6 explicitly confirmed existing-stream move

Existing-stream movement is not a broker reconciliation mode and is not a policy
operation. `audio plan-stream-move` accepts one opaque `playback-N` or
`recording-N` token and one direction-compatible endpoint token. It reloads the
same-UID process identity, process start time, canonical executable, backend
stream index, exact current raw endpoint and requested raw endpoint. It exposes
only the opaque stream ID, opaque original/requested endpoint IDs and a bounded
`move-…` cohort token.

A changed plan can be applied only by `audio move-stream` with the same stream,
exact original endpoint, requested endpoint, cohort and the exact acknowledgement
`synapse-settings/audio-existing-stream-move/v1`. C11 repeats preflight twice and
fails closed if the process, stream, current endpoint, requested endpoint or raw
endpoint cohort changed. It executes one fixed `move-sink-input` or
`move-source-output` argv and reports `Applied` only when a fresh postflight
proves command success, the same process instance and the requested endpoint.

If a backend command reports failure but postflight proves that the same stream
reached the requested endpoint, C11 attempts one rollback to the exact original
raw endpoint captured by the accepted cohort and verifies it. It does not roll
back after process identity loss, stream loss, inventory loss or an intervening
unexpected target. Receipts always state `policyApplied=false` and
`persistentRuleCreated=false`.

The Qt adapter independently validates both exact contracts, owns the cohort and
acknowledgement, serializes the transaction, and refreshes the complete
inventory/policy/broker-status cohort after every apply outcome, including an
uncertain transport or contract failure. QML presents a separate modal
confirmation for one stream and receives only typed stream and endpoint tokens.
It cannot construct the plan, acknowledgement, cohort or backend command.

## Alpha 8 guarded volume and mute controls

Volume and mute are one-target transactions, not ambient sliders and not
routing/default operations. A read-only plan accepts one opaque output, input,
playback or recording target. Volume requests are exact integer percentages
from 0 through 100; mute requests are exact booleans. The plan returns the
current typed value and an opaque `control-…` cohort without exposing a raw
PipeWire name, stream index, PID, executable, process start time or command.

Apply requires the exact original value, requested value, cohort and
`synapse-settings/audio-control/v1`. C11 resolves and checks the private target
identity twice before one fixed setter. It then reloads the target and reports
`Applied` only if command success, identity and requested value are all proven.
The Qt adapter sends acknowledged apply for every accepted plan so even a
same-value result is independently validated; C11 returns `AlreadySet` without
invoking a setter. Stale original values, cohorts, streams or process identities
refuse before mutation.

A command failure or timeout can be ambiguous. If fresh state proves the same
target but a non-original value, C11 revalidates both identity and that observed
value once more immediately before attempting one exact-original compensation.
It does not mutate again after an external restoration, an intervening third
value, an unproven or vanished target, or an identity change. Requested volume
is capped at 100% to prevent software
amplification; an original value up to 999% is accepted only for exact rollback.
Every plan and receipt states that no playback, capture, profile or routing
change occurred. The operation also does not create a default or durable rule.

The Qt adapter independently decodes both contracts, owns the acknowledgement
and complete plan/apply sequence, and refreshes inventory, policy and broker
status after every apply result. QML presents explicit volume and mute dialogs
for one published item and receives only the target token and bounded typed
values. This source increment performed no live mutation, playback or capture.

## Alpha 9 read-only GoXLR provider status

GoXLR presence is an observation capability, not hardware control.
`synapse-settings audio goxlr-status` checks the fixed production executable
`/usr/bin/synapse-goxlr` and, when present, invokes only
`provider-status --format json`. It does not start the provider, initialize a
device, apply a profile, route audio, play media or capture input. The outer
process is bounded to three seconds and 65536 response bytes.

C11 strictly decodes the complete `synapse.goxlr.provider-status/v2` object,
including every nested system-output field, solely to prove the provider
contract. Duplicate, missing, extra, malformed or out-of-range values, duplicate
device IDs, unsupported models, count disagreement, oversized output and timeout
all fail closed. Profile values are then discarded. The Settings-owned
`synapse.settings.audio-goxlr-status/v1` projection carries only typed status,
redacted device model and whether the provider reports system-output capability.
It fixes `stateAuthority=provider-profile-model`, `hardwareReadback=false`,
`hardwareExactRollback=false`, `mutationAvailable=false` and `readOnly=true`.

`Unavailable` means the adapter executable is absent; `Inactive` means the
read-only command completed with a clean nonzero provider result; launch/setup
failure, signal termination and unavailable status transport are typed
`Failed/status-unavailable`. Other bounded inspection or contract failures are
also `Failed`; `Ready` means only that the provider status contract was accepted.
Ready does not mean hardware state was read back,
a device is audible, a route was applied or rollback is exact. A Ready provider
may validly report zero devices.

The Qt adapter independently validates the exact Settings contract and adds this
stage after inventory, policy and broker status. It publishes a QML projection
containing status, a bounded reason, activity, truncation and device model plus
reported capability. It removes even the provider's redacted device token and
exposes no GoXLR plan/apply/control method. QML maps the typed state to localized
text and explicitly states that it cannot start the provider or mutate hardware.
All provider responses in this increment came from compile-time fixture paths;
no live provider or hardware was queried.

## Alpha 10 guarded card profiles and endpoint ports

Profiles and ports remain separate from default selection, durable routing,
existing-stream movement and `audio-control/v1`. The read-only
`audio profile-port-inventory` command publishes bounded cards and physical
output/input endpoints with owner-scoped opaque profile or port choices. C11
keeps raw PipeWire-Pulse card, endpoint, profile and port names inside C11. The
Qt projection contains only opaque tokens, labels bounded to 255 UTF-8 bytes and
the typed availability `available`, `unknown` or `unavailable`. Unknown choices
are selectable; unavailable choices are not. PulseAudio 17 card `profiles` are
accepted only in their emitted object form, with an optional boolean
`available`. Sink/source `ports` are accepted only as arrays of objects with a
bounded `name`; their optional C-locale `availability` value must be exactly
`available`, `availability unknown` or `not available` and is normalized to the
public three-state vocabulary. Omitted collections advertise no choices;
present null or wrong-shaped collections and present null availability fail
closed. Labels are presentation data and never bind identity or a cohort.
Missing labels remain empty in typed contracts; QML uses localized `Audio card`,
`Audio endpoint`, `Unnamed profile` or `Unnamed port` display text without
exposing a raw backend identity. Monitor entries
count toward the 64-item raw input bound; C11 validates their raw identity,
backend index, monitor metadata, label, complete ports and active selection
before omission. Hidden options consume the global 512-port budget, and
malformed choices, availability, duplicate indexes or generated tokens fail
closed before the physical-input projection is published.

`audio plan-profile` accepts one `card-…` and owner-compatible `profile-…` token.
`audio plan-port` accepts one direction-compatible `output-…` or `input-…` and
owner-compatible `port-…` token. A target must have a resolvable active choice.
The plan exposes bounded target/original/requested labels and an opaque
`selection-…` cohort while retaining the raw target, backend index and choices
in C11. A profile plan reports that the software graph and signal path may
change. A port plan reports that only the selected signal path may change.

Apply requires the exact original selection, requested selection, cohort and
`synapse-settings/audio-profile-port/v1`. C11 loads and validates the bound state
twice before executing at most one fixed `set-card-profile`, `set-sink-port` or
`set-source-port` command. It refuses stale originals, cohorts, identities,
owners, directions, missing active choices and unavailable requested choices
before mutation. A same-selection apply returns a verified `AlreadySet` receipt
without a setter.

A setter exit alone is never success. Fresh state must prove the same target
identity and exact requested choice. An uncertain requested mutation is never
retried. Compensation is considered only if the first postflight visibly
observed the requested choice without establishing a successful transaction;
immediately before at most one exact-original setter, C11 must freshly prove the
same identity and that the requested choice is still selected. It does not
overwrite an external restoration, an immediate or intervening third choice, or
an original choice that became unavailable. Restoration must be verified;
otherwise the receipt remains `Failed` and reports whether rollback was
attempted and verified.

Inventory, plans and receipts fix
`stateAuthority=pipewire-pulse-model`, `hardwareReadback=false` and
`hardwareExactRollback=false`. They never start playback or capture, change a
default or durable policy, or claim audibility. The Qt adapter independently
validates the contracts and active-selection agreement with base Audio
inventory, owns acknowledgement/cohort/apply construction, and refreshes the
complete base/profile-port/policy/broker/GoXLR cohort after every apply outcome.
QML receives only bounded models and confirmation labels; it receives no raw
names, cohort, acknowledgement or command authority. All Alpha 10 transactions
were exercised through compile-time fixtures only.

Before `json-c` decoding, C11 validates the complete raw `pactl` document as
strict UTF-8 JSON. Raw or escaped NUL, malformed escapes, unpaired surrogates,
invalid literals or number grammar, leading-zero numbers, trailing commas or
data, duplicate decoded keys, nesting over 64, over 4096 keys in one object,
over 16384 keys in one document, and byte 1 MiB + 1 fail closed. Valid surrogate
pairs and the exact depth, key and byte limits remain accepted. The Qt adapter
independently performs strict lexical JSON and UTF-8 validation before any Qt
JSON decoding, in addition to exact typed contract and cross-field checks.

Base inventory decoding also requires exact nonnegative backend indexes through
`INT_MAX`. Present mute and volume fields must have their expected types;
channel values are exact integers through `UINT32_MAX` and aggregate with
checked arithmetic before rounding. Present stream `properties` must be an
object, and present `application.name` and `media.name` values must be bounded
strings. Missing optional values retain defined fallbacks, while a malformed
process-ID property disables process-rule selection. Default sink/source values
remain opaque bounded identities: an empty value means no named default, and an
invalid nonempty value clears the entire inventory. The Qt outer process remains
bounded while applying transaction-specific deadlines for commands that can
legitimately execute several sequential two-second capture cohorts.

## Application identity

A durable “single process” selection is stored as the canonical executable
behind that process, not its PID and not a process-name string. PIDs are reused;
process names collide and can be spoofed. The adapter passes only a selected
opaque Audio stream ID. The C backend privately pins the same-UID `/proc/PID`
directory, resolves its executable, and persists only that canonical path; raw
process metadata never reaches QML.

A directory rule matches only on a path-component boundary. `/Games/Steam`
matches `/Games/Steam/library/game`, but not `/Games/Steam-old/game`. Symlinks
are resolved before persistence.

Rules resolve independently for output and input:

1. matching exact executable;
2. longest matching directory prefix;
3. current system default.

Exact executable and directory rules can coexist, including separate input and
output rules for the same application.

## Enforcement status

Alpha 6 preserves the fixture-qualified broker's typed runtime state in the
standalone Settings candidate without installing or activating the service.
Policy view, policy receipt and resolution v1 continue to report
`audio-route-broker-not-integrated` and `enforcementAvailable=false` because a
stored policy is not runtime evidence. A policy receipt means only that the
private policy was atomically stored and still reports `routingApplied=false`.
The separate broker-status contract is authoritative for current activity and
new-stream enforcement availability.

A broker event receipt can report only a verified post-baseline new-stream move.
A separate existing-stream receipt can report only the explicitly acknowledged
single-stream transaction described above. Neither receipt implies the other,
and neither a policy write nor broker status proves a move. QML never subscribes
to PipeWire, opens the runtime socket or constructs raw `pactl` operations.

## Alpha 7 shell-host module

The existing Qt adapter and host-neutral feature QML are packaged as the
`Synapse.Settings.Audio` module. `AudioBackend` is an engine-owned singleton;
its production constructor uses only `/usr/bin/synapse-settings`. The module's
`AudioShellHost` exposes only bounded readiness, availability, activity and
typed reason/status/error properties while rendering the same
`AudioSettings.qml` presentation.

The shell controls one explicit `active` property. In Alpha 10, activating the
module starts only the established read-only base inventory, profile/port
inventory, policy view, broker-status and GoXLR-status cohort.
Mutations remain behind the same visible confirmations and adapter-owned plan,
acknowledgement, receipt validation and complete refresh. Neither loading the
module nor selecting the Audio section changes a default, policy, stream,
profile, service or package state.

Executable and directory selection remains a native adapter responsibility. A
shell must opt into `QApplication`; otherwise the chooser fails with
`native-dialog-unavailable` before any backend call. Module tests compile a
separate plugin with the fixture backend override. The production plugin has no
test-backend literal or environment hook.

The module embeds provisional `en_US` and `it_IT` catalogues. Unsupported or
malformed locale requests fall back to `en_US`; the remaining pinned GUI
catalogues are still required before localization can be called complete.
Installation, shell deployment, package promotion and live Audio qualification
remain separate gates.

## Remaining Audio work

Balance, level metering, safe playback tests, Bluetooth state, hotplug and
GoXLR hardware control remain separate capabilities. Alpha 10 profile/port
selection, Alpha 9 presence/status observation and Alpha 8 volume/mute authority
never imply any of them. A safe sample never authorizes capture, profile import
or GoXLR firmware/mixer mutation.
The existing Quickshell `AudioPanel.qml` direct mutation model must not be reused
inside Settings.
