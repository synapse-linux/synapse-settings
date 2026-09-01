# Audio Settings QML candidate

`AudioSettings.qml` lazily instantiates `AudioSettingsSection.qml` only while the
Audio section is visible. QML consumes typed models and invokes typed adapter
methods. It does not parse JSON, inspect `/proc`, resolve executable paths,
construct commands, select raw PipeWire node names, or inject environments.

Required adapter surface:

- properties: `audioBusy`, `audioAvailable`, `audioReason`, `audioOutputs`,
  `audioInputs`, `audioStreams`, `audioCards`, `audioRouteRules`, and
  `audioRouteEnforcementAvailable`;
- operations: `loadAudio()`, `setAudioDefault(direction, deviceId)`,
  `chooseAudioProcessRule(direction, deviceId)`,
  `chooseAudioExecutableRule(direction, deviceId)`,
  `chooseAudioDirectoryRule(direction, deviceId)`, and
  `removeAudioRouteRule(ruleId)`.

The trusted adapter passes the selected opaque Audio stream ID to the C11
`set-process-rule` operation. The C backend privately maps its same-UID process
to a pinned `/proc/PID/exe`; neither PID nor raw process metadata reaches QML.
Path selection uses a native chooser and the C backend canonicalizes the result.
The future audio route broker owns PipeWire stream observation and movement;
QML never does.
