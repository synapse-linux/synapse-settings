// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SYNAPSE_SETTINGS_GUI_LOCALIZATION_H
#define SYNAPSE_SETTINGS_GUI_LOCALIZATION_H

#include <QObject>
#include <QTranslator>

class SettingsLocalization final : public QObject {
  Q_OBJECT

public:
  explicit SettingsLocalization(QObject *parent = nullptr);
  ~SettingsLocalization() override;
  bool initialize(const QString &requestedLocale);
  QString localeId() const;

private:
  QTranslator translator_;
  QString localeId_;
  bool installed_ = false;
};

#endif
