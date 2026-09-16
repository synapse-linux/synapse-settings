// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Button {
    id: control

    required property var presentation
    property bool emphasized: false

    implicitHeight: 34
    leftPadding: 12
    rightPadding: 12
    topPadding: 7
    bottomPadding: 7
    hoverEnabled: true
    font.family: presentation.fontFamily
    font.pixelSize: 12
    palette.window: presentation.backgroundColor
    palette.windowText: presentation.textColor
    palette.button: presentation.surfaceColor
    palette.buttonText: presentation.textColor
    palette.text: presentation.textColor
    palette.highlight: presentation.accentColor
    palette.highlightedText: presentation.onAccentColor
    palette.mid: presentation.borderColor

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.emphasized && control.enabled
               ? control.presentation.onAccentColor
               : control.enabled
                 ? control.presentation.textColor
                 : control.presentation.secondaryTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: control.presentation.radius
        color: control.emphasized && control.enabled
               ? control.presentation.accentColor
               : control.down
                 ? control.presentation.accentSoftColor
                 : control.hovered && control.enabled
                   ? control.presentation.surfaceHoverColor
                   : control.presentation.surfaceColor
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus || control.emphasized
                      ? control.presentation.accentColor
                      : control.presentation.borderColor
        opacity: control.enabled ? 1.0 : 0.82
    }
}
