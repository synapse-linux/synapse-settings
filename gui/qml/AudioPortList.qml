// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Retained view; navigation, confirmations and shared state belong to section.
ColumnLayout {
    id: page
    required property AudioSettingsSection section
    Label {
        Layout.fillWidth: true
        visible: page.section.currentPage === AudioSettingsSection.Device
        text: page.section.profilePortBoundaryText()
        wrapMode: Text.WordWrap
        color: page.section.secondaryTextColor
    }
    Repeater {
        model: page.section.portEndpoints
        delegate: AudioCard {
            id: portEndpointRow
            presentation: page.section
            required property var modelData
            Layout.fillWidth: true
            visible: page.section.currentPage === AudioSettingsSection.Advanced || (page.section.deviceAvailable && modelData.id === page.section.deviceId && modelData.direction === page.section.deviceDirection)
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: page.section.selectionTargetDisplayLabel(portEndpointRow.modelData.label || "", "port")
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: portEndpointRow.modelData.direction === "output" ? qsTranslate("AudioSettingsSection", "Output port") : qsTranslate("AudioSettingsSection", "Input port")
                        color: page.section.secondaryTextColor
                    }
                    Label {
                        text: portEndpointRow.modelData.activePort ? page.section.selectionDisplayLabel(portEndpointRow.modelData.activePortLabel || "", "port") : qsTranslate("AudioSettingsSection", "No active port")
                        color: page.section.secondaryTextColor
                    }
                }
                AudioComboBox {
                    id: portChooser
                    presentation: page.section
                    onPopupOpened: popupItem => page.section.popupOpened(popupItem)
                    onPopupClosed: popupItem => page.section.popupClosed(popupItem)
                    objectName: "audioPortChooser-" + portEndpointRow.modelData.id
                    Layout.fillWidth: true
                    model: portEndpointRow.modelData.ports || []
                    textRole: "label"
                    valueRole: "id"
                    enabled: portEndpointRow.modelData.mutationAvailable && !page.section.backendBusy && !page.section.backend.audioSelectionConfirmationOpen
                    function activeIndex() {
                        const options = portEndpointRow.modelData.ports || []
                        for (let index = 0; index < options.length; ++index) {
                            if (options[index].id === portEndpointRow.modelData.activePort)
                                return index
                        }
                        return -1
                    }
                    currentIndex: activeIndex()
                    displayText: currentIndex >= 0 && model[currentIndex] ? page.section.selectionDisplayLabel(model[currentIndex].label || "", "port") : qsTranslate("AudioSettingsSection", "No active port")
                    delegate: ItemDelegate {
                        required property var modelData
                        width: portChooser.width
                        text: page.section.selectionDisplayLabel(modelData.label || "", "port")
                        enabled: modelData.availability !== "unavailable"
                    }
                    onActivated: function (index) {
                        const option = model[index]
                        currentIndex = Qt.binding(function () {
                            return portChooser.activeIndex()
                        })
                        if (enabled && visible && page.section.active && page.section.backend && option && option.availability !== "unavailable" && option.id !== portEndpointRow.modelData.activePort)
                            page.section.backend.planAudioPort(portEndpointRow.modelData.direction, portEndpointRow.modelData.id, option.id)
                    }
                }
            }
        }
    }
}
