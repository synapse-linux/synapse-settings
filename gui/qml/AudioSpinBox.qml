// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

SpinBox {
    id: control
    required property var presentation
    implicitHeight: 38
    font.family: presentation.fontFamily
    font.pixelSize: 12
    leftPadding: 42
    rightPadding: 42
    wheelEnabled: false
    validator: IntValidator {
        bottom: control.from
        top: control.to
    }

    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        font: control.font
        color: control.presentation.textColor
        selectionColor: control.presentation.accentColor
        selectedTextColor: control.presentation.onAccentColor
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhDigitsOnly
    }
    background: Rectangle {
        color: control.presentation.surfaceColor
        radius: control.presentation.radius
        border.color: control.activeFocus ? control.presentation.accentColor : control.presentation.borderColor
    }
    up.indicator: Rectangle {
        x: control.width - width
        width: 38
        height: control.height
        radius: control.presentation.radius
        color: control.up.pressed ? control.presentation.accentSoftColor : control.presentation.surfaceHoverColor
        border.color: control.presentation.borderColor
        Text {
            anchors.centerIn: parent
            text: "+"
            font: control.font
            color: control.up.indicator.enabled ? control.presentation.textColor : control.presentation.secondaryTextColor
        }
    }
    down.indicator: Rectangle {
        width: 38
        height: control.height
        radius: control.presentation.radius
        color: control.down.pressed ? control.presentation.accentSoftColor : control.presentation.surfaceHoverColor
        border.color: control.presentation.borderColor
        Text {
            anchors.centerIn: parent
            text: "−"
            font: control.font
            color: control.down.indicator.enabled ? control.presentation.textColor : control.presentation.secondaryTextColor
        }
    }
}
