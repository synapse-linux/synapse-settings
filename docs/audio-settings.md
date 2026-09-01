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

Alpha 4 provides a typed new-stream broker source and fixture-qualified engine,
but it is not installed, active or integrated into the deployed Settings status.
Policy view, policy receipt and resolution v1 therefore continue to report
`audio-route-broker-not-integrated` and `enforcementAvailable=false`. A policy
receipt means only that the private policy was atomically stored and still
reports `routingApplied=false`.

Only the separate broker event receipt can report a move, and only after verified
postflight. Existing-stream migration remains unavailable and requires a future
independent acknowledgement and rollback contract. QML never subscribes to
PipeWire or constructs raw `pactl` operations.

## Remaining Audio work

Typed volume/mute/balance, profiles and ports, levels, safe playback tests,
Bluetooth state, hotplug and GoXLR presence remain separate capabilities. A safe
sample never authorizes capture, profile import or GoXLR firmware/mixer mutation.
The existing Quickshell `AudioPanel.qml` direct mutation model must not be reused
inside Settings.
