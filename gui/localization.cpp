// SPDX-License-Identifier: MIT
#include "localization.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

namespace {

QStringList localeCandidates(const QString &requestedLocale) {
  const QString requested = requestedLocale.trimmed();
  static const QRegularExpression localeExpression(QStringLiteral(
      "^[A-Za-z]{2,3}(?:[-_][A-Za-z]{2})?(?:\\.[A-Za-z0-9_-]{1,32})?"
      "(?:@[A-Za-z][A-Za-z0-9_-]{0,31})?$"));
  if (requested.isEmpty() || requested.toUtf8().size() > 64 ||
      !localeExpression.match(requested).hasMatch())
    return {};

  const qsizetype modifierOffset = requested.indexOf(QLatin1Char('@'));
  QString stem =
      modifierOffset >= 0 ? requested.left(modifierOffset) : requested;
  QString modifier =
      modifierOffset >= 0 ? requested.mid(modifierOffset + 1) : QString();
  const qsizetype encodingOffset = stem.indexOf(QLatin1Char('.'));
  if (encodingOffset >= 0)
    stem.truncate(encodingOffset);
  stem.replace(QLatin1Char('-'), QLatin1Char('_'));
  const QStringList parts = stem.split(QLatin1Char('_'));
  if (parts.isEmpty() || parts.size() > 2)
    return {};
  const QString language = parts.constFirst().toLower();
  const QString region = parts.size() == 2 ? parts.at(1).toUpper() : QString();
  modifier = modifier.toLower();

  QStringList candidates;
  const auto appendCandidate = [&candidates](const QString &candidate) {
    if (!candidate.isEmpty() && !candidates.contains(candidate))
      candidates.append(candidate);
  };
  const QString regional =
      region.isEmpty() ? language : language + QLatin1Char('_') + region;
  if (!modifier.isEmpty()) {
    appendCandidate(regional + QLatin1Char('@') + modifier);
    appendCandidate(language + QLatin1Char('@') + modifier);
  }
  appendCandidate(regional);
  appendCandidate(language);

  if (language == QStringLiteral("en"))
    appendCandidate(QStringLiteral("en_US"));
  else if (language == QStringLiteral("cs"))
    appendCandidate(QStringLiteral("cs_CZ"));
  else if (language == QStringLiteral("fi"))
    appendCandidate(QStringLiteral("fi_FI"));
  else if (language == QStringLiteral("it"))
    appendCandidate(QStringLiteral("it_IT"));
  else if (language == QStringLiteral("tr"))
    appendCandidate(QStringLiteral("tr_TR"));
  return candidates;
}

} // namespace

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
  const QString requested =
      requestedLocale.isEmpty() ? QLocale::system().name() : requestedLocale;
  for (const QString &candidate : localeCandidates(requested)) {
    const QString resource =
        QStringLiteral(":/i18n/synapse-settings_%1.qm").arg(candidate);
    if (QFile::exists(resource) && translator_.load(resource)) {
      localeId_ = candidate;
      break;
    }
  }
  if (localeId_.isEmpty()) {
    localeId_ = QStringLiteral("en_US");
    if (!translator_.load(QStringLiteral(":/i18n/synapse-settings_en_US.qm")))
      return false;
  }
  installed_ = QCoreApplication::installTranslator(&translator_);
  if (installed_)
    QGuiApplication::setLayoutDirection(QLocale(localeId_).textDirection());
  return installed_;
}

QString SettingsLocalization::localeId() const { return localeId_; }
