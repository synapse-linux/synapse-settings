// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var backend
    property bool loaded: false
    property string pendingDirection: ""
    property string pendingDevice: ""
    property string selectedProcessStream: ""
    readonly property var outputs: backend ? backend.audioOutputs || [] : []
    readonly property var inputs: backend ? backend.audioInputs || [] : []
    readonly property var streams: backend ? backend.audioStreams || [] : []
    readonly property var cards: backend ? backend.audioCards || [] : []
    readonly property var routeRules: backend ? backend.audioRouteRules || [] : []
    readonly property bool routeBrokerAvailable: backend ? backend.audioRouteBrokerAvailable : false
    readonly property bool routeBrokerActive: backend ? backend.audioRouteBrokerActive : false
    readonly property string routeBrokerReason: backend ? backend.audioRouteBrokerReason || "" : ""
    readonly property bool routeEnforcementAvailable: backend ? backend.audioRouteEnforcementAvailable : false

    function activate() {
        if (loaded)
            return
        loaded = true
        if (backend)
            backend.loadAudio()
    }

    function requestDefault(direction, device) {
        pendingDirection = direction
        pendingDevice = device
        confirmDefault.open()
    }

    function applyPendingDefault() {
        if (pendingDirection === "" || pendingDevice === "" || !backend)
            return false
        backend.setAudioDefault(pendingDirection, pendingDevice)
        pendingDirection = ""
        pendingDevice = ""
        return true
    }

    function chooseProcessRule(direction, device) {
        if (backend)
            backend.chooseAudioProcessRule(direction, device)
    }

    function chooseExecutableRule(direction, device) {
        if (backend)
            backend.chooseAudioExecutableRule(direction, device)
    }

    function chooseDirectoryRule(direction, device) {
        if (backend)
            backend.chooseAudioDirectoryRule(direction, device)
    }

    function removeRouteRule(ruleId) {
        if (backend)
            backend.removeAudioRouteRule(ruleId)
    }

    function statusText(statusId) {
        switch (statusId) {
        case "audio-loaded": return qsTr("Audio devices updated.")
        case "audio-default-applied": return qsTr("Default Audio device changed and verified.")
        case "audio-default-unchanged": return qsTr("The selected device was already the default.")
        case "audio-route-rule-saved": return qsTr("Application Audio rule saved.")
        case "audio-route-rule-unchanged": return qsTr("The Application Audio rule was already present.")
        case "audio-route-rule-removed": return qsTr("Application Audio rule removed.")
        default: return ""
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
        return qsTr("Existing streams are not moved.")
    }

    function errorText(errorId) {
        switch (errorId) {
        case "audio-process-unavailable": return qsTr("No eligible active Audio process is available.")
        case "selection-invalid": return qsTr("The selected Audio item is no longer available.")
        case "path-invalid": return qsTr("The selected path is invalid.")
        case "timeout": return qsTr("The Audio backend did not respond in time.")
        case "contract-invalid": return qsTr("The Audio backend returned an invalid contract.")
        case "backend-unavailable":
        case "start-failed": return qsTr("The Audio backend is unavailable.")
        case "backend-failed":
        case "default-plan-failed":
        case "default-apply-failed":
        case "route-policy-failed":
        case "broker-status-unavailable":
        case "policy-unavailable": return qsTr("The Audio operation failed safely.")
        case "output-too-large":
        case "process-crashed": return qsTr("The Audio backend response was rejected.")
        default: return errorId === "" ? "" : qsTr("Audio error: %1").arg(errorId)
        }
    }

    onVisibleChanged: if (visible) activate()
    Component.onCompleted: if (visible) activate()

    Connections {
        target: root.backend
        function onAudioProcessChoiceRequested() {
            root.selectedProcessStream = ""
            processDialog.open()
        }
    }

    Dialog {
        id: confirmDefault
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

    Dialog {
        id: processDialog
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
            if (root.backend && root.selectedProcessStream !== "")
                root.backend.confirmAudioProcessRule(root.selectedProcessStream)
        }
        onRejected: if (root.backend) root.backend.cancelAudioProcessRule()

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("The rule will follow the canonical executable, never the PID.")
                wrapMode: Text.WordWrap
                opacity: 0.7
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

        Label {
            text: qsTr("Audio")
            font.pixelSize: 22
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Devices and streams are published by the typed backend.")
            wrapMode: Text.WordWrap
            opacity: 0.7
        }

        BusyIndicator {
            visible: root.backend ? root.backend.audioBusy : false
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            Layout.fillWidth: true
            visible: root.backend && root.statusText(root.backend.audioStatusId || "") !== ""
            text: root.backend ? root.statusText(root.backend.audioStatusId || "") : ""
            color: palette.highlight
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: root.backend && root.errorText(root.backend.audioErrorId || "") !== ""
            text: root.backend ? root.errorText(root.backend.audioErrorId || "") : ""
            color: palette.brightText
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: root.loaded && root.backend && !root.backend.audioBusy && !root.backend.audioAvailable
            text: root.backend && root.backend.audioReason ? qsTr("Audio unavailable: %1").arg(root.backend.audioReason) : qsTr("Audio unavailable")
            wrapMode: Text.WordWrap
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.backend ? root.backend.audioAvailable : false
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 14

                Label { text: qsTr("Outputs"); font.bold: true }
                Repeater {
                    model: root.outputs
                    delegate: Frame {
                        id: outputRow
                        required property var modelData
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            Label {
                                Layout.fillWidth: true
                                text: outputRow.modelData.label
                                elide: Text.ElideRight
                            }
                            Label { text: outputRow.modelData.muted ? qsTr("Muted") : outputRow.modelData.volumePercent + "%" }
                            Button {
                                text: outputRow.modelData.default ? qsTr("Default") : qsTr("Set")
                                enabled: !outputRow.modelData.default && !root.backend.audioBusy
                                onClicked: root.requestDefault("output", outputRow.modelData.id)
                            }
                            Button {
                                text: qsTr("Route…")
                                enabled: !root.backend.audioBusy
                                onClicked: outputRouteMenu.open()
                                Menu {
                                    id: outputRouteMenu
                                    MenuItem {
                                        text: qsTr("Active process…")
                                        onTriggered: root.chooseProcessRule("output", outputRow.modelData.id)
                                    }
                                    MenuItem {
                                        text: qsTr("Executable…")
                                        onTriggered: root.chooseExecutableRule("output", outputRow.modelData.id)
                                    }
                                    MenuItem {
                                        text: qsTr("Directory…")
                                        onTriggered: root.chooseDirectoryRule("output", outputRow.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }

                Label { text: qsTr("Inputs"); font.bold: true }
                Repeater {
                    model: root.inputs
                    delegate: Frame {
                        id: inputRow
                        required property var modelData
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            Label {
                                Layout.fillWidth: true
                                text: inputRow.modelData.label
                                elide: Text.ElideRight
                            }
                            Label { text: inputRow.modelData.muted ? qsTr("Muted") : inputRow.modelData.volumePercent + "%" }
                            Button {
                                text: inputRow.modelData.default ? qsTr("Default") : qsTr("Set")
                                enabled: !inputRow.modelData.default && !root.backend.audioBusy
                                onClicked: root.requestDefault("input", inputRow.modelData.id)
                            }
                            Button {
                                text: qsTr("Route…")
                                enabled: !root.backend.audioBusy
                                onClicked: inputRouteMenu.open()
                                Menu {
                                    id: inputRouteMenu
                                    MenuItem {
                                        text: qsTr("Active process…")
                                        onTriggered: root.chooseProcessRule("input", inputRow.modelData.id)
                                    }
                                    MenuItem {
                                        text: qsTr("Executable…")
                                        onTriggered: root.chooseExecutableRule("input", inputRow.modelData.id)
                                    }
                                    MenuItem {
                                        text: qsTr("Directory…")
                                        onTriggered: root.chooseDirectoryRule("input", inputRow.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }

                Label { text: qsTr("Application streams"); font.bold: true }
                Label {
                    visible: root.streams.length === 0
                    text: qsTr("No active streams")
                    opacity: 0.7
                }
                Repeater {
                    model: root.streams
                    delegate: Frame {
                        id: streamRow
                        required property var modelData
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            Label { Layout.fillWidth: true; text: streamRow.modelData.label; elide: Text.ElideRight }
                            Label { text: streamRow.modelData.direction === "playback" ? qsTr("Playback") : qsTr("Recording") }
                            Label { text: streamRow.modelData.volumePercent + "%" }
                        }
                    }
                }

                Label { text: qsTr("Per-application routing"); font.bold: true }
                Frame {
                    Layout.fillWidth: true
                    RowLayout {
                        anchors.fill: parent
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("New-stream Audio broker")
                            font.bold: true
                        }
                        Label {
                            text: root.brokerStateText()
                            color: root.routeBrokerActive ? palette.highlight : palette.text
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: root.brokerDetailText()
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                }
                Label {
                    Layout.fillWidth: true
                    text: root.existingStreamBoundaryText()
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                }
                Label {
                    visible: root.routeRules.length === 0
                    text: qsTr("No process or path rules")
                    opacity: 0.7
                }
                Repeater {
                    model: root.routeRules
                    delegate: Frame {
                        id: routeRow
                        required property var modelData
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: routeRow.modelData.displayPath
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    text: (routeRow.modelData.matchType === "executable" ? qsTr("Executable") : qsTr("Directory"))
                                          + " · " + (routeRow.modelData.direction === "output" ? qsTr("Output") : qsTr("Input"))
                                    opacity: 0.7
                                }
                            }
                            Label {
                                text: routeRow.modelData.deviceAvailable === false ? qsTr("Unavailable device") : routeRow.modelData.deviceLabel
                                elide: Text.ElideRight
                            }
                            Button {
                                text: qsTr("Remove")
                                enabled: !root.backend.audioBusy
                                onClicked: root.removeRouteRule(routeRow.modelData.id)
                            }
                        }
                    }
                }

                Label { text: qsTr("Cards and profiles"); font.bold: true }
                Repeater {
                    model: root.cards
                    delegate: Frame {
                        id: cardRow
                        required property var modelData
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            Label { Layout.fillWidth: true; text: cardRow.modelData.label; elide: Text.ElideRight }
                            Label { text: cardRow.modelData.activeProfile }
                        }
                    }
                }
            }
        }
    }
}
