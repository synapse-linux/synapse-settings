# Settings Audio and Graphics architecture

## Boundary

`synapse-settings` is the authoritative C11 core. QML consumes typed models
through a future first-party adapter; it never receives raw PipeWire names,
parses JSON, injects process environments or builds commands.

## Audio

The Alpha 2 adapter executes only fixed absolute `/usr/bin/pactl --format=json`
inventory argv. Each subprocess is capped at 1 MiB and two seconds. Executable,
sysfs and policy-path overrides exist only in the separately built fixture test
binary and are absent from production behavior. Outputs, inputs and cards are
bounded; raw backend names remain internal and become stable opaque tokens.

Changing a default takes an opaque token plus the exact
`synapse-settings/audio-default/v1` acknowledgement. The core resolves it
against a fresh cohort, invokes only `set-default-sink` or
`set-default-source`, then refreshes state and verifies the token is default.
Volume, profile, port, Bluetooth, stream and GoXLR mutations remain later typed
operations; QML's current direct PipeWire mutation must not be reused.

## Graphics

GPU inventory reads bounded `/sys/class/drm/cardN/device` attributes and emits
an opaque stable ID, user label, driver and semantic selection strategy. PCI
addresses and render-node paths are not exposed.

The private policy contains a default application GPU and at most 128 rules:

1. exact canonical executable path;
2. longest canonical directory prefix;
3. default application GPU.

Configuration requires existing paths of the declared type. Files are regular,
owner-only, non-symlink, canonical JSON and atomically replaced with mode 0600.
Unknown/duplicate semantic records fail closed.

A directory rule can represent a Steam library. Prefix matching observes path
component boundaries, so a rule for `steam` never matches `steam2`.

## Enforcement boundary

Policy selection is implemented; enforcement is deliberately not. A future
launch broker will resolve the executable before launch and apply a typed
Mesa `DRI_PRIME` or NVIDIA PRIME plan internally. It must mediate desktop
entries, Synapse launch actions and Steam inheritance without exposing raw
environment values to QML.

The policy:

- affects only future broker-launched processes;
- cannot migrate an existing process;
- does not switch the display/compositor GPU;
- cannot intercept arbitrary execution outside the broker;
- does not install packages or mutate the current live session in Alpha 2.
