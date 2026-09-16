// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Retained view; navigation, confirmations and shared state belong to section.
ColumnLayout {
    id: page
    required property AudioSettingsSection section
    AudioSectionHeading {
        objectName: "audioHeadingGoxlr"
        Layout.fillWidth: true
        presentation: page.section
        text: qsTranslate("AudioSettingsSection", "GoXLR provider")
    }
    AudioCard {
        objectName: "audioGoxlrSummaryCard"
        presentation: page.section
        Layout.fillWidth: true
        RowLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                text: qsTranslate("AudioSettingsSection", "Typed provider status")
                font.bold: true
            }
            Label {
                text: page.section.goxlrStateText()
                color: page.section.goxlrProviderActive ? page.section.accentColor : page.section.textColor
            }
            AudioButton {
                presentation: page.section
                text: qsTranslate("AudioSettingsSection", "Complete mixer…")
                enabled: !page.section.navigationBlocked
                onClicked: if (enabled)
                    page.section.backend.openGoxlrMixer()
            }
        }
    }
    Label {
        Layout.fillWidth: true
        text: page.section.goxlrDetailText()
        wrapMode: Text.WordWrap
        color: page.section.secondaryTextColor
    }
    Label {
        Layout.fillWidth: true
        text: page.section.goxlrBoundaryText()
        wrapMode: Text.WordWrap
        color: page.section.secondaryTextColor
    }
    Repeater {
        model: page.section.goxlrReadyDevice ? page.section.goxlrDevices : []
        delegate: AudioCard {
            id: goxlrRow
            presentation: page.section
            required property var modelData
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: goxlrRow.modelData.model
                        font.bold: true
                    }
                    Label {
                        text: goxlrRow.modelData.profileModelReady ? qsTranslate("AudioSettingsSection", "Profile model ready") : qsTranslate("AudioSettingsSection", "Profile model unavailable")
                        color: page.section.secondaryTextColor
                    }
                }
                Repeater {
                    model: goxlrRow.modelData.faders
                    delegate: RowLayout {
                        id: faderControl
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        Label {
                            Layout.preferredWidth: 120
                            text: faderControl.modelData.fader + " · " + faderControl.modelData.channel
                            elide: Text.ElideRight
                        }
                        AudioSlider {
                            presentation: page.section
                            Layout.fillWidth: true
                            from: 0
                            to: 255
                            stepSize: 1
                            value: faderControl.modelData.volume
                            enabled: page.section.goxlrMutationAllowed && faderControl.modelData.volumeAvailable
                            onPressedChanged: {
                                if (!pressed && enabled && Math.round(value) !== faderControl.modelData.volume)
                                    page.section.backend.setAudioGoxlrFaderVolume(faderControl.index, Math.round(value))
                            }
                        }
                        Label {
                            Layout.preferredWidth: 38
                            text: Math.round(faderControl.modelData.volume * 100 / 255) + "%"
                            horizontalAlignment: Text.AlignRight
                        }
                        AudioButton {
                            presentation: page.section
                            text: faderControl.modelData.muted ? qsTranslate("AudioSettingsSection", "Unmute") : qsTranslate("AudioSettingsSection", "Mute")
                            enabled: page.section.goxlrMutationAllowed && faderControl.modelData.muteAvailable
                            onClicked: if (enabled && visible)
                                page.section.backend.setAudioGoxlrFaderMuted(faderControl.index, !faderControl.modelData.muted)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.preferredWidth: 120
                        text: qsTranslate("AudioSettingsSection", "Headphones")
                    }
                    AudioSlider {
                        presentation: page.section
                        Layout.fillWidth: true
                        from: 0
                        to: 255
                        stepSize: 1
                        value: goxlrRow.modelData.outputs.headphonesVolume
                        enabled: page.section.goxlrMutationAllowed && goxlrRow.modelData.outputs.headphonesAvailable
                        onPressedChanged: {
                            if (!pressed && enabled && Math.round(value) !== goxlrRow.modelData.outputs.headphonesVolume)
                                page.section.backend.setAudioGoxlrHeadphonesVolume(Math.round(value))
                        }
                    }
                    Label {
                        Layout.preferredWidth: 38
                        text: Math.round(goxlrRow.modelData.outputs.headphonesVolume * 100 / 255) + "%"
                        horizontalAlignment: Text.AlignRight
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.preferredWidth: 120
                        text: qsTranslate("AudioSettingsSection", "Line Out")
                    }
                    AudioSlider {
                        presentation: page.section
                        Layout.fillWidth: true
                        from: 0
                        to: 255
                        stepSize: 1
                        value: goxlrRow.modelData.outputs.lineOutVolume
                        enabled: page.section.goxlrMutationAllowed && goxlrRow.modelData.outputs.lineOutAvailable
                        onPressedChanged: {
                            if (!pressed && enabled && Math.round(value) !== goxlrRow.modelData.outputs.lineOutVolume)
                                page.section.backend.setAudioGoxlrLineOutVolume(Math.round(value))
                        }
                    }
                    Label {
                        Layout.preferredWidth: 38
                        text: Math.round(goxlrRow.modelData.outputs.lineOutVolume * 100 / 255) + "%"
                        horizontalAlignment: Text.AlignRight
                    }
                }
                AudioButton {
                    presentation: page.section
                    Layout.fillWidth: true
                    text: goxlrRow.modelData.cough.muted ? qsTranslate("AudioSettingsSection", "Release cough mute") : qsTranslate("AudioSettingsSection", "Cough mute")
                    enabled: page.section.goxlrMutationAllowed && goxlrRow.modelData.cough.available
                    onClicked: if (enabled && visible)
                        page.section.backend.setAudioGoxlrCoughMuted(!goxlrRow.modelData.cough.muted)
                }
            }
        }
    }
}
