# Settings JSON contracts

All contracts reject undeclared fields through their schemas in `schemas/`.
Text output remains deterministic `en_US` operational output.

## Sections

`synapse.settings.sections/v2` lists lazy `layers`, `audio`, `graphics`, `input`
and `themes` sections.

## Audio

- `synapse.settings.audio-inventory/v1`
- `synapse.settings.audio-default-plan/v1`
- `synapse.settings.audio-default-receipt/v1`

Inventory uses `pipewire-pulse-model` authority, bounded opaque endpoint IDs and
nullable deterministic unavailability. It never claims hardware readback.
Default mutation requires `synapse-settings/audio-default/v1` and returns a
verified receipt.

## Graphics

- `synapse.settings.graphics-inventory/v1`
- internal `synapse.settings.graphics-policy/v1`
- presentation `synapse.settings.graphics-policy-view/v1`
- `synapse.settings.graphics-policy-receipt/v1`
- `synapse.settings.graphics-resolution/v1`

The internal policy retains canonical paths in an owner-only file. The view
uses display paths and opaque rule IDs. Resolution does not echo the queried
path and reports availability, match source and semantic driver strategy.

Until the launch broker exists, every policy view, receipt and resolution says
`enforcementAvailable=false` with reason `launch-broker-not-integrated`.
`runningProcessMigration=false` is invariant.
