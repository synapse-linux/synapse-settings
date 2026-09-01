# Synapse Settings agent contract

- The authoritative settings backend is bounded C11 and works headlessly.
- QML is presentation-only: it cannot parse raw JSON, inspect `/proc`, resolve
  paths, construct commands, inject environments, or mutate PipeWire directly.
- The Qt/C++ adapter is a thin typed boundary only. It may decode fixed C-core
  contracts, own native choosers and publish bounded projections; Audio policy
  and PipeWire authority remain in the C11 core and separate C11 broker.
- Execute Audio subprocesses with fixed absolute argv, bounded output and bounded
  time. Test executable/path overrides are compile-time test hooks only.
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
- Broker activation, existing-stream movement and deployed Settings status are
  separate gates. Policy receipts must never claim that the broker moved audio.
- Keep capture, Bluetooth pairing, safe playback tests, GoXLR control and profile
  mutation behind independent consent and capability gates.
- Composition lock data and pacman state remain read-only inputs. Docker
  inspection remains optional, bounded and non-mutating.
- Build against released `libsynapse-core`; packaging lives only in
  `synapse-pkgbuilds/synapse-settings`.
- Broker receipts expose no PID, executable, raw endpoint or subscriber line.
  A success claim requires fresh same-process identity, command success and
  post-move endpoint verification; partial failures compensate when provable.
- Pass strict GCC/Clang, ASan/UBSan, analyzers, malformed policy, subprocess
  timeout, broker race/failure, schema, QML, exact-ISA and reproducibility tests.
