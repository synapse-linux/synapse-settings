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
by a future Synapse shell without changing the Audio contracts.

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

A receipt reports `routingApplied=true` only when the command succeeds and a
fresh postflight proves the same process instance on the selected opaque target.
A command that reports failure after reaching the target is compensated to the
exact original endpoint when the same identity remains provable. Vanished or
changed identities, unavailable targets, timeout and unverifiable state never
produce a success claim. Receipts contain no PID, executable path, raw endpoint
or subscriber text.

The service unit restricts address families to AF_UNIX, requests a private
runtime directory and applies user-service hardening, but installation,
enablement, startup and live qualification remain separate gates.
Existing-stream movement is absent. Policy view/receipt v1 still reports
`audio-route-broker-not-integrated` because a policy write cannot prove runtime
activity or a stream move. The separate broker-status contract is the sole
runtime authority presented by Settings.
