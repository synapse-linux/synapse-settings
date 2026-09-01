# Synapse Settings

CLI-first C11 settings backend, a lazy Qt Quick presentation, and a separate C11
new-stream Audio route broker. Alpha 4 keeps Audio policy and PipeWire authority
outside QML while adding an offline broker candidate that can be activated only
through a later deployment gate.

```bash
make CORE_ROOT=/path/to/staged-core clean all test-all
build/synapse-settings audio inventory --format json
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
do not migrate active streams. Existing-stream movement remains a separately
reviewed capability with no implementation or acknowledgement in Alpha 4.

`make install` stages a hardened systemd user unit but does not enable or start
it. The current source candidate was not installed, enabled or run against live
Audio. Fixture tests exercise all move paths through the test-only `pactl`
override. Policy receipts still describe policy persistence only and therefore
continue to report `routingApplied=false`; Settings runtime-status integration
is a later gate.

Docker inventory from Alpha 1 remains optional and bounded. CLI and broker
runtime dependencies are `libsynapse-core.so.0` and `json-c`; the optional GUI
uses Qt 6 Core, Gui, Qml, Quick, Quick Controls, Widgets and Test at build time.
