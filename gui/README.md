<!-- SPDX-License-Identifier: MIT -->
# Audio Settings Qt presentation

`AudioSettings.qml` lazily instantiates `AudioSettingsSection.qml` only while an
explicit host-owned `active` property is true. `Main.qml` is a standalone Qt
Quick host that injects required `AudioAdapter` and `SettingsTheme` objects;
feature QML has no Quickshell import.

`QQuickStyle::Basic` is the deterministic controls substrate. The visible
surface is owned by Synapse QML components for cards, buttons, sliders, combo
boxes and section headings. Background, surface, hover, border, accent, text,
muted, urgent, font, radius and spacing tokens flow explicitly through
`AudioSettings` and `AudioShellHost`, so a shell can inject its palette without
creating a shell dependency in feature QML. Standalone Settings discovers a
theme provider only as a trusted executable sibling, `/usr/bin/synapse-theme`,
or `/usr/local/bin/synapse-theme`; it never searches `PATH`. Its strict v3
adapter watches fixed theme state paths, rejects oversized output, duplicate
JSON keys, unsafe IDs and deadline overruns, and kills its isolated provider
process group on overflow or timeout. It retains the last valid palette after a
failed reload and updates existing QML bindings without recreating the Audio
backend. Accent foregrounds use WCAG relative-luminance contrast.

Alpha 7 also exports the same presentation through the
`Synapse.Settings.Audio` QML module. Its Qt extension plugin registers one
engine-owned `AudioBackend` singleton and `AudioShellHost` renders the unchanged
feature QML. Production fixes the singleton backend to
`/usr/bin/synapse-settings`; the test-only plugin variant can select the fixture
binary. Package-owned translations initialize when the module loads. All 64
pinned GUI locale identifiers have embedded catalogues; `en_US` and `it_IT` are
complete, while the other 62 alpha catalogues contain explicitly unfinished
source-English entries and therefore fall back per message without claiming
translated coverage. Unknown or malformed locale requests fall back to
`en_US`, and recognized RTL locales set the Qt layout direction and mirror the
feature presentation tree.

The adapter implements these properties:

- `audioBusy`, `audioSnapshotReady`, `audioAvailable`, `audioReason`;
- `audioOutputs`, `audioInputs`, `audioStreams`, `audioCards`;
- `audioProfilePortAvailable`, `audioProfilePortMutationAvailable`,
  `audioProfilePortReason`, `audioProfileCards`, `audioPortEndpoints`;
- `audioRouteRules`, `audioRouteBrokerAvailable`, `audioRouteBrokerActive`,
  `audioRouteBrokerReason`, `audioRouteEnforcementAvailable`;
- `audioGoxlrStatus`, `audioGoxlrReason`, `audioGoxlrProviderActive`,
  `audioGoxlrPresenceKnown`, `audioGoxlrDevicePresent`,
  `audioGoxlrMutationAvailable`, `audioGoxlrTruncated`, `audioGoxlrDevices`;
- `audioProcessChoices`, `audioProcessChoiceOpen`;
- `audioSelectionConfirmationOpen`, `audioSelectionKind`,
  `audioSelectionTargetLabel`, `audioSelectionOriginalLabel`,
  `audioSelectionRequestedLabel`;
- `audioStatusId`, `audioErrorId`.

It implements these fixed operations:

- `loadAudio()`;
- `setAudioDefault(direction, deviceId)`;
- `moveAudioStream(streamId, originalDeviceId, requestedDeviceId)`;
- `setAudioVolume(targetId, percent)`;
- `setAudioMuted(targetId, muted)`;
- `setAudioGoxlrFaderVolume(faderIndex, value)` and
  `setAudioGoxlrFaderMuted(faderIndex, muted)`;
- `setAudioGoxlrCoughMuted(muted)`,
  `setAudioGoxlrHeadphonesVolume(value)` and
  `setAudioGoxlrLineOutVolume(value)`;
- `openGoxlrMixer()` and `deactivateAudio()`;
- `planAudioProfile(cardId, profileId)`;
- `planAudioPort(direction, deviceId, portId)`;
- `confirmAudioSelection()` and `cancelAudioSelection()`;
- `chooseAudioProcessRule(direction, deviceId)`;
- `confirmAudioProcessRule(streamId)` and `cancelAudioProcessRule()`;
- `chooseAudioExecutableRule(direction, deviceId)`;
- `chooseAudioDirectoryRule(direction, deviceId)`;
- `removeAudioRouteRule(ruleId)`.

QML does not parse JSON, inspect `/proc`, resolve paths, construct commands,
select raw PipeWire card, endpoint, profile or port names, receive transaction
cohorts/acknowledgements, or inject environments. Native path dialogs remain
inside the adapter. Active-process choices contain only a sanitized label and an
opaque stream token; the C backend privately maps the selected same-UID process
to a canonical executable.

The adapter plans default changes before applying them, validates every receipt,
and republishes only a complete base-inventory-plus-profile/port-inventory-plus-
policy-plus-broker-status-plus-GoXLR-status cohort. It cross-validates
profile/port targets and active opaque selections against base inventory before
publishing either model.
Volume and mute follow their own plan, opaque-cohort, exact-acknowledgement,
apply, postflight and compensation contract. Every accepted plan reaches the
apply decoder; an unchanged value is a verified `AlreadySet` receipt without a
setter. QML passes only a published target and bounded typed requested value; it
cannot construct the cohort or setter.
Requested volume is capped at 100%, and the dialog explicitly starts no playback
or capture and changes no routing or profile. For an existing active stream it separately validates the original endpoint,
requests a fresh opaque cohort, supplies the core-owned exact acknowledgement,
and independently validates the one-stream receipt. QML only opens the explicit
confirmation and chooses a typed endpoint; it never receives the cohort or
acknowledgement. Profile and port selection uses another independent plan and
receipt family: QML selects only owner-compatible projected tokens, shows
bounded original/requested labels and confirms graph/signal-path consequences.
Empty labels remain presentation-only and QML substitutes localized generic
text without using an opaque or raw identity as a label. The adapter owns the
opaque cohort, exact original selection, acknowledgement and apply argv, then
refreshes after success, refusal, failure or restoration. QML receives no raw
choice identity and cannot bypass confirmation. Adapter commands have null
standard input/error, bounded incremental output capture and a dedicated process
group that is terminated on timeout, output overflow or teardown.

Policy persistence still does not imply stream movement. The separate C11 broker
owns new-stream observation and enforcement. The adapter obtains only typed,
read-only runtime status through the C11 CLI; QML maps it to Active, Inactive or
Unavailable presentation and cannot start, stop or configure the service.

GoXLR inspection remains read-only and non-activating. C11 invokes only fixed
provider-status and, when needed, inventory commands, validates the full source
contracts, and emits a single-device Settings-owned typed projection. The Qt
adapter validates it again and removes provider tokens, generations and profile
identity before publishing four faders, cough, outputs and independent
capabilities.

GoXLR mutation uses separate fixed adapter methods. Each method requires the
published Ready/profile/capability gates and starts an adapter-owned plan,
acknowledged apply, receipt-validation and complete-refresh sequence. QML never
receives the original value, cohort, either acknowledgement, receipt, command,
executable path or process metadata. Provider-model compensation does not prove
hardware rollback, an uncertain write is never retried, and a third state is
never overwritten. `AudioPopupHost` is a passive shared-backend projection:
hide/destruction does not call `deactivateAudio()`. Explicit adapter deactivation
still cancels pending work and clears its snapshot. Popup activation reuses a
validated snapshot; a blocked initial load attempt is not automatically replayed.
Refresh and commands require activity and no busy/shared-choice owner. Hosts must
also honor retained Settings confirmation/draft owners; see
[the local surface contract](../docs/audio-surface.md).

C++ alone invokes fixed `/usr/bin/synapse-goxlr-gui --open-or-activate` as a
finite client (512-byte stdout limit, 5-second deadline, null stdin/stderr and
owned-group cleanup). `openGoxlrMixer()` returning true means initiation only.
A pending request is installed before synchronous publication; duplicates refuse
without a queue. Only exit 0 **and** the exact canonical
`synapse.goxlr.gui-activation/v1` `activation-requested` receipt publish the
existing `audio-goxlr-mixer-opened` status ID. This confirms a request to a ready
scene, not compositor focus, backend/device readiness or physical acceptance.
Every other result is unavailable/unconfirmed, including exit 0 without a receipt.

The native GoXLR GUI elects its own cooperative per-user/session owner. The
finite helper may start one independently persistent GUI when no owner exists;
it never discovers or terminates ambient processes. Helper destruction or timeout
can leave an already launched GUI running and proves neither absence nor focus.
Audio deactivation does not own this presentation request; adapter destruction
cancels only its finite helper. Display/session and graphical locale inheritance
have a separate native environment policy and do not broaden ordinary Audio
command environments. Runtime executable overrides exist only in test builds.
Opening or refreshing either UI cannot start the provider, play audio, capture
input or claim hardware readback.

## Isolated installation regression

`make test-install BUILD_GUI=1` builds the ordinary prerequisites, then runs
`tests/install-run.py`. This target is also part of `test-all`; use
`BUILD_GUI=0` with `test-install` for the CLI-only installation contract.
`INSTALL_TEST_OUTPUT_DIR=/absolute/new/directory` retains the test destinations,
command receipts, bounded logs and inventories. Otherwise destinations are
private temporary directories, removed when the test finishes.

The test invokes the actual `install` recipe into two empty `DESTDIR`s (including
a path with spaces): the `/usr` layout and a custom prefix with independent
binary/data/library/QML directories. It never installs on the host. Prebuilt
inputs are held old for the nested Make invocation and toolchain commands are
disabled: this tests installation, not whether those binaries were built from
the current source. Source/build provenance remains a separate gate.

`GUI_MODULE_QML` is shared by production/test module staging and installation.
The regression independently derives required QML files from `qmldir`, checks
exact destination inventory, source bytes and file/directory modes, rejects
unexpected files (including standalone `Main.qml` or test hooks), and detects
every registered QML file's individual omission. The eight historical omissions
also receive separate real QML-load rejection checks. GUI installs must construct
the production module's inactive presentation with explicitly fake projections
in `en_US` and `it_IT`, with fatal warnings and embedded translation checks.
The native Audio singleton is not used; no CLI, broker or standalone GUI is
launched, and no Audio/provider operation is requested.

Short private HOME/XDG paths, explicit offscreen/Basic/software settings and
bounded child groups keep the regression separate from a graphical session.
`QMLTESTRUNNER` may select a separately qualified sanitizer-runtime wrapper;
only the established `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1` settings are forwarded, not ambient `LD_PRELOAD`.
Strict QML lint and warning-fatal runtime checks are separate gates. This is not
a sandbox for untrusted Makefiles, native UX qualification, dependency release
approval, a package, a deployment allowlist or a rollback transaction.
