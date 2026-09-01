// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool active: visible
    readonly property bool loaded: AudioBackend.audioSnapshotReady
    readonly property bool contentLoaded: audioView.contentLoaded
    readonly property bool backendAvailable: AudioBackend.audioAvailable
    readonly property bool busy: AudioBackend.audioBusy
    readonly property bool routeBrokerActive: AudioBackend.audioRouteBrokerActive
    readonly property bool routeEnforcementAvailable: AudioBackend.audioRouteEnforcementAvailable
    readonly property string goxlrStatus: AudioBackend.audioGoxlrStatus || "Unavailable"
    readonly property bool goxlrProviderActive: AudioBackend.audioGoxlrProviderActive
    readonly property int goxlrDeviceCount: AudioBackend.audioGoxlrDevices.length
    readonly property bool goxlrTruncated: AudioBackend.audioGoxlrTruncated
    readonly property string reasonId: AudioBackend.audioReason || ""
    readonly property string statusId: AudioBackend.audioStatusId || ""
    readonly property string errorId: AudioBackend.audioErrorId || ""

    AudioSettings {
        id: audioView
        anchors.fill: parent
        active: root.active
        backend: AudioBackend
    }
}
