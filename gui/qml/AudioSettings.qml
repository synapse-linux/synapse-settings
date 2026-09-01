// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    required property var backend
    property bool active: visible
    readonly property bool contentLoaded: sectionLoader.status === Loader.Ready

    Loader {
        id: sectionLoader
        anchors.fill: parent
        active: root.active
        asynchronous: true
        sourceComponent: AudioSettingsSection {
            active: root.active
            backend: root.backend
        }
    }
}
