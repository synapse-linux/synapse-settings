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

The shell controls one explicit `active` property. Activating the module starts
only the established read-only inventory, policy view and broker-status cohort.
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

Typed volume/mute/balance, profiles and ports, levels, safe playback tests,
Bluetooth state, hotplug and GoXLR presence remain separate capabilities. A safe
sample never authorizes capture, profile import or GoXLR firmware/mixer mutation.
The existing Quickshell `AudioPanel.qml` direct mutation model must not be reused
inside Settings.
