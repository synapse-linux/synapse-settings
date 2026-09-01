// SPDX-License-Identifier: GPL-3.0-or-later
#include "localization.h"

#include <QCoreApplication>
#include <QLocale>

SettingsLocalization::SettingsLocalization(QObject *parent) : QObject(parent) {}

SettingsLocalization::~SettingsLocalization() {
  if (installed_)
    QCoreApplication::removeTranslator(&translator_);
}

bool SettingsLocalization::initialize(const QString &requestedLocale) {
  if (installed_) {
    QCoreApplication::removeTranslator(&translator_);
    installed_ = false;
  }
  localeId_.clear();
  QString candidate = requestedLocale;
  if (candidate.isEmpty())
    candidate = QLocale::system().name();
  candidate = candidate.trimmed();
  candidate.replace(QLatin1Char('-'), QLatin1Char('_'));
  const qsizetype encoding = candidate.indexOf(QLatin1Char('.'));
  if (encoding >= 0)
    candidate.truncate(encoding);
  const qsizetype modifier = candidate.indexOf(QLatin1Char('@'));
  if (modifier >= 0)
    candidate.truncate(modifier);

  bool safe = !candidate.isEmpty() && candidate.toUtf8().size() <= 64;
  for (const QChar character : candidate) {
    if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
      safe = false;
      break;
    }
  }
  const QString language =
      safe ? candidate.section(QLatin1Char('_'), 0, 0) : QString();
  localeId_ = language.compare(QStringLiteral("it"), Qt::CaseInsensitive) == 0
                  ? QStringLiteral("it_IT")
                  : QStringLiteral("en_US");

  if (!translator_.load(
          QStringLiteral(":/i18n/synapse-settings_%1.qm").arg(localeId_))) {
    localeId_ = QStringLiteral("en_US");
    if (!translator_.load(QStringLiteral(":/i18n/synapse-settings_en_US.qm")))
      return false;
  }
  installed_ = QCoreApplication::installTranslator(&translator_);
  return installed_;
}

QString SettingsLocalization::localeId() const { return localeId_; }
