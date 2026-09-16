// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import Synapse.Settings.Audio 1.0 as SynapseAudio

Item {
    id: root

    property bool active: false
    property var backend: SynapseAudio.AudioBackend
    property color backgroundColor: "#10151f"
    property color surfaceColor: "#171f2c"
    property color surfaceHoverColor: "#27364a"
    property color borderColor: "#354760"
    property color accentColor: "#7aa2f7"
    property color textColor: "#e7eefb"
    property color mutedColor: "#93a6c0"
    property color urgentColor: "#f7768e"
    property string fontFamily: "monospace"
    property int radius: 8
    property int presentationSpacing: 8
    readonly property bool contentLoaded: contentLoader.item !== null
    readonly property bool loaded: backend.audioSnapshotReady
    readonly property string surfaceApi: "synapse.settings.audio.surface/v1"
    readonly property bool contentReady: contentLoader.presentation !== null && contentLoader.presentation.contentReady
    readonly property bool canUnload: !contentLoader.retained && !contentLoader.active
    function requestUnload() { return canUnload }
    function requestHide() {
        return contentLoader.presentation ? contentLoader.presentation.requestHide() : !busy
    }
    readonly property bool backendAvailable: backend.audioAvailable
    readonly property bool busy: backend.audioBusy
    readonly property bool routeBrokerActive: backend.audioRouteBrokerActive
    readonly property bool routeEnforcementAvailable: backend.audioRouteEnforcementAvailable
    readonly property var outputs: backend.audioOutputs
    readonly property var inputs: backend.audioInputs
    readonly property string goxlrStatus: backend.audioGoxlrStatus || "Unavailable"
    readonly property bool goxlrProviderActive: backend.audioGoxlrProviderActive
    readonly property bool goxlrPresenceKnown: backend.audioGoxlrPresenceKnown
    readonly property bool goxlrDevicePresent: backend.audioGoxlrDevicePresent
    readonly property bool goxlrMutationAvailable: backend.audioGoxlrMutationAvailable
    readonly property var goxlrDevices: backend.audioGoxlrDevices
    readonly property int goxlrDeviceCount: goxlrDevices.length
    readonly property bool goxlrTruncated: backend.audioGoxlrTruncated
    readonly property string reasonId: backend.audioReason || ""
    readonly property string statusId: backend.audioStatusId || ""
    readonly property string errorId: backend.audioErrorId || ""

    function refresh() {
        return backend.loadAudio()
    }

    function setOutputVolume(targetId, value) {
        return backend.setAudioVolume(targetId, value)
    }

    function setOutputMuted(targetId, muted) {
        return backend.setAudioMuted(targetId, muted)
    }

    function setGoxlrFaderVolume(faderIndex, value) {
        return backend.setAudioGoxlrFaderVolume(faderIndex, value)
    }

    function setGoxlrFaderMuted(faderIndex, muted) {
        return backend.setAudioGoxlrFaderMuted(faderIndex, muted)
    }

    function setGoxlrCoughMuted(muted) {
        return backend.setAudioGoxlrCoughMuted(muted)
    }

    function setGoxlrHeadphonesVolume(value) {
        return backend.setAudioGoxlrHeadphonesVolume(value)
    }

    function setGoxlrLineOutVolume(value) {
        return backend.setAudioGoxlrLineOutVolume(value)
    }

    // Backend operations and their confirmations outlive visibility changes.
    // The enclosing host must consult requestHide/requestUnload before closing
    // or reloading this surface; active=false alone is not cancellation.
    Loader {
        id: contentLoader
        readonly property SynapseAudio.AudioSettings presentation: item as SynapseAudio.AudioSettings
        property bool retained: false
        anchors.fill: parent
        active: root.active || retained
        asynchronous: true
        onLoaded: retained = true
        sourceComponent: SynapseAudio.AudioSettings {
            active: root.active
            backend: root.backend
            backgroundColor: root.backgroundColor
            surfaceColor: root.surfaceColor
            surfaceHoverColor: root.surfaceHoverColor
            borderColor: root.borderColor
            accentColor: root.accentColor
            textColor: root.textColor
            mutedColor: root.mutedColor
            urgentColor: root.urgentColor
            fontFamily: root.fontFamily
            radius: root.radius
            presentationSpacing: root.presentationSpacing
        }
    }
}
