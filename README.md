# Synapse Settings

CLI-first C11 settings backend, a lazy Qt Quick presentation, and a separate C11
new-stream Audio route broker. Alpha 8 adds separately planned, acknowledged and
verified volume/mute control for one opaque device or active-stream target. The
`Synapse.Settings.Audio` module keeps PipeWire authority, raw identities,
private cohorts, acknowledgements and rollback in C11/Qt boundaries rather than
QML. Existing-stream movement remains an independent Alpha 6 capability.
Installation, shell replacement, broker activation, live mutation and physical
qualification remain later deployment gates.

```bash
make CORE_ROOT=/path/to/staged-core clean all test-all
build/synapse-settings audio inventory --format json
build/synapse-settings audio broker-status --format json
build/synapse-settings audio plan-stream-move --stream PLAYBACK_ID --device OUTPUT_ID --format json
build/synapse-settings audio plan-volume --target OUTPUT_ID --percent 40 --format json
build/synapse-settings audio plan-mute --target PLAYBACK_ID --muted true --format json
build/synapse-settings audio plan-default --direction output --device OUTPUT_ID --format json
build/synapse-settings audio policy show --format json
build/synapse-settings audio resolve --path /canonical/application --direction output --format json
build/synapse-audio-route-broker --probe --format json
build/synapse-settings-gui --locale it_IT
make qml-module
```

The reusable module stages under
`build/qml/Synapse/Settings/Audio`. A production install places the module under
the configured Qt 6 QML directory, but building or testing it does not install
or activate anything.

Audio capabilities in this slice:

- bounded PipeWire-Pulse outputs, physical inputs, cards and active streams;
- stable opaque endpoint and stream identities;
- guarded, planned and verified default output/input selection;
- one-target device/stream volume and mute planning, exact acknowledgement,
  postflight verification and identity-safe exact-original compensation;
- requested volume bounded to 0–100% with no software amplification, implicit
  playback, capture, routing, profile or durable-policy change;
- private application rules for a selected active process, exact executable or
  canonical directory prefix;
- simultaneous output and input rules;
- deterministic precedence: exact executable, longest matching directory, then
  the current system default;
- native executable/directory chooser owned by the trusted Qt adapter;
- lazy standalone QML host and reusable `Synapse.Settings.Audio` module with
  provisional `en_US` and `it_IT` catalogues and deterministic `en_US`
  fallback;
- a bounded broker for new playback and recording streams created after its
  startup baseline;
- typed broker status and per-event receipts with no PID, executable path or raw
  PipeWire endpoint;
- owner-private, same-UID, bounded AF_UNIX status IPC with no mutation requests;
- typed Active, Inactive and Unavailable broker presentation in Settings;
- a separate, one-stream existing-stream plan and exact-acknowledgement move with
  same-identity postflight and exact-original rollback when safe;
- an explicit Qt confirmation surface that never creates a durable rule;
- strict contract decoding, bounded output, bounded execution and single-flight
  GUI operations;
- a shell-facing `AudioShellHost` that publishes only bounded typed booleans and
  reason/status identifiers, never raw JSON, process paths, endpoint internals,
  acknowledgements, cohorts, argv or environments.

Durable rules never persist PIDs. The C backend maps an opaque Audio stream to a
same-UID canonical executable and privately pins its process start time. Before a
new-stream move, the broker reloads the stream, endpoint cohort and policy. It
claims success only after a fresh identity and endpoint verification. A proven
partial application is compensated to the original endpoint when that identity
and endpoint remain available.

Starting or restarting the broker establishes a baseline and never moves those
existing streams. Duplicate, `change`, policy-change and endpoint-hotplug events
do not migrate active streams. Alpha 6 keeps existing-stream movement outside the
broker and outside policy enforcement: the user selects one currently published
stream and one endpoint in a separate confirmation dialog. C11 creates a fresh
opaque cohort, requires the exact original endpoint and acknowledgement, repeats
preflight, and returns a dedicated receipt. No persistent rule is created.

`make install` stages a hardened systemd user unit but does not enable or start
it. The current source candidate and QML module were not installed, enabled or
run against live Audio. Existing-stream movement and level-control transactions
were exercised only through the compile-time test `pactl` override; no live
stream, volume or mute state was changed. Fixtures cover device and stream
controls, playback and recording moves, typed preflight refusal, stale cohorts,
wrong originals, endpoint drift, timeout, false backend success, target or
identity loss, unavailable verification, amplified pre-state restoration,
external restoration, intervening values, verified rollback and failed rollback.
Policy receipts still describe policy
persistence only and therefore continue to report `routingApplied=false`.
Settings obtains runtime state through
`audio broker-status`, which sends one fixed read-only request to the broker's
mode-0600 AF_UNIX socket in an owner-mode-0700 runtime directory. The C11 client
rejects stale sockets, wrong ownership or modes, timeouts and noncanonical
responses before the Qt adapter receives a typed contract.

Docker inventory from Alpha 1 remains optional and bounded. CLI and broker
runtime dependencies are `libsynapse-core.so.0` and `json-c`; the optional GUI
uses Qt 6 Core, Gui, Qml, Quick, Quick Controls, Widgets and Test at build time.
