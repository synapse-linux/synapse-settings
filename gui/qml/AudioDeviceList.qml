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
        objectName: "audioHeadingOutputs"
        Layout.fillWidth: true
        presentation: page.section
        text: qsTranslate("AudioSettingsSection", "Outputs")
    }
    Repeater {
        model: page.section.outputs
        delegate: AudioCard {
            id: outputRow
            presentation: page.section
            required property var modelData
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: outputRow.modelData.label
                    elide: Text.ElideRight
                }
                Label {
                    text: outputRow.modelData.muted ? qsTranslate("AudioSettingsSection", "Muted") : outputRow.modelData.volumePercent + "%"
                }
                AudioButton {
                    presentation: page.section
                    text: qsTranslate("AudioSettingsSection", "Level…")
                    enabled: outputRow.modelData.levelControlAvailable !== false && !page.section.backendBusy
                    onClicked: if (enabled && visible)
                        outputLevelMenu.open()
                    Menu {
                        id: outputLevelMenu
                        onAboutToShow: page.section.popupOpened(outputLevelMenu)
                        onClosed: page.section.popupClosed(outputLevelMenu)
                        Component.onDestruction: page.section.popupClosed(outputLevelMenu)
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Volume…")
                            onTriggered: if (enabled && outputRow.visible)
                                page.section.requestVolume(outputRow.modelData.id, outputRow.modelData.label, outputRow.modelData.volumePercent)
                        }
                        MenuItem {
                            text: outputRow.modelData.muted ? qsTranslate("AudioSettingsSection", "Unmute…") : qsTranslate("AudioSettingsSection", "Mute…")
                            onTriggered: if (enabled && outputRow.visible)
                                page.section.requestMute(outputRow.modelData.id, outputRow.modelData.label, outputRow.modelData.muted)
                        }
                    }
                }
                AudioButton {
                    presentation: page.section
                    text: outputRow.modelData.default ? qsTranslate("AudioSettingsSection", "Default") : qsTranslate("AudioSettingsSection", "Set")
                    enabled: !outputRow.modelData.default && !page.section.backendBusy
                    onClicked: if (enabled && visible)
                        page.section.requestDefault("output", outputRow.modelData.id)
                }
                AudioButton {
                    presentation: page.section
                    //% "Details"
                    text: qsTrId("settings.audio.navigation.details")
                    visible: page.section.currentPage === AudioSettingsSection.Devices
                    enabled: !page.section.navigationBlocked
                    onClicked: page.section.openDevice("output", outputRow.modelData.id)
                }
                AudioButton {
                    presentation: page.section
                    text: qsTranslate("AudioSettingsSection", "Route…")
                    visible: page.section.currentPage === AudioSettingsSection.Advanced
                    enabled: !page.section.navigationBlocked
                    onClicked: if (enabled)
                        outputRouteMenu.open()
                    Menu {
                        id: outputRouteMenu
                        onAboutToShow: page.section.popupOpened(outputRouteMenu)
                        onClosed: page.section.popupClosed(outputRouteMenu)
                        Component.onDestruction: page.section.popupClosed(outputRouteMenu)
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Active process…")
                            onTriggered: if (enabled && outputRow.visible)
                                page.section.chooseProcessRule("output", outputRow.modelData.id)
                        }
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Executable…")
                            onTriggered: if (enabled && outputRow.visible)
                                page.section.chooseExecutableRule("output", outputRow.modelData.id)
                        }
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Directory…")
                            onTriggered: if (enabled && outputRow.visible)
                                page.section.chooseDirectoryRule("output", outputRow.modelData.id)
                        }
                    }
                }
            }
        }
    }

    AudioSectionHeading {
        objectName: "audioHeadingInputs"
        Layout.fillWidth: true
        presentation: page.section
        text: qsTranslate("AudioSettingsSection", "Inputs")
    }
    Repeater {
        model: page.section.inputs
        delegate: AudioCard {
            id: inputRow
            presentation: page.section
            required property var modelData
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: inputRow.modelData.label
                    elide: Text.ElideRight
                }
                Label {
                    text: inputRow.modelData.muted ? qsTranslate("AudioSettingsSection", "Muted") : inputRow.modelData.volumePercent + "%"
                }
                AudioButton {
                    presentation: page.section
                    text: qsTranslate("AudioSettingsSection", "Level…")
                    enabled: inputRow.modelData.levelControlAvailable !== false && !page.section.backendBusy
                    onClicked: if (enabled && visible)
                        inputLevelMenu.open()
                    Menu {
                        id: inputLevelMenu
                        onAboutToShow: page.section.popupOpened(inputLevelMenu)
                        onClosed: page.section.popupClosed(inputLevelMenu)
                        Component.onDestruction: page.section.popupClosed(inputLevelMenu)
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Volume…")
                            onTriggered: if (enabled && inputRow.visible)
                                page.section.requestVolume(inputRow.modelData.id, inputRow.modelData.label, inputRow.modelData.volumePercent)
                        }
                        MenuItem {
                            text: inputRow.modelData.muted ? qsTranslate("AudioSettingsSection", "Unmute…") : qsTranslate("AudioSettingsSection", "Mute…")
                            onTriggered: if (enabled && inputRow.visible)
                                page.section.requestMute(inputRow.modelData.id, inputRow.modelData.label, inputRow.modelData.muted)
                        }
                    }
                }
                AudioButton {
                    presentation: page.section
                    text: inputRow.modelData.default ? qsTranslate("AudioSettingsSection", "Default") : qsTranslate("AudioSettingsSection", "Set")
                    enabled: !inputRow.modelData.default && !page.section.backendBusy
                    onClicked: if (enabled && visible)
                        page.section.requestDefault("input", inputRow.modelData.id)
                }
                AudioButton {
                    presentation: page.section
                    //% "Details"
                    text: qsTrId("settings.audio.navigation.details")
                    visible: page.section.currentPage === AudioSettingsSection.Devices
                    enabled: !page.section.navigationBlocked
                    onClicked: page.section.openDevice("input", inputRow.modelData.id)
                }
                AudioButton {
                    presentation: page.section
                    text: qsTranslate("AudioSettingsSection", "Route…")
                    visible: page.section.currentPage === AudioSettingsSection.Advanced
                    enabled: !page.section.navigationBlocked
                    onClicked: if (enabled)
                        inputRouteMenu.open()
                    Menu {
                        id: inputRouteMenu
                        onAboutToShow: page.section.popupOpened(inputRouteMenu)
                        onClosed: page.section.popupClosed(inputRouteMenu)
                        Component.onDestruction: page.section.popupClosed(inputRouteMenu)
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Active process…")
                            onTriggered: if (enabled && inputRow.visible)
                                page.section.chooseProcessRule("input", inputRow.modelData.id)
                        }
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Executable…")
                            onTriggered: if (enabled && inputRow.visible)
                                page.section.chooseExecutableRule("input", inputRow.modelData.id)
                        }
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Directory…")
                            onTriggered: if (enabled && inputRow.visible)
                                page.section.chooseDirectoryRule("input", inputRow.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
