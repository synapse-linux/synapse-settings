// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Dialog {
    id: control
    required property var presentation
    //% "Confirm"
    property string acceptText: qsTrId("settings.audio.dialog.confirm")
    font.family: presentation.fontFamily
    font.pixelSize: 12
    padding: 16
    spacing: 12
    closePolicy: Popup.CloseOnEscape
    palette.window: presentation.surfaceColor
    palette.windowText: presentation.textColor
    palette.text: presentation.textColor
    palette.button: presentation.surfaceColor
    palette.buttonText: presentation.textColor
    palette.base: presentation.surfaceColor
    palette.highlight: presentation.accentColor
    palette.highlightedText: presentation.onAccentColor

    background: Rectangle {
        color: control.presentation.surfaceColor
        radius: control.presentation.radius
        border.color: control.presentation.borderColor
    }
    header: Label {
        text: control.title
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        color: control.presentation.textColor
        font.family: control.presentation.fontFamily
        font.pixelSize: 16
        font.bold: true
        padding: 16
        Accessible.role: Accessible.Heading
    }
    footer: DialogButtonBox {
        background: null
        padding: 16
        spacing: control.presentation.presentationSpacing
        alignment: Qt.AlignRight
        delegate: AudioButton {
            id: response
            presentation: control.presentation
            emphasized: response.DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
        }
    }
    Connections {
        target: control
        function onOpened() {
            const accept = control.standardButton(Dialog.Ok)
            const cancel = control.standardButton(Dialog.Cancel)
            if (accept)
                accept.text = Qt.binding(() => control.acceptText)
            if (cancel) {
                //% "Cancel"
                cancel.text = Qt.binding(() => qsTrId("settings.audio.dialog.cancel"))
            }
        }
    }
}
