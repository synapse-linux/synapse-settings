// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import "../../gui/qml" as SettingsUi

TestCase {
    name: "AudioSettingsSection"

    Component {
        id: fakeBackend
        QtObject {
            property bool audioBusy: false
            property bool audioAvailable: true
            property string audioReason: ""
            property var audioOutputs: [
                { id: "output-0123456789abcdef", label: "Output", default: true, volumePercent: 40, muted: false, levelControlAvailable: true },
                { id: "output-fedcba9876543210", label: "Headset", default: false, volumePercent: 50, muted: false, levelControlAvailable: true }
            ]
            property var audioInputs: [{ id: "input-0123456789abcdef", label: "Input", default: true, volumePercent: 50, muted: false, levelControlAvailable: true }]
            property var audioStreams: [{
                id: "playback-30",
                label: "Game",
                direction: "playback",
                target: "output-0123456789abcdef",
                volumePercent: 75,
                muted: false,
                processRuleAvailable: true,
                moveAvailable: true,
                levelControlAvailable: true
            }]
            property var audioCards: []
            property var audioRouteRules: [{
                id: "rule-0001",
                matchType: "directory",
                displayPath: "~/Games/Steam",
                direction: "output",
                deviceLabel: "Output"
            }]
            property bool audioRouteBrokerAvailable: true
            property bool audioRouteBrokerActive: false
            property string audioRouteBrokerReason: "broker-not-running"
            property bool audioRouteEnforcementAvailable: false
            property string audioGoxlrStatus: "Ready"
            property string audioGoxlrReason: ""
            property bool audioGoxlrProviderActive: true
            property bool audioGoxlrTruncated: false
            property var audioGoxlrDevices: [{ model: "GoXLR Mini", systemOutputSupported: true }]
            property var audioProcessChoices: [{
                id: "playback-30",
                label: "Game",
                direction: "playback",
                target: "output-0123456789abcdef"
            }]
            property bool audioProcessChoiceOpen: false
            property string audioStatusId: ""
            property string audioErrorId: ""
            property int audioLoads: 0
            property int audioSets: 0
            property int processRules: 0
            property int streamMoves: 0
            property int volumeSets: 0
            property int muteSets: 0
            property int executableRules: 0
            property int directoryRules: 0
            property int confirmedProcessRules: 0
            property int cancelledProcessRules: 0
            property int removedRules: 0
            property var lastCall: []
            signal audioProcessChoiceRequested()

            function loadAudio() { audioLoads++ }
            function setAudioDefault(direction, device) {
                audioSets++
                lastCall = [direction, device]
            }
            function moveAudioStream(stream, originalDevice, requestedDevice) {
                streamMoves++
                lastCall = [stream, originalDevice, requestedDevice]
            }
            function setAudioVolume(target, percent) {
                volumeSets++
                lastCall = [target, percent]
            }
            function setAudioMuted(target, muted) {
                muteSets++
                lastCall = [target, muted]
            }
            function chooseAudioProcessRule(direction, device) {
                processRules++
                audioProcessChoiceOpen = true
                lastCall = [direction, device]
                audioProcessChoiceRequested()
            }
            function confirmAudioProcessRule(streamId) {
                confirmedProcessRules++
                audioProcessChoiceOpen = false
                lastCall = [streamId]
            }
            function cancelAudioProcessRule() {
                cancelledProcessRules++
                audioProcessChoiceOpen = false
            }
            function chooseAudioExecutableRule(direction, device) {
                executableRules++
                lastCall = [direction, device]
            }
            function chooseAudioDirectoryRule(direction, device) {
                directoryRules++
                lastCall = [direction, device]
            }
            function removeAudioRouteRule(rule) {
                removedRules++
                lastCall = [rule]
            }
        }
    }

    Component {
        id: audioComponent
        SettingsUi.AudioSettingsSection { width: 960; height: 640; visible: false }
    }

    function test_lazyInventoryAndDefaultMutation() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        compare(backend.audioLoads, 0)
        section.visible = true
        section.activate()
        compare(backend.audioLoads, 1)
        section.activate()
        compare(backend.audioLoads, 1)
        section.pendingDirection = "output"
        section.pendingDevice = "output-0123456789abcdef"
        verify(section.applyPendingDefault())
        compare(backend.audioSets, 1)
        compare(backend.lastCall, ["output", "output-0123456789abcdef"])
    }

    function test_typedBrokerRuntimeState() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        compare(section.brokerStateText(), "Inactive")
        compare(section.brokerDetailText(), "Rules are stored; the new-stream Audio broker is not running.")
        compare(section.existingStreamBoundaryText(), "Automatic rules never move existing streams. A stream moves only after separate confirmation, one at a time.")
        backend.audioRouteBrokerActive = true
        backend.audioRouteBrokerReason = ""
        backend.audioRouteEnforcementAvailable = true
        compare(section.brokerStateText(), "Active")
        compare(section.brokerDetailText(), "Rules apply automatically to new streams.")
        compare(section.existingStreamBoundaryText(), "Automatic rules never move existing streams. A stream moves only after separate confirmation, one at a time.")
        backend.audioRouteBrokerAvailable = false
        backend.audioRouteBrokerActive = false
        backend.audioRouteBrokerReason = "invalid-response"
        backend.audioRouteEnforcementAvailable = false
        compare(section.brokerStateText(), "Unavailable")
        compare(section.brokerDetailText(), "Rules are stored; the Audio broker status was rejected safely.")
        compare(section.existingStreamBoundaryText(), "Automatic rules never move existing streams. A stream moves only after separate confirmation, one at a time.")
    }

    function test_typedReadOnlyGoxlrStatus() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        compare(section.goxlrStateText(), "Ready")
        compare(section.goxlrDetailText(), "Devices are projected from the provider profile model, not from hardware readback.")
        compare(section.goxlrBoundaryText(), "Status is read-only. This screen cannot start the provider, change GoXLR hardware, play audio, or claim hardware readback.")
        compare(section.goxlrCapabilityText(true), "System output capability reported")
        compare(section.goxlrCapabilityText(false), "System output capability not reported")
        backend.audioGoxlrStatus = "Inactive"
        backend.audioGoxlrReason = "provider-inactive"
        backend.audioGoxlrProviderActive = false
        backend.audioGoxlrDevices = []
        compare(section.goxlrStateText(), "Inactive")
        compare(section.goxlrDetailText(), "The GoXLR provider is not running. Audio Settings did not start it.")
        backend.audioGoxlrStatus = "Unavailable"
        backend.audioGoxlrReason = "adapter-unavailable"
        compare(section.goxlrStateText(), "Unavailable")
        compare(section.goxlrDetailText(), "The GoXLR status adapter is unavailable.")
        backend.audioGoxlrStatus = "Failed"
        backend.audioGoxlrReason = "invalid-response"
        compare(section.goxlrStateText(), "Failed")
        compare(section.goxlrDetailText(), "The GoXLR status response was rejected safely.")
    }

    function test_separatelyConfirmedSingleStreamMove() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        section.requestStreamMove("playback-30", "playback", "output-0123456789abcdef")
        compare(section.pendingMoveStream, "playback-30")
        compare(section.pendingMoveDirection, "playback")
        compare(section.pendingMoveOriginalDevice, "output-0123456789abcdef")
        section.pendingMoveRequestedDevice = "output-fedcba9876543210"
        verify(section.applyPendingStreamMove())
        compare(backend.streamMoves, 1)
        compare(backend.lastCall, ["playback-30", "output-0123456789abcdef", "output-fedcba9876543210"])
        compare(section.pendingMoveStream, "")
        compare(section.pendingMoveRequestedDevice, "")
    }

    function test_typedVolumeAndMuteControl() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        compare(section.controlBoundaryText(), "Volume and mute affect one selected item. Synapse does not play or record a test sound and does not change routing or profiles.")
        section.pendingControlTarget = "output-0123456789abcdef"
        section.pendingControl = "volume"
        section.pendingControlOriginalVolume = 40
        section.pendingControlRequestedVolume = 35
        verify(section.applyPendingControl())
        compare(backend.volumeSets, 1)
        compare(backend.lastCall, ["output-0123456789abcdef", 35])
        compare(section.pendingControlTarget, "")
        section.pendingControlTarget = "playback-30"
        section.pendingControl = "mute"
        section.pendingControlOriginalMuted = false
        section.pendingControlRequestedMuted = true
        verify(section.applyPendingControl())
        compare(backend.muteSets, 1)
        compare(backend.lastCall, ["playback-30", true])
        compare(section.pendingControlTarget, "")
    }

    function test_typedProcessExecutableAndDirectoryRules() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        section.chooseProcessRule("output", "output-0123456789abcdef")
        compare(backend.processRules, 1)
        backend.confirmAudioProcessRule("playback-30")
        compare(backend.confirmedProcessRules, 1)
        compare(backend.lastCall, ["playback-30"])
        section.chooseExecutableRule("input", "input-0123456789abcdef")
        compare(backend.executableRules, 1)
        section.chooseDirectoryRule("output", "output-0123456789abcdef")
        compare(backend.directoryRules, 1)
        section.removeRouteRule("rule-0001")
        compare(backend.removedRules, 1)
        compare(backend.lastCall, ["rule-0001"])
    }
}
