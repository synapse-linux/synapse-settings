// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Retained view; navigation, confirmations and shared state belong to section.
ColumnLayout {
    id: page
    required property AudioSettingsSection section
    AudioSectionHeading {
        objectName: "audioHeadingRouting"
        Layout.fillWidth: true
        presentation: page.section
        text: qsTranslate("AudioSettingsSection", "Per-application routing")
    }
    AudioCard {
        presentation: page.section
        Layout.fillWidth: true
        RowLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                text: qsTranslate("AudioSettingsSection", "New-stream Audio broker")
                font.bold: true
            }
            Label {
                text: page.section.brokerStateText()
                color: page.section.routeBrokerActive ? page.section.accentColor : page.section.textColor
            }
        }
    }
    Label {
        Layout.fillWidth: true
        text: page.section.brokerDetailText()
        wrapMode: Text.WordWrap
        color: page.section.secondaryTextColor
    }
    Label {
        Layout.fillWidth: true
        text: page.section.existingStreamBoundaryText()
        wrapMode: Text.WordWrap
        color: page.section.secondaryTextColor
    }
    Label {
        visible: page.section.routeRules.length === 0
        text: qsTranslate("AudioSettingsSection", "No process or path rules")
        color: page.section.secondaryTextColor
    }
    Repeater {
        model: page.section.routeRules
        delegate: AudioCard {
            id: routeRow
            presentation: page.section
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
                        text: (routeRow.modelData.matchType === "executable" ? qsTranslate("AudioSettingsSection", "Executable") : qsTranslate("AudioSettingsSection", "Directory")) + " · " + (routeRow.modelData.direction === "output" ? qsTranslate("AudioSettingsSection", "Output") : qsTranslate("AudioSettingsSection", "Input"))
                        color: page.section.secondaryTextColor
                    }
                }
                Label {
                    text: routeRow.modelData.deviceAvailable === false ? qsTranslate("AudioSettingsSection", "Unavailable device") : routeRow.modelData.deviceLabel
                    elide: Text.ElideRight
                }
                AudioButton {
                    presentation: page.section
                    text: qsTranslate("AudioSettingsSection", "Remove")
                    enabled: !page.section.backendBusy
                    onClicked: if (enabled && visible)
                        page.section.removeRouteRule(routeRow.modelData.id)
                }
            }
        }
    }

    AudioSectionHeading {
        objectName: "audioHeadingProfilesPorts"
        Layout.fillWidth: true
        presentation: page.section
        text: qsTranslate("AudioSettingsSection", "Profiles and ports")
    }
    Label {
        Layout.fillWidth: true
        text: page.section.profilePortBoundaryText()
        wrapMode: Text.WordWrap
        color: page.section.secondaryTextColor
    }
    Label {
        Layout.fillWidth: true
        visible: !page.section.profilePortAvailable
        text: page.section.profilePortUnavailableText()
        wrapMode: Text.WordWrap
        color: page.section.secondaryTextColor
    }
    Repeater {
        model: page.section.profileCards
        delegate: AudioCard {
            id: profileCardRow
            presentation: page.section
            required property var modelData
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: page.section.selectionTargetDisplayLabel(profileCardRow.modelData.label || "", "profile")
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: profileCardRow.modelData.activeProfile ? page.section.selectionDisplayLabel(profileCardRow.modelData.activeProfileLabel || "", "profile") : qsTranslate("AudioSettingsSection", "No active profile")
                        color: page.section.secondaryTextColor
                    }
                }
                AudioComboBox {
                    id: profileChooser
                    presentation: page.section
                    onPopupOpened: popupItem => page.section.popupOpened(popupItem)
                    onPopupClosed: popupItem => page.section.popupClosed(popupItem)
                    objectName: "audioProfileChooser-" + profileCardRow.modelData.id
                    Layout.fillWidth: true
                    model: profileCardRow.modelData.profiles || []
                    textRole: "label"
                    valueRole: "id"
                    enabled: profileCardRow.modelData.mutationAvailable && !page.section.backendBusy && !page.section.backend.audioSelectionConfirmationOpen
                    function activeIndex() {
                        const options = profileCardRow.modelData.profiles || []
                        for (let index = 0; index < options.length; ++index) {
                            if (options[index].id === profileCardRow.modelData.activeProfile)
                                return index
                        }
                        return -1
                    }
                    currentIndex: activeIndex()
                    displayText: currentIndex >= 0 && model[currentIndex] ? page.section.selectionDisplayLabel(model[currentIndex].label || "", "profile") : qsTranslate("AudioSettingsSection", "No active profile")
                    delegate: ItemDelegate {
                        required property var modelData
                        width: profileChooser.width
                        text: page.section.selectionDisplayLabel(modelData.label || "", "profile")
                        enabled: modelData.availability !== "unavailable"
                    }
                    onActivated: function (index) {
                        const option = model[index]
                        currentIndex = Qt.binding(function () {
                            return profileChooser.activeIndex()
                        })
                        if (enabled && visible && page.section.active && page.section.backend && option && option.availability !== "unavailable" && option.id !== profileCardRow.modelData.activeProfile)
                            page.section.backend.planAudioProfile(profileCardRow.modelData.id, option.id)
                    }
                }
            }
        }
    }
}
