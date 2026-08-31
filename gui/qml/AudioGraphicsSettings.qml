// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property QtObject backend

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Audio") }
            TabButton { text: qsTr("Grafica") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            AudioSettingsSection { backend: root.backend }
            GraphicsSettingsSection { backend: root.backend }
        }
    }
}
