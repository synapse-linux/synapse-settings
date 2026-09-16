// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Label {
    id: control

    required property var presentation
    leftPadding: 11
    topPadding: 3
    bottomPadding: 3
    color: presentation.textColor
    font.family: presentation.fontFamily
    font.pixelSize: 13
    font.bold: true

    background: Rectangle {
        color: "transparent"
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: Math.max(14, parent.height - 6)
            radius: width / 2
            color: control.presentation.accentColor
        }
    }
}
