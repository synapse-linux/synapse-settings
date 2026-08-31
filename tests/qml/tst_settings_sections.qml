// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import "../../gui/qml" as SettingsUi

TestCase {
    name: "SettingsSections"

    Component {
        id: fakeBackend
        QtObject {
            property bool audioBusy: false
            property bool audioAvailable: true
            property string audioReason: ""
            property var audioOutputs: [{ id: "output-a", label: "Output", default: true, volumePercent: 40, muted: false }]
            property var audioInputs: []
            property var audioStreams: []
            property var audioCards: []
            property int audioLoads: 0
            property int audioSets: 0
            property string lastAudioDirection: ""
            property string lastAudioDevice: ""
            function loadAudio() { audioLoads++ }
            function setAudioDefault(direction, device) {
                audioSets++
                lastAudioDirection = direction
                lastAudioDevice = device
            }

            property bool graphicsBusy: false
            property var graphicsGpus: [{ id: "gpu-a", label: "Gaming GPU", driver: "driver", displayOwner: false, gamingCandidate: true }]
            property var graphicsRules: []
            property string graphicsDefaultGpu: "system"
            property bool graphicsEnforcementAvailable: false
            property int graphicsLoads: 0
            property int graphicsDefaults: 0
            property int graphicsAdds: 0
            property int graphicsRemoves: 0
            property var lastGraphics: []
            function loadGraphics() { graphicsLoads++ }
            function setGraphicsDefault(gpu) { graphicsDefaults++; lastGraphics = [gpu] }
            function addGraphicsRule(type, path, gpu) { graphicsAdds++; lastGraphics = [type, path, gpu] }
            function removeGraphicsRule(rule) { graphicsRemoves++; lastGraphics = [rule] }
        }
    }

    Component {
        id: audioComponent
        SettingsUi.AudioSettingsSection { width: 720; height: 520; visible: false }
    }

    Component {
        id: graphicsComponent
        SettingsUi.GraphicsSettingsSection { width: 720; height: 520; visible: false }
    }

    function test_audioLazyAndTypedMutation() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(audioComponent, this, { backend: backend })
        verify(section)
        compare(backend.audioLoads, 0)
        section.visible = true
        section.activate()
        compare(backend.audioLoads, 1)
        section.activate()
        compare(backend.audioLoads, 1)
        section.pendingDirection = "output"
        section.pendingDevice = "output-a"
        verify(section.applyPendingDefault())
        compare(backend.audioSets, 1)
        compare(backend.lastAudioDirection, "output")
        compare(backend.lastAudioDevice, "output-a")
    }

    function test_graphicsLazyAndTypedRules() {
        let backend = createTemporaryObject(fakeBackend, this)
        let section = createTemporaryObject(graphicsComponent, this, { backend: backend })
        verify(section)
        compare(backend.graphicsLoads, 0)
        section.visible = true
        section.activate()
        compare(backend.graphicsLoads, 1)
        section.activate()
        compare(backend.graphicsLoads, 1)
        verify(section.applyDefault("gpu-a"))
        compare(backend.graphicsDefaults, 1)
        verify(section.addRule("directory", "/games/steam", "gpu-a"))
        compare(backend.graphicsAdds, 1)
        compare(backend.lastGraphics, ["directory", "/games/steam", "gpu-a"])
        verify(!section.addRule("process-name", "steam", "gpu-a"))
        verify(section.removeRule("rule-0001"))
        compare(backend.graphicsRemoves, 1)
    }
}
