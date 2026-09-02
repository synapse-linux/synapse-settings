// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    required property var backend
    width: 980
    height: 700
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: qsTr("Synapse Settings — Audio")
    color: palette.window
    LayoutMirroring.enabled: Application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            Label {
                Layout.fillWidth: true
                text: qsTr("Settings")
                font.bold: true
                font.pixelSize: 18
            }
            Label {
                text: qsTr("Audio")
                opacity: 0.7
            }
        }
    }

    AudioSettings {
        anchors.fill: parent
        anchors.margins: 18
        backend: window.backend
    }
}
