# Audio Settings Qt presentation

`AudioSettings.qml` lazily instantiates `AudioSettingsSection.qml` only while the
Audio section is visible. `Main.qml` is a standalone Qt Quick host that injects
one `AudioAdapter` object as a required root property; feature QML has no
Quickshell import.

The adapter implements these properties:

- `audioBusy`, `audioAvailable`, `audioReason`;
- `audioOutputs`, `audioInputs`, `audioStreams`, `audioCards`;
- `audioRouteRules`, `audioRouteEnforcementAvailable`;
- `audioProcessChoices`, `audioProcessChoiceOpen`;
- `audioStatusId`, `audioErrorId`.

It implements these fixed operations:

- `loadAudio()`;
- `setAudioDefault(direction, deviceId)`;
- `chooseAudioProcessRule(direction, deviceId)`;
- `confirmAudioProcessRule(streamId)` and `cancelAudioProcessRule()`;
- `chooseAudioExecutableRule(direction, deviceId)`;
- `chooseAudioDirectoryRule(direction, deviceId)`;
- `removeAudioRouteRule(ruleId)`.

QML does not parse JSON, inspect `/proc`, resolve paths, construct commands,
select raw PipeWire node names or inject environments. Native path dialogs remain
inside the adapter. Active-process choices contain only a sanitized label and an
opaque stream token; the C backend privately maps the selected same-UID process
to a canonical executable.

The adapter plans default changes before applying them, validates every receipt,
and republishes only a complete inventory-plus-policy cohort. Policy persistence
still does not imply stream movement. The separate Alpha 4 C11 broker owns
new-stream observation and enforcement, but service activation and typed runtime
status integration are not part of the GUI candidate.
