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

The policy receipt distinguishes `policyApplied` from `routingApplied`. Alpha 2
sets the former true and the latter false. The resolution contract returns one
of `exact-executable`, `directory-prefix`, or `system-default` and separately
reports whether the selected endpoint is currently available.

Opaque Audio IDs are scoped by direction (`output-…` or `input-…`). Raw backend
node names are never public contract fields.
