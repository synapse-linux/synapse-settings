// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    required property var presentation
    // Surface owners may observe popup lifetime without acquiring any action
    // authority. Palette changes do not close the popup or consume a choice.
    signal popupOpened(var popupItem)
    signal popupClosed(var popupItem)
    Component.onDestruction: popupClosed(choicesPopup)
    implicitHeight: 36
    leftPadding: 11
    rightPadding: 32
    font.family: presentation.fontFamily
    font.pixelSize: 12
    palette.window: presentation.backgroundColor
    palette.windowText: presentation.textColor
    palette.base: presentation.surfaceColor
    palette.alternateBase: presentation.surfaceHoverColor
    palette.button: presentation.surfaceColor
    palette.buttonText: presentation.textColor
    palette.text: presentation.textColor
    palette.highlight: presentation.accentColor
    palette.highlightedText: presentation.onAccentColor
    palette.mid: presentation.borderColor
    palette.placeholderText: presentation.secondaryTextColor

    contentItem: Text {
        leftPadding: 0
        rightPadding: 0
        text: control.displayText
        font: control.font
        color: control.enabled
               ? control.presentation.textColor
               : control.presentation.secondaryTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Canvas {
        id: indicatorCanvas
        x: control.width - width - 11
        y: (control.height - height) / 2
        width: 12
        height: 8
        contextType: "2d"
        onPaint: {
            context.reset()
            context.beginPath()
            context.moveTo(1, 1)
            context.lineTo(width / 2, height - 1)
            context.lineTo(width - 1, 1)
            context.lineWidth = 1.6
            context.strokeStyle = control.enabled
                                  ? control.presentation.accentColor
                                  : control.presentation.secondaryTextColor
            context.lineCap = "round"
            context.lineJoin = "round"
            context.stroke()
        }
        Connections {
            target: control
            function onEnabledChanged() { indicatorCanvas.requestPaint() }
        }
        Connections {
            target: control.presentation
            function onAccentColorChanged() { indicatorCanvas.requestPaint() }
            function onSecondaryTextColorChanged() {
                indicatorCanvas.requestPaint()
            }
        }
    }

    background: Rectangle {
        radius: control.presentation.radius
        color: control.down
               ? control.presentation.accentSoftColor
               : control.hovered && control.enabled
                 ? control.presentation.surfaceHoverColor
                 : control.presentation.surfaceColor
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus
                      ? control.presentation.accentColor
                      : control.presentation.borderColor
        opacity: control.enabled ? 1.0 : 0.82
    }

    delegate: ItemDelegate {
        id: optionDelegate
        required property var modelData
        width: control.width
        text: control.textRole && modelData
              ? String(modelData[control.textRole] || "")
              : String(modelData || "")
        font: control.font
        palette.text: control.presentation.textColor
        palette.buttonText: control.presentation.textColor
        palette.highlight: control.presentation.accentColor
        palette.highlightedText: control.presentation.onAccentColor
        background: Rectangle {
            color: optionDelegate.highlighted
                   ? control.presentation.accentColor
                   : optionDelegate.hovered
                     ? control.presentation.surfaceHoverColor
                     : control.presentation.surfaceColor
        }
    }

    popup: Popup {
        id: choicesPopup
        onAboutToShow: control.popupOpened(choicesPopup)
        onClosed: control.popupClosed(choicesPopup)
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 300)
        padding: 1
        palette.text: control.presentation.textColor
        palette.highlight: control.presentation.accentColor
        palette.highlightedText: control.presentation.onAccentColor

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: control.presentation.radius
            color: control.presentation.surfaceColor
            border.width: 1
            border.color: control.presentation.borderColor
        }
    }
}
