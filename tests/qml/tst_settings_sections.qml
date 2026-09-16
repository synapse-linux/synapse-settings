// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtTest
import "../../gui/qml" as SettingsUi

TestCase {
    id: testCase
    name: "AudioSettingsSection"
    width: 960
    height: 640
    visible: true
    when: windowShown
    function init() { failOnWarning(/.*/) }

    Component {
        id: fakeBackend
        QtObject {
            property bool audioBusy: false
            property bool audioAvailable: true
            property bool audioSnapshotReady: true
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
            property bool audioProfilePortAvailable: true
            property bool audioProfilePortMutationAvailable: true
            property string audioProfilePortReason: ""
            property var audioProfileCards: [{
                id: "card-0123456789abcdef",
                label: "Primary audio card",
                activeProfile: "profile-0123456789abcdef",
                activeProfileLabel: "High Fidelity",
                mutationAvailable: true,
                profiles: [
                    { id: "profile-0123456789abcdef", label: "High Fidelity", availability: "available" },
                    { id: "profile-fedcba9876543210", label: "Pro Audio", availability: "unknown" },
                    { id: "profile-1111111111111111", label: "Unavailable profile", availability: "unavailable" }
                ]
            }]
            property var audioPortEndpoints: [{
                id: "output-0123456789abcdef",
                direction: "output",
                label: "Output",
                activePort: "port-0123456789abcdef",
                activePortLabel: "Speakers",
                mutationAvailable: true,
                ports: [
                    { id: "port-0123456789abcdef", label: "Speakers", availability: "available" },
                    { id: "port-fedcba9876543210", label: "Headphones", availability: "unknown" }
                ]
            }]
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
            property bool audioGoxlrPresenceKnown: true
            property bool audioGoxlrDevicePresent: true
            property bool audioGoxlrMutationAvailable: true
            property bool audioGoxlrTruncated: false
            property var audioGoxlrDevices: [{
                model: "GoXLR Mini",
                profileModelReady: true,
                systemOutputSupported: true,
                controlAvailable: true,
                faders: [
                    { fader: "A", channel: "Mic", volume: 110, muted: false, volumeAvailable: true, muteAvailable: true },
                    { fader: "B", channel: "Chat", volume: 120, muted: false, volumeAvailable: true, muteAvailable: true },
                    { fader: "C", channel: "Music", volume: 130, muted: false, volumeAvailable: true, muteAvailable: true },
                    { fader: "D", channel: "System", volume: 127, muted: false, volumeAvailable: true, muteAvailable: true }
                ],
                cough: { mode: "Toggle", muted: false, available: true },
                outputs: { headphonesVolume: 180, lineOutVolume: 200, monitoredOutput: "Headphones", headphonesAvailable: true, lineOutAvailable: true }
            }]
            property var audioProcessChoices: [{
                id: "playback-30",
                label: "Game",
                direction: "playback",
                target: "output-0123456789abcdef"
            }]
            property bool audioProcessChoiceOpen: false
            property bool audioSelectionConfirmationOpen: false
            property string audioSelectionKind: ""
            property string audioSelectionTargetLabel: ""
            property string audioSelectionOriginalLabel: ""
            property string audioSelectionRequestedLabel: ""
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
            property int profilePlans: 0
            property int portPlans: 0
            property int confirmedSelections: 0
            property int cancelledSelections: 0
            property int goxlrSets: 0
            property int goxlrMixerOpens: 0
            property var lastCall: []
            signal audioProcessChoiceRequested()
            signal audioSelectionConfirmationRequested()
            signal audioSelectionChanged()

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
            function setAudioGoxlrFaderVolume(index, value) {
                goxlrSets++
                lastCall = ["fader-volume", index, value]
            }
            function setAudioGoxlrFaderMuted(index, muted) {
                goxlrSets++
                lastCall = ["fader-mute", index, muted]
            }
            function setAudioGoxlrCoughMuted(muted) {
                goxlrSets++
                lastCall = ["cough-mute", muted]
            }
            function setAudioGoxlrHeadphonesVolume(value) {
                goxlrSets++
                lastCall = ["headphones-volume", value]
            }
            function setAudioGoxlrLineOutVolume(value) {
                goxlrSets++
                lastCall = ["line-out-volume", value]
            }
            function openGoxlrMixer() { goxlrMixerOpens++; return true }
            function planAudioProfile(card, profile) {
                profilePlans++
                lastCall = [card, profile]
                audioSelectionKind = "profile"
                audioSelectionTargetLabel = "Primary audio card"
                audioSelectionOriginalLabel = "High Fidelity"
                audioSelectionRequestedLabel = "Pro Audio"
                audioSelectionConfirmationOpen = true
                audioSelectionChanged()
                audioSelectionConfirmationRequested()
            }
            function planAudioPort(direction, device, port) {
                portPlans++
                lastCall = [direction, device, port]
                audioSelectionKind = "port"
                audioSelectionTargetLabel = "Output"
                audioSelectionOriginalLabel = "Speakers"
                audioSelectionRequestedLabel = "Headphones"
                audioSelectionConfirmationOpen = true
                audioSelectionChanged()
                audioSelectionConfirmationRequested()
            }
            function confirmAudioSelection() {
                confirmedSelections++
                audioSelectionConfirmationOpen = false
                audioSelectionChanged()
            }
            function cancelAudioSelection() {
                cancelledSelections++
                audioSelectionConfirmationOpen = false
                audioSelectionChanged()
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

    function verifyNoWrites(backend) {
        compare(backend.audioSets + backend.streamMoves + backend.volumeSets
                + backend.muteSets + backend.goxlrSets + backend.processRules
                + backend.executableRules + backend.directoryRules
                + backend.confirmedSelections + backend.removedRules, 0)
        compare(backend.goxlrMixerOpens, 0)
    }

    function test_retainedComposition_data() {
        return [{ tag: "compact", width: 640, height: 480 },
                { tag: "wide", width: 960, height: 640 }]
    }

    function test_retainedComposition(data) {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, {
            backend: backend, visible: true, width: data.width, height: data.height
        })
        const names = ["audioDeviceDetails", "audioDeviceVolume", "audioHeadingOutputs",
                       "audioHeadingInputs", "audioHeadingStreams", "audioGoxlrSummaryCard",
                       "audioHeadingRouting", "audioHeadingProfilesPorts",
                       "audioProfileChooser-card-0123456789abcdef",
                       "audioPortChooser-output-0123456789abcdef", "audioVolumeDialog",
                       "audioVolumeInput", "audioSelectionDialog"]
        const owners = names.map(name => findChild(section, name))
        verify(owners.every(owner => owner !== null))
        verify(section.openDevice("output", "output-0123456789abcdef"))
        for (const page of [0, 1, 2, 3, 1, 0]) {
            verify(section.showPage(page))
            wait(50) // settle layout and the existing callLater scroll restoration
            const geometry = []
            for (let index = 0; index < names.length; ++index) {
                const item = findChild(section, names[index])
                compare(item, owners[index])
                if (index < 10 && item.visible) {
                    const point = item.mapToItem(section, 0, 0)
                    geometry.push([names[index], point.x, point.y, item.width, item.height]
                        .map(value => typeof value === "number" ? Math.round(value * 1000) / 1000 : value))
                }
            }
            console.info("page-composition " + JSON.stringify({ size: data.tag, page: page, geometry: geometry }))
        }
        section.active = false
        section.surfaceColor = "#ffffff"
        section.textColor = "#161616"
        section.active = true
        for (let index = 0; index < names.length; ++index)
            compare(findChild(section, names[index]), owners[index])
        compare(backend.audioLoads, 1)
        compare(backend.profilePlans + backend.portPlans + backend.cancelledSelections
                + backend.cancelledProcessRules, 0)
        verifyNoWrites(backend)
    }

    function test_homeStartsWithEndpointsNotAdvancedControls() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        compare(section.currentPage, 0)
        verify(findChild(section, "audioHeadingOutputs").visible)
        verify(findChild(section, "audioHeadingInputs").visible)
        verify(!findChild(section, "audioGoxlrSummaryCard").visible)
        verify(!findChild(section, "audioHeadingStreams").visible)
        verify(!findChild(section, "audioHeadingRouting").visible)
        verify(!findChild(section, "audioHeadingProfilesPorts").visible)
        verifyNoWrites(backend)
    }

    function test_deviceIdentitySurvivesReorderAndDisappearance() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        verify(section.openDevice("output", "output-fedcba9876543210"))
        compare(section.currentPage, 1)
        compare(section.deviceEndpoint.label, "Headset")
        backend.audioOutputs = [backend.audioOutputs[1], backend.audioOutputs[0]]
        compare(section.deviceEndpoint.label, "Headset")
        backend.audioOutputs = [backend.audioOutputs[1]]
        compare(section.deviceAvailable, false)
        compare(section.deviceId, "output-fedcba9876543210")
        verify(!findChild(section, "audioDeviceVolume").enabled)
        verify(!section.openDevice("input", "output-0123456789abcdef"))
        compare(section.deviceId, "output-fedcba9876543210")
        verify(section.showPage(0))
        verify(section.openDevice("input", "input-0123456789abcdef"))
        compare(section.deviceDirection, "input")
        verifyNoWrites(backend)
    }

    function test_navigationRoundTripAndSyntheticGuards() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        for (const page of [2, 3, 0, 3, 2, 0]) {
            verify(section.showPage(page))
            compare(section.currentPage, page)
        }
        verify(!section.showPage(17))
        backend.audioBusy = true
        findChild(section, "audioNavigateAdvanced").clicked()
        compare(section.currentPage, 0)
        backend.audioBusy = false
        section.requestVolume("output-0123456789abcdef", "Output", 40)
        const dialog = findChild(section, "audioVolumeDialog")
        tryCompare(dialog, "opened", true)
        const spin = findChild(section, "audioVolumeInput")
        spin.value = 42
        spin.valueModified()
        verify(!section.showPage(3))
        verify(!section.requestUnload())
        section.surfaceColor = "#ffffff"
        section.textColor = "#161616"
        section.accentColor = "#12549a"
        compare(section.pendingControlRequestedVolume, 42)
        compare(spin.value, 42)
        compare(section.currentPage, 0)
        compare(dialog.opened, true)
        keyClick(Qt.Key_Escape)
        tryCompare(dialog, "opened", false)
        compare(section.pendingControlTarget, "")
        verify(section.showPage(3))
        verifyNoWrites(backend)
    }

    function test_detailActionsKeepOriginalOpaqueTarget() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        verify(section.openDevice("output", "output-fedcba9876543210"))
        const volume = findChild(section, "audioDeviceVolume")
        volume.clicked()
        compare(section.pendingControlTarget, "output-fedcba9876543210")
        const dialog = findChild(section, "audioVolumeDialog")
        tryCompare(dialog, "opened", true)
        backend.audioOutputs = [backend.audioOutputs[0]]
        compare(section.deviceAvailable, false)
        compare(section.pendingControlTarget, "output-fedcba9876543210")
        dialog.reject()
        tryCompare(dialog, "opened", false)
        volume.clicked()
        compare(section.pendingControlTarget, "")
        verifyNoWrites(backend)
    }

    Component {
        id: retainedComponent
        SettingsUi.AudioSettings { width: 640; height: 480; active: false }
    }

    function test_hiddenSurfaceRetainsNavigationAndDraftWithoutReload() {
        const backend = createTemporaryObject(fakeBackend, this)
        const surface = createTemporaryObject(retainedComponent, this, { backend: backend })
        compare(surface.contentLoaded, false)
        verify(surface.requestUnload())
        surface.active = true
        tryCompare(surface, "contentLoaded", true)
        const section = findChild(surface, "audioSettingsPresentation")
        verify(section.openDevice("output", "output-fedcba9876543210"))
        section.requestVolume(section.deviceId, "Headset", 50)
        const dialog = findChild(section, "audioVolumeDialog")
        tryCompare(dialog, "opened", true)
        const spin = findChild(section, "audioVolumeInput")
        spin.value = 42
        spin.valueModified()
        verify(!surface.requestHide())
        surface.active = false
        wait(0)
        compare(surface.contentLoaded, true)
        compare(section.pendingControlRequestedVolume, 42)
        verify(!surface.requestUnload())
        surface.backgroundColor = "#f4f1ea"
        surface.active = true
        compare(findChild(surface, "audioSettingsPresentation"), section)
        compare(section.deviceId, "output-fedcba9876543210")
        compare(section.pendingControlRequestedVolume, 42)
        compare(spin.value, 42)
        compare(backend.audioLoads, 1)
        dialog.reject()
        tryCompare(dialog, "opened", false)
        verify(surface.requestHide())
        surface.active = false
        compare(surface.contentLoaded, true)
        verifyNoWrites(backend)
    }

    Component { id: windowComponent; SettingsUi.Main {} }
    readonly property var windowTheme: ({
        background: "#121212", surface: "#0d0d0d", surfaceHover: "#1e1e1e",
        border: "#333333", accent: "#e68e0d", onAccent: "#000000",
        text: "#bebebe", muted: "#555555", urgent: "#d35f5f"
    })

    function test_standaloneCloseDefersToConfirmationOwner() {
        const backend = createTemporaryObject(fakeBackend, this)
        const window = createTemporaryObject(windowComponent, this, { backend: backend, theme: windowTheme })
        const surface = findChild(window, "settingsAudioContent")
        tryCompare(surface, "contentLoaded", true)
        const section = surface.presentationItem
        section.requestVolume("output-0123456789abcdef", "Output", 40)
        const dialog = findChild(section, "audioVolumeDialog")
        tryCompare(dialog, "opened", true)
        window.close()
        compare(window.visible, true)
        compare(dialog.opened, true)
        dialog.reject()
        tryCompare(dialog, "opened", false)
        backend.audioBusy = true
        window.close()
        compare(window.visible, true)
        backend.audioBusy = false
        window.close()
        compare(window.visible, false)
        verifyNoWrites(backend)
    }

    function test_popupAndFocusSurviveThemeAndNavigation() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        verify(section.showPage(3))
        wait(0)
        const chooser = findChild(section, "audioProfileChooser-card-0123456789abcdef")
        chooser.forceActiveFocus()
        chooser.popup.open()
        tryCompare(chooser.popup, "opened", true)
        verify(!section.showPage(0))
        section.surfaceColor = "#ffffff"
        section.textColor = "#161616"
        compare(chooser.popup.opened, true)
        chooser.popup.close()
        tryCompare(chooser.popup, "opened", false)
        chooser.forceActiveFocus()
        verify(section.showPage(0))
        wait(0)
        verify(section.showPage(3))
        tryCompare(chooser, "activeFocus", true)
        verifyNoWrites(backend)
    }

    function test_inactiveAndBusyHelpersCannotDispatchOrReplaceDraft() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        section.requestVolume("output-0123456789abcdef", "Output", 40)
        const dialog = findChild(section, "audioVolumeDialog")
        tryCompare(dialog, "opened", true)
        for (const inactive of [true, false]) {
            section.active = !inactive
            backend.audioBusy = !inactive
            section.requestMute("output-fedcba9876543210", "Headset", false)
            section.requestDefault("output", "output-fedcba9876543210")
            section.requestStreamMove("stream-0123456789abcdef", "playback", "output-0123456789abcdef")
            section.chooseProcessRule("output", "output-0123456789abcdef")
            section.chooseExecutableRule("output", "output-0123456789abcdef")
            section.chooseDirectoryRule("output", "output-0123456789abcdef")
            section.removeRouteRule("rule-0123456789abcdef")
            verify(!section.applyPendingControl())
            verify(!section.applyPendingDefault())
            verify(!section.applyPendingStreamMove())
            compare(section.pendingControlTarget, "output-0123456789abcdef")
            compare(section.pendingControl, "volume")
            compare(section.pendingDirection, "")
            compare(section.pendingMoveStream, "")
            verifyNoWrites(backend)
        }
        backend.audioBusy = false
        section.active = true
        dialog.reject()
        tryCompare(dialog, "opened", false)
    }

    function test_eachPopupRetainsItsOwnHideBlock() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        verify(section.showPage(3))
        const profile = findChild(section, "audioProfileChooser-card-0123456789abcdef")
        const port = findChild(section, "audioPortChooser-output-0123456789abcdef")
        profile.popup.open()
        port.popup.open()
        tryCompare(profile.popup, "opened", true)
        tryCompare(port.popup, "opened", true)
        port.popup.close()
        tryCompare(port.popup, "opened", false)
        compare(profile.popup.opened, true)
        verify(!section.showPage(0))
        profile.popup.close()
        tryCompare(profile.popup, "opened", false)
        verify(section.showPage(0))
        verifyNoWrites(backend)
    }

    function test_volumeKeyboardDraftThemeAndExplicitAccept() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        verify(section.openDevice("output", "output-0123456789abcdef"))
        const dialog = findChild(section, "audioVolumeDialog")
        const spin = findChild(section, "audioVolumeInput")
        for (let attempt = 0; attempt < 2; ++attempt) {
            section.requestVolume(section.deviceId, "Output", 40)
            tryCompare(dialog, "opened", true)
            compare(spin.value, 40)
            spin.contentItem.forceActiveFocus()
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_4)
            keyClick(Qt.Key_2)
            keyClick(Qt.Key_Tab)
            tryCompare(spin, "value", 42)
            compare(section.pendingControlRequestedVolume, 42)
            section.surfaceColor = "#eeeeee"
            section.textColor = "#111111"
            section.accentColor = "#145da0"
            verify(Qt.colorEqual(section.onAccentColor, "#ffffff"))
            compare(dialog.standardButton(Dialog.Ok).contentItem.color, section.onAccentColor)
            compare(dialog.background.color, section.surfaceColor)
            compare(spin.value, 42)
            keyClick(Qt.Key_Escape)
            tryCompare(dialog, "opened", false)
            compare(section.pendingControlTarget, "")
            verifyNoWrites(backend)
        }
        section.requestVolume(section.deviceId, "Output", 40)
        tryCompare(dialog, "opened", true)
        spin.value = 43
        spin.valueModified()
        // The owned footer still participates in Dialog's accept-role dispatch.
        mouseClick(dialog.standardButton(Dialog.Ok))
        tryCompare(dialog, "opened", false)
        compare(backend.volumeSets, 1)
        compare(backend.lastCall[0], "output-0123456789abcdef")
        compare(backend.lastCall[1], 43)
    }

    function test_volumeIndicatorStatesMatchTheBoundedRange() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        section.requestVolume("output-0123456789abcdef", "Output", 40)
        const dialog = findChild(section, "audioVolumeDialog")
        const spin = findChild(section, "audioVolumeInput")
        tryCompare(dialog, "opened", true)
        for (const value of [40, 0, 100]) {
            spin.value = value
            compare(spin.up.indicator.enabled, value < 100)
            compare(spin.down.indicator.enabled, value > 0)
            // Compare displayed RGB: blended QColor intermediates can retain
            // different precision while representing the same 8-bit color.
            compare(String(spin.up.indicator.children[0].color),
                    String(value < 100 ? section.textColor : section.secondaryTextColor))
            compare(String(spin.down.indicator.children[0].color),
                    String(value > 0 ? section.textColor : section.secondaryTextColor))
        }
        mouseClick(spin.up.indicator)
        compare(spin.value, 100)
        spin.enabled = false
        compare(String(spin.up.indicator.children[0].color), String(section.secondaryTextColor))
        compare(String(spin.down.indicator.children[0].color), String(section.secondaryTextColor))
        mouseClick(spin.down.indicator)
        compare(spin.value, 100)
        dialog.reject()
        tryCompare(dialog, "opened", false)
        verifyNoWrites(backend)
    }

    function test_scrollRestorationIsPerPage() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, {
            backend: backend, visible: true, width: 640, height: 480
        })
        verify(section.showPage(3))
        const scroll = findChild(section, "audioSettingsContent").contentItem
        tryVerify(() => scroll.contentHeight > scroll.height + 280)
        scroll.contentY = 280
        wait(0)
        verify(section.showPage(0))
        tryCompare(scroll, "contentY", 0)
        verify(section.showPage(3))
        tryCompare(scroll, "contentY", 280)
        verifyNoWrites(backend)
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

    function test_typedGoxlrStatusAndControlBoundary() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend, visible: true })
        compare(section.goxlrMutationAllowed, false)
        verify(section.showPage(3))
        verify(section)
        compare(section.goxlrStateText(), "Ready")
        compare(section.goxlrDetailText(), "Devices are projected from the provider profile model, not from hardware readback.")
        compare(section.goxlrBoundaryText(), "Opening and refreshing this screen never starts the provider. Each enabled control uses a bounded plan and one acknowledged setter. Values and compensation have provider-profile-model authority; they are not hardware readback or hardware-exact restoration. No playback or capture is started.")
        compare(section.goxlrReadyDevice, true)
        compare(section.goxlrMutationAllowed, true)
        backend.audioGoxlrStatus = "Inactive"
        backend.audioGoxlrReason = "provider-inactive"
        backend.audioGoxlrProviderActive = false
        backend.audioGoxlrMutationAvailable = false
        backend.audioGoxlrDevices = []
        compare(section.goxlrStateText(), "Inactive")
        compare(section.goxlrDetailText(), "A GoXLR is connected, but its provider is inactive. Audio Settings did not start it.")
        compare(section.goxlrReadyDevice, false)
        compare(section.goxlrMutationAllowed, false)
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
        section.visible = true
        verify(section.showPage(2))
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
        section.visible = true
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

    function test_guardedProfileAndPortConfirmation() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        section.visible = true
        verify(section.showPage(3))
        wait(0)
        compare(section.profilePortBoundaryText(), "A profile may rebuild the software Audio graph; a port changes one selected signal path. Synapse does not play or record a test sound, and software verification is not hardware readback.")
        compare(section.profilePortUnavailableText(), "Audio profiles and ports are unavailable.")

        let profileChooser = findChild(section, "audioProfileChooser-card-0123456789abcdef")
        verify(profileChooser)
        compare(profileChooser.currentIndex, 0)
        profileChooser.activated(1)
        compare(backend.profilePlans, 1)
        compare(backend.lastCall, ["card-0123456789abcdef", "profile-fedcba9876543210"])
        let selectionDialog = findChild(section, "audioSelectionDialog")
        verify(selectionDialog)
        tryCompare(selectionDialog, "opened", true)
        selectionDialog.accept()
        compare(backend.confirmedSelections, 1)
        compare(backend.audioSelectionConfirmationOpen, false)

        let portChooser = findChild(section, "audioPortChooser-output-0123456789abcdef")
        verify(portChooser)
        compare(portChooser.currentIndex, 0)
        portChooser.activated(1)
        compare(backend.portPlans, 1)
        compare(backend.lastCall, ["output", "output-0123456789abcdef", "port-fedcba9876543210"])
        tryCompare(selectionDialog, "opened", true)
        selectionDialog.reject()
        compare(backend.cancelledSelections, 1)
        compare(backend.audioSelectionConfirmationOpen, false)

        backend.audioProfilePortAvailable = false
        backend.audioProfilePortReason = "timeout"
        compare(section.profilePortUnavailableText(), "Audio profile and port discovery timed out safely.")
        backend.audioProfilePortReason = "invalid-response"
        compare(section.profilePortUnavailableText(), "The Audio profile and port inventory was rejected safely.")
    }

    function test_typedProcessExecutableAndDirectoryRules() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        section.visible = true
        verify(section.showPage(3))
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

    function linearChannel(value) {
        return value <= 0.04045 ? value / 12.92
                                : Math.pow((value + 0.055) / 1.055, 2.4)
    }

    function contrastRatio(first, second) {
        const firstLuminance = 0.2126 * linearChannel(first.r)
                               + 0.7152 * linearChannel(first.g)
                               + 0.0722 * linearChannel(first.b)
        const secondLuminance = 0.2126 * linearChannel(second.r)
                                + 0.7152 * linearChannel(second.g)
                                + 0.0722 * linearChannel(second.b)
        const lighter = Math.max(firstLuminance, secondLuminance)
        const darker = Math.min(firstLuminance, secondLuminance)
        return (lighter + 0.05) / (darker + 0.05)
    }

    function test_synapsePresentationIsReadableAndDiscoverable() {
        const backend = createTemporaryObject(fakeBackend, this)
        const section = createTemporaryObject(audioComponent, this, {
            backend: backend,
            visible: true,
            backgroundColor: "#121212",
            surfaceColor: "#0d0d0d",
            surfaceHoverColor: "#1e1e1e",
            borderColor: "#333333",
            accentColor: "#e68e0d",
            textColor: "#bebebe",
            mutedColor: "#555555",
            urgentColor: "#d35f5f"
        })
        verify(section)
        wait(0)

        verify(Qt.colorEqual(section.palette.window, "#121212"))
        verify(Qt.colorEqual(section.palette.text, "#bebebe"))
        verify(contrastRatio(section.secondaryTextColor,
                             section.surfaceColor) >= 4.5)
        verify(contrastRatio(section.onAccentColor,
                             section.accentColor) >= 4.5)

        const title = findChild(section, "audioPresentationTitle")
        const description = findChild(section, "audioPresentationDescription")
        const refresh = findChild(section, "audioRefreshButton")
        const content = findChild(section, "audioSettingsContent")
        const summary = findChild(section, "audioGoxlrSummaryCard")
        verify(title && description && refresh && content && summary)
        verify(Qt.colorEqual(title.color, section.textColor))
        compare(String(description.color), String(section.secondaryTextColor))
        verify(Qt.colorEqual(summary.background.color, section.surfaceColor))
        compare(String(refresh.contentItem.color),
                String(section.onAccentColor))
        verify(content.visible)

        const headings = ["audioHeadingGoxlr", "audioHeadingOutputs",
                          "audioHeadingInputs", "audioHeadingStreams",
                          "audioHeadingRouting", "audioHeadingProfilesPorts"]
        for (let index = 0; index < headings.length; ++index) {
            const heading = findChild(section, headings[index])
            verify(heading)
            verify(heading.text.length > 0)
            verify(Qt.colorEqual(heading.color, section.textColor))
        }

        backend.audioBusy = true
        wait(0)
        compare(refresh.enabled, false)
        verify(contrastRatio(refresh.contentItem.color,
                             section.surfaceColor) >= 4.5)
    }
}
