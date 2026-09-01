# Synapse Settings

CLI-first C11 settings backend for Synapse. Alpha 2 adds a typed Audio section
without moving PipeWire authority into QML.

```bash
make CORE_ROOT=/path/to/staged-core clean all test-all
build/synapse-settings audio inventory --format json
build/synapse-settings audio plan-default --direction output --device OUTPUT_ID --format json
build/synapse-settings audio policy show --format json
build/synapse-settings audio resolve --path /canonical/application --direction output --format json
```

Audio capabilities in this slice:

- bounded PipeWire-Pulse outputs, inputs, cards and active streams;
- stable opaque endpoint identities;
- guarded and verified default output/input selection;
- private application rules for an exact executable or directory prefix;
- simultaneous output and input rules;
- deterministic precedence: exact executable, longest matching directory, then
  the current system default;
- lazy presentation-only QML candidate.

Durable rules never persist PIDs. The C backend can map a selected opaque Audio
stream to its same-UID canonical executable while keeping process metadata out
of QML. Automatic application to new or existing streams remains explicitly unavailable until the first-party audio
route broker is integrated; policy receipts never claim that routing occurred.

Docker inventory from Alpha 1 remains optional and bounded. Runtime dependencies
are `libsynapse-core.so.0` and `json-c`.
