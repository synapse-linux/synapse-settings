# Architecture

## Layers

1. `synapse-settings` C11 core owns inventory, validation, canonicalization,
   policy persistence, precedence and guarded default-device operations.
2. `AudioAdapter` is a thin Qt/C++ boundary. It owns bounded asynchronous core
   execution, strict contract decoding, native path choosers and publication of
   bounded presentation projections.
3. Lazy QML renders only those projections and invokes fixed adapter methods.
4. `synapse-audio-route-broker` is a separate C11 process that owns bounded
   PipeWire-Pulse observation and new-stream enforcement. Its user service is
   staged but never enabled automatically.

No lower layer delegates authority upward. QML never sees raw JSON, PipeWire node
names, PIDs, command lines, environments, acknowledgements or backend argv. A
process candidate consists only of an opaque Audio stream ID and a sanitized
label. Executable and directory paths remain inside the native chooser and
adapter-to-core invocation.

The standalone GUI is also a portability boundary: feature QML imports Qt Quick
rather than Quickshell modules. The same QML and adapter can therefore be hosted
by a Synapse shell without changing the Audio contracts.

## Reusable QML module boundary

Alpha 7 packages `AudioSettings.qml`, `AudioSettingsSection.qml` and
`AudioShellHost.qml` as `Synapse.Settings.Audio` beside a Qt extension plugin.
The plugin registers one engine-owned `AudioBackend` singleton and initializes
package-owned translations. The production singleton always targets
`/usr/bin/synapse-settings`; only the separately compiled test plugin recognizes
the bounded fixture backend override. Because a shared object does not receive
an executable startup note, `x86_64_baseline_note.cpp` publishes the plugin's
GNU baseline ISA property explicitly; `-march=x86-64 -mtune=generic` remains
the code-generation authority.

`AudioShellHost` exposes readiness, availability, busy state, broker activity,
enforcement availability and bounded status/reason/error identifiers. It does
not expose models containing private process data, the backend executable path,
raw JSON, PipeWire names, acknowledgements, cohorts, argv, environment or IPC
frames. The existing feature QML receives the same typed adapter as the
standalone application and remains free of Quickshell imports.

The host sets an explicit `active` boolean. Activation lazily creates the Audio
section and starts the read-only inventory-policy-broker-status load. Hiding the
section releases its presentation objects without adding ambient mutation or
reconciliation; the typed singleton remains single-flight. A host that offers
native executable/directory dialogs must run as `QApplication`. If it does not,
the adapter returns the typed `native-dialog-unavailable` presentation error
without invoking a backend operation.

Unknown, missing or malformed GUI locale requests fall back to embedded
`en_US`. The current source candidate still has only `en_US` and `it_IT`
catalogues; the other pinned GUI catalogues remain a release blocker rather
than an implied completion claim.

## Adapter transaction boundary

The production GUI discovers only a same-directory `synapse-settings` binary or
fixed `/usr/bin/synapse-settings`. It invokes it through `QProcess` with a fixed
program, allowlisted arguments, locale-independent output, one in-flight command,
a bounded response and a bounded timeout. It never invokes a shell.

Every JSON object is decoded into an exact expected field set. Arrays, text,
identities and integer ranges are bounded; duplicate endpoint, stream, card and
rule identities fail closed. Models publish only after inventory, route policy
and the separate broker runtime status all pass validation.

A GUI default-device transaction is:

1. validate the direction-specific opaque endpoint ID against the published
   cohort;
2. request and validate a fresh read-only plan;
3. if changed, invoke the fixed setter with the adapter-owned exact
   acknowledgement;
4. validate the verified receipt;
5. reload inventory and policy before publishing success.

Application policy mutations validate the exact receipt and then perform the
same complete refresh. A failed mutation preserves the last accepted model and
publishes only a deterministic error identifier.

A GUI existing-stream transaction is independent:

1. validate one published opaque stream, its exact current opaque endpoint and a
   direction-compatible requested endpoint;
2. request and independently validate a fresh read-only plan and opaque cohort;
3. when changed, invoke the one-stream setter with the exact original endpoint,
   cohort and adapter-owned acknowledgement;
4. independently validate the exact `Applied`, `AlreadyRouted`, `Refused` or
   `Failed` receipt and its postflight/rollback invariants;
5. reload inventory, policy and broker status before reporting success, a typed
   transaction error, or an uncertain apply transport/contract result.

QML owns only the visible modal confirmation. It never receives the cohort,
acknowledgement, process identity, backend index or raw endpoint.

## PipeWire-Pulse core adapter

Production executes only absolute `/usr/bin/pactl` with fixed operation argv.
Each child has bounded capture and a two-second timeout. Executable and policy
path overrides exist only in a separately compiled fixture binary.

## Policy persistence

`$XDG_CONFIG_HOME/synapse/audio-route-policy-v1.json` is canonical JSON owned by
the current user. Its directory is private, its file mode is 0600, loading uses
`O_NOFOLLOW`, and replacement is write/fsync/rename/directory-fsync atomic.
Unknown fields, duplicate identities, loose permissions, symlinks, oversized
files and noncanonical serialization fail closed.

Policy rules use canonical exact executable paths or canonical directories.
They are bounded to 128 and resolved per Audio direction. For a selected active
Audio stream, the C backend privately parses its process ID, pins the same-UID
`/proc/PID` directory, resolves its executable, and persists only that canonical
path. Persisted PIDs and process-name matching are excluded.

## New-stream broker boundary

The Alpha 4 broker subscribes through fixed production `/usr/bin/pactl
subscribe`, establishes a bounded baseline after starting the subscriber, and
acts only on unseen `new` events for sink inputs or source outputs. Baseline
streams are never moved. Remove events retire tracked identities; duplicate and
change events are inert. Broker restart, policy changes and endpoint hotplug do
not migrate active streams.

For each unseen event, C11 reloads the stream, privately pins its same-UID
process and `/proc/PID/stat` start time, resolves the canonical executable, loads
the current policy generation, and reloads the stream and endpoint cohort. A
fixed `move-sink-input` or `move-source-output` argv is issued only for an
explicit exact-executable or directory-prefix rule with both current and target
endpoints available. System-default resolution performs no move.

## Broker runtime status boundary

Alpha 5 exposes runtime state through one status-only AF_UNIX socket at
`$XDG_RUNTIME_DIR/synapse/audio-route-broker-v1.sock`. The broker creates or
accepts only an owner-mode-0700 `synapse` directory, serializes instances with an
owner-mode-0600 advisory lock, and publishes the socket at mode 0600 only after
subscriber startup, baseline capture and policy validation complete. A second
broker fails without unlinking the active socket. Shutdown removes the socket;
a crash leaves at most an unreachable stale socket, which clients classify as
inactive.

The protocol accepts exactly `status-v1` followed by one newline. It has no
mutation, configuration or stream-selection request. The broker checks
`SO_PEERCRED` for the same UID, serves one bounded response per connection and
bounds accept-side read/write time. The C11 `audio broker-status` client verifies
the runtime directory and socket type, owner and exact modes, checks the
connected server UID with `SO_PEERCRED`, uses bounded nonblocking
connect/send/receive, and strictly parses a canonical status object.
Malformed, oversized, stalled, loose-mode, symlinked or unreachable state cannot
produce an active or enforcement-available projection.

The Qt adapter invokes only the fixed C11 status command after inventory and
policy. It revalidates the exact contract and exposes booleans plus a bounded
typed reason identifier. QML sees neither the socket path nor IPC frames and
cannot query or control the broker directly.

## Explicit existing-stream transaction boundary

Alpha 6 does not add reconciliation to the broker. The C11 CLI implements one
separate read-only plan and one separately acknowledged apply operation for an
exact opaque stream. The cohort binds the backend index, direction, PID plus
start time, canonical executable, current opaque and raw endpoint, requested
opaque and raw endpoint. Only the opaque cohort and opaque endpoint/stream IDs
cross into the Qt adapter.

Apply requires the exact original endpoint and repeats the complete preflight
twice before a fixed `move-sink-input` or `move-source-output` call. A command
success becomes `Applied` only after fresh same-instance/target verification. If
a failed command nevertheless reached the requested target, one rollback to the
exact original raw endpoint is permitted only while the same identity is still
proven, followed by another verification. Stream disappearance, identity loss,
inventory loss or an unexpected intervening target blocks rollback. The receipt
explicitly denies policy application and persistent-rule creation.

This path was exercised only with compile-time fixture overrides. It does not
install or activate the broker and did not move a live stream.

A broker receipt reports `routingApplied=true` only when the command succeeds and
a fresh postflight proves the same process instance on the selected opaque target.
A command that reports failure after reaching the target is compensated to the
exact original endpoint when the same identity remains provable. Vanished or
changed identities, unavailable targets, timeout and unverifiable state never
produce a success claim. Receipts contain no PID, executable path, raw endpoint
or subscriber text.

The service unit restricts address families to AF_UNIX, requests a private
runtime directory and applies user-service hardening, but installation,
enablement, startup and live qualification remain separate gates.
Existing-stream movement is never ambient and is available only through the
explicit transaction above. Policy view/receipt v1 still reports
`audio-route-broker-not-integrated` because a policy write cannot prove runtime
activity or a stream move. The separate broker-status contract is the sole
runtime authority presented by Settings.
