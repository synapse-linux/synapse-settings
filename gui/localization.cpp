// SPDX-License-Identifier: GPL-3.0-or-later
#include "localization.h"

#include <QCoreApplication>
#include <QLocale>

SettingsLocalization::SettingsLocalization(QObject *parent) : QObject(parent) {}

bool SettingsLocalization::initialize(const QString &requestedLocale) {
  QString candidate = requestedLocale;
  if (candidate.isEmpty())
    candidate = QLocale::system().name();
  candidate.replace(QLatin1Char('-'), QLatin1Char('_'));
  if (candidate.startsWith(QStringLiteral("it"), Qt::CaseInsensitive))
    localeId_ = QStringLiteral("it_IT");
  else if (candidate.startsWith(QStringLiteral("en"), Qt::CaseInsensitive) ||
           candidate == QStringLiteral("C") ||
           candidate == QStringLiteral("POSIX"))
    localeId_ = QStringLiteral("en_US");
  else
    return false;
  if (!translator_.load(
          QStringLiteral(":/i18n/synapse-settings_%1.qm").arg(localeId_)))
    return false;
  return QCoreApplication::installTranslator(&translator_);
}

QString SettingsLocalization::localeId() const { return localeId_; }
