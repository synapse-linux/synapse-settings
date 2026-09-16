// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Synapse.Settings.Audio 1.0

TestCase {
    id: testCase
    name: "InstalledAudioModule"
    width: 960
    height: 640
    visible: true
    when: windowShown

    // Explicit presentation-only projection: never instantiate/use AudioBackend.
    QtObject {
        id: projection
        property int requests: 0
        property bool audioBusy: false
        property bool audioSnapshotReady: false
        property bool audioAvailable: false
        property string audioReason: ""
        property var audioOutputs: []
        property var audioInputs: []
        property var audioStreams: []
        property var audioCards: []
        property bool audioProfilePortAvailable: false
        property bool audioProfilePortMutationAvailable: false
        property string audioProfilePortReason: ""
        property var audioProfileCards: []
        property var audioPortEndpoints: []
        property var audioRouteRules: []
        property bool audioRouteBrokerAvailable: false
        property bool audioRouteBrokerActive: false
        property string audioRouteBrokerReason: ""
        property bool audioRouteEnforcementAvailable: false
        property string audioGoxlrStatus: "Unavailable"
        property string audioGoxlrReason: ""
        property bool audioGoxlrProviderActive: false
        property bool audioGoxlrPresenceKnown: false
        property bool audioGoxlrDevicePresent: false
        property bool audioGoxlrMutationAvailable: false
        property bool audioGoxlrTruncated: false
        property var audioGoxlrDevices: []
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
        function loadAudio() { requests++ }
        function deactivateAudio() { requests++ }
    }

    AudioSettingsSection {
        id: section
        width: 900
        height: 600
        visible: true
        active: false
        backend: projection
    }
    AudioSettings { id: settings; active: false; backend: projection }
    AudioShellHost { id: host; active: false; backend: projection }
    AudioPopupHost { id: popup; active: false; backend: projection }

    function init() { failOnWarning(/.*/) }

    function test_installedPresentationClosure() {
        // All extracted views are eager children of the inactive section.
        for (const name of ["audioDeviceDetails", "audioHeadingOutputs",
                            "audioHeadingInputs", "audioHeadingStreams",
                            "audioHeadingGoxlr", "audioHeadingRouting",
                            "audioHeadingProfilesPorts", "audioVolumeDialog",
                            "audioVolumeInput"]) {
            verify(findChild(section, name) !== null, name)
        }
        compare(settings.backend, projection)
        compare(host.backend, projection)
        compare(popup.backend, projection)
        compare(host.contentLoaded, false)
        compare(popup.loaded, false)
    }

    function test_catalogAndInactivity() {
        const italian = Qt.locale().name.substring(0, 2).toLowerCase() === "it"
        compare(qsTranslate("AudioSettingsSection", "Output"), italian ? "Uscita" : "Output")
        compare(qsTrId("settings.audio.navigation.mixer"), italian ? "Mixer applicazioni" : "Application mixer")
        wait(50)
        compare(projection.requests, 0)
        compare(projection.audioSnapshotReady, false)
        compare(projection.audioOutputs.length, 0)
        compare(projection.audioGoxlrDevices.length, 0)
    }
}
