// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Control {
    id: root
    objectName: "audioSettingsPresentation"

    required property var backend
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
    readonly property color secondaryTextColor: mixColor(surfaceColor, textColor, 0.72)
    readonly property color accentSoftColor: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.18)
    // Avoid an onXxx initializer: QML can interpret it as a signal handler
    // instead of a color binding. Keep the presentation alias for consumers.
    readonly property color accentForegroundColor: contrastColor(accentColor)
    readonly property alias onAccentColor: root.accentForegroundColor
    property bool active: visible
    enabled: active
    padding: 0
    font.family: fontFamily
    font.pixelSize: 12
    palette.window: backgroundColor
    palette.windowText: textColor
    palette.base: surfaceColor
    palette.alternateBase: surfaceHoverColor
    palette.text: textColor
    palette.button: surfaceColor
    palette.buttonText: textColor
    palette.highlight: accentColor
    palette.highlightedText: onAccentColor
    palette.brightText: urgentColor
    palette.mid: borderColor
    palette.dark: borderColor
    palette.light: surfaceHoverColor
    palette.placeholderText: secondaryTextColor

    background: Rectangle {
        color: "transparent"
    }

    function mixColor(first, second, amount) {
        const bounded = Math.max(0, Math.min(1, amount))
        return Qt.rgba(first.r + (second.r - first.r) * bounded, first.g + (second.g - first.g) * bounded, first.b + (second.b - first.b) * bounded, 1)
    }

    function linearColorChannel(value) {
        return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4)
    }

    function contrastColor(color) {
        const luminance = 0.2126 * linearColorChannel(color.r) + 0.7152 * linearColorChannel(color.g) + 0.0722 * linearColorChannel(color.b)
        const blackContrast = (luminance + 0.05) / 0.05
        const whiteContrast = 1.05 / (luminance + 0.05)
        return blackContrast >= whiteContrast ? "#000000" : "#ffffff"
    }
    // Page changes retain the presentation and its confirmation owners. They
    // never activate another application or select a replacement device.
    enum Page {
        Devices,
        Device,
        Applications,
        Advanced
    }
    QtObject {
        id: navigation
        property int page: AudioSettingsSection.Devices
        property string device: ""
        property string direction: ""
        property var scrollOffsets: [0, 0, 0, 0]
        property var focusItems: [null, null, null, null]
    }
    readonly property int currentPage: navigation.page
    readonly property string deviceId: navigation.device
    readonly property string deviceDirection: navigation.direction
    readonly property var deviceEndpoint: endpoint(deviceDirection, deviceId)
    readonly property bool deviceAvailable: deviceEndpoint !== null && backend && backend.audioAvailable
    readonly property bool backendBusy: !backend || backend.audioBusy
    property var openPopups: []
    readonly property var activePopup: openPopups.length > 0 ? openPopups[openPopups.length - 1] : null
    function popupOpened(popup) {
        if (popup && openPopups.indexOf(popup) < 0)
            openPopups = openPopups.concat([popup])
    }
    function popupClosed(popup) {
        openPopups = openPopups.filter(item => item && item !== popup)
    }
    readonly property bool navigationBlocked: !active || interactionPending
    readonly property bool interactionPending: backendBusy || pendingDirection !== "" || pendingMoveStream !== "" || pendingControlTarget !== "" || activePopup !== null || confirmDefault.visible || confirmStreamMove.visible || volumeDialog.visible || muteDialog.visible || processDialog.visible || selectionDialog.visible || backend.audioProcessChoiceOpen || backend.audioSelectionConfirmationOpen
    readonly property bool canUnload: !loaded
    property bool loaded: false

    // The initial migration deliberately refuses destructive view reload after
    // activation. A future host must negotiate state transfer, not destroy it.
    function requestUnload() {
        return canUnload
    }

    function endpoint(direction, identity) {
        const items = direction === "output" ? outputs : direction === "input" ? inputs : []
        for (let index = 0; index < items.length; ++index) {
            if (items[index].id === identity)
                return items[index]
        }
        return null
    }

    function showPage(page) {
        if (navigationBlocked || !Number.isInteger(page) || page < 0 || page > 3 || (page === AudioSettingsSection.Device && deviceId === ""))
            return false
        if (page === currentPage)
            return true
        navigation.scrollOffsets[currentPage] = audioScroll.contentItem.contentY
        navigation.focusItems[currentPage] = root.Window.window ? root.Window.window.activeFocusItem : null
        navigation.page = page
        Qt.callLater(function () {
            if (root.currentPage !== page || root.navigationBlocked)
                return
            const viewport = audioScroll.contentItem
            viewport.contentY = Math.max(0, Math.min(navigation.scrollOffsets[page], viewport.contentHeight - viewport.height))
            const target = navigation.focusItems[page]
            if (target && target.visible && target.enabled)
                target.forceActiveFocus(Qt.TabFocusReason)
            else
                pageTitle.forceActiveFocus(Qt.TabFocusReason)
        })
        return true
    }

    function openDevice(direction, identity) {
        if (navigationBlocked || !endpoint(direction, identity))
            return false
        if (deviceId !== identity || deviceDirection !== direction) {
            navigation.scrollOffsets[AudioSettingsSection.Device] = 0
            navigation.focusItems[AudioSettingsSection.Device] = null
        }
        navigation.device = identity
        navigation.direction = direction
        return showPage(AudioSettingsSection.Device)
    }

    property string pendingDirection: ""
    property string pendingDevice: ""
    property string selectedProcessStream: ""
    property string pendingMoveStream: ""
    property string pendingMoveDirection: ""
    property string pendingMoveOriginalDevice: ""
    property string pendingMoveRequestedDevice: ""
    property string pendingControlTarget: ""
    property string pendingControlLabel: ""
    property string pendingControl: ""
    property int pendingControlOriginalVolume: 0
    property int pendingControlRequestedVolume: 0
    property bool pendingControlOriginalMuted: false
    property bool pendingControlRequestedMuted: false
    readonly property var outputs: backend ? backend.audioOutputs || [] : []
    readonly property var inputs: backend ? backend.audioInputs || [] : []
    readonly property var streams: backend ? backend.audioStreams || [] : []
    readonly property var cards: backend ? backend.audioCards || [] : []
    readonly property bool profilePortAvailable: backend ? backend.audioProfilePortAvailable : false
    readonly property bool profilePortMutationAvailable: backend ? backend.audioProfilePortMutationAvailable : false
    readonly property string profilePortReason: backend ? backend.audioProfilePortReason || "" : ""
    readonly property var profileCards: backend ? backend.audioProfileCards || [] : []
    readonly property var portEndpoints: backend ? backend.audioPortEndpoints || [] : []
    readonly property var routeRules: backend ? backend.audioRouteRules || [] : []
    readonly property bool routeBrokerAvailable: backend ? backend.audioRouteBrokerAvailable : false
    readonly property bool routeBrokerActive: backend ? backend.audioRouteBrokerActive : false
    readonly property string routeBrokerReason: backend ? backend.audioRouteBrokerReason || "" : ""
    readonly property bool routeEnforcementAvailable: backend ? backend.audioRouteEnforcementAvailable : false
    readonly property string goxlrStatus: backend ? backend.audioGoxlrStatus || "Unavailable" : "Unavailable"
    readonly property string goxlrReason: backend ? backend.audioGoxlrReason || "" : ""
    readonly property bool goxlrProviderActive: backend ? backend.audioGoxlrProviderActive : false
    readonly property bool goxlrPresenceKnown: backend ? backend.audioGoxlrPresenceKnown : false
    readonly property bool goxlrDevicePresent: backend ? backend.audioGoxlrDevicePresent : false
    readonly property bool goxlrMutationAvailable: backend ? backend.audioGoxlrMutationAvailable : false
    readonly property bool goxlrTruncated: backend ? backend.audioGoxlrTruncated : false
    readonly property var goxlrDevices: backend ? backend.audioGoxlrDevices || [] : []
    readonly property bool goxlrReadyDevice: goxlrStatus === "Ready" && goxlrProviderActive && goxlrDevices.length === 1
    readonly property bool goxlrMutationAllowed: goxlrReadyDevice && goxlrMutationAvailable && !navigationBlocked && currentPage === AudioSettingsSection.Advanced

    function activate() {
        if (loaded)
            return
        loaded = true
        if (backend)
            backend.loadAudio()
    }

    function requestDefault(direction, device) {
        if (!active || backendBusy)
            return
        pendingDirection = direction
        pendingDevice = device
        confirmDefault.open()
    }

    function applyPendingDefault() {
        if (!active || backendBusy || pendingDirection === "" || pendingDevice === "")
            return false
        backend.setAudioDefault(pendingDirection, pendingDevice)
        pendingDirection = ""
        pendingDevice = ""
        return true
    }

    function endpointLabel(device) {
        const items = device.indexOf("output-") === 0 ? outputs : inputs
        for (let index = 0; index < items.length; ++index) {
            if (items[index].id === device)
                return items[index].label
        }
        return qsTr("Unavailable device")
    }

    function requestStreamMove(stream, direction, originalDevice) {
        if (!active || backendBusy)
            return
        pendingMoveStream = stream
        pendingMoveDirection = direction
        pendingMoveOriginalDevice = originalDevice
        pendingMoveRequestedDevice = ""
        confirmStreamMove.open()
    }

    function clearPendingStreamMove() {
        pendingMoveStream = ""
        pendingMoveDirection = ""
        pendingMoveOriginalDevice = ""
        pendingMoveRequestedDevice = ""
    }

    function applyPendingStreamMove() {
        if (!active || backendBusy || pendingMoveStream === "" || pendingMoveOriginalDevice === "" || pendingMoveRequestedDevice === "")
            return false
        backend.moveAudioStream(pendingMoveStream, pendingMoveOriginalDevice, pendingMoveRequestedDevice)
        clearPendingStreamMove()
        return true
    }

    function requestVolume(target, label, currentVolume) {
        if (!active || backendBusy)
            return
        pendingControlTarget = target
        pendingControlLabel = label
        pendingControl = "volume"
        pendingControlOriginalVolume = currentVolume
        pendingControlRequestedVolume = Math.min(100, Math.max(0, currentVolume))
        volumeDialog.open()
    }

    function requestMute(target, label, currentMuted) {
        if (!active || backendBusy)
            return
        pendingControlTarget = target
        pendingControlLabel = label
        pendingControl = "mute"
        pendingControlOriginalMuted = currentMuted
        pendingControlRequestedMuted = !currentMuted
        muteDialog.open()
    }

    function clearPendingControl() {
        pendingControlTarget = ""
        pendingControlLabel = ""
        pendingControl = ""
        pendingControlOriginalVolume = 0
        pendingControlRequestedVolume = 0
        pendingControlOriginalMuted = false
        pendingControlRequestedMuted = false
    }

    function applyPendingControl() {
        if (!active || backendBusy || pendingControlTarget === "")
            return false
        if (pendingControl === "volume")
            backend.setAudioVolume(pendingControlTarget, pendingControlRequestedVolume)
        else if (pendingControl === "mute")
            backend.setAudioMuted(pendingControlTarget, pendingControlRequestedMuted)
        else
            return false
        clearPendingControl()
        return true
    }

    function chooseProcessRule(direction, device) {
        if (active && !backendBusy)
            backend.chooseAudioProcessRule(direction, device)
    }

    function chooseExecutableRule(direction, device) {
        if (active && !backendBusy)
            backend.chooseAudioExecutableRule(direction, device)
    }

    function chooseDirectoryRule(direction, device) {
        if (active && !backendBusy)
            backend.chooseAudioDirectoryRule(direction, device)
    }

    function removeRouteRule(ruleId) {
        if (active && !backendBusy)
            backend.removeAudioRouteRule(ruleId)
    }

    function statusText(statusId) {
        switch (statusId) {
        case "audio-loaded":
            return qsTr("Audio devices updated.")
        case "audio-default-applied":
            return qsTr("Default Audio device changed and verified.")
        case "audio-default-unchanged":
            return qsTr("The selected device was already the default.")
        case "audio-stream-moved":
            return qsTr("The active Audio stream moved and was verified.")
        case "audio-stream-unchanged":
            return qsTr("The active Audio stream was already on that device.")
        case "audio-volume-applied":
            return qsTr("Volume changed and verified.")
        case "audio-volume-unchanged":
            return qsTr("The selected Audio item already had that volume.")
        case "audio-mute-applied":
            return qsTr("Mute state changed and verified.")
        case "audio-mute-unchanged":
            return qsTr("The selected Audio item already had that mute state.")
        case "audio-selection-confirmation-required":
            return qsTr("Review the Audio signal-path change before applying it.")
        case "audio-profile-applied":
            return qsTr("Audio profile changed and verified in the software model.")
        case "audio-profile-unchanged":
            return qsTr("The selected Audio profile was already active.")
        case "audio-port-applied":
            return qsTr("Audio port changed and verified in the software model.")
        case "audio-port-unchanged":
            return qsTr("The selected Audio port was already active.")
        case "audio-route-rule-saved":
            return qsTr("Application Audio rule saved.")
        case "audio-route-rule-unchanged":
            return qsTr("The Application Audio rule was already present.")
        case "audio-route-rule-removed":
            return qsTr("Application Audio rule removed.")
        case "audio-goxlr-control-applied":
            return qsTr("GoXLR provider-model control changed and verified.")
        case "audio-goxlr-control-unchanged":
            return qsTr("The GoXLR provider-model control already had that value.")
        case "audio-goxlr-mixer-opened":
            return qsTr("The complete GoXLR mixer was opened.")
        default:
            return ""
        }
    }

    function brokerStateText() {
        if (routeBrokerActive)
            return qsTr("Active")
        if (routeBrokerAvailable)
            return qsTr("Inactive")
        return qsTr("Unavailable")
    }

    function brokerDetailText() {
        if (routeEnforcementAvailable)
            return qsTr("Rules apply automatically to new streams.")
        if (routeBrokerAvailable && routeBrokerReason === "broker-not-running")
            return qsTr("Rules are stored; the new-stream Audio broker is not running.")
        if (routeBrokerReason === "runtime-unavailable")
            return qsTr("Rules are stored; private runtime status is unavailable.")
        if (routeBrokerReason === "runtime-state-invalid" || routeBrokerReason === "invalid-response" || routeBrokerReason === "timeout")
            return qsTr("Rules are stored; the Audio broker status was rejected safely.")
        return qsTr("Rules are stored; new-stream enforcement is unavailable.")
    }

    function existingStreamBoundaryText() {
        return qsTr("Automatic rules never move existing streams. A stream moves only after separate confirmation, one at a time.")
    }

    function goxlrStateText() {
        switch (goxlrStatus) {
        case "Ready":
            return qsTr("Ready")
        case "Inactive":
            return qsTr("Inactive")
        case "Failed":
            return qsTr("Failed")
        default:
            return qsTr("Unavailable")
        }
    }

    function goxlrDetailText() {
        if (goxlrStatus === "Ready" && goxlrDevices.length === 0)
            return qsTr("The GoXLR provider is running and reports no devices.")
        if (goxlrStatus === "Ready")
            return qsTr("Devices are projected from the provider profile model, not from hardware readback.")
        if (goxlrStatus === "Inactive" && goxlrPresenceKnown && goxlrDevicePresent)
            return qsTr("A GoXLR is connected, but its provider is inactive. Audio Settings did not start it.")
        if (goxlrStatus === "Inactive")
            return qsTr("The GoXLR provider is inactive. Audio Settings did not start it.")
        if (goxlrStatus === "Failed")
            return qsTr("The GoXLR status response was rejected safely.")
        return qsTr("The GoXLR status adapter is unavailable.")
    }

    function goxlrBoundaryText() {
        return qsTr("Opening and refreshing this screen never starts the provider. Each enabled control uses a bounded plan and one acknowledged setter. Values and compensation have provider-profile-model authority; they are not hardware readback or hardware-exact restoration. No playback or capture is started.")
    }

    function controlBoundaryText() {
        return qsTr("Volume and mute affect one selected item. Synapse does not play or record a test sound and does not change routing or profiles.")
    }

    function profilePortBoundaryText() {
        return qsTr("A profile may rebuild the software Audio graph; a port changes one selected signal path. Synapse does not play or record a test sound, and software verification is not hardware readback.")
    }

    function profilePortUnavailableText() {
        if (profilePortReason === "timeout")
            return qsTr("Audio profile and port discovery timed out safely.")
        if (profilePortReason === "invalid-response")
            return qsTr("The Audio profile and port inventory was rejected safely.")
        return qsTr("Audio profiles and ports are unavailable.")
    }

    function selectionDisplayLabel(label, selection) {
        if (label)
            return label
        return selection === "profile" ? qsTr("Unnamed profile") : qsTr("Unnamed port")
    }

    function selectionTargetDisplayLabel(label, selection) {
        if (label)
            return label
        return selection === "profile" ? qsTr("Audio card") : qsTr("Audio endpoint")
    }

    function errorText(errorId) {
        switch (errorId) {
        case "audio-process-unavailable":
            return qsTr("No eligible active Audio process is available.")
        case "selection-invalid":
            return qsTr("The selected Audio item is no longer available.")
        case "path-invalid":
            return qsTr("The selected path is invalid.")
        case "native-dialog-unavailable":
            return qsTr("The native Audio path chooser is unavailable in this host.")
        case "timeout":
            return qsTr("The Audio backend did not respond in time.")
        case "contract-invalid":
            return qsTr("The Audio backend returned an invalid contract.")
        case "backend-unavailable":
        case "start-failed":
            return qsTr("The Audio backend is unavailable.")
        case "backend-failed":
        case "default-plan-failed":
        case "default-apply-failed":
        case "route-policy-failed":
        case "broker-status-unavailable":
        case "goxlr-status-unavailable":
        case "policy-unavailable":
            return qsTr("The Audio operation failed safely.")
        case "audio-stream-plan-failed":
            return qsTr("The active stream could not be planned safely.")
        case "audio-stream-move-refused":
            return qsTr("The stream state changed or became unavailable. Nothing was moved.")
        case "audio-stream-move-restored":
            return qsTr("The move failed; the exact original device was restored and verified.")
        case "audio-stream-move-failed":
            return qsTr("The stream move could not be verified safely.")
        case "audio-control-plan-failed":
            return qsTr("The Audio control could not be planned safely.")
        case "audio-control-refused":
            return qsTr("The Audio item changed or became unavailable. Nothing was changed.")
        case "audio-control-restored":
            return qsTr("The Audio control failed; the exact original value was restored and verified.")
        case "audio-control-failed":
            return qsTr("The Audio control outcome could not be verified safely.")
        case "audio-goxlr-selection-invalid":
            return qsTr("The selected GoXLR control is unavailable or stale.")
        case "audio-goxlr-control-plan-failed":
            return qsTr("The GoXLR control could not be planned safely.")
        case "audio-goxlr-control-refused":
            return qsTr("The GoXLR provider-model state changed. Nothing was overwritten.")
        case "audio-goxlr-control-restored":
            return qsTr("The GoXLR provider-model change failed and was compensated. Hardware restoration is not claimed.")
        case "audio-goxlr-control-failed":
            return qsTr("The GoXLR control outcome is uncertain or drifted. Refresh before trying again.")
        case "audio-goxlr-mixer-unavailable":
            return qsTr("The complete GoXLR mixer application is unavailable.")
        case "audio-selection-plan-failed":
            return qsTr("The Audio profile or port change could not be planned safely.")
        case "audio-selection-refused":
            return qsTr("The Audio profile or port state changed. Nothing was applied.")
        case "audio-selection-restored":
            return qsTr("The Audio signal-path change failed; the exact original software state was restored and verified.")
        case "audio-selection-failed":
            return qsTr("The Audio profile or port outcome could not be verified safely.")
        case "profile-port-inventory-unavailable":
            return qsTr("The Audio profile and port inventory is unavailable.")
        case "output-too-large":
        case "process-crashed":
            return qsTr("The Audio backend response was rejected.")
        default:
            return errorId === "" ? "" : qsTr("Audio error: %1").arg(errorId)
        }
    }

    onActiveChanged: if (active)
        activate()
    Component.onCompleted: if (active)
        activate()

    Connections {
        target: root.backend
        function onAudioProcessChoiceRequested() {
            root.selectedProcessStream = ""
            processDialog.open()
        }
        function onAudioSelectionConfirmationRequested() {
            selectionDialog.open()
        }
        function onAudioSelectionChanged() {
            if (selectionDialog.opened && root.backend && !root.backend.audioSelectionConfirmationOpen)
                selectionDialog.close()
        }
    }

    AudioDialog {
        id: confirmDefault
        presentation: root
        title: qsTr("Change default device")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.applyPendingDefault()
        onRejected: {
            root.pendingDirection = ""
            root.pendingDevice = ""
        }
    }

    AudioDialog {
        id: selectionDialog
        presentation: root
        acceptText: qsTr("Apply change")
        objectName: "audioSelectionDialog"
        title: root.backend && root.backend.audioSelectionKind === "profile" ? qsTr("Change Audio profile") : qsTr("Change Audio port")
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.max(root.width - 40, 320), 560)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (root.active && !root.backendBusy)
            root.backend.confirmAudioSelection()
        onRejected: if (root.backend)
            root.backend.cancelAudioSelection()

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: root.backend ? root.selectionTargetDisplayLabel(root.backend.audioSelectionTargetLabel || "", root.backend.audioSelectionKind || "") : ""
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: root.backend ? qsTr("%1 → %2").arg(root.selectionDisplayLabel(root.backend.audioSelectionOriginalLabel || "", root.backend.audioSelectionKind || "")).arg(root.selectionDisplayLabel(root.backend.audioSelectionRequestedLabel || "", root.backend.audioSelectionKind || "")) : ""
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: root.profilePortBoundaryText()
                wrapMode: Text.WordWrap
                color: root.secondaryTextColor
            }
        }
    }

    AudioDialog {
        id: confirmStreamMove
        presentation: root
        acceptText: qsTr("Move stream")
        title: qsTr("Move this active stream")
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.max(root.width - 40, 320), 560)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            streamMoveDevice.currentIndex = -1
            const button = standardButton(Dialog.Ok)
            if (button)
                button.enabled = false
        }
        onAccepted: root.applyPendingStreamMove()
        onRejected: root.clearPendingStreamMove()

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("Only this active stream will move. No routing rule will be saved.")
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("If verification fails, Synapse restores the exact original device only while it can prove the same stream identity.")
                wrapMode: Text.WordWrap
                color: root.secondaryTextColor
            }
            AudioComboBox {
                id: streamMoveDevice
                presentation: root
                Layout.fillWidth: true
                model: root.pendingMoveDirection === "playback" ? root.outputs : root.inputs
                textRole: "label"
                valueRole: "id"
                displayText: currentIndex < 0 ? qsTr("Choose a device") : currentText
                onActivated: {
                    root.pendingMoveRequestedDevice = currentValue || ""
                    const button = confirmStreamMove.standardButton(Dialog.Ok)
                    if (button)
                        button.enabled = root.pendingMoveRequestedDevice !== "" && root.pendingMoveRequestedDevice !== root.pendingMoveOriginalDevice
                }
            }
        }
    }

    AudioDialog {
        id: volumeDialog
        presentation: root
        acceptText: qsTr("Apply volume")
        objectName: "audioVolumeDialog"
        title: qsTr("Change volume")
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.max(root.width - 40, 320), 520)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            volumeSpin.value = root.pendingControlRequestedVolume
            const button = standardButton(Dialog.Ok)
            if (button)
                button.enabled = volumeSpin.value !== root.pendingControlOriginalVolume
        }
        onAccepted: {
            root.pendingControlRequestedVolume = volumeSpin.value
            root.applyPendingControl()
        }
        onRejected: root.clearPendingControl()

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: root.pendingControlLabel
                font.bold: true
                elide: Text.ElideRight
            }
            AudioSpinBox {
                id: volumeSpin
                presentation: root
                objectName: "audioVolumeInput"
                Layout.fillWidth: true
                from: 0
                to: 100
                editable: true
                textFromValue: function (value, locale) {
                    return Number(value).toLocaleString(locale, "f", 0) + "%"
                }
                valueFromText: function (text, locale) {
                    return Number.fromLocaleString(locale, text.replace("%", ""))
                }
                onValueModified: {
                    root.pendingControlRequestedVolume = value
                    const button = volumeDialog.standardButton(Dialog.Ok)
                    if (button)
                        button.enabled = value !== root.pendingControlOriginalVolume
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Volume is capped at 100% to avoid software amplification.")
                wrapMode: Text.WordWrap
                color: root.secondaryTextColor
            }
            Label {
                Layout.fillWidth: true
                text: root.controlBoundaryText()
                wrapMode: Text.WordWrap
                color: root.secondaryTextColor
            }
        }
    }

    AudioDialog {
        id: muteDialog
        presentation: root
        acceptText: root.pendingControlRequestedMuted ? qsTr("Mute") : qsTr("Unmute")
        title: root.pendingControlRequestedMuted ? qsTr("Mute Audio item") : qsTr("Unmute Audio item")
        modal: true
        anchors.centerIn: parent
        width: Math.min(Math.max(root.width - 40, 320), 520)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.applyPendingControl()
        onRejected: root.clearPendingControl()

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: root.pendingControlLabel
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: root.pendingControlRequestedMuted ? qsTr("Mute only this selected Audio item?") : qsTr("Unmute only this selected Audio item?")
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: root.controlBoundaryText()
                wrapMode: Text.WordWrap
                color: root.secondaryTextColor
            }
        }
    }

    AudioDialog {
        id: processDialog
        presentation: root
        title: qsTr("Choose an active Audio process")
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 560)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            root.selectedProcessStream = ""
            const button = standardButton(Dialog.Ok)
            if (button)
                button.enabled = false
        }
        onAccepted: {
            if (root.active && !root.backendBusy && root.selectedProcessStream !== "")
                root.backend.confirmAudioProcessRule(root.selectedProcessStream)
        }
        onRejected: if (root.backend)
            root.backend.cancelAudioProcessRule()

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("The rule will follow the canonical executable, never the PID.")
                wrapMode: Text.WordWrap
                color: root.secondaryTextColor
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(320, Math.max(64, processChoicesColumn.implicitHeight))
                clip: true

                ColumnLayout {
                    id: processChoicesColumn
                    width: parent.width
                    Repeater {
                        model: root.backend ? root.backend.audioProcessChoices || [] : []
                        delegate: RadioButton {
                            id: processChoice
                            required property var modelData
                            Layout.fillWidth: true
                            text: processChoice.modelData.label
                            checked: root.selectedProcessStream === processChoice.modelData.id
                            onClicked: {
                                root.selectedProcessStream = processChoice.modelData.id
                                const button = processDialog.standardButton(Dialog.Ok)
                                if (button)
                                    button.enabled = true
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: root.presentationSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    id: pageTitle
                    textFormat: Text.PlainText
                    objectName: "audioPresentationTitle"
                    Layout.fillWidth: true
                    text: root.currentPage === AudioSettingsSection.Applications ?
                    //% "Application mixer"
                    qsTrId("settings.audio.navigation.mixer") : root.currentPage === AudioSettingsSection.Advanced ?
                    //% "Advanced"
                    qsTrId("settings.audio.navigation.advanced") : root.currentPage === AudioSettingsSection.Device ? (root.deviceEndpoint ? root.deviceEndpoint.label : qsTr("Unavailable device")) : qsTr("Audio")
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                    color: root.textColor
                    font.family: root.fontFamily
                    font.pixelSize: 22
                    font.bold: true
                }

                Label {
                    objectName: "audioPresentationDescription"
                    Layout.fillWidth: true
                    text: root.currentPage === AudioSettingsSection.Devices ?
                    //% "Choose where sound plays and which microphone is used."
                    qsTrId("settings.audio.navigation.devices-description") : root.currentPage === AudioSettingsSection.Device ? (root.deviceDirection === "output" ? qsTr("Output") : qsTr("Input")) : root.currentPage === AudioSettingsSection.Applications ?
                    //% "Adjust active streams without changing saved routing rules."
                    qsTrId("settings.audio.navigation.mixer-description") :
                    //% "Saved routing, signal paths and device-specific controls."
                    qsTrId("settings.audio.navigation.advanced-description")
                    color: root.secondaryTextColor
                    font.family: root.fontFamily
                    wrapMode: Text.WordWrap
                }
            }

            AudioButton {
                objectName: "audioRefreshButton"
                presentation: root
                text: qsTr("Refresh")
                emphasized: true
                enabled: !root.navigationBlocked
                onClicked: if (enabled)
                    root.backend.loadAudio()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: root.presentationSpacing
            AudioButton {
                presentation: root
                objectName: "audioNavigateDevices"
                //% "Devices"
                text: qsTrId("settings.audio.navigation.devices")
                emphasized: root.currentPage === AudioSettingsSection.Devices
                enabled: !root.navigationBlocked
                onClicked: root.showPage(AudioSettingsSection.Devices)
            }
            AudioButton {
                presentation: root
                objectName: "audioNavigateMixer"
                //% "Application mixer"
                text: qsTrId("settings.audio.navigation.mixer")
                emphasized: root.currentPage === AudioSettingsSection.Applications
                enabled: !root.navigationBlocked
                onClicked: root.showPage(AudioSettingsSection.Applications)
            }
            AudioButton {
                presentation: root
                objectName: "audioNavigateAdvanced"
                //% "Advanced"
                text: qsTrId("settings.audio.navigation.advanced")
                emphasized: root.currentPage === AudioSettingsSection.Advanced
                enabled: !root.navigationBlocked
                onClicked: root.showPage(AudioSettingsSection.Advanced)
            }
            Item {
                Layout.fillWidth: true
            }
        }

        AudioButton {
            presentation: root
            objectName: "audioDeviceBack"
            visible: root.currentPage === AudioSettingsSection.Device
            //% "Back to devices"
            text: qsTrId("settings.audio.navigation.back")
            enabled: !root.navigationBlocked
            onClicked: root.showPage(AudioSettingsSection.Devices)
        }

        BusyIndicator {
            visible: root.backend ? root.backend.audioBusy : false
            running: visible
            palette.highlight: root.accentColor
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            Layout.fillWidth: true
            visible: root.backend && root.statusText(root.backend.audioStatusId || "") !== ""
            text: root.backend ? root.statusText(root.backend.audioStatusId || "") : ""
            color: root.accentColor
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: root.backend && root.errorText(root.backend.audioErrorId || "") !== ""
            text: root.backend ? root.errorText(root.backend.audioErrorId || "") : ""
            color: root.urgentColor
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: root.loaded && root.backend && !root.backend.audioBusy && !root.backend.audioAvailable
            text: root.backend && root.backend.audioReason ? qsTr("Audio unavailable: %1").arg(root.backend.audioReason) : qsTr("Audio unavailable")
            wrapMode: Text.WordWrap
        }

        ScrollView {
            id: audioScroll
            objectName: "audioSettingsContent"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.backend ? root.backend.audioAvailable : false
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 14

                Label {
                    Layout.fillWidth: true
                    text: root.controlBoundaryText()
                    wrapMode: Text.WordWrap
                    color: root.secondaryTextColor
                }

                AudioDeviceDetail {
                    section: root
                    Layout.fillWidth: true
                    visible: root.currentPage === AudioSettingsSection.Device
                }

                AudioAdvancedGoXLR {
                    section: root
                    Layout.fillWidth: true
                    visible: root.currentPage === AudioSettingsSection.Advanced
                }

                AudioDeviceList {
                    section: root
                    Layout.fillWidth: true
                    visible: root.currentPage === AudioSettingsSection.Devices || root.currentPage === AudioSettingsSection.Advanced
                }

                AudioButton {
                    presentation: root
                    text: qsTr("Complete mixer…")
                    visible: root.currentPage === AudioSettingsSection.Devices
                    enabled: !root.navigationBlocked
                    onClicked: if (enabled)
                        root.backend.openGoxlrMixer()
                }

                AudioApplicationMixer {
                    section: root
                    Layout.fillWidth: true
                    visible: root.currentPage === AudioSettingsSection.Applications
                }

                AudioAdvancedRouting {
                    section: root
                    Layout.fillWidth: true
                    visible: root.currentPage === AudioSettingsSection.Advanced
                }

                AudioPortList {
                    section: root
                    Layout.fillWidth: true
                    visible: root.currentPage === AudioSettingsSection.Advanced || root.currentPage === AudioSettingsSection.Device
                }
            }
        }
    }
}
