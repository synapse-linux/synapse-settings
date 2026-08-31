// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var backend
    property bool loaded: false
    property string pendingDefaultGpu: ""
    property string pendingRemovalRule: ""
    readonly property var gpus: backend ? backend.graphicsGpus || [] : []
    readonly property var rules: backend ? backend.graphicsRules || [] : []

    function activate() {
        if (loaded)
            return
        loaded = true
        if (backend)
            backend.loadGraphics()
    }

    function applyDefault(gpu) {
        if (!gpu)
            return false
        if (!backend)
            return false
        backend.setGraphicsDefault(gpu)
        return true
    }

    function addRule(matchType, path, gpu) {
        if ((matchType !== "executable" && matchType !== "directory") || !path || !gpu)
            return false
        if (!backend)
            return false
        backend.addGraphicsRule(matchType, path, gpu)
        return true
    }

    function removeRule(rule) {
        if (!rule)
            return false
        if (!backend)
            return false
        backend.removeGraphicsRule(rule)
        return true
    }

    onVisibleChanged: if (visible) activate()
    Component.onCompleted: if (visible) activate()

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: qsTr("Grafica")
            font.pixelSize: 22
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Seleziona la GPU per le nuove applicazioni avviate tramite Synapse. I processi già attivi non possono essere migrati.")
            wrapMode: Text.WordWrap
            opacity: 0.7
        }

        BusyIndicator {
            visible: root.backend ? root.backend.graphicsBusy : false
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 14

                Label { text: qsTr("GPU predefinita per le applicazioni"); font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: defaultGpu
                        Layout.fillWidth: true
                        model: [{ id: "system", label: qsTr("Scelta di sistema") }].concat(root.gpus)
                        textRole: "label"
                        valueRole: "id"
                        Component.onCompleted: {
                            var wanted = root.backend ? root.backend.graphicsDefaultGpu : "system"
                            for (var index = 0; index < count; index++) {
                                if (valueAt(index) === wanted) {
                                    currentIndex = index
                                    break
                                }
                            }
                        }
                    }
                    Button {
                        text: qsTr("Salva")
                        enabled: root.backend && !root.backend.graphicsBusy && defaultGpu.currentValue !== root.backend.graphicsDefaultGpu
                        onClicked: root.applyDefault(defaultGpu.currentValue)
                    }
                }

                Label { text: qsTr("Regola per applicazione o cartella"); font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: matchType
                        model: [
                            { value: "executable", label: qsTr("Eseguibile") },
                            { value: "directory", label: qsTr("Cartella") }
                        ]
                        textRole: "label"
                        valueRole: "value"
                    }
                    TextField {
                        id: rulePath
                        Layout.fillWidth: true
                        placeholderText: matchType.currentValue === "directory" ? qsTr("Cartella Steam o libreria") : qsTr("Percorso eseguibile")
                    }
                    ComboBox {
                        id: ruleGpu
                        model: [{ id: "system", label: qsTr("Sistema") }].concat(root.gpus)
                        textRole: "label"
                        valueRole: "id"
                    }
                    Button {
                        text: qsTr("Aggiungi")
                        enabled: root.backend && rulePath.text.length > 0 && !root.backend.graphicsBusy
                        onClicked: {
                            if (root.addRule(matchType.currentValue, rulePath.text, ruleGpu.currentValue))
                                rulePath.clear()
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.backend ? !root.backend.graphicsEnforcementAvailable : true
                    text: qsTr("Regole salvabili e verificabili; attivazione al lancio non ancora integrata.")
                    wrapMode: Text.WordWrap
                    color: palette.mid
                }

                Label { text: qsTr("Regole"); font.bold: true }
                Label {
                    visible: root.rules.length === 0
                    text: qsTr("Nessuna regola")
                    opacity: 0.7
                }
                Repeater {
                    model: root.rules
                    delegate: Frame {
                        id: ruleRow
                        required property var modelData
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            Label { text: ruleRow.modelData.matchType === "directory" ? qsTr("Cartella") : qsTr("Eseguibile") }
                            Label { Layout.fillWidth: true; text: ruleRow.modelData.displayPath; elide: Text.ElideMiddle }
                            Label { text: ruleRow.modelData.gpuLabel }
                            Button {
                                text: qsTr("Rimuovi")
                                enabled: !root.backend.graphicsBusy
                                onClicked: root.removeRule(ruleRow.modelData.id)
                            }
                        }
                    }
                }

                Label { text: qsTr("GPU rilevate"); font.bold: true }
                Repeater {
                    model: root.gpus
                    delegate: Frame {
                        id: gpuRow
                        required property var modelData
                        Layout.fillWidth: true
                        RowLayout {
                            anchors.fill: parent
                            Label { Layout.fillWidth: true; text: gpuRow.modelData.label; elide: Text.ElideRight }
                            Label { text: gpuRow.modelData.driver }
                            Label { text: gpuRow.modelData.displayOwner ? qsTr("Display") : (gpuRow.modelData.gamingCandidate ? qsTr("Gaming") : qsTr("Render")) }
                        }
                    }
                }
            }
        }
    }
}
