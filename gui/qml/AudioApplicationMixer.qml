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
        objectName: "audioHeadingStreams"
        Layout.fillWidth: true
        presentation: page.section
        text: qsTranslate("AudioSettingsSection", "Application streams")
    }
    Label {
        visible: page.section.streams.length === 0
        text: qsTranslate("AudioSettingsSection", "No active streams")
        color: page.section.secondaryTextColor
    }
    Repeater {
        model: page.section.streams
        delegate: AudioCard {
            id: streamRow
            presentation: page.section
            required property var modelData
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: streamRow.modelData.label
                    elide: Text.ElideRight
                }
                Label {
                    text: streamRow.modelData.direction === "playback" ? qsTranslate("AudioSettingsSection", "Playback") : qsTranslate("AudioSettingsSection", "Recording")
                }
                Label {
                    Layout.maximumWidth: 180
                    text: page.section.endpointLabel(streamRow.modelData.target)
                    elide: Text.ElideRight
                }
                Label {
                    text: streamRow.modelData.muted ? qsTranslate("AudioSettingsSection", "Muted") : streamRow.modelData.volumePercent + "%"
                }
                AudioButton {
                    presentation: page.section
                    text: qsTranslate("AudioSettingsSection", "Level…")
                    enabled: streamRow.modelData.levelControlAvailable && !page.section.backendBusy
                    onClicked: if (enabled && visible)
                        streamLevelMenu.open()
                    Menu {
                        id: streamLevelMenu
                        onAboutToShow: page.section.popupOpened(streamLevelMenu)
                        onClosed: page.section.popupClosed(streamLevelMenu)
                        Component.onDestruction: page.section.popupClosed(streamLevelMenu)
                        MenuItem {
                            text: qsTranslate("AudioSettingsSection", "Volume…")
                            onTriggered: if (enabled && streamRow.visible)
                                page.section.requestVolume(streamRow.modelData.id, streamRow.modelData.label, streamRow.modelData.volumePercent)
                        }
                        MenuItem {
                            text: streamRow.modelData.muted ? qsTranslate("AudioSettingsSection", "Unmute…") : qsTranslate("AudioSettingsSection", "Mute…")
                            onTriggered: if (enabled && streamRow.visible)
                                page.section.requestMute(streamRow.modelData.id, streamRow.modelData.label, streamRow.modelData.muted)
                        }
                    }
                }
                AudioButton {
                    presentation: page.section
                    text: qsTranslate("AudioSettingsSection", "Move…")
                    enabled: streamRow.modelData.moveAvailable && !page.section.backendBusy
                    onClicked: if (enabled && visible)
                        page.section.requestStreamMove(streamRow.modelData.id, streamRow.modelData.direction, streamRow.modelData.target)
                }
            }
        }
    }
}
