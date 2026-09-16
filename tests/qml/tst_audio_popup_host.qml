// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Synapse.Settings.Audio 1.0

TestCase {
    id: testCase
    name: "AudioPopupHost"
    property int observedDeactivations: 0
    function init() { failOnWarning(/.*/) }

    Component {
        id: backendComponent
        QtObject {
            property bool audioSnapshotReady: true
            property bool audioAvailable: true
            property bool audioBusy: false
            property bool audioProcessChoiceOpen: false
            property bool audioSelectionConfirmationOpen: false
            property var audioOutputs: [{ id: "output-1", default: true }]
            property var audioInputs: []
            property string audioGoxlrStatus: "Ready"
            property string audioGoxlrReason: ""
            property bool audioGoxlrProviderActive: true
            property bool audioGoxlrPresenceKnown: true
            property bool audioGoxlrDevicePresent: true
            property bool audioGoxlrMutationAvailable: true
            property var audioGoxlrDevices: [{
                model: "GoXLR Mini",
                profileModelReady: true,
                faders: [{ volumeAvailable: true, muteAvailable: true }],
                cough: { available: true },
                outputs: { headphonesAvailable: true, lineOutAvailable: true }
            }]
            property string audioReason: ""
            property string audioStatusId: ""
            property string audioErrorId: ""
            property bool launchResult: true
            property int loads: 0
            property int deactivations: 0
            property int mutations: 0
            property int launches: 0
            property var lastCall: []
            signal deactivateCalled()

            function loadAudio() { loads++; return true }
            function deactivateAudio() { deactivations++; deactivateCalled() }
            function setAudioVolume(targetId, value) {
                mutations++; lastCall = ["output-volume", targetId, value]; return true
            }
            function setAudioMuted(targetId, muted) {
                mutations++; lastCall = ["output-mute", targetId, muted]; return true
            }
            function setAudioGoxlrFaderVolume(index, value) {
                mutations++; lastCall = ["fader-volume", index, value]; return true
            }
            function setAudioGoxlrFaderMuted(index, muted) {
                mutations++; lastCall = ["fader-mute", index, muted]; return true
            }
            function setAudioGoxlrCoughMuted(muted) {
                mutations++; lastCall = ["cough-mute", muted]; return true
            }
            function setAudioGoxlrHeadphonesVolume(value) {
                mutations++; lastCall = ["headphones-volume", value]; return true
            }
            function setAudioGoxlrLineOutVolume(value) {
                mutations++; lastCall = ["line-out-volume", value]; return true
            }
            function openGoxlrMixer() {
                launches++
                if (!launchResult)
                    audioErrorId = "audio-goxlr-mixer-unavailable"
                return launchResult
            }
        }
    }

    Component {
        id: popupComponent
        AudioPopupHost { width: 400; height: 300 }
    }

    function makePopup(properties) {
        const popup = createTemporaryObject(popupComponent, testCase)
        verify(popup)
        const backend = backendComponent.createObject(popup)
        verify(backend)
        popup.backend = backend
        const values = properties || {}
        for (const name in values)
            popup[name] = values[name]
        return { backend: backend, popup: popup }
    }

    function test_readyProjectionAndTypedDelegation() {
        const fixture = makePopup({ active: true })
        compare(fixture.backend.loads, 0) // Reuse an already validated shared snapshot.
        compare(fixture.popup.loaded, true)
        compare(fixture.popup.goxlrReady, true)
        compare(fixture.popup.goxlrControlsReady, true)
        compare(fixture.popup.goxlrDeviceCount, 1)
        compare(fixture.popup.goxlrDevices[0].model, "GoXLR Mini")

        verify(fixture.popup.setGoxlrFaderVolume(0, 111))
        compare(fixture.backend.lastCall, ["fader-volume", 0, 111])
        verify(fixture.popup.setGoxlrFaderMuted(0, true))
        compare(fixture.backend.lastCall, ["fader-mute", 0, true])
        verify(fixture.popup.setGoxlrCoughMuted(true))
        compare(fixture.backend.lastCall, ["cough-mute", true])
        verify(fixture.popup.setGoxlrHeadphonesVolume(180))
        compare(fixture.backend.lastCall, ["headphones-volume", 180])
        verify(fixture.popup.setGoxlrLineOutVolume(200))
        compare(fixture.backend.lastCall, ["line-out-volume", 200])
        verify(fixture.popup.setOutputVolume("output-1", 60))
        compare(fixture.backend.lastCall, ["output-volume", "output-1", 60])
        verify(fixture.popup.setOutputMuted("output-1", true))
        compare(fixture.backend.lastCall, ["output-mute", "output-1", true])
        compare(fixture.backend.mutations, 7)
    }

    function test_inactiveAttachedAbsentAndPartialCapabilityStates() {
        const fixture = makePopup()
        fixture.backend.audioGoxlrStatus = "Inactive"
        fixture.backend.audioGoxlrProviderActive = false
        fixture.backend.audioGoxlrMutationAvailable = false
        fixture.backend.audioGoxlrDevices = []
        compare(fixture.popup.goxlrPresenceKnown, true)
        compare(fixture.popup.goxlrDevicePresent, true)
        compare(fixture.popup.goxlrReady, false)
        compare(fixture.popup.goxlrControlsReady, false)

        fixture.backend.audioGoxlrDevicePresent = false
        compare(fixture.popup.goxlrDevicePresent, false)
        compare(fixture.popup.goxlrDeviceCount, 0)

        fixture.backend.audioGoxlrStatus = "Ready"
        fixture.backend.audioGoxlrProviderActive = true
        fixture.backend.audioGoxlrDevicePresent = true
        fixture.backend.audioGoxlrMutationAvailable = true
        fixture.backend.audioGoxlrDevices = [{
            model: "GoXLR Mini",
            profileModelReady: true,
            faders: [{ volumeAvailable: true, muteAvailable: false }],
            cough: { available: false },
            outputs: { headphonesAvailable: true, lineOutAvailable: false }
        }]
        compare(fixture.popup.goxlrControlsReady, true)
        compare(fixture.popup.goxlrDevices[0].faders[0].muteAvailable, false)
        compare(fixture.popup.goxlrDevices[0].cough.available, false)
        compare(fixture.popup.goxlrDevices[0].outputs.lineOutAvailable, false)
    }

    function test_timeoutDriftRollbackAndLauncherFailureProjection() {
        const fixture = makePopup({ active: true })
        fixture.backend.audioGoxlrStatus = "Failed"
        fixture.backend.audioGoxlrReason = "timeout"
        fixture.backend.audioErrorId = "timeout"
        compare(fixture.popup.goxlrStatus, "Failed")
        compare(fixture.popup.goxlrReasonId, "timeout")
        compare(fixture.popup.errorId, "timeout")

        fixture.backend.audioErrorId = "audio-goxlr-control-failed"
        compare(fixture.popup.errorId, "audio-goxlr-control-failed")
        fixture.backend.audioErrorId = "audio-goxlr-control-restored"
        compare(fixture.popup.errorId, "audio-goxlr-control-restored")

        fixture.backend.launchResult = false
        verify(!fixture.popup.openCompleteMixer())
        compare(fixture.backend.launches, 1)
        compare(fixture.popup.errorId, "audio-goxlr-mixer-unavailable")
    }

    function test_visibilityAndDestructionNeverDeactivateSharedBackend() {
        observedDeactivations = 0
        const fixture = makePopup()
        fixture.backend.deactivateCalled.connect(function() {
            testCase.observedDeactivations++
        })
        fixture.backend.audioSnapshotReady = false
        compare(fixture.backend.loads, 0)
        fixture.popup.active = true
        compare(fixture.backend.loads, 1)
        fixture.backend.audioSnapshotReady = true
        verify(fixture.popup.refresh())
        compare(fixture.backend.loads, 2)
        fixture.popup.active = false
        compare(fixture.backend.deactivations, 0)
        fixture.popup.active = true
        compare(fixture.backend.loads, 2)
        fixture.popup.destroy()
        wait(0)
        compare(observedDeactivations, 0)
    }

    function test_blockedFirstActivationIsNotReplayed() {
        const fixture = makePopup()
        fixture.backend.audioSnapshotReady = false
        fixture.backend.audioBusy = true
        fixture.popup.active = true
        compare(fixture.backend.loads, 0)
        fixture.backend.audioBusy = false
        fixture.popup.active = false
        fixture.popup.active = true
        wait(0)
        compare(fixture.backend.loads, 0)
        fixture.popup.backend = null
        fixture.popup.backend = fixture.backend
        compare(fixture.backend.loads, 0)
        verify(fixture.popup.refresh())
        compare(fixture.backend.loads, 1)
        fixture.backend.audioBusyChanged.connect(() => {
            if (fixture.backend.audioBusy)
                verify(!fixture.popup.setOutputVolume("output-1", 42))
        })
        fixture.backend.audioBusy = true
        compare(fixture.backend.mutations, 0)
    }

    function test_busyAndSharedConfirmationBlockHideAndDirectCalls() {
        const fixture = makePopup({ active: true })
        compare(fixture.popup.surfaceApi, "synapse.settings.audio.popup/v1")
        for (const field of ["audioBusy", "audioProcessChoiceOpen", "audioSelectionConfirmationOpen"]) {
            fixture.backend[field] = true
            verify(!fixture.popup.requestHide())
            verify(!fixture.popup.requestUnload())
            verify(!fixture.popup.refresh())
            verify(!fixture.popup.setOutputVolume("output-1", 42))
            verify(!fixture.popup.setOutputMuted("output-1", true))
            verify(!fixture.popup.setGoxlrFaderVolume(0, 42))
            verify(!fixture.popup.setGoxlrFaderMuted(0, true))
            verify(!fixture.popup.setGoxlrCoughMuted(true))
            verify(!fixture.popup.setGoxlrHeadphonesVolume(42))
            verify(!fixture.popup.setGoxlrLineOutVolume(42))
            verify(!fixture.popup.openCompleteMixer())
            fixture.popup.active = false
            fixture.popup.active = true
            compare(fixture.backend[field], true)
            fixture.backend[field] = false
        }
        fixture.popup.active = false
        verify(fixture.popup.requestHide())
        verify(fixture.popup.requestUnload())
        verify(!fixture.popup.refresh())
        verify(!fixture.popup.setOutputVolume("output-1", 42))
        verify(!fixture.popup.openCompleteMixer())
        compare(fixture.backend.loads, 0)
        compare(fixture.backend.mutations, 0)
        compare(fixture.backend.launches, 0)
        compare(fixture.backend.deactivations, 0)
        fixture.popup.backend = null
        compare(fixture.popup.loaded, false)
        compare(fixture.popup.goxlrDeviceCount, 0)
        verify(!fixture.popup.refresh())
    }
}
