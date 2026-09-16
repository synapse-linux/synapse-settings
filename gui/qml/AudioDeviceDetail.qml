// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Retained view; navigation, confirmations and shared state belong to section.
AudioCard {
    id: page
    required property AudioSettingsSection section
    presentation: page.section
    objectName: "audioDeviceDetails"
    ColumnLayout {
        anchors.fill: parent
        Label {
            Layout.fillWidth: true
            text: page.section.deviceAvailable ? (page.section.deviceEndpoint.muted ? qsTranslate("AudioSettingsSection", "Muted") : page.section.deviceEndpoint.volumePercent + "%") : qsTranslate("AudioSettingsSection", "Unavailable device")
            wrapMode: Text.WordWrap
        }
        Flow {
            Layout.fillWidth: true
            spacing: page.section.presentationSpacing
            AudioButton {
                presentation: page.section
                objectName: "audioDeviceVolume"
                text: qsTranslate("AudioSettingsSection", "Volume…")
                enabled: page.section.deviceAvailable && !page.section.navigationBlocked && page.section.deviceEndpoint.levelControlAvailable !== false
                onClicked: if (enabled)
                    page.section.requestVolume(page.section.deviceId, page.section.deviceEndpoint.label, page.section.deviceEndpoint.volumePercent)
            }
            AudioButton {
                presentation: page.section
                objectName: "audioDeviceMute"
                text: page.section.deviceEndpoint && page.section.deviceEndpoint.muted ? qsTranslate("AudioSettingsSection", "Unmute…") : qsTranslate("AudioSettingsSection", "Mute…")
                enabled: page.section.deviceAvailable && !page.section.navigationBlocked && page.section.deviceEndpoint.levelControlAvailable !== false
                onClicked: if (enabled)
                    page.section.requestMute(page.section.deviceId, page.section.deviceEndpoint.label, page.section.deviceEndpoint.muted)
            }
            AudioButton {
                presentation: page.section
                objectName: "audioDeviceDefault"
                text: page.section.deviceEndpoint && page.section.deviceEndpoint.default ? qsTranslate("AudioSettingsSection", "Default") : qsTranslate("AudioSettingsSection", "Set")
                enabled: page.section.deviceAvailable && !page.section.navigationBlocked && !page.section.deviceEndpoint.default
                onClicked: if (enabled)
                    page.section.requestDefault(page.section.deviceDirection, page.section.deviceId)
            }
        }
    }
}
