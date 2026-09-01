// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Synapse.Settings.Audio 1.0

TestCase {
    id: testCase
    name: "AudioQmlModule"

    QtObject {
        id: fakeBackend
        property bool audioBusy: false
        property bool audioAvailable: true
        property string audioReason: ""
        property var audioOutputs: []
        property var audioInputs: []
        property var audioStreams: []
        property var audioCards: []
        property var audioRouteRules: []
        property bool audioRouteBrokerAvailable: true
        property bool audioRouteBrokerActive: false
        property string audioRouteBrokerReason: "broker-not-running"
        property bool audioRouteEnforcementAvailable: false
        property string audioGoxlrStatus: "Ready"
        property string audioGoxlrReason: ""
        property bool audioGoxlrProviderActive: true
        property bool audioGoxlrTruncated: false
        property var audioGoxlrDevices: [{ model: "GoXLR Mini", systemOutputSupported: true }]
        property var audioProcessChoices: []
        property bool audioProcessChoiceOpen: false
        property string audioStatusId: ""
        property string audioErrorId: ""
        signal audioProcessChoiceRequested()
        function loadAudio() {}
        function setAudioVolume(target, percent) {}
        function setAudioMuted(target, muted) {}
    }

    Component {
        id: sectionComponent
        AudioSettingsSection {
            width: 900
            height: 640
            visible: false
            backend: fakeBackend
        }
    }

    AudioShellHost {
        id: shellHost
        width: 900
        height: 640
        visible: true
        active: false
    }

    function test_1_singletonContract() {
        compare(AudioBackend.audioSnapshotReady, false)
        compare(AudioBackend.audioBusy, false)
        compare(typeof AudioBackend.loadAudio, "function")
        compare(typeof AudioBackend.setAudioDefault, "function")
        compare(typeof AudioBackend.moveAudioStream, "function")
        compare(typeof AudioBackend.setAudioVolume, "function")
        compare(typeof AudioBackend.setAudioMuted, "function")
        compare(typeof AudioBackend.chooseAudioProcessRule, "function")
        compare(typeof AudioBackend.chooseAudioExecutableRule, "function")
        compare(typeof AudioBackend.chooseAudioDirectoryRule, "function")
        compare(typeof AudioBackend.audioGoxlrStatus, "string")
        compare(typeof AudioBackend.audioGoxlrDevices, "object")
        compare(shellHost.loaded, false)
        compare(shellHost.contentLoaded, false)
    }

    function test_2_moduleLocalization() {
        let section = createTemporaryObject(sectionComponent, testCase)
        verify(section)
        const italian = Qt.locale().name.substring(0, 2).toLowerCase() === "it"
        compare(section.brokerStateText(), italian ? "Inattivo" : "Inactive")
        compare(section.goxlrStateText(), italian ? "Pronto" : "Ready")
    }

    function test_3_typedReadOnlyLoad() {
        shellHost.active = true
        compare(shellHost.active, true)
        tryCompare(shellHost, "contentLoaded", true, 10000)
        tryVerify(function() {
            return shellHost.loaded || shellHost.errorId !== ""
        }, 10000, "typed Audio load did not complete")
        compare(shellHost.errorId, "")
        compare(shellHost.loaded, true)
        compare(shellHost.backendAvailable, true)
        compare(shellHost.busy, false)
        compare(shellHost.routeBrokerActive, false)
        compare(shellHost.routeEnforcementAvailable, false)
        compare(shellHost.goxlrStatus, "Ready")
        compare(shellHost.goxlrProviderActive, true)
        compare(shellHost.goxlrDeviceCount, 1)
        compare(AudioBackend.audioGoxlrDevices[0].model, "GoXLR Mini")
        compare(typeof AudioBackend.audioGoxlrDevices[0].id, "undefined")
        compare(shellHost.reasonId, "")
        compare(shellHost.errorId, "")
        compare(AudioBackend.audioOutputs.length, 1)
        compare(AudioBackend.audioInputs.length, 1)
        compare(AudioBackend.audioStreams.length, 0)
        shellHost.active = false
        tryCompare(shellHost, "contentLoaded", false, 10000)
        compare(shellHost.loaded, true)
    }

    function test_4_nativeChooserRequiresQApplication() {
        compare(AudioBackend.audioOutputs.length, 1)
        const outputId = AudioBackend.audioOutputs[0].id
        verify(!AudioBackend.chooseAudioExecutableRule("output", outputId))
        compare(AudioBackend.audioErrorId, "native-dialog-unavailable")
    }
}
