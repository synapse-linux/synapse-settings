# Architecture

## Layers

1. `synapse-settings` C11 core owns inventory, validation, canonicalization,
   policy persistence, precedence and guarded default-device operations.
2. A future typed Qt adapter owns asynchronous execution, presentation of typed
   process/path choices and conversion from JSON contracts to bounded models.
3. Lazy QML renders only those models and invokes fixed adapter methods.
4. A future first-party Audio route broker owns PipeWire stream observation and
   enforcement.

No lower layer delegates authority upward. In particular, QML never sees raw
PipeWire node names, PIDs, command lines, environments or arbitrary argv.

## PipeWire-Pulse adapter

Production executes only absolute `/usr/bin/pactl` with fixed operation argv.
Each child has bounded capture and a two-second timeout. Executable and policy
path overrides exist only in a separately compiled fixture binary.

The default-device transaction is:

1. inventory and map opaque ID to a backend-owned raw endpoint;
2. emit a read-only plan;
3. require exact acknowledgement;
4. execute one fixed setter;
5. collect a fresh inventory and verify the selected default.

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

## Honest enforcement boundary

Policy storage and resolution do not imply stream movement. Until the route
broker has an independently reviewed typed contract, all policy contracts expose
`enforcementAvailable=false`, reason `audio-route-broker-not-integrated`, and
receipts expose `routingApplied=false`.
