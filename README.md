# Synapse Settings

License: MIT. See [`docs/LICENSE-AUTHORITY.md`](docs/LICENSE-AUTHORITY.md) for first-party provenance and separate dependency obligations.

CLI-first C11 settings backend, a lazy Qt Quick presentation, and a separate C11
new-stream Audio route broker. Alpha 11 adds a Settings-owned GoXLR popup
mediation boundary after the qualified Alpha 10 inventory, profile/port, policy
and broker stages. The `Synapse.Settings.Audio` module keeps PipeWire and GoXLR
provider authority, raw identities, generations, private cohorts,
acknowledgements, receipts and compensation in C11/Qt boundaries rather than
QML. It projects four assigned faders, their two-state mutes, cough mute,
headphones and Line Out only when the exact independent capability is ready, and
never represents provider-model state as hardware readback. The corrected
presentation keeps `QQuickStyle::Basic` as its deterministic substrate while
Synapse-owned controls, explicit host theme tokens and the standalone strict v3
theme adapter provide the visible product surface. Installation, shell
replacement, provider or broker activation, live mutation and physical
qualification remain later deployment gates.

```bash
make CORE_ROOT=/path/to/staged-core clean all test-all
build/synapse-settings audio inventory --format json
build/synapse-settings audio profile-port-inventory --format json
build/synapse-settings audio plan-profile --card CARD_ID --profile PROFILE_ID --format json
build/synapse-settings audio plan-port --direction output --device OUTPUT_ID --port PORT_ID --format json
build/synapse-settings audio broker-status --format json
build/synapse-settings audio goxlr-status --format json
build/synapse-settings audio plan-goxlr-control --control fader-a-volume --value 96 --format json
build/synapse-settings audio set-goxlr-control --control fader-a-volume --value 96 --original 80 --cohort COHORT --ack synapse-settings/audio-goxlr-popup/v1 --format json
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
- stable opaque endpoint, card, profile, port and stream identities;
- guarded, planned and verified default output/input selection;
- a separate bounded card-profile and physical endpoint-port inventory with
  owner-scoped tokens, bounded labels, localized generic presentation for
  missing labels and typed choice availability;
- one-target profile/port planning, explicit confirmation, exact
  acknowledgement, double preflight, software-model postflight and guarded
  exact-original compensation;
- profile plans that honestly allow graph and signal-path changes, and port
  plans that allow only a selected signal-path change, without playback,
  capture, default, policy or audibility claims;
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
  Synapse-owned cards, buttons, sliders, combo boxes and section headings above
  the deterministic Basic substrate;
- explicit host-neutral theme tokens, WCAG-derived accent foregrounds, plus
  trusted fixed-provider discovery and strict bounded live v3 theme reload in
  standalone Settings without `PATH` lookup;
- all 64 pinned GUI locale catalogues, complete `en_US`/`it_IT` coverage,
  explicit unfinished `en_US` fallback entries for the other 62 alpha
  catalogues, deterministic unknown-locale fallback and RTL layout projection;
- a bounded broker for new playback and recording streams created after its
  startup baseline;
- typed broker status and per-event receipts with no PID, executable path or raw
  PipeWire endpoint;
- owner-private, same-UID, bounded AF_UNIX status IPC with no mutation requests;
- typed Active, Inactive and Unavailable broker presentation in Settings;
- fixed-path, bounded GoXLR provider-v3 inspection with typed Ready, Inactive,
  Unavailable and Failed outcomes plus a separate read-only USB presence probe
  when the provider is not active;
- a single-device redacted projection of four assigned faders, independent
  volume/mute capabilities, toggle-mode cough mute, headphones, Line Out and
  reported system-output support, without identity or profile metadata;
- separately planned GoXLR popup controls using the Settings acknowledgement
  `synapse-settings/audio-goxlr-popup/v1`, provider cohort revalidation, one
  setter, fresh observation, typed provider-model compensation and a complete
  post-operation refresh;
- a lazy `AudioPopupHost` that reuses the shared snapshot without cancelling
  another surface's work on hide/destruction, interaction-aware host permission
  queries, and a fixed `/usr/bin/synapse-goxlr gui` launcher owned by C++ rather
  than QML;
- a separate, one-stream existing-stream plan and exact-acknowledgement move with
  same-identity postflight and exact-original rollback when safe;
- an explicit Qt confirmation surface that never creates a durable rule;
- strict UTF-8/JSON and contract decoding, bounded output, bounded execution,
  whole-process-group termination and single-flight GUI operations;
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
run against live Audio or a live GoXLR provider. Existing-stream movement and
level-control transactions were exercised only through the compile-time test
`pactl` override; no live stream, volume, mute, profile or port state was
changed. Fixtures cover device and stream controls, playback and recording
moves, card profiles, output/input ports, typed availability, owner separation,
preflight refusal, stale cohorts, wrong originals, target drift, malformed and
bounded inventories, timeout, false backend success, target or identity loss,
unavailable verification or restoration, external restoration, immediate and
intervening third values, verified rollback, failed rollback, exact aggregate
limits and parent/descendant process termination.
Policy receipts still describe policy
persistence only and therefore continue to report `routingApplied=false`.
Settings obtains broker runtime state through
`audio broker-status`, which sends one fixed read-only request to the broker's
mode-0600 AF_UNIX socket in an owner-mode-0700 runtime directory. The C11 client
rejects stale sockets, wrong ownership or modes, timeouts and noncanonical
responses before the Qt adapter receives a typed contract. The independent `audio goxlr-status` bridge executes only
`/usr/bin/synapse-goxlr` with fixed `provider-status --format json` arguments,
bounds time and output, strictly decodes the provider v3 contract and emits the
Settings-owned `synapse.settings.audio-goxlr-status/v2` projection. When status
is inactive or fails, a separate fixed `inventory --format json` call may
establish read-only attached/absent presence; neither path starts the provider.
`plan-goxlr-control` and `set-goxlr-control` translate the Settings acknowledgement
to `synapse-goxlr/popup-control/v1` only inside C11, accept only one of eleven
fixed controls, and strictly translate provider plans and receipts. A missing
adapter or inactive provider remains ordinary typed state.

Docker inventory from Alpha 1 remains optional and bounded. CLI and broker
runtime dependencies are `libsynapse-core.so.0` and `json-c`; the optional GUI
uses Qt 6 Core, Gui, Qml, Quick, Quick Controls, Widgets and Test at build time.
