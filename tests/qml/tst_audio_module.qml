// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Synapse.Settings.Audio 1.0

TestCase {
    id: testCase
    name: "AudioQmlModule"
    width: 960
    height: 640
    visible: true
    when: windowShown
    function init() { failOnWarning(/.*/) }

    QtObject {
        id: fakeBackend
        property bool audioBusy: false
        property bool audioSnapshotReady: true
        property bool audioAvailable: true
        property string audioReason: ""
        property var audioOutputs: []
        property var audioInputs: []
        property var audioStreams: []
        property var audioCards: []
        property bool audioProfilePortAvailable: true
        property bool audioProfilePortMutationAvailable: true
        property string audioProfilePortReason: ""
        property var audioProfileCards: []
        property var audioPortEndpoints: []
        property var audioRouteRules: []
        property bool audioRouteBrokerAvailable: true
        property bool audioRouteBrokerActive: false
        property string audioRouteBrokerReason: "broker-not-running"
        property bool audioRouteEnforcementAvailable: false
        property string audioGoxlrStatus: "Ready"
        property string audioGoxlrReason: ""
        property bool audioGoxlrProviderActive: true
        property bool audioGoxlrPresenceKnown: true
        property bool audioGoxlrDevicePresent: true
        property bool audioGoxlrMutationAvailable: true
        property bool audioGoxlrTruncated: false
        property var audioGoxlrDevices: [{
            model: "GoXLR Mini", profileModelReady: true,
            systemOutputSupported: true, controlAvailable: true,
            faders: [
                { fader: "A", channel: "Mic", volume: 110, muted: false, volumeAvailable: true, muteAvailable: true },
                { fader: "B", channel: "Chat", volume: 120, muted: false, volumeAvailable: true, muteAvailable: true },
                { fader: "C", channel: "Music", volume: 130, muted: false, volumeAvailable: true, muteAvailable: true },
                { fader: "D", channel: "System", volume: 127, muted: false, volumeAvailable: true, muteAvailable: true }
            ],
            cough: { mode: "Toggle", muted: false, available: true },
            outputs: { headphonesVolume: 180, lineOutVolume: 200, monitoredOutput: "Headphones", headphonesAvailable: true, lineOutAvailable: true }
        }]
        property var audioProcessChoices: []
        property bool audioProcessChoiceOpen: false
        property bool audioSelectionConfirmationOpen: false
        property string audioSelectionKind: ""
        property string audioSelectionTargetLabel: ""
        property string audioSelectionOriginalLabel: ""
        property string audioSelectionRequestedLabel: ""
        property string audioStatusId: ""
        property string audioErrorId: ""
        signal audioProcessChoiceRequested()
        signal audioSelectionConfirmationRequested()
        signal audioSelectionChanged()
        function loadAudio() {}
        function setAudioVolume(target, percent) {}
        function setAudioMuted(target, muted) {}
        function setAudioGoxlrFaderVolume(index, value) {}
        function setAudioGoxlrFaderMuted(index, muted) {}
        function setAudioGoxlrCoughMuted(muted) {}
        function setAudioGoxlrHeadphonesVolume(value) {}
        function setAudioGoxlrLineOutVolume(value) {}
        function openGoxlrMixer() { return true }
        function planAudioProfile(card, profile) {}
        function planAudioPort(direction, device, port) {}
        function confirmAudioSelection() {}
        function cancelAudioSelection() {}
        function deactivateAudio() {}
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

    Component {
        id: themedHostComponent
        AudioShellHost {
            width: 900
            height: 640
            visible: true
            active: true
            backend: fakeBackend
            backgroundColor: "#121212"
            surfaceColor: "#0d0d0d"
            surfaceHoverColor: "#1e1e1e"
            borderColor: "#333333"
            accentColor: "#e68e0d"
            textColor: "#bebebe"
            mutedColor: "#555555"
            urgentColor: "#d35f5f"
            fontFamily: "Unit Test Mono"
            radius: 11
            presentationSpacing: 9
        }
    }

    function test_1_singletonContract() {
        compare(AudioBackend.audioSnapshotReady, false)
        compare(AudioBackend.audioBusy, false)
        compare(typeof AudioBackend.loadAudio, "function")
        compare(typeof AudioBackend.setAudioDefault, "function")
        compare(typeof AudioBackend.moveAudioStream, "function")
        compare(typeof AudioBackend.setAudioVolume, "function")
        compare(typeof AudioBackend.setAudioMuted, "function")
        compare(typeof AudioBackend.setAudioGoxlrFaderVolume, "function")
        compare(typeof AudioBackend.setAudioGoxlrFaderMuted, "function")
        compare(typeof AudioBackend.setAudioGoxlrCoughMuted, "function")
        compare(typeof AudioBackend.setAudioGoxlrHeadphonesVolume, "function")
        compare(typeof AudioBackend.setAudioGoxlrLineOutVolume, "function")
        compare(typeof AudioBackend.openGoxlrMixer, "function")
        compare(typeof AudioBackend.deactivateAudio, "function")
        compare(typeof AudioBackend.planAudioProfile, "function")
        compare(typeof AudioBackend.planAudioPort, "function")
        compare(typeof AudioBackend.confirmAudioSelection, "function")
        compare(typeof AudioBackend.cancelAudioSelection, "function")
        compare(typeof AudioBackend.chooseAudioProcessRule, "function")
        compare(typeof AudioBackend.chooseAudioExecutableRule, "function")
        compare(typeof AudioBackend.chooseAudioDirectoryRule, "function")
        compare(typeof AudioBackend.audioGoxlrStatus, "string")
        compare(typeof AudioBackend.audioGoxlrDevices, "object")
        compare(typeof AudioBackend.audioProfileCards, "object")
        compare(typeof AudioBackend.audioPortEndpoints, "object")
        compare(typeof AudioBackend.audioProfilePortAvailable, "boolean")
        compare(shellHost.loaded, false)
        compare(shellHost.contentLoaded, false)
    }

    function test_2_hostPropagatesSynapsePresentationTokens() {
        const host = createTemporaryObject(themedHostComponent, testCase)
        verify(host)
        tryCompare(host, "contentLoaded", true, 10000)
        const hostLoader = host.children[0]
        verify(hostLoader)
        tryVerify(function() {
            return hostLoader.item && hostLoader.item.contentLoaded
        }, 10000)
        const settings = hostLoader.item
        const sectionLoader = settings.children[0]
        verify(sectionLoader && sectionLoader.item)
        const presentation = sectionLoader.item
        compare(presentation.objectName, "audioSettingsPresentation")
        const title = findChild(presentation, "audioPresentationTitle")
        const refresh = findChild(presentation, "audioRefreshButton")
        const summary = findChild(presentation, "audioGoxlrSummaryCard")
        verify(title)
        verify(refresh)
        verify(summary)
        verify(Qt.colorEqual(presentation.backgroundColor, host.backgroundColor))
        verify(Qt.colorEqual(presentation.surfaceColor, host.surfaceColor))
        verify(Qt.colorEqual(presentation.accentColor, host.accentColor))
        verify(Qt.colorEqual(presentation.textColor, host.textColor))
        compare(presentation.fontFamily, "Unit Test Mono")
        compare(presentation.radius, 11)
        compare(presentation.presentationSpacing, 9)
        verify(Qt.colorEqual(title.color, host.textColor))
        verify(Qt.colorEqual(refresh.background.color, host.accentColor))
        verify(Qt.colorEqual(summary.background.color, host.surfaceColor))
    }

    function test_2_moduleLocalization() {
        let section = createTemporaryObject(sectionComponent, testCase)
        verify(section)
        const italian = Qt.locale().name.substring(0, 2).toLowerCase() === "it"
        compare(section.brokerStateText(), italian ? "Inattivo" : "Inactive")
        compare(section.goxlrStateText(), italian ? "Pronto" : "Ready")
    }

    function test_3_typedLoadAndNavigationRetention() {
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
        compare(shellHost.goxlrPresenceKnown, true)
        compare(shellHost.goxlrDevicePresent, true)
        compare(shellHost.goxlrMutationAvailable, true)
        compare(shellHost.goxlrDeviceCount, 1)
        compare(AudioBackend.audioGoxlrDevices[0].model, "GoXLR Mini")
        compare(typeof AudioBackend.audioGoxlrDevices[0].id, "undefined")
        compare(shellHost.reasonId, "")
        compare(shellHost.errorId, "")
        compare(AudioBackend.audioOutputs.length, 1)
        compare(AudioBackend.audioInputs.length, 1)
        compare(AudioBackend.audioStreams.length, 0)
        compare(AudioBackend.audioProfilePortAvailable, true)
        compare(AudioBackend.audioProfileCards.length, 1)
        compare(AudioBackend.audioProfileCards[0].activeProfileLabel, "High Fidelity")
        compare(AudioBackend.audioPortEndpoints.length, 2)
        compare(typeof AudioBackend.audioProfileCards[0].rawName, "undefined")
        compare(typeof AudioBackend.audioProfileCards[0].profiles[0].rawName, "undefined")
        compare(shellHost.surfaceApi, "synapse.settings.audio.surface/v1")
        verify(shellHost.requestHide())
        verify(!shellHost.requestUnload())
        shellHost.active = false
        compare(shellHost.contentLoaded, true)
        compare(shellHost.loaded, true)
        compare(shellHost.goxlrDeviceCount, 1)
        compare(shellHost.goxlrMutationAvailable, true)
        verify(!shellHost.requestUnload())
    }

    function test_5_sourceAndIdTranslationsShareOneCatalog() {
        const italian = Qt.locale().name.substring(0, 2).toLowerCase() === "it"
        compare(qsTranslate("AudioSettingsSection", "Output"), italian ? "Uscita" : "Output")
        compare(qsTrId("settings.audio.navigation.mixer"), italian ? "Mixer applicazioni" : "Application mixer")
        compare(qsTrId("settings.audio.navigation.devices"), italian ? "Dispositivi" : "Devices")
    }

    Component { id: popupComponent; AudioPopupHost { active: false } }
    SignalSpy { id: sharedState; target: AudioBackend; signalName: "audioStateChanged" }
    SignalSpy { id: sharedModels; target: AudioBackend; signalName: "audioModelsChanged" }

    function test_6_popupDestructionPreservesSharedSnapshotAndSettingsDraft() {
        shellHost.active = true
        tryCompare(shellHost, "contentReady", true, 10000)
        const section = findChild(shellHost, "audioSettingsPresentation")
        verify(section)
        const output = AudioBackend.audioOutputs[0]
        verify(section.openDevice("output", output.id))
        section.requestVolume(output.id, output.label, output.volumePercent)
        const dialog = findChild(section, "audioVolumeDialog")
        const spin = findChild(section, "audioVolumeInput")
        tryCompare(dialog, "opened", true)
        spin.value = 42
        spin.valueModified()
        sharedState.clear()
        sharedModels.clear()
        const priorError = AudioBackend.audioErrorId
        const popup = createTemporaryObject(popupComponent, testCase)
        verify(popup)
        compare(popup.backend, shellHost.backend)
        popup.active = true
        popup.active = false
        popup.destroy()
        wait(0)
        compare(sharedState.count, 0)
        compare(sharedModels.count, 0)
        compare(AudioBackend.audioSnapshotReady, true)
        compare(AudioBackend.audioErrorId, priorError)
        compare(section.deviceId, output.id)
        compare(section.pendingControlRequestedVolume, 42)
        compare(spin.value, 42)
        verify(!shellHost.requestHide())
        verify(!shellHost.requestUnload())
        dialog.reject()
        verify(shellHost.requestHide())
        shellHost.active = false
    }

    function test_4_nativeChooserRequiresQApplication() {
        shellHost.active = true
        tryCompare(shellHost, "loaded", true, 10000)
        compare(AudioBackend.audioOutputs.length, 1)
        const outputId = AudioBackend.audioOutputs[0].id
        verify(!AudioBackend.chooseAudioExecutableRule("output", outputId))
        compare(AudioBackend.audioErrorId, "native-dialog-unavailable")
    }
}
