// SPDX-License-Identifier: MIT
#ifndef SYNAPSE_SETTINGS_GUI_AUDIO_QML_PLUGIN_H
#define SYNAPSE_SETTINGS_GUI_AUDIO_QML_PLUGIN_H

#include <QQmlExtensionPlugin>

class SynapseSettingsAudioPlugin final : public QQmlExtensionPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface/1.0")

public:
  void registerTypes(const char *uri) override;
  void initializeEngine(QQmlEngine *engine, const char *uri) override;
};

#endif
