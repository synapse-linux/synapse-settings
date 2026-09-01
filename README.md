# Synapse Settings

CLI-first C11 settings backend plus a lazy Qt Quick presentation for Synapse.
Alpha 3 wires the Audio QML to a bounded typed Qt/C++ adapter without moving
Audio policy or PipeWire authority into QML.

```bash
make CORE_ROOT=/path/to/staged-core clean all test-all
build/synapse-settings audio inventory --format json
build/synapse-settings audio plan-default --direction output --device OUTPUT_ID --format json
build/synapse-settings audio policy show --format json
build/synapse-settings audio resolve --path /canonical/application --direction output --format json
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
- strict contract decoding, bounded output, bounded execution and single-flight
  GUI operations.

Durable rules never persist PIDs. The C backend maps a selected opaque Audio
stream to its same-UID canonical executable while keeping process metadata out
of QML. The adapter publishes only bounded display labels and opaque identities.

Automatic application to new or existing streams remains explicitly unavailable
until the first-party Audio route broker is integrated. Policy receipts never
claim that routing occurred. This candidate is not installed or deployed by the
build and performs no mutation until the user confirms a specific GUI action.

Docker inventory from Alpha 1 remains optional and bounded. CLI runtime
dependencies are `libsynapse-core.so.0` and `json-c`; the optional GUI uses Qt 6
Core, Gui, Qml, Quick, Quick Controls, Widgets and Test at build time.
