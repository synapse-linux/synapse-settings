# Synapse Settings

CLI-first C11 settings backend, a lazy Qt Quick presentation, and a separate C11
new-stream Audio route broker. Alpha 6 adds an independently acknowledged,
one-stream existing-stream move transaction with exact postflight and bounded
rollback. Audio policy, PipeWire authority, private cohort data and
acknowledgements remain outside QML. Installation, broker activation and live
qualification remain later deployment gates.

```bash
make CORE_ROOT=/path/to/staged-core clean all test-all
build/synapse-settings audio inventory --format json
build/synapse-settings audio broker-status --format json
build/synapse-settings audio plan-stream-move --stream PLAYBACK_ID --device OUTPUT_ID --format json
build/synapse-settings audio plan-default --direction output --device OUTPUT_ID --format json
build/synapse-settings audio policy show --format json
build/synapse-settings audio resolve --path /canonical/application --direction output --format json
build/synapse-audio-route-broker --probe --format json
build/synapse-settings-gui --locale it_IT
```

Audio capabilities in this slice:

- bounded PipeWire-Pulse outputs, physical inputs, cards and active streams;
- stable opaque endpoint and stream identities;
- guarded, planned and verified default output/input selection;
- private application rules for a selected active process, exact executable or
  canonical directory prefix;
- simultaneous output and input rules;
- deterministic precedence: exact executable, longest matching directory, then
  the current system default;
- native executable/directory chooser owned by the trusted Qt adapter;
- lazy standalone QML host with provisional `en_US` and `it_IT` catalogues;
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
  GUI operations.

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
it. The current source candidate was not installed, enabled or run against live
Audio. Existing-stream planning and movement were exercised only through the
compile-time test `pactl` override; no live stream was moved. Fixture tests cover successful playback and recording moves, every reachable
typed preflight refusal, stale cohorts, wrong original targets, endpoint drift,
timeouts, false backend success, stream and identity loss, unavailable
verification and verified rollback. Policy receipts still describe policy
persistence only and therefore continue to report `routingApplied=false`.
Settings obtains runtime state through
`audio broker-status`, which sends one fixed read-only request to the broker's
mode-0600 AF_UNIX socket in an owner-mode-0700 runtime directory. The C11 client
rejects stale sockets, wrong ownership or modes, timeouts and noncanonical
responses before the Qt adapter receives a typed contract.

Docker inventory from Alpha 1 remains optional and bounded. CLI and broker
runtime dependencies are `libsynapse-core.so.0` and `json-c`; the optional GUI
uses Qt 6 Core, Gui, Qml, Quick, Quick Controls, Widgets and Test at build time.
