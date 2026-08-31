# Audio Settings scope

Alpha 2 provides the first typed backend slice:

- bounded outputs, inputs, streams and cards;
- stable opaque endpoint identities;
- default output/input planning;
- exact-acknowledgement fixed-argv mutation;
- post-write verification and deterministic unavailability.

The complete lazy Audio tab will add typed volume/mute/balance, profile/port,
per-stream route, level meters, safe playback tests, Bluetooth state, hotplug
and GoXLR presence. Those operations must remain independent; a safe sample
never authorizes capture, profile import or GoXLR firmware/mixer mutation.

The existing Quickshell `AudioPanel.qml` writes PipeWire objects directly. It is
not the implementation model for Settings and must be replaced by a typed
first-party adapter before deployment.
