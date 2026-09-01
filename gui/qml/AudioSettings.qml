// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    required property var backend
    readonly property bool contentLoaded: sectionLoader.status === Loader.Ready

    Loader {
        id: sectionLoader
        anchors.fill: parent
        active: root.visible
        asynchronous: true
        sourceComponent: AudioSettingsSection {
            backend: root.backend
        }
    }
}
