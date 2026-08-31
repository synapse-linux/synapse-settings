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
    readonly property var outputs: backend ? backend.audioOutputs || [] : []
    readonly property var inputs: backend ? backend.audioInputs || [] : []
    readonly property var streams: backend ? backend.audioStreams || [] : []
    readonly property var cards: backend ? backend.audioCards || [] : []

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
        if (pendingDirection === "" || pendingDevice === "")
            return false
        if (!backend)
            return false
        backend.setAudioDefault(pendingDirection, pendingDevice)
        pendingDirection = ""
        pendingDevice = ""
        return true
    }

    onVisibleChanged: if (visible) activate()
    Component.onCompleted: if (visible) activate()

    Dialog {
        id: confirmDefault
        title: qsTr("Cambia dispositivo predefinito")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.applyPendingDefault()
        onRejected: {
            root.pendingDirection = ""
            root.pendingDevice = ""
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
            text: qsTr("Dispositivi e stream sono pubblicati dal backend tipizzato.")
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
            visible: root.loaded && root.backend && !root.backend.audioBusy && !root.backend.audioAvailable
            text: root.backend && root.backend.audioReason ? qsTr("Audio non disponibile: %1").arg(root.backend.audioReason) : qsTr("Audio non disponibile")
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

                Label { text: qsTr("Uscite"); font.bold: true }
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
                            Label { text: outputRow.modelData.muted ? qsTr("Muto") : outputRow.modelData.volumePercent + "%" }
                            Button {
                                text: outputRow.modelData.default ? qsTr("Predefinita") : qsTr("Imposta")
                                enabled: !outputRow.modelData.default && !root.backend.audioBusy
                                onClicked: root.requestDefault("output", outputRow.modelData.id)
                            }
                        }
                    }
                }

                Label { text: qsTr("Ingressi"); font.bold: true }
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
                            Label { text: inputRow.modelData.muted ? qsTr("Muto") : inputRow.modelData.volumePercent + "%" }
                            Button {
                                text: inputRow.modelData.default ? qsTr("Predefinito") : qsTr("Imposta")
                                enabled: !inputRow.modelData.default && !root.backend.audioBusy
                                onClicked: root.requestDefault("input", inputRow.modelData.id)
                            }
                        }
                    }
                }

                Label { text: qsTr("Stream applicazioni"); font.bold: true }
                Label {
                    visible: root.streams.length === 0
                    text: qsTr("Nessuno stream attivo")
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
                            Label { text: streamRow.modelData.direction === "playback" ? qsTr("Riproduzione") : qsTr("Registrazione") }
                            Label { text: streamRow.modelData.volumePercent + "%" }
                        }
                    }
                }

                Label { text: qsTr("Schede e profili"); font.bold: true }
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
