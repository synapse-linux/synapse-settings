// SPDX-License-Identifier: MIT
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    required property var backend
    property bool active: visible
    property color backgroundColor: "#10151f"
    property color surfaceColor: "#171f2c"
    property color surfaceHoverColor: "#27364a"
    property color borderColor: "#354760"
    property color accentColor: "#7aa2f7"
    property color textColor: "#e7eefb"
    property color mutedColor: "#93a6c0"
    property color urgentColor: "#f7768e"
    property string fontFamily: "monospace"
    property int radius: 8
    property int presentationSpacing: 8
    readonly property string surfaceApi: "synapse.settings.audio.surface/v1"
    readonly property bool contentLoaded: sectionLoader.status === Loader.Ready
    readonly property bool contentReady: contentLoaded && backend !== null
        && backend.audioSnapshotReady && backend.audioAvailable
    readonly property bool canUnload: !sectionLoader.retained && !sectionLoader.active
    readonly property var presentationItem: sectionLoader.presentation
    function requestUnload() { return canUnload }
    function requestHide() {
        return sectionLoader.presentation ? !sectionLoader.presentation.interactionPending
            : backend !== null && !backend.audioBusy
    }
    LayoutMirroring.enabled: Application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    Loader {
        id: sectionLoader
        readonly property AudioSettingsSection presentation: item as AudioSettingsSection
        property bool retained: false
        anchors.fill: parent
        // Hiding is not destruction. Dialogs, drafts and opaque selections
        // remain owned here until the containing application is closed.
        active: root.active || retained
        asynchronous: true
        onLoaded: retained = true
        sourceComponent: AudioSettingsSection {
            active: root.active
            backend: root.backend
            backgroundColor: root.backgroundColor
            surfaceColor: root.surfaceColor
            surfaceHoverColor: root.surfaceHoverColor
            borderColor: root.borderColor
            accentColor: root.accentColor
            textColor: root.textColor
            mutedColor: root.mutedColor
            urgentColor: root.urgentColor
            fontFamily: root.fontFamily
            radius: root.radius
            presentationSpacing: root.presentationSpacing
        }
    }
}
