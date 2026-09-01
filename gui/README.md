# Audio Settings Qt presentation

`AudioSettings.qml` lazily instantiates `AudioSettingsSection.qml` only while an
explicit host-owned `active` property is true. `Main.qml` is a standalone Qt
Quick host that injects one `AudioAdapter` object as a required root property;
feature QML has no Quickshell import.

Alpha 7 also exports the same presentation through the
`Synapse.Settings.Audio` QML module. Its Qt extension plugin registers one
engine-owned `AudioBackend` singleton and `AudioShellHost` renders the unchanged
feature QML. Production fixes the singleton backend to
`/usr/bin/synapse-settings`; the test-only plugin variant can select the fixture
binary. Package-owned translations initialize when the module loads, with
unknown locales falling back to `en_US`.

The adapter implements these properties:

- `audioBusy`, `audioSnapshotReady`, `audioAvailable`, `audioReason`;
- `audioOutputs`, `audioInputs`, `audioStreams`, `audioCards`;
- `audioRouteRules`, `audioRouteBrokerAvailable`, `audioRouteBrokerActive`,
  `audioRouteBrokerReason`, `audioRouteEnforcementAvailable`;
- `audioProcessChoices`, `audioProcessChoiceOpen`;
- `audioStatusId`, `audioErrorId`.

It implements these fixed operations:

- `loadAudio()`;
- `setAudioDefault(direction, deviceId)`;
- `moveAudioStream(streamId, originalDeviceId, requestedDeviceId)`;
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
and republishes only a complete inventory-plus-policy-plus-broker-status cohort.
For an existing active stream it separately validates the original endpoint,
requests a fresh opaque cohort, supplies the core-owned exact acknowledgement,
and independently validates the one-stream receipt. QML only opens the explicit
confirmation and chooses a typed endpoint; it never receives the cohort or
acknowledgement. Policy persistence still does not imply stream movement. The separate C11 broker
owns new-stream observation and enforcement. The adapter obtains only typed,
read-only runtime status through the C11 CLI; QML maps it to Active, Inactive or
Unavailable presentation and cannot start, stop or configure the service.
