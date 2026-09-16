<!-- SPDX-License-Identifier: MIT -->
# Architecture

## Layers

1. `synapse-settings` C11 core owns inventory, validation, canonicalization,
   policy persistence, precedence, guarded default-device, level-control,
   card-profile and endpoint-port operations, plus read-only GoXLR inspection
   and separately acknowledged popup-control mediation.
2. `AudioAdapter` is a thin Qt/C++ boundary. It owns bounded asynchronous core
   execution, strict contract decoding, native path choosers and publication of
   bounded presentation projections.
3. Lazy QML renders only those projections and invokes fixed adapter methods.
4. `synapse-audio-route-broker` is a separate C11 process that owns bounded
   PipeWire-Pulse observation and new-stream enforcement. Its user service is
   staged but never enabled automatically.

No lower layer delegates authority upward. QML never sees raw JSON, raw
PipeWire card/endpoint/profile/port names, provider identities, provider profile
values, PIDs, command lines, environments, acknowledgements, cohorts or backend
argv. A
process candidate consists only of an opaque Audio stream ID and a sanitized
label. Executable and directory paths remain inside the native chooser and
adapter-to-core invocation.

The standalone GUI is also a portability boundary: feature QML imports Qt Quick
rather than Quickshell modules. The same QML and adapter can therefore be hosted
by a Synapse shell without changing the Audio contracts.

## Private C Audio composition

`audio.c` owns the base inventory, opaque endpoint/card identities, process
inspection, stream snapshots, strict bounded JSON acquisition and the existing
finite command runner. `audio_control.c` owns volume/mute option parsing,
cohorts, plans, receipts, setters and compensation. `audio_profile_port.c` owns
the separate profile/port inventory, choice parsing, owner-scoped tokens,
transaction cohorts, plans, receipts and compensation. These are independent C11
translation units, not source fragments included by the inventory implementation.
The finite C runner terminates its complete owned process group even after a
successful leader exit; a successful CLI child cannot leave detached work in
that group. A separately owned descendant fixture tests this cleanup contract.

The noninstalled `src/audio_private.h` is limited to shared bounds, existing
validation/hash/acquisition helpers, value snapshots and family dispatch entry
points. Full base-inventory arrays and process inspection remain private to
`audio.c`. Control target classification, snapshot acquisition and identity
comparison also stay there; transaction code borrows no inventory pointers.
Returned capture buffers and JSON objects have explicit caller-owned release
rules. A snapshot, parsed value or successful subprocess exit is not mutation
permission or a verified transaction result.

No callback registry, generic transaction engine or new public API is introduced.
Both transaction families retain their distinct preflight, postflight, failure
reason ordering and compensation rules. The profile/port unit requires
`SYNAPSE_SETTINGS_WITH_PROFILE_PORT` and is absent from broker source/link inputs;
it cannot silently add that capability to the new-stream broker. Executable and
option-alias overrides remain compile-time-only fixture hooks. Qt, QML, schemas,
acknowledgements and normal CLI output are unchanged.

`make test-audio-units` runs a separately compiled private-helper probe and a
fixed fake-pactl corpus for endpoint volume/mute, card profiles and input/output
ports. The corpus checks refusal, double preflight, no-op, postflight and guarded
compensation behavior and records every fixture request. Existing stream,
policy, broker and malformed-input tests remain separate and unchanged.
`AUDIO_UNITS_OUTPUT_DIR` must be a new absolute directory when captures are
requested; `AUDIO_UNITS_EXPECTED` optionally compares exact command results and
request traces with an already frozen corpus. Neither test facility is installed
or linked into production.

## Private Qt implementation boundaries

`audio_contracts_p.h` / `audio_contracts.cpp` contain bounded presentation DTOs,
family-specific decoders and semantic validation of opaque identities. Raw token
patterns remain private; QML receives no validators or transport authority.
`audio_command_p.h` / `audio_command.cpp` implement the finite asynchronous
command runner. The runner uses null stdin/stderr, working directory `/`, a
necessary allowlisted session/server environment, parent-death handling and
whole-group cleanup before callback publication, even after a successful leader
exit.

`audio_adapter.cpp` retains fixed executable discovery, native choosers,
single-flight family coordination and guarded atomic snapshot publication.
Cancellation, teardown and reentrant signals remain coordinator concerns. The
standalone application and Audio plugin compile these repository-local private
modules directly; neither an installed C++ library nor a generic transaction
framework is introduced. Application/window activation and persistent provider
lifetimes remain outside the finite runner.

Standalone fixture smoke uses the separately compiled
`synapse-settings-gui-test`; production builds do not propagate fixture backend
or staged-library environment overrides. The smoke harness rejects a production
GUI before launch and guards its fixture entry point against falling back to
host Audio tools when required fixture paths are missing.

## Presentation and theme boundary

`QQuickStyle::Basic` remains the deterministic Qt Controls substrate. It is not
the product theme: Synapse-owned `AudioCard`, `AudioButton`, `AudioSlider`,
`AudioComboBox` and `AudioSectionHeading` components define the visible Audio
surface above it. Primary and secondary foregrounds are derived against their
actual surfaces, and disabled or busy controls retain readable presentation.
The scrollable content keeps all output, input, stream, routing, profile, port
and GoXLR sections discoverable without moving transaction authority into QML.

`AudioSettingsSection` remains host-neutral. `AudioSettings` and
`AudioShellHost` forward typed background, surface, hover, border, accent, text,
muted, urgent, font, radius and spacing properties. A shell injects its own
trusted palette through those values; feature QML imports no shell singleton
and resolves no provider executable.

The standalone host owns `SettingsTheme`, a strict
`synapse.theme.current/v3` projection. It discovers only a trusted sibling
`synapse-theme`, `/usr/bin/synapse-theme`, or the fixed compatibility path
`/usr/local/bin/synapse-theme`, in that order. It performs no `PATH` lookup,
rejects unsafe executable or directory ownership/modes, invokes only
`current --format json` with a bounded environment, output and deadline, and
rejects responses over 16 KiB, duplicate decoded object keys, unsafe theme
identifiers, malformed contracts and providers exceeding the 1.2-second
deadline. The provider has null stdin, an isolated process group and a
parent-death kill signal; overflow and timeout kill the complete group. The
adapter retains the last valid palette after failed reloads. Fixed theme
configuration/state paths are watched so property notifications repaint the
existing QML object tree without recreating the Audio backend or controls.
Accent foregrounds are selected with WCAG relative luminance rather than an
unadjusted RGB threshold.

## Reusable QML module boundary

Alpha 7 packages `AudioSettings.qml`, `AudioSettingsSection.qml` and
`AudioShellHost.qml` as `Synapse.Settings.Audio` beside a Qt extension plugin.
The plugin registers one engine-owned `AudioBackend` singleton and initializes
package-owned translations. The production singleton always targets
`/usr/bin/synapse-settings`; only the separately compiled test plugin recognizes
the bounded fixture backend override. Because a shared object does not receive
an executable startup note, `x86_64_baseline_note.cpp` publishes the plugin's
GNU baseline ISA property explicitly; `-march=x86-64 -mtune=generic` remains
the code-generation authority. The executable targets also compile
`src/x86_64_baseline_note.c`; its bounded companion linker script replaces the
startup property so needed and used remain exactly baseline without using the
rejected linker `-z x86-64-baseline` path. The production ELF boundary test
checks all four installed artifacts.

`AudioShellHost` and the lazy `AudioPopupHost` expose readiness, availability,
busy state, broker activity, enforcement availability, typed GoXLR
status/presence, four faders, cough, outputs, independent capabilities and
bounded status/reason/error identifiers. They do not expose private process or
provider data, the backend executable path,
raw JSON, PipeWire names, acknowledgements, cohorts, argv, environment or IPC
frames. The existing feature QML receives the same typed adapter as the
standalone application and remains free of Quickshell imports.

Each host sets an explicit `active` boolean. Activation lazily starts the
read-only base-inventory-profile/port-inventory-policy-broker-status-GoXLR-status
load. Settings navigation now retains instantiated content on hiding and exposes
versioned hide/unload queries; see [Audio surface](audio-surface.md) for the local
integration revision and its explicit mixed-host/outer-loader limitations.
Explicit adapter deactivation still cancels work, clears pending choices and
clears the snapshot; the predecessor `AudioPopupHost` retains that behavior and
has not been migrated here. The typed singleton remains single-flight. A host that offers
native executable/directory dialogs must run as `QApplication`. If it does not,
the adapter returns the typed `native-dialog-unavailable` presentation error
without invoking a backend operation.

All 64 locale identifiers in the pinned GUI inventory resolve to embedded,
package-owned catalogues. `en_US` and `it_IT` have complete translations. The
other 62 alpha catalogues deliberately mark every source-English entry
unfinished, so Qt can provide a deterministic per-message `en_US` fallback
without overstating translation coverage. Exact catalogue inventory, active
message parity, placeholders and completion states are validated from source;
recognized RTL locales also set Qt's layout direction and mirror descendants
of the feature presentation root. Unknown, missing or
malformed requests fall back to embedded `en_US`.

## Adapter transaction boundary

The production GUI discovers only a same-directory `synapse-settings` binary or
fixed `/usr/bin/synapse-settings`. It invokes it through `QProcess` with a fixed
program, allowlisted arguments, locale-independent output, one in-flight command,
a bounded response and a bounded timeout. Read-only and planning commands retain
the base outer deadline; apply paths use separately bounded transaction-aware
multipliers sized for their maximum sequential C11 capture cohorts, capped at
two minutes. It never invokes a shell.

Every JSON object is decoded into an exact expected field set. Arrays, text,
identities and integer ranges are bounded; duplicate endpoint, stream, card,
profile, port and rule identities fail closed. Models publish only after base
inventory, the separately decoded profile/port inventory, route policy, broker
runtime status and typed GoXLR status/presence all pass validation. Profile/port
active selections and target labels must also agree with the accepted base
inventory before publication.

A GUI default-device transaction is:

1. validate the direction-specific opaque endpoint ID against the published
   cohort;
2. request and validate a fresh read-only plan;
3. if changed, invoke the fixed setter with the adapter-owned exact
   acknowledgement;
4. validate the verified receipt;
5. reload inventory, policy, broker status and GoXLR status before publishing
   success.

Application policy mutations validate the exact receipt and then perform the
same complete refresh. A failed mutation preserves the last accepted model and
publishes only a deterministic error identifier.

A GUI volume or mute transaction is independent from defaults, policy and
routing:

1. validate one published opaque output, input, playback-stream or
   recording-stream target and a requested typed value;
2. request and independently validate a fresh read-only plan and opaque cohort;
3. invoke acknowledged apply with the exact original typed value and cohort for
   every accepted plan; unchanged state returns `AlreadySet` without a setter;
4. independently validate `Applied`, `AlreadySet`, `Refused` or `Failed` plus
   postflight and compensation invariants;
5. reload inventory, policy, broker status and GoXLR status after every apply
   outcome.

Requested volume is limited to 0–100%. Mute is an exact boolean. QML opens a
visible confirmation and passes only the opaque target and requested typed
value; it never receives the original backend identity, cohort,
acknowledgement, stream process identity or setter argv.

A GUI existing-stream transaction is independent:

1. validate one published opaque stream, its exact current opaque endpoint and a
   direction-compatible requested endpoint;
2. request and independently validate a fresh read-only plan and opaque cohort;
3. when changed, invoke the one-stream setter with the exact original endpoint,
   cohort and adapter-owned acknowledgement;
4. independently validate the exact `Applied`, `AlreadyRouted`, `Refused` or
   `Failed` receipt and its postflight/rollback invariants;
5. reload inventory, policy, broker status and GoXLR status before reporting
   success, a typed transaction error, or an uncertain apply transport/contract
   result.

QML owns only the visible modal confirmation. It never receives the cohort,
acknowledgement, process identity, backend index or raw endpoint.

A GUI profile or port transaction is a third independent family:

1. validate one published card/profile pair or direction-compatible
   endpoint/port pair, rejecting choices typed unavailable;
2. request and independently validate a fresh plan, owner-scoped choices and
   opaque `selection-…` cohort;
3. show only bounded target/original/requested labels and explicit graph/signal-
   path consequences in QML;
4. invoke acknowledged apply with the exact original selection and adapter-owned
   cohort, then independently validate the receipt and compensation invariants;
5. reload the complete base/profile-port/policy/broker/GoXLR cohort after every
   apply, refusal, failure, restoration or uncertain transport result.

The acknowledgement, cohort, raw target and choice names, backend index and
setter argv never enter QML. Opening or cancelling confirmation performs no
mutation.

A GUI GoXLR popup transaction is a fourth independent family:

1. require Ready, an active provider, exactly one profile-ready typed device,
   global mutation availability and the selected control's capability;
2. request and independently validate a fresh plan for one of eleven fixed
   controls and retain its original value and 16-hex cohort inside the adapter;
3. invoke acknowledged apply with the exact original/requested pair and cohort;
4. independently validate the complete applied, unchanged, refused, drifted or
   provider-model compensation receipt;
5. reload the complete Audio cohort after every result, malformed response or
   transport failure.

The lazy popup sends only a control-specific typed method call. It receives no
provider identity, generation, cohort, acknowledgement, receipt, command or
process metadata. Destroying the popup deactivates the adapter and cancels its
pending plan/apply path.

## Guarded profile and port transaction boundary

Alpha 10 keeps card-profile and endpoint-port authority outside
`audio-control/v1`. `profile-port-inventory` is a bounded, read-only v1 contract
with at most 32 cards, 64 outputs, 64 physical inputs, 64 options per target,
512 total profiles and 512 total ports. Raw names and labels are bounded to 255
bytes in C11. Missing labels remain empty contract presentation data; only QML
supplies localized generic display text. The 64-item input bound includes
monitor sources: their index, raw name, monitor metadata, label, port array,
per-port name/label/availability and active selection are validated before
exclusion. PulseAudio 17 card profiles retain their object-keyed representation
and optional boolean `available`; endpoint ports retain their array
representation and optional fixed-C-locale `availability` string (`available`,
`availability unknown` or `not available`). Omitted collections mean no choices,
whereas present null or wrong-shaped collections fail closed. Their options
consume the global 512-port budget, and duplicate indexes or option tokens fail
closed even when an item is excluded.
Raw `pactl` JSON is captured for at most two seconds and exactly 1
MiB; byte 1 MiB + 1, invalid UTF-8, raw or escaped NUL, malformed escapes,
unpaired surrogates, invalid literals or numbers, trailing commas/data,
excessive nesting/key counts and duplicate decoded object keys fail closed.
Valid paired surrogates and exact configured limits remain accepted. The fixed-
argv child runs in a dedicated process group with null stdin/stderr and a
parent-death `SIGKILL`; setup, timeout, capture errors and non-successful exits
terminate its whole group. Target tokens retain
the established `card-…`, `output-…` and `input-…` forms; profile and port
choices are owner-scoped `profile-…` and `port-…` tokens. Labels never determine
identity or cohort binding.

Availability is exactly `available`, `unknown` or `unavailable`. Unknown choices
may be planned; unavailable requested choices fail before mutation. A target
without a resolvable active choice cannot mutate. `plan-profile` and `plan-port`
bind the private target name, backend index, exact active and requested raw
choices, requested availability and opaque tokens into one `selection-…`
cohort. Apply requires the exact original token and
`synapse-settings/audio-profile-port/v1`, then repeats state and identity
preflight twice.

A changed transaction issues at most one fixed `set-card-profile`,
`set-sink-port` or `set-source-port` request and never retries an uncertain
request. Command exit does not prove success; fresh state must retain the same
target identity and exact requested selection. Compensation is considered only
when that first postflight visibly observed the requested selection after a
non-successful or otherwise unverified setter. Immediately before one exact-
original setter, another fresh same-identity read must still observe that same
requested selection. External restoration causes no setter. An immediate or
intervening third choice, unavailable original choice, vanished target, identity
change or unavailable verification blocks compensation; a requested mutation
is never retried. Any compensation must itself be verified in the software
model.

Plans and receipts declare `stateAuthority=pipewire-pulse-model`,
`hardwareReadback=false` and `hardwareExactRollback=false`. A profile may rebuild
the graph and change the signal path; a port may change only the selected signal
path. Neither operation starts playback/capture, changes a default or policy, or
proves audibility. Qualification in this increment is fixture-only.

## Guarded volume and mute transaction boundary

Alpha 8 exposes separate `plan-volume`, `set-volume`, `plan-mute` and `set-mute`
operations. A target must be an opaque output/input endpoint token or bounded
playback/recording stream token. C11 resolves the private raw endpoint or stream
index and, for streams, pins the same-UID process executable and start time.
The opaque `control-…` cohort binds that identity, target kind, backend index,
control kind, exact original value and requested value.

Apply requires the exact original value, cohort and
`synapse-settings/audio-control/v1`. It repeats the complete state/identity
preflight twice before issuing exactly one fixed `set-sink-volume`,
`set-source-volume`, `set-sink-input-volume`, `set-source-output-volume` or
corresponding mute argv. Command exit alone never proves success: a fresh
postflight must show the same target identity and requested value. Requested
volume cannot exceed 100%; an original value through 999% may be carried only
so a previously amplified target can be restored exactly.

If an uncertain command visibly changed the same proven target without a
verified requested result, C11 freshly revalidates the exact identity and
observed value immediately before performing at most one compensation to the
exact original value. An external restoration causes no second mutation; an
intervening third value, target disappearance, stream-process identity change
or unavailable state blocks unsafe compensation and cannot produce an
`Applied` receipt. A failed compensation is explicit. The contracts fix
`singleTarget=true` and state that playback, capture, profiles and routing were
not changed. Default selection, persistent policy and stream movement remain
separate authorities.

This capability was qualified only with compile-time fixture overrides. It did
not change a live device or stream and did not start playback or capture.

## Mediated GoXLR status and popup-control bridge

The Settings-only C11 bridge checks the fixed production
`/usr/bin/synapse-goxlr` executable and invokes fixed commands with no shell.
Status, plan and presence work is bounded to three seconds; apply is bounded to
twelve seconds; every response is capped at 65536 bytes. Each child has an
isolated process group, parent-death `SIGKILL`, null input/error streams and a
monotonic deadline. Descriptor setup remains valid with closed inherited
standard descriptors, and whole-process-group termination covers setup,
timeout, capture and non-success paths. A close-on-exec report channel separates
setup or `exec` failure from clean child exits. The executable override is
compiled only into the fixture Settings binary; the production Audio broker
neither links this bridge nor contains its path or hook.

Status invokes exactly `provider-status --format json` and accepts only
`synapse.goxlr.provider-status/v3`. The C11 bounded parser requires exact
nested fields, no truncation, at most one device, fixed source authority,
supported model, coherent generation/profile readiness, all four faders,
two-state mute, cough and output state, all eleven independent capability
booleans and complete system-output state. It rejects duplicate, missing,
unknown, malformed or incoherent values before emitting anything. The Settings
v2 projection strips the provider ID, generation, profile identity and routing
state while retaining typed popup values and independent capability gates.

Provider activity and USB presence remain separate. After inactive or failed
status only, C11 may invoke exactly `inventory --format json` and validate its
complete bounded device inventory. This read-only probe sets only
`presenceKnown` and `devicePresent`; it cannot publish controls or start the
provider. Opening or refreshing Settings and the popup therefore never invokes
`provider-serve`.

Planning accepts one of eleven compile-time control IDs and a canonical bounded
integer. C11 invokes fixed `plan-popup-control` argv, strictly validates the
provider plan, and translates it to a Settings plan with a private cohort. Apply
requires the exact original/requested pair, cohort and
`synapse-settings/audio-goxlr-popup/v1`; only C11 translates that acknowledgement
to `synapse-goxlr/popup-control/v1` and invokes fixed `apply-popup-control` argv.
The provider recreates the plan against fresh state, performs the second
preflight, issues no more than one setter, observes fresh state and persists its
model before producing a receipt. Settings strictly translates applied,
already-applied, refused, drifted, compensated and rollback-failed outcomes.
Compensation has provider-model-only authority, never proves hardware
restoration, is not attempted over an external restoration or third state, and
an uncertain write is never retried.

The Qt adapter repeats exact status, plan and receipt validation, owns the
serialized plan/apply sequence and refreshes the full Audio cohort after every
outcome. `AudioPopupHost` exists only while its host is active; deactivation or
destruction terminates pending work, clears choices and clears its snapshot.
QML receives bounded typed values, booleans and methods only—never JSON,
provider IDs, generations, cohorts, acknowledgements, argv, executable paths or
transport state. C++ alone launches the fixed `/usr/bin/synapse-goxlr gui`
application. Loading, refreshing and launching the UI do not imply provider
activity, hardware readback, audibility or physical qualification.

## PipeWire-Pulse core adapter

Production executes only absolute `/usr/bin/pactl` with fixed operation argv.
Each child has bounded capture, a two-second timeout, an isolated process group,
null input/error streams and parent-death `SIGKILL`. Executable and policy path
overrides exist only in a separately compiled fixture binary.

Backend indexes are exact nonnegative JSON integers through `INT_MAX` and must
be unique within each raw inventory. Present mute and volume values must retain
the expected types; every volume channel is an integer through `UINT32_MAX`, and
projection arithmetic rejects overflow before rounding. A present stream
`properties` value must be an object, and present `application.name` or
`media.name` values must be bounded strings. Missing optional fields retain
their defined fallbacks. A malformed process-ID property only disables the
process-bound rule capability. Default sink/source names are opaque bounded raw
identities: an empty string means no named default, while an invalid nonempty
value invalidates and clears the complete inventory.

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
