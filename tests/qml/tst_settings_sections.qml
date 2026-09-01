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
            property var audioOutputs: [{ id: "output-0123456789abcdef", label: "Output", default: true, volumePercent: 40, muted: false }]
            property var audioInputs: [{ id: "input-0123456789abcdef", label: "Input", default: true, volumePercent: 50, muted: false }]
            property var audioStreams: []
            property var audioCards: []
            property var audioRouteRules: [{
                id: "rule-0001",
                matchType: "directory",
                displayPath: "~/Games/Steam",
                direction: "output",
                deviceLabel: "Output"
            }]
            property bool audioRouteEnforcementAvailable: false
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
