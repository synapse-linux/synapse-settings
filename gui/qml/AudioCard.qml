// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Frame {
    id: control

    required property var presentation
    padding: 12

    background: Rectangle {
        radius: control.presentation.radius
        color: control.presentation.surfaceColor
        border.width: 1
        border.color: control.presentation.borderColor
    }
}
