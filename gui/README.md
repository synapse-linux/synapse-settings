# Settings QML presentation candidate

These components contain presentation and typed user intent only. They require a
first-party `QtObject` adapter and never discover executables, parse JSON,
construct shell commands, inject GPU environment variables or write PipeWire.

Required adapter surface:

- audio: `audioBusy`, `audioAvailable`, `audioReason`, `audioOutputs`,
  `audioInputs`, `audioStreams`, `audioCards`, `loadAudio()`,
  `setAudioDefault(direction, opaqueId)`;
- graphics: `graphicsBusy`, `graphicsGpus`, `graphicsRules`,
  `graphicsDefaultGpu`, `graphicsEnforcementAvailable`, `loadGraphics()`,
  `setGraphicsDefault(opaqueId)`, `addGraphicsRule(type, path, opaqueId)`,
  `removeGraphicsRule(ruleId)`.

The adapter must invoke the installed core through fixed argv with bounded I/O,
validate the versioned schemas, and publish typed models. It hard-codes exact
acknowledgements internally; QML never receives them.
