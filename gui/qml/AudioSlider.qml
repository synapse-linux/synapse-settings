// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Slider {
    id: control

    required property var presentation
    implicitHeight: 30
    palette.highlight: presentation.accentColor
    palette.mid: presentation.borderColor

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 5
        radius: height / 2
        color: control.presentation.borderColor

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled
                   ? control.presentation.accentColor
                   : control.presentation.secondaryTextColor
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 17
        implicitHeight: 17
        radius: width / 2
        color: control.enabled
               ? control.presentation.accentColor
               : control.presentation.secondaryTextColor
        border.width: control.activeFocus ? 3 : 2
        border.color: control.activeFocus
                      ? control.presentation.textColor
                      : control.presentation.surfaceColor
    }
}
