# Changelog

## 1.1.0-alpha.1

- Correct the standalone and reusable Audio presentation above the deterministic
  `QQuickStyle::Basic` substrate with Synapse-owned cards, buttons, sliders,
  combo boxes, section headings, typography, spacing and contrast-safe text.
- Keep feature QML host-neutral by propagating explicit theme tokens through
  `AudioSettings` and `AudioShellHost`; the shell can inject its palette while
  standalone Settings owns a strict live-reloading `synapse.theme.current/v3`
  adapter.
- Discover the standalone theme provider only as a trusted executable sibling,
  `/usr/bin/synapse-theme`, or the fixed compatibility path
  `/usr/local/bin/synapse-theme`, never through `PATH`; reject oversized output,
  duplicate decoded JSON keys, unsafe IDs and deadline overruns, kill the
  isolated provider process group on overflow or timeout, then retain the last
  valid palette or deterministic fallback on failed responses.
- Add rendered presentation, host-token propagation, disabled-control
  readability, provider-fallback, atomic theme-reload and bounded test-settle
  coverage. Expand all 64 catalogues to 148 active messages with reviewed
  `en_US` and `it_IT` entries.
- Align all first-party Settings source and SPDX metadata with the owner-selected MIT license, retaining separate licenses and notices for dependencies, and make the negative-tested 43-file license gate part of `test-all`.
- Replace the read-only GoXLR v1 projection with the strict
  `synapse.settings.audio-goxlr-status/v2` presentation contract decoded from
  the complete `synapse.goxlr.provider-status/v3` source contract.
- Project at most one ready device with four assigned faders, two-state mute,
  toggle-mode cough mute, headphones, Line Out and eleven independent
  capabilities; retain `provider-profile-model` authority and explicitly deny
  hardware readback and hardware-exact rollback.
- Probe `inventory --format json` separately after inactive or failed provider
  status so attached hardware and provider activity remain distinct claims,
  without starting or configuring the provider.
- Add read-only `plan-goxlr-control` and acknowledged `set-goxlr-control`
  mediation for the eleven fixed popup controls. Translate
  `synapse-settings/audio-goxlr-popup/v1` to the private provider
  acknowledgement only in C11 and strictly validate every upstream plan and
  receipt field.
- Require the provider's bounded cohort and second preflight, one setter, fresh
  observation and persistence path; surface applied, unchanged, refused,
  drifted, compensated and rollback-failed results without retrying an
  uncertain mutation or claiming hardware restoration.
- Extend the Qt adapter with exact status, plan and receipt decoders, serialized
  plan/apply execution, complete post-result refresh, lifecycle cancellation,
  typed fader/output/cough methods and a fixed `/usr/bin/synapse-goxlr gui`
  launcher.
- Add a lazy injectable `AudioPopupHost`, complete typed controls in the Audio
  Settings section, and tests for inactive-attached, absent, partial-capability,
  timeout, drift, provider-model compensation, launcher failure, close and
  synchronous destruction paths.
- Expand all 64 pinned GUI catalogues to the Alpha 11 message inventory, with
  reviewed `en_US` and `it_IT`, explicit unfinished English fallback for the
  other 62 catalogues and retained RTL projection.
- Remove the unused legacy Qt status-v1 decoder and obsolete Settings GoXLR
  status-v1 schema; production boundaries now require Settings v2/provider v3
  and reject provider-start authority in Settings.
- Keep the candidate source-only: fixtures perform all control operations; no
  installation, provider activation, live USB mutation, playback, capture or
  deployment is performed.

## 1.0.0-alpha.1

- Add a separate bounded `audio profile-port-inventory` capability for card
  profiles and physical endpoint ports, using owner-scoped opaque profile and
  port tokens and typed `available`, `unknown` or `unavailable` choices.
- Decode PulseAudio 17's emitted shapes exactly: object-keyed card profiles with
  optional boolean availability, and sink/source port arrays whose optional
  fixed-C-locale availability strings are strictly normalized to the public
  three-state vocabulary.
- Advance the base Audio inventory to v2 so a card's active profile is an opaque
  token rather than a raw PipeWire-Pulse profile name.
- Add read-only `plan-profile` and `plan-port` contracts plus separately
  acknowledged `set-profile` and `set-port` transactions for exactly one card,
  output or input target.
- Bind every plan to target identity, backend index, exact original/requested
  selections and availability with an opaque cohort; repeat preflight twice and
  refuse stale, cross-owner, unavailable or changed state before mutation.
- Execute at most one fixed `set-card-profile`, `set-sink-port` or
  `set-source-port` request, require fresh software-model verification, never
  retry an uncertain requested mutation and compensate only after a fresh
  same-identity, unchanged-observation proof.
- Do not overwrite external restoration or an intervening third state, and do
  not compensate to a choice that became unavailable; report exact verified
  restoration or failed/unavailable rollback honestly.
- Add a strict Qt boundary, atomic base/profile-port/policy/broker/GoXLR
  publication, localized profile and port selectors, localized generic text for
  missing labels, and an explicit signal-path confirmation that keeps cohorts,
  acknowledgements and raw names out of QML.
- Reject invalid UTF-8, raw or escaped NUL, malformed JSON grammar, unpaired
  surrogates, duplicate decoded keys, trailing data and excessive depth/key
  counts before `json-c`, while accepting exact configured bounds.
- Reject malformed present mute, volume and stream-property fields, bound every
  channel value through `UINT32_MAX` with overflow-safe aggregation, and treat
  an empty default name as no named default while rejecting invalid nonempty
  defaults.
- Run fixed-argv backend children in dedicated process groups with null standard
  input/error, parent-death `SIGKILL` and whole-group termination on setup,
  timeout, capture failure or non-success; keep descriptor setup valid with
  closed inherited standard streams, bound Qt child output incrementally and
  use bounded transaction-aware outer deadlines for multi-capture apply paths.
- Count excluded monitor sources within the raw input bound and fully validate
  their identities, indexes, metadata, labels, ports, availability and active
  selections, including global port budgets, before omission.
- Add strict Draft 2020-12 inventory, plan and receipt schemas; bounded,
  malformed, duplicate, cross-owner, race, timeout, verification and
  compensation fixtures; adapter tests; and QML tests.
- Embed and validate all 64 pinned GUI locale catalogues, retain complete
  `en_US`/`it_IT` coverage, mark the other 62 catalogues as explicit unfinished
  source-English alpha fallbacks, reject malformed locale requests, and render
  recognized RTL layout mirroring without claiming translated coverage.
- Require profile/port receipt status to agree with the child exit code, and
  normalize relative generated-source build paths so production GUI and QML
  module binaries reproduce across distinct build directories.
- State explicitly that profiles may rebuild the software graph and ports may
  change a signal path, while no transaction starts playback/capture, changes a
  default or durable policy, proves audibility, reads hardware state or claims
  exact hardware rollback.
- Keep the candidate source-only: no installation, package promotion, live
  profile/port mutation, playback, capture, provider startup or deployment is
  performed.

## 0.9.0-alpha.1

The provider-status v2 and Settings status v1 contracts described in this
historical entry were retired in 1.1.0-alpha.1 and are not accepted production
inputs.

- Add `audio goxlr-status` as a bounded read-only bridge to the fixed production
  `/usr/bin/synapse-goxlr provider-status --format json` command.
- Strictly decode the complete provider-status v2 contract in C11, reject
  duplicate or unknown fields and identities, and cap execution at three
  seconds, response bytes at 65536 and devices at eight.
- Isolate the fixed provider-status child in its own process group with null
  input/error streams and a monotonic deadline; classify launch, signal and
  status-transport failures separately from a clean inactive-provider result.
- Emit a Settings-owned v1 status contract with Ready, Inactive, Unavailable and
  Failed outcomes while treating a missing adapter or stopped provider as typed
  nonfatal state.
- Redact all provider profile values and project only device model and reported
  system-output capability; retain `provider-profile-model` as explicit source
  authority and fix hardware readback, exact rollback and mutation availability
  false.
- Add independent Qt contract validation and extend the atomic load cohort to
  inventory, policy, broker status and GoXLR status before publishing models.
- Present localized read-only provider status without exposing device IDs,
  provider commands, profile values or any GoXLR planning/apply invokable to
  QML.
- Add strict schema, malformed/duplicate/oversized/timeout fixture coverage,
  production/test-hook separation, adapter tests, QML module tests and updated
  `en_US` and `it_IT` catalogues.
- Keep the candidate source-only: no installation, provider startup, hardware
  transaction, playback, capture, package promotion or deployment is performed.

## 0.8.0-alpha.1

- Add independently planned and acknowledged volume and mute transactions for
  one opaque output, input, playback-stream or recording-stream target.
- Cap requested volume at 100% while retaining bounded original values through
  999% solely for exact rollback; reject raw backend identities and untyped
  values before mutation.
- Bind each plan to the target kind, backend identity, stream process instance
  when applicable, original value and requested value through an opaque cohort.
- Repeat preflight before one fixed `pactl` setter, require postflight identity
  and value verification, and never infer success from command exit alone.
- Freshly revalidate identity and observed value before exact-original rollback
  when an uncertain command visibly changed the target; never compensate an
  intervening value, and report failed or unverifiable rollback without success.
- Add strict plan/receipt schemas and independent Qt decoding, plus adapter-owned
  plan, acknowledgement, apply and complete refresh sequencing, including a
  receipt-validated `AlreadySet` path that invokes no `pactl` setter.
- Add explicit QML confirmation for volume and mute while keeping cohorts,
  acknowledgement, raw endpoint names, stream indexes and command construction
  out of presentation code.
- State and enforce that controls do not start playback or capture and do not
  change routing, defaults, profiles or durable policy.
- Add endpoint/stream fixtures covering success, no-op, stale state, invalid
  authority, false success, timeout, identity or target loss, amplified
  pre-state restoration, external restoration, intervening values, verified
  compensation and failed compensation.
- Publish explicit source-defined baseline ISA notes for every production ELF
  without using the rejected linker `-z x86-64-baseline` path.
- Keep the candidate source-only: no installation, package promotion, live
  volume/mute mutation, playback, capture or deployment is performed.

## 0.7.0-alpha.1

- Package the existing typed Audio adapter and host-neutral feature QML as the
  reusable `Synapse.Settings.Audio` Qt QML module.
- Register one engine-owned `AudioBackend` singleton that uses only the fixed
  production `/usr/bin/synapse-settings` boundary; a separately compiled test
  plugin may use the existing fixture backend override.
- Add `AudioShellHost`, exposing only bounded readiness, availability, busy,
  broker, status, reason and error projections to a shell host.
- Keep feature loading explicit and lazy through an `active` property while
  preserving the complete inventory-policy-broker-status refresh cohort.
- Fail path selection safely when a host did not opt into `QApplication`, and
  make unknown or malformed GUI locales fall back deterministically to
  `en_US`.
- Add plugin metadata, QML tooling types, staged-module tests, two-catalogue
  localization smoke tests and production test-hook exclusion checks.
- Keep the integration candidate source-only: no installation, package
  promotion, shell replacement, broker activation or live Audio mutation is
  performed.

## 0.6.0-alpha.1

- Add a separate read-only `plan-stream-move` contract and explicitly
  acknowledged `move-stream` transaction for exactly one active playback or
  recording stream.
- Bind the plan to an opaque cohort covering the stream index, process instance,
  canonical executable, exact current endpoint, requested endpoint and raw
  backend targets without exposing those private values.
- Revalidate the stream and cohort twice before mutation and require the caller
  to provide the exact original opaque endpoint.
- Claim `Applied` only after fixed-argv success and fresh same-identity target
  verification; otherwise return a typed `Refused` or `Failed` receipt.
- Restore the exact original raw endpoint only when a failed backend call
  nevertheless reached the requested endpoint and the same stream identity is
  still provable, then verify that rollback.
- Add a Qt confirmation dialog that selects one active stream and one typed
  endpoint, states that no persistent rule is created, and keeps cohort and
  acknowledgement details outside QML.
- Add strict schemas, independent C++ decoding, playback and recording fixtures,
  all typed preflight failures, stale-cohort and endpoint drift, timeout, stream
  loss, identity loss, verification and rollback tests.
- Refresh the complete typed presentation cohort after every apply outcome,
  including uncertain transport or receipt-contract failures.
- Keep the candidate source-only: no installation, service activation, live
  stream movement, playback, capture or deployment is performed.

## 0.5.0-alpha.1

- Add an owner-private, status-only AF_UNIX channel under
  `$XDG_RUNTIME_DIR/synapse` for the separate Audio route broker.
- Serialize broker startup with an owner-mode-0600 advisory lock and reject a
  concurrent broker without touching the active status socket.
- Verify runtime directory, socket type, owner and exact modes; bound connect,
  request, response and peer handling; require same-UID clients.
- Add `synapse-settings audio broker-status` as the authoritative C11 read-only
  client with strict canonical response validation.
- Extend the Qt adapter with an exact broker-status decoder and publish only
  typed available, active, reason and enforcement projections to QML.
- Present Active, Inactive and Unavailable new-stream broker states while always
  stating that existing streams are not moved.
- Reject stale, malformed, oversized, stalled, symlinked and loose-mode runtime
  states without enabling enforcement.
- Keep the service source disabled and perform no installation, activation or
  live Audio mutation.

## 0.4.0-alpha.1

- Add a separate first-party C11 Audio route broker for post-baseline new
  playback and recording streams.
- Establish the active-stream baseline after subscriber startup so broker start
  and restart never migrate an existing stream.
- Resolve each unseen stream from a fresh same-UID executable identity and fresh
  policy generation; exact executable still precedes longest directory prefix.
- Revalidate PID start time, executable, endpoint cohort and policy before a
  fixed `move-sink-input` or `move-source-output` transaction.
- Claim routing only after post-move target verification; compensate a proven
  partial failure back to the exact original endpoint when it remains available.
- Handle duplicate events, vanished streams, missing processes, endpoint loss,
  policy errors, command timeout and verification failure without success
  claims.
- Add strict broker status/receipt contracts and a hardened, disabled-by-default
  systemd user service source.
- Keep existing-stream migration, service activation, package installation and
  live qualification behind separate gates.

## 0.3.0-alpha.1

- Add a standalone lazy Qt Quick Audio Settings host.
- Add a strict typed Qt/C++ adapter with bounded asynchronous core execution.
- Plan before changing defaults and decode the verified receipt before refresh.
- Add an opaque active-process chooser and native executable/directory choosers.
- Keep raw JSON, PipeWire names, PIDs, paths, commands and acknowledgements out
  of QML.
- Add provisional complete `en_US` and `it_IT` catalogues, adapter unit tests and
  two-locale offscreen GUI smoke tests.
- Reject duplicate or negative active-stream identities and exclude both modern
  `monitor_source` and legacy monitor entries from the input-device inventory.
- Keep automatic stream enforcement unavailable pending the Audio route broker.

## 0.2.0-alpha.1

- Add bounded typed Audio outputs, inputs, cards and stream inventory.
- Add guarded default output/input planning, exact acknowledgement and readback
  verification.
- Add private exact-executable and canonical directory-prefix Audio rules for
  output and input devices.
- Resolve exact executable before longest directory prefix before current system
  default, with component-boundary matching.
- Add a lazy presentation-only Audio QML candidate and versioned JSON schemas.
- Report automatic stream routing honestly unavailable pending the typed broker.

## 0.1.0-alpha.1

- Publish the standalone CLI-first settings inventory.
- Expose semantic layers, package versions and generated live drift as v2 JSON.
- Keep optional Docker inspection direct-argv, bounded and non-mutating.
- Add version reporting and deterministic headless fixtures.
