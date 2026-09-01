# Changelog

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
