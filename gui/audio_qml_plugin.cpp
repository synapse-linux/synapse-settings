// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_qml_plugin.h"

#include "audio_adapter.h"
#include "localization.h"

#include <QQmlEngine>
#include <QtQml/qqml.h>

#include <cstring>

namespace {

constexpr char kAudioModuleUri[] = "Synapse.Settings.Audio";

} // namespace

void SynapseSettingsAudioPlugin::registerTypes(const char *uri) {
  if (!uri || std::strcmp(uri, kAudioModuleUri) != 0)
    qFatal("synapse-settings: invalid Audio QML module URI");
  qmlRegisterSingletonType<AudioAdapter>(
      uri, 1, 0, "AudioBackend",
      [](QQmlEngine *, QJSEngine *) -> AudioAdapter * {
        return new AudioAdapter();
      });
  qmlRegisterModule(uri, 1, 0);
}

void SynapseSettingsAudioPlugin::initializeEngine(QQmlEngine *engine,
                                                  const char *uri) {
  if (!engine || !uri || std::strcmp(uri, kAudioModuleUri) != 0)
    return;
  auto *localization = new SettingsLocalization(engine);
  if (!localization->initialize(QString()))
    qWarning("synapse-settings: Audio QML translation catalog unavailable; "
             "using source copy");
}
