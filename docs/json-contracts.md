# JSON contracts

All contracts are UTF-8, locale-independent and versioned. Unknown input-policy
fields fail closed.

| Schema | Purpose |
|---|---|
| `synapse.settings.sections/v2` | Lazy Settings section inventory |
| `synapse.settings.audio-inventory/v1` | Bounded outputs, inputs, streams and cards |
| `synapse.settings.audio-default-plan/v1` | Read-only default-device plan |
| `synapse.settings.audio-default-receipt/v1` | Verified default-device transaction |
| `synapse.settings.audio-route-policy/v1` | Private canonical persisted policy |
| `synapse.settings.audio-route-policy-view/v1` | Redacted presentation model |
| `synapse.settings.audio-route-policy-receipt/v1` | Atomic policy mutation receipt |
| `synapse.settings.audio-route-resolution/v1` | Deterministic per-executable resolution |
| `synapse.settings.audio-route-broker-status/v1` | Read-only capability or active new-stream broker status |
| `synapse.settings.audio-route-broker-receipt/v1` | Verified per-new-stream enforcement result |

The policy receipt distinguishes `policyApplied` from `routingApplied`. Alpha 4
still sets the former true and the latter false because a policy write never
proves a broker move. The resolution contract returns one
of `exact-executable`, `directory-prefix`, or `system-default` and separately
reports whether the selected endpoint is currently available.

Opaque Audio IDs are scoped by direction (`output-…` or `input-…`). Stream IDs
are bounded `playback-N` or `recording-N` tokens and must be unique within one
inventory cohort. Raw backend node names are never public contract fields.

The broker status distinguishes source capability, process activity and actual
enforcement availability. Its event receipt is restricted to
`new-streams-only`; `routingApplied=true` requires `status=Applied`,
`changed=true`, and verified post-state. Failed compensation and unverified
command success cannot claim routing. Both broker contracts fix
`existingStreamMigration=false` and contain no PID, path, raw endpoint or event
line.

The Qt adapter validates exact field sets, bounds, token forms, duplicate
identities, direction/device consistency and the honest enforcement flags before
publishing any projection. It never forwards raw contract objects to QML.
