# Synapse Settings agent contract

- All first-party Synapse Settings source is MIT-licensed; preserve separate dependency and attribution notices and never relabel third-party material.
- The authoritative settings backend is bounded C11 and works headlessly.
- QML is presentation-only: it cannot parse raw JSON, inspect `/proc`, resolve
  paths, construct commands, inject environments, or mutate PipeWire directly.
- The Qt/C++ adapter is a thin typed boundary only. It may decode fixed C-core
  contracts, own native choosers and publish bounded projections; Audio policy
  and PipeWire authority remain in the C11 core and separate C11 broker.
- Execute Audio subprocesses with fixed absolute argv, bounded output and bounded
  time, null standard input/error, dedicated process groups, whole-group
  termination and parent-death signals. Test executable/path overrides are
  compile-time test hooks only.
- Expose endpoint and stream identities as stable opaque tokens; do not expose
  raw PipeWire names, device addresses, PIDs, command lines or environments.
- Production GUI discovery is fixed to a same-directory installed core or
  `/usr/bin/synapse-settings`; never expose backend or argv selection to QML.
- Default-device mutation requires the exact acknowledgement, a typed endpoint
  ID, and post-write verification.
- Persist application Audio policy privately with canonical JSON, mode 0600,
  no-follow loading and atomic replacement.
- Durable application identity is a canonical executable or component-safe
  directory prefix. Never persist unstable PIDs or match process-name strings.
- Resolve rules independently per direction: exact executable, longest directory
  prefix, then current system default.
- Policy persistence is not routing enforcement. The broker may enforce only
  post-baseline new-stream events; startup, restart, policy change, endpoint
  hotplug and `change` events must never move an already active stream.
- Broker activation and existing-stream movement are separate gates. Policy
  receipts must never claim that the broker moved audio; Settings may consume
  only the separate read-only broker status contract.
- An existing stream may move only through its separate read-only plan and exact
  acknowledgement, one opaque stream at a time. Bind the plan to the same-UID
  process instance, exact original endpoint, requested endpoint and backend
  index; revalidate twice before fixed-argv mutation.
- Existing-stream success requires same-identity postflight. Roll back only to
  the exact captured original raw endpoint and only while the same stream
  identity remains provable. Never create or apply a policy rule as part of this
  transaction, and keep its cohort and acknowledgement outside QML.
- Volume and mute are separate one-target plan/apply transactions for opaque
  endpoint or stream IDs. Cap requested volume at 100%, revalidate the bound
  identity twice, require exact acknowledgement and postflight, and compensate
  only to the exact original value after freshly proving both identity and the
  unchanged observed value. Keep cohorts, acknowledgements and raw setter
  identities outside QML; never imply playback, capture, profile, routing,
  default or policy authority.
- Card profiles and endpoint ports are a separate inventory and transaction
  family. Use owner-scoped opaque choice tokens, typed availability, bounded
  labels, an exact acknowledgement and a cohort bound to target identity and
  exact original/requested selection. Revalidate twice before one fixed setter,
  require software-model postflight, never retry an uncertain requested
  mutation, and compensate only when the first postflight observed the requested
  choice and a fresh same-identity proof still observes it while the original
  remains safe. Do not overwrite external restoration, an immediate or
  intervening third choice, or an unavailable original choice.
- Profile selection may rebuild the software graph and change a signal path;
  port selection may change one signal path. Neither capability may start
  playback/capture, change defaults or policy, prove audibility, claim hardware
  readback or claim exact hardware rollback. Keep raw names, acknowledgements,
  cohorts and setter construction out of Qt projections and QML.
- GoXLR status inspection is read-only. Invoke only fixed production
  `/usr/bin/synapse-goxlr provider-status --format json`, bound execution and
  output, and strictly validate the complete provider contract. A missing or
  inactive provider is typed state, not authority to start it. A separate
  read-only inventory probe may establish attached-device presence after an
  inactive status, but must not activate the provider.
- Popup GoXLR mutation is a separate mediated boundary. QML may receive only one
  bounded presentation projection with four assigned faders, cough mode/state,
  output levels and independent capability booleans. It receives no device
  token, generation, cohort, acknowledgement, raw identity, command or process
  metadata. The C11 core alone plans and applies one control through
  `synapse-settings/audio-goxlr-popup/v1`, translating internally to the exact
  provider acknowledgement, revalidating twice, issuing one setter, and
  refreshing status after every outcome.
- The GoXLR Settings contract must state provider-profile-model authority,
  hardware readback and rollback limits honestly. Never retry an uncertain
  write, overwrite a freshly observed third state, or describe provider-model
  compensation as hardware-exact restoration. Opening or refreshing Settings
  must never start the provider.
- Keep capture, Bluetooth pairing, safe playback tests, GoXLR control and profile
  mutation behind independent consent and capability gates. The popup may
  enable a control only for Ready state, an active provider, exactly one typed
  ready device, global mutation availability and that control's exact
  independent capability.
- Composition lock data and pacman state remain read-only inputs. Docker
  inspection remains optional, bounded and non-mutating.
- Build against released `libsynapse-core`; packaging lives only in
  `synapse-pkgbuilds/synapse-settings`.
- Broker receipts expose no PID, executable, raw endpoint or subscriber line.
  A success claim requires fresh same-process identity, command success and
  post-move endpoint verification; partial failures compensate when provable.
- Runtime status uses only a private `$XDG_RUNTIME_DIR/synapse` AF_UNIX endpoint,
  mode 0600 in a mode-0700 owner directory. It accepts one bounded status request,
  verifies same-UID peers and exposes no mutation operation or raw provider state.
- Pass strict GCC/Clang, ASan/UBSan, analyzers, malformed policy, subprocess
  timeout, broker race/failure, schema, QML, exact-ISA and reproducibility tests.
- The `Synapse.Settings.Audio` module reuses the typed adapter and host-neutral
  feature QML. Its production singleton targets only
  `/usr/bin/synapse-settings`; fixture selection belongs only to a separately
  compiled test plugin.
- Shell-facing Audio state is limited to bounded booleans, counts and typed
  identifiers.
  Loading the module is read-only and never waives the independent installation,
  activation, default, policy, stream-movement or deployment gates.
