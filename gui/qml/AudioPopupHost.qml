// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import Synapse.Settings.Audio 1.0 as SynapseAudio

Item {
    id: root

    property bool active: false
    property var backend: SynapseAudio.AudioBackend
    property bool _componentReady: false
    property bool _activationRequested: false
    readonly property string surfaceApi: "synapse.settings.audio.popup/v1"
    readonly property bool loaded: backend !== null && backend.audioSnapshotReady
    readonly property bool backendAvailable: backend !== null && backend.audioAvailable
    readonly property bool busy: backend === null || backend.audioBusy
    readonly property bool interactionPending: busy || (backend !== null
        && (backend.audioProcessChoiceOpen || backend.audioSelectionConfirmationOpen))
    readonly property bool canInteract: active && !interactionPending
    readonly property bool canUnload: !active && !interactionPending
    readonly property var outputs: backend ? backend.audioOutputs : []
    readonly property var inputs: backend ? backend.audioInputs : []
    readonly property string goxlrStatus: backend ? backend.audioGoxlrStatus || "Unavailable" : "Unavailable"
    readonly property string goxlrReasonId: backend ? backend.audioGoxlrReason || "" : ""
    readonly property bool goxlrProviderActive: backend !== null && backend.audioGoxlrProviderActive
    readonly property bool goxlrPresenceKnown: backend !== null && backend.audioGoxlrPresenceKnown
    readonly property bool goxlrDevicePresent: backend !== null && backend.audioGoxlrDevicePresent
    readonly property bool goxlrMutationAvailable: backend !== null && backend.audioGoxlrMutationAvailable
    readonly property var goxlrDevices: backend ? backend.audioGoxlrDevices : []
    readonly property int goxlrDeviceCount: goxlrDevices.length
    readonly property bool goxlrReady: goxlrStatus === "Ready"
        && goxlrProviderActive && goxlrDeviceCount === 1
    readonly property bool goxlrControlsReady: goxlrReady && goxlrMutationAvailable
    readonly property string reasonId: backend ? backend.audioReason || "" : ""
    readonly property string statusId: backend ? backend.audioStatusId || "" : ""
    readonly property string errorId: backend ? backend.audioErrorId || "" : ""

    function requestHide() {
        return backend !== null && !backend.audioBusy
            && !backend.audioProcessChoiceOpen && !backend.audioSelectionConfirmationOpen
    }
    function requestUnload() { return !active && requestHide() }
    function canRequest() { return active && requestHide() }

    function refresh() {
        return canRequest() && backend.loadAudio()
    }

    function setOutputVolume(targetId, value) {
        return canRequest() && backend.setAudioVolume(targetId, value)
    }

    function setOutputMuted(targetId, muted) {
        return canRequest() && backend.setAudioMuted(targetId, muted)
    }

    function setGoxlrFaderVolume(faderIndex, value) {
        return canRequest() && backend.setAudioGoxlrFaderVolume(faderIndex, value)
    }

    function setGoxlrFaderMuted(faderIndex, muted) {
        return canRequest() && backend.setAudioGoxlrFaderMuted(faderIndex, muted)
    }

    function setGoxlrCoughMuted(muted) {
        return canRequest() && backend.setAudioGoxlrCoughMuted(muted)
    }

    function setGoxlrHeadphonesVolume(value) {
        return canRequest() && backend.setAudioGoxlrHeadphonesVolume(value)
    }

    function setGoxlrLineOutVolume(value) {
        return canRequest() && backend.setAudioGoxlrLineOutVolume(value)
    }

    function openCompleteMixer() {
        return canRequest() && backend.openGoxlrMixer()
    }

    function activateOnce() {
        if (!_componentReady || !active || _activationRequested)
            return
        _activationRequested = true
        if (backend !== null && !backend.audioSnapshotReady)
            refresh()
    }

    onActiveChanged: activateOnce()
    Component.onCompleted: {
        _componentReady = true
        activateOnce()
    }
    // The engine-owned adapter is shared with Settings. Neither hiding nor
    // destroying this projection owns cancellation or clearing that adapter.
}
