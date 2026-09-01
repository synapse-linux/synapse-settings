# JSON contracts

All contracts are UTF-8, locale-independent and versioned. Unknown input-policy
fields fail closed.

| Schema | Purpose |
|---|---|
| `synapse.settings.sections/v2` | Lazy Settings section inventory |
| `synapse.settings.audio-inventory/v1` | Bounded outputs, inputs, streams and cards |
| `synapse.settings.audio-default-plan/v1` | Read-only default-device plan |
| `synapse.settings.audio-default-receipt/v1` | Verified default-device transaction |
| `synapse.settings.audio-control-plan/v1` | Read-only one-target volume or mute plan |
| `synapse.settings.audio-control-receipt/v1` | Verified one-target control or compensation result |
| `synapse.settings.audio-existing-stream-move-plan/v1` | Read-only plan bound to one active-stream cohort |
| `synapse.settings.audio-existing-stream-move-receipt/v1` | Explicit one-stream move, postflight and rollback result |
| `synapse.settings.audio-route-policy/v1` | Private canonical persisted policy |
| `synapse.settings.audio-route-policy-view/v1` | Redacted presentation model |
| `synapse.settings.audio-route-policy-receipt/v1` | Atomic policy mutation receipt |
| `synapse.settings.audio-route-resolution/v1` | Deterministic per-executable resolution |
| `synapse.settings.audio-route-broker-status/v1` | Read-only capability or active new-stream broker status |
| `synapse.settings.audio-route-broker-receipt/v1` | Verified per-new-stream enforcement result |

The policy receipt distinguishes `policyApplied` from `routingApplied`. Alpha 8
still sets the former true and the latter false because a policy write never
proves a broker move. The resolution contract returns one
of `exact-executable`, `directory-prefix`, or `system-default` and separately
reports whether the selected endpoint is currently available.

Opaque Audio IDs are scoped by direction (`output-…` or `input-…`). Stream IDs
are bounded `playback-N` or `recording-N` tokens and must be unique within one
inventory cohort. Raw backend node names are never public contract fields.

The control plan fixes `status=Planned`, `singleTarget=true`,
`safeVolumeMaximumPercent=100`, `postflightRequired=true`,
`rollbackOnUnverified=true`, and `applied=false`. Its `control-…` cohort binds
private target identity, target kind, backend index, control kind and exact
original/requested values. Volume is an integer; mute is a boolean. Requested
volume is 0–100 while original volume may be 0–999 solely to permit exact
rollback. A receipt is `Applied`, `AlreadySet`, `Refused` or `Failed` and
separately reports mutation attempt, verified requested result, compensation
attempt and verified compensation. `rollback-failed` requires an actual,
unverified compensation attempt; a verified compensation is limited to the
original mutation/verification failure reasons. `changed` means a verified
requested result, not a claim that failed compensation left state unchanged.
All control contracts fix playback, capture, profile and routing side effects
false.

The existing-stream plan fixes `status=Planned`, `singleStream=true`,
`postflightRequired=true`, `rollbackOnUnverified=true`, `applied=false`, and the
exact acknowledgement identifier. Its `move-…` value is an opaque bounded cohort
binding the private stream index, process instance, executable and exact current
and requested raw endpoints. The receipt is one of `Applied`, `AlreadyRouted`,
`Refused`, or `Failed`; cross-field invariants prevent any refused or failed
receipt from claiming `changed`, `moveApplied` or `verified`. A verified rollback
requires `rollbackAttempted=true`. Every receipt fixes `policyApplied=false`,
`persistentRuleCreated=false`, and `existingStreamMovement=true`.

The broker status distinguishes source capability, process activity and actual
enforcement availability. The broker serves the same contract for one fixed
`status-v1` request over its owner-private, status-only AF_UNIX channel. Absence,
stale sockets, unsafe ownership or modes, timeout, response limits and malformed
responses map to the schema's bounded identifiers such as `broker-not-running`,
`runtime-unavailable`, `runtime-state-invalid`, `timeout` and `invalid-response`,
with `active=false` and `enforcementAvailable=false`. Its event receipt is restricted to
`new-streams-only`; `routingApplied=true` requires `status=Applied`,
`changed=true`, and verified post-state. Failed compensation and unverified
command success cannot claim routing. Both broker contracts fix
`existingStreamMigration=false` and contain no PID, path, raw endpoint or event
line.

The Qt adapter validates exact field sets, bounds, token forms, duplicate
identities, direction/device consistency and the honest enforcement flags before
publishing any projection. It invokes only the C11 `audio broker-status` client
and never forwards raw contract objects, socket paths or transport text to QML.
