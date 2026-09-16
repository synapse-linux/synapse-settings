// SPDX-License-Identifier: MIT
#include "theme.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

class ThemeTest final : public QObject {
  Q_OBJECT

private:
  QString fixturePath() const {
    return QFileInfo(QString::fromLocal8Bit(__FILE__))
        .absoluteDir()
        .filePath(QStringLiteral("fake_theme_provider.py"));
  }

private slots:
  void validV3ProjectionIsApplied() {
    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "valid");
    SettingsTheme theme(fixturePath());
    QVERIFY(theme.reload());
    QVERIFY(theme.providerValid());
    QCOMPARE(theme.id(), QStringLiteral("tokyo-night"));
    QCOMPARE(theme.background(), QColor(QStringLiteral("#1a1b26")));
    QCOMPARE(theme.accent(), QColor(QStringLiteral("#7aa2f7")));
    QCOMPARE(theme.onAccent(), QColor(QStringLiteral("#000000")));
    QCOMPARE(theme.onUrgent(), QColor(QStringLiteral("#000000")));
    QCOMPARE(theme.reasonId(), QStringLiteral("provider-ok"));
    QCOMPARE(theme.revision(), quint64(1));
  }

  void invalidMajorPreservesLastKnownPalette() {
    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "valid");
    SettingsTheme theme(fixturePath());
    QVERIFY(theme.reload());
    const QColor acceptedAccent = theme.accent();
    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "invalid");
    QVERIFY(!theme.reload());
    QVERIFY(!theme.providerValid());
    QCOMPARE(theme.reasonId(), QStringLiteral("provider-contract-mismatch"));
    QCOMPARE(theme.accent(), acceptedAccent);
    QCOMPARE(theme.id(), QStringLiteral("tokyo-night"));
  }

  void duplicateKeysAndUnsafeIdentifiersFailClosed() {
    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "duplicate-key");
    SettingsTheme duplicateTheme(fixturePath());
    QVERIFY(!duplicateTheme.reload());
    QCOMPARE(duplicateTheme.reasonId(),
             QStringLiteral("provider-output-invalid"));
    QCOMPARE(duplicateTheme.id(), QStringLiteral("fallback"));

    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "unsafe-id");
    SettingsTheme unsafeTheme(fixturePath());
    QVERIFY(!unsafeTheme.reload());
    QCOMPARE(unsafeTheme.reasonId(),
             QStringLiteral("provider-contract-mismatch"));
    QCOMPARE(unsafeTheme.id(), QStringLiteral("fallback"));
  }

  void failedProviderUsesDeterministicFallback() {
    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "fail");
    SettingsTheme theme(fixturePath());
    QVERIFY(!theme.reload());
    QVERIFY(!theme.providerValid());
    QCOMPARE(theme.id(), QStringLiteral("fallback"));
    QCOMPARE(theme.reasonId(), QStringLiteral("provider-failed"));
  }

  void providerOutputAndExecutionAreBounded() {
    for (const QByteArray &mode :
         {QByteArray("oversized-stdout"), QByteArray("oversized-stderr")}) {
      qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", mode);
      SettingsTheme theme(fixturePath());
      QVERIFY(!theme.reload());
      QVERIFY(!theme.providerValid());
      QCOMPARE(theme.reasonId(), QStringLiteral("provider-output-invalid"));
      QCOMPARE(theme.id(), QStringLiteral("fallback"));
    }

    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "stream-output");
    SettingsTheme streamingTheme(fixturePath());
    QElapsedTimer streamingElapsed;
    streamingElapsed.start();
    QVERIFY(!streamingTheme.reload());
    QVERIFY(streamingElapsed.elapsed() < 1000);
    QCOMPARE(streamingTheme.reasonId(),
             QStringLiteral("provider-output-invalid"));

    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "timeout");
    SettingsTheme theme(fixturePath());
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(!theme.reload());
    QVERIFY(elapsed.elapsed() >= 1000);
    QVERIFY(elapsed.elapsed() < 3000);
    QCOMPARE(theme.reasonId(), QStringLiteral("provider-timeout"));
  }

  void timeoutKillsProviderProcessGroup() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString childPidPath =
        temporary.filePath(QStringLiteral("theme-provider-child.pid"));
    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE",
            QStringLiteral("fork-timeout:%1").arg(childPidPath).toUtf8());

    SettingsTheme theme(fixturePath());
    QVERIFY(!theme.reload());
    QCOMPARE(theme.reasonId(), QStringLiteral("provider-timeout"));

    QFile childPidFile(childPidPath);
    QVERIFY(childPidFile.open(QIODevice::ReadOnly));
    bool validPid = false;
    const qint64 childPid =
        childPidFile.readAll().trimmed().toLongLong(&validPid);
    QVERIFY(validPid);
    QVERIFY(childPid > 1);
    QTRY_VERIFY_WITH_TIMEOUT(
        !QFileInfo(QStringLiteral("/proc/%1").arg(childPid)).exists(), 2000);
  }

  void atomicThemeSurfaceReplacementReloadsPalette() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString configPath =
        temporary.filePath(QStringLiteral("synapse/theme.conf"));
    QVERIFY(QDir().mkpath(QFileInfo(configPath).absolutePath()));
    QFile initial(configPath);
    QVERIFY(initial.open(QIODevice::WriteOnly));
    QCOMPARE(initial.write("id=tokyo-night\n"), qint64(15));
    initial.close();
    const QString statePath =
        temporary.filePath(QStringLiteral("state/synapse/current-theme"));
    QVERIFY(QDir().mkpath(QFileInfo(statePath).absolutePath()));
    QFile initialState(statePath);
    QVERIFY(initialState.open(QIODevice::WriteOnly));
    QCOMPARE(initialState.write("tokyo-night\n"), qint64(12));
    initialState.close();

    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "valid");
    SettingsTheme theme(fixturePath());
    theme.watchConfig(configPath, {statePath});
    QVERIFY(theme.reload());
    QCOMPARE(theme.id(), QStringLiteral("tokyo-night"));
    const quint64 firstRevision = theme.revision();

    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "alternate");
    QSaveFile replacement(configPath);
    QVERIFY(replacement.open(QIODevice::WriteOnly));
    QCOMPARE(replacement.write("id=matte-black\n"), qint64(15));
    QVERIFY(replacement.commit());

    QTRY_COMPARE(theme.id(), QStringLiteral("matte-black"));
    QCOMPARE(theme.background(), QColor(QStringLiteral("#121212")));
    QCOMPARE(theme.accent(), QColor(QStringLiteral("#e68e0d")));
    QVERIFY(theme.providerValid());
    QVERIFY(theme.revision() > firstRevision);
    const quint64 secondRevision = theme.revision();

    qputenv("SYNAPSE_SETTINGS_THEME_FIXTURE", "valid");
    QSaveFile stateReplacement(statePath);
    QVERIFY(stateReplacement.open(QIODevice::WriteOnly));
    QCOMPARE(stateReplacement.write("tokyo-night\n"), qint64(12));
    QVERIFY(stateReplacement.commit());

    QTRY_COMPARE(theme.id(), QStringLiteral("tokyo-night"));
    QCOMPARE(theme.accent(), QColor(QStringLiteral("#7aa2f7")));
    QVERIFY(theme.revision() > secondRevision);
  }
};

QTEST_GUILESS_MAIN(ThemeTest)
#include "test_theme.moc"
