// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "settingsAudioWindow"

    required property var backend
    required property var theme
    width: 980
    height: 700
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: qsTr("Synapse Settings — Audio")
    color: theme.background
    font.family: "monospace"
    palette.window: theme.background
    palette.windowText: theme.text
    palette.base: theme.surface
    palette.alternateBase: theme.surfaceHover
    palette.text: theme.text
    palette.button: theme.surface
    palette.buttonText: theme.text
    palette.highlight: theme.accent
    palette.highlightedText: theme.onAccent
    palette.brightText: theme.urgent
    palette.mid: theme.border
    palette.dark: theme.border
    palette.light: theme.surfaceHover
    palette.placeholderText: theme.muted
    LayoutMirroring.enabled: Application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    onClosing: close => {
        if (!audioSurface.requestHide())
            close.accepted = false
    }

    header: Rectangle {
        objectName: "settingsAudioHeader"
        implicitHeight: 56
        color: window.theme.surface
        border.width: 1
        border.color: window.theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 4
                Layout.preferredHeight: 24
                radius: 2
                color: window.theme.accent
            }

            Text {
                objectName: "settingsHeaderTitle"
                Layout.fillWidth: true
                text: qsTr("Settings")
                color: window.theme.text
                font.family: window.font.family
                font.bold: true
                font.pixelSize: 18
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                objectName: "settingsHeaderSection"
                text: qsTr("Audio")
                color: window.theme.text
                font.family: window.font.family
                font.pixelSize: 13
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    AudioSettings {
        id: audioSurface
        objectName: "settingsAudioContent"
        anchors.fill: parent
        anchors.margins: 18
        backend: window.backend
        backgroundColor: window.theme.background
        surfaceColor: window.theme.surface
        surfaceHoverColor: window.theme.surfaceHover
        borderColor: window.theme.border
        accentColor: window.theme.accent
        textColor: window.theme.text
        mutedColor: window.theme.muted
        urgentColor: window.theme.urgent
        fontFamily: window.font.family
        radius: 8
        presentationSpacing: 8
    }
}
