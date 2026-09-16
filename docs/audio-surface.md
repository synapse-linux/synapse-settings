<!-- SPDX-License-Identifier: MIT -->
# Audio presentation surface — local integration revision 1

This is a bounded Settings navigation/lifetime increment, **not** the shared
Synapse graphical plugin manifest, registry or complete host API. Existing
`Synapse.Settings.Audio 1.0` imports and the typed C/Qt authority remain intact.
No foreign runtime imports or executable metadata are introduced.

## Navigation and actions

- Devices starts with Output and Input. The separate GoXLR application is opened
  only by an explicit Complete mixer action.
- Details selects one existing opaque endpoint ID and direction. Reordering does
  not retarget it. Removal makes the selected device unavailable, never selects a
  replacement, and disables new detail actions. Ports are filtered by both ID and
  direction. Profiles remain in Advanced because the current typed inventory does
  not establish a safe endpoint-to-card association.
- Application mixer contains current streams and the existing explicit volume,
  mute and separately confirmed move actions. It does not change saved rules.
- Advanced contains the mediated GoXLR controls, durable routing, broker status
  and profile/port choices. Saving policy is not active-stream enforcement.

Page changes keep instantiated delegates and confirmation owners. Each of the
four pages retains its scroll position and focus target; a missing/disabled focus
target falls back to the page heading. Dialogs, open menus/choices and busy work
block navigation. Display labels never become action identities.

Owned `AudioDialog` and `AudioSpinBox` controls use the injected presentation
palette and typography. Dialog role dispatch is retained: only explicit acceptance
calls the existing typed operation; Escape cancels without a setter. Numeric input
is bounded to the existing 0–100 range. Theme updates do not reset its edited value.

## Retained view composition

`AudioSettingsSection.qml` is the presentation container: it owns navigation,
opaque selection, shared projection/theme bindings, activation, request helpers,
scroll/focus bookkeeping and every confirmation dialog. Its six private views
are instantiated eagerly, not loaded or destroyed by page changes:

- `AudioDeviceDetail.qml`: the selected endpoint's existing detail card.
- `AudioDeviceList.qml`: the same Output/Input delegates on Devices and Advanced.
- `AudioApplicationMixer.qml`: current streams and existing confirmed actions.
- `AudioAdvancedGoXLR.qml`: the existing mediated GoXLR presentation.
- `AudioAdvancedRouting.qml`: policy, broker and card-profile presentation.
- `AudioPortList.qml`: the same port choices on Device and Advanced.

Each receives the existing section explicitly; it does not create an adapter,
load a snapshot or own shared cancellation. Visibility bindings remain in the
container. Controls keep their names, palette bindings and confirmation routing.
Extracted legacy messages use `qsTranslate("AudioSettingsSection", ...)` so their
source/context identities remain unchanged; typed navigation IDs are unchanged.

The Make-generated standalone QRC embeds all six views. The module's explicit
copy list includes them and `qmldir` marks them internal; the public module
version/API is unchanged. There is no source `gui/resources.qrc` in Settings.
Retained-composition tests check object identity, compact/wide geometry and
write-free navigation/theme round trips; geometry is not visual acceptance.

## Surface queries and lifetime

`AudioSettings` and `AudioShellHost` expose:

| Member | Meaning |
|---|---|
| `surfaceApi` | Exact identifier `synapse.settings.audio.surface/v1`; not a plugin registration |
| `active` | Host-supplied interaction/activation request; first activation starts the existing read-only load |
| `contentLoaded` | Instantiated content; does not establish functional readiness |
| `contentReady` | Feature content instantiated, validated snapshot ready, Audio backend available |
| `requestHide()` | Boolean query; refuses while an interaction, confirmation or operation is pending |
| `canUnload`, `requestUnload()` | Boolean query; refuses destructive unload once content has been instantiated |

The queries have no cancellation, mutation or implicit state-transfer effect.
They are not a lock or authority to destroy the surface later without rechecking.
The standalone window consults `requestHide()` on close, so pending confirmation
or busy work must finish or be cancelled explicitly first.

The two internal loaders stay lazy until first activation, then retain content.
Hiding no longer calls `AudioAdapter::deactivateAudio()` through `AudioShellHost`.
Reactivation does not refresh or replay work automatically. Refresh remains
explicit; retained readiness is the last validated snapshot, not a freshness claim.
The adapter still implements bounded cancellation on explicit deactivation and
final destruction. Retention has a resource cost that has not been measured on a
target and is not a resource-saving claim.

## Shared popup projection and cooperating hosts

`AudioPopupHost` exposes `synapse.settings.audio.popup/v1`, `active`,
`interactionPending`, `canInteract`, `canUnload`, `requestHide()` and
`requestUnload()`. Hide permission refuses a missing backend, busy work or a
shared process/profile-port choice. Unload additionally requires inactivity.
These are permission queries, not cancellation or state transfer.

The popup neither deactivates the engine singleton on hide nor on destruction.
Its first activation consumes exactly one load attempt, reusing an already
validated snapshot. A blocked attempt is not replayed when busy clears, a backend
is replaced, or the popup reopens. Explicit refresh, setters and Complete mixer
require current activity and permission. Command-time checks read those inputs
directly rather than relying on derived bindings having updated in a signal
callback. Refresh remains hardware-write-free, but `loadAudio()` clears shared
process/profile-port choices: it is not a presentation-neutral read.

The separate OS host increment checks both retained Audio surfaces before panel
routing, rejects incompatible surface versions before activation or consuming
health projections, and retains outer content after the first Audio attempt,
including load failure. Cooperating hosts must preserve local Settings dialogs
as well as the popup's shared-backend gates; the popup alone cannot discover
another presentation's local draft. A real fixture-module regression verifies
that passive popup activation/hide/destruction preserves the singleton snapshot,
selected opaque device and Settings volume draft without state/model publications.

The shell holds cooperative file watching while either outer Audio owner refuses
unload. Forced external reload, termination/restart and arbitrary QML writes can
still destroy state; these queries do not provide isolation against them. Native
Quickshell focus/reload timing and retained-resource costs remain unqualified.
This is not a complete plugin migration: shared discovery/manifest contracts,
existing-instance activation, multi-monitor hosting and state transfer remain
separate work.

## Localization and evidence scope

New navigation and dialog captions use ten typed translation IDs. Existing
source/context translations remain in place. The 64 pinned catalogs each contain
157 active entries: en_US/it_IT complete, 62 explicitly unfinished English fallback
catalogs. Catalog identity, source parity and duplicate-ID checks are enforced;
module tests check source-based and ID-based lookup together with the same Qt
catalog compiler flags. This is an incremental migration, not a claim that all
legacy Settings messages already use typed IDs.

Local Qt Quick tests cover opaque selection, disappearance, page/focus/scroll
round trips, popup/theme retention, actual numeric keyboard editing, repeated
Escape, explicit acceptance, hidden retained state and standalone close refusal.
Offscreen fixture images and counter assertions are not native rendering,
physical audibility, hardware setter traces or deployment acceptance.
