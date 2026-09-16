// SPDX-License-Identifier: MIT
#include "audio_adapter.h"
#include "localization.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <malloc.h>
#include <sys/prctl.h>
#include <unistd.h>

#ifndef SYNAPSE_SETTINGS_VERSION
#define SYNAPSE_SETTINGS_VERSION "1.1.0-alpha.1"
#endif

namespace {

constexpr const char *kThemeProviderPath = "/usr/bin/synapse-theme";
constexpr const char *kThemeProviderCompatibilityPath =
    "/usr/local/bin/synapse-theme";

bool trustedDirectory(const QFileInfo &directory) {
  const QFile::Permissions unsafe = QFile::WriteGroup | QFile::WriteOther;
  const uint owner = static_cast<uint>(geteuid());
  return directory.exists() && directory.isDir() && !directory.isSymLink() &&
         (directory.ownerId() == 0U || directory.ownerId() == owner) &&
         !(directory.permissions() & unsafe);
}

bool validExecutable(const QString &path) {
  const QFileInfo file(path);
  const QFile::Permissions unsafe = QFile::WriteGroup | QFile::WriteOther;
  const uint owner = static_cast<uint>(geteuid());
  return file.isAbsolute() && file.exists() && file.isFile() &&
         !file.isSymLink() && file.isExecutable() &&
         (file.ownerId() == 0U || file.ownerId() == owner) &&
         !(file.permissions() & unsafe) &&
         trustedDirectory(QFileInfo(file.absolutePath()));
}

QString discoverThemeProvider() {
  const QString sibling =
      QDir(QCoreApplication::applicationDirPath())
          .absoluteFilePath(QStringLiteral("synapse-theme"));
  if (validExecutable(sibling))
    return sibling;
  const QString canonical = QString::fromLatin1(kThemeProviderPath);
  if (validExecutable(canonical))
    return canonical;
  const QString compatibility =
      QString::fromLatin1(kThemeProviderCompatibilityPath);
  return validExecutable(compatibility) ? compatibility : canonical;
}

QString themeConfigPath() {
  const QByteArray configured = qgetenv("XDG_CONFIG_HOME");
  if (!configured.isEmpty() && configured.size() < 4096)
    return QDir(QString::fromLocal8Bit(configured))
        .filePath(QStringLiteral("synapse/theme.conf"));
  const QByteArray home = qgetenv("HOME");
  if (!home.isEmpty() && home.size() < 4096)
    return QDir(QString::fromLocal8Bit(home))
        .filePath(QStringLiteral(".config/synapse/theme.conf"));
  return QString();
}

QStringList themeStatePaths() {
  QString root;
  const QByteArray configured = qgetenv("XDG_STATE_HOME");
  if (!configured.isEmpty() && configured.size() < 4096) {
    root = QDir(QString::fromLocal8Bit(configured))
               .filePath(QStringLiteral("synapse"));
  } else {
    const QByteArray home = qgetenv("HOME");
    if (!home.isEmpty() && home.size() < 4096)
      root = QDir(QString::fromLocal8Bit(home))
                 .filePath(QStringLiteral(".local/state/synapse"));
  }
  if (root.isEmpty())
    return {};
  return {QDir(root).filePath(QStringLiteral("current-theme")),
          QDir(root).filePath(QStringLiteral("current"))};
}

QString discoverBackend() {
  const QFileInfo sibling(
      QDir(QCoreApplication::applicationDirPath())
          .absoluteFilePath(QStringLiteral("synapse-settings")));
  if (sibling.isFile() && sibling.isExecutable())
    return sibling.canonicalFilePath();
  return QStringLiteral("/usr/bin/synapse-settings");
}

bool parseWindowSize(const QString &value, QSize *size) {
  if (!size)
    return false;
  const QStringList parts = value.split(QLatin1Char('x'));
  bool widthOk = false;
  bool heightOk = false;
  const int width = parts.size() == 2 ? parts.at(0).toInt(&widthOk) : 0;
  const int height = parts.size() == 2 ? parts.at(1).toInt(&heightOk) : 0;
  if (!widthOk || !heightOk || width < 640 || width > 3840 || height < 480 ||
      height > 2160)
    return false;
  *size = QSize(width, height);
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (prctl(PR_SET_THP_DISABLE, 1L, 0L, 0L, 0L) != 0) {
    const int error = errno;
    std::fprintf(stderr,
                 "synapse-settings-gui: PR_SET_THP_DISABLE failed: %s\n",
                 std::strerror(error));
    return 70;
  }
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
  QQuickStyle::setStyle(QStringLiteral("Basic"));

  QApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("synapse-settings-gui"));
  QCoreApplication::setApplicationVersion(
      QStringLiteral(SYNAPSE_SETTINGS_VERSION));
  QCoreApplication::setOrganizationName(QStringLiteral("Synapse"));
  QGuiApplication::setDesktopFileName(QStringLiteral("org.synapse.Settings"));
  QFont interfaceFont = application.font();
  interfaceFont.setFamily(QStringLiteral("monospace"));
  application.setFont(interfaceFont);

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Synapse Settings graphical presentation"));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption localeOption(
      QStringLiteral("locale"),
      QStringLiteral(
          "Managed GUI locale (unavailable catalogs fall back to en_US)."),
      QStringLiteral("locale"));
  const QCommandLineOption testExitOption(
      QStringLiteral("test-exit-after-load"),
      QStringLiteral("Exit after one validated Audio publication."));
  const QCommandLineOption testTimeoutOption(
      QStringLiteral("test-ready-timeout"),
      QStringLiteral("Bounded test timeout in milliseconds."),
      QStringLiteral("milliseconds"));
  const QCommandLineOption testSizeOption(
      QStringLiteral("test-window-size"),
      QStringLiteral("Bounded WIDTHxHEIGHT test window size."),
      QStringLiteral("size"));
  const QCommandLineOption testScreenshotOption(
      QStringLiteral("test-screenshot"),
      QStringLiteral("Absolute PNG output after validated Audio load."),
      QStringLiteral("path"));
  const QCommandLineOption testSettleDelayOption(
      QStringLiteral("test-settle-delay"),
      QStringLiteral("Bounded presentation settle delay in milliseconds."),
      QStringLiteral("milliseconds"));
  parser.addOptions({localeOption, testExitOption, testTimeoutOption,
                     testSizeOption, testScreenshotOption,
                     testSettleDelayOption});
  parser.process(application);

  const bool testExit = parser.isSet(testExitOption);
  int testTimeout = 0;
  int testSettleDelay = 150;
  QSize testSize;
  QString testScreenshot;
  if (parser.isSet(testTimeoutOption)) {
    bool ok = false;
    testTimeout = parser.value(testTimeoutOption).toInt(&ok);
    if (!testExit || !ok || testTimeout < 250 || testTimeout > 30000) {
      qCritical("synapse-settings-gui: invalid test timeout");
      return 2;
    }
  }
  if (parser.isSet(testSizeOption) &&
      (!testExit ||
       !parseWindowSize(parser.value(testSizeOption), &testSize))) {
    qCritical("synapse-settings-gui: invalid test window size");
    return 2;
  }
  if (parser.isSet(testSettleDelayOption)) {
    bool ok = false;
    testSettleDelay = parser.value(testSettleDelayOption).toInt(&ok);
    if (!testExit || !ok || testSettleDelay < 50 || testSettleDelay > 5000) {
      std::fprintf(stderr, "synapse-settings-gui: invalid test settle delay\n");
      return 2;
    }
  }
  if (parser.isSet(testScreenshotOption)) {
    const QFileInfo output(parser.value(testScreenshotOption));
    if (!testExit || !output.isAbsolute() || output.fileName().isEmpty() ||
        output.absoluteFilePath().toUtf8().size() >= 4096 ||
        !QDir(output.absolutePath()).exists()) {
      qCritical("synapse-settings-gui: invalid test screenshot path");
      return 2;
    }
    testScreenshot = output.absoluteFilePath();
  }

  SettingsLocalization localization;
  if (!localization.initialize(parser.value(localeOption))) {
    qCritical("synapse-settings-gui: translation catalog unavailable");
    return 2;
  }

  SettingsTheme theme(discoverThemeProvider());
  theme.watchConfig(themeConfigPath(), themeStatePaths());
  (void)theme.reload();

  AudioAdapter adapter(discoverBackend());
  QQmlApplicationEngine engine;
  QObject::connect(&engine, &QQmlApplicationEngine::warnings, &application,
                   [](const QList<QQmlError> &warnings) {
                     for (const QQmlError &warning : warnings)
                       std::fprintf(stderr, "synapse-settings-gui: qml=%s\n",
                                    warning.toString().toUtf8().constData());
                   });
  engine.setInitialProperties(
      {{QStringLiteral("backend"), QVariant::fromValue(&adapter)},
       {QStringLiteral("theme"), QVariant::fromValue(&theme)}});
  if (!QFile::exists(QStringLiteral(":/qml/Main.qml"))) {
    qCritical("synapse-settings-gui: embedded QML unavailable");
    return 3;
  }
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty())
    return 3;
  QQuickWindow *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  if (!window)
    return 3;
  if (window->rendererInterface()->graphicsApi() !=
      QSGRendererInterface::Software) {
    qCritical("synapse-settings-gui: software renderer unavailable");
    return 3;
  }
  if (testSize.isValid()) {
    window->setWidth(testSize.width());
    window->setHeight(testSize.height());
  }

  if (testExit) {
    QPointer<QQuickWindow> guardedWindow(window);
    QObject::connect(
        &adapter, &AudioAdapter::audioLoaded, &application,
        [&application, guardedWindow, testScreenshot, testSettleDelay,
         &theme](bool success) {
          if (!success) {
            application.exit(4);
            return;
          }
          QTimer::singleShot(
              testSettleDelay, &application,
              [&application, guardedWindow, testScreenshot, &theme]() {
                if (!guardedWindow) {
                  application.exit(4);
                  return;
                }
                if (!testScreenshot.isEmpty()) {
                  const QImage image = guardedWindow->grabWindow();
                  if (image.isNull() || !image.save(testScreenshot, "PNG")) {
                    application.exit(5);
                    return;
                  }
                }
                QCoreApplication::sendPostedEvents(nullptr,
                                                   QEvent::DeferredDelete);
                const int trimmed = malloc_trim(0);
                std::fprintf(stderr,
                             "synapse-settings-gui: audio=validated "
                             "renderer=software theme=%s theme-provider=%s "
                             "allocator=%s\n",
                             theme.id().toUtf8().constData(),
                             theme.providerValid() ? "valid" : "fallback",
                             trimmed != 0 ? "trimmed" : "stable");
                application.quit();
              });
        });
    if (testTimeout > 0) {
      QTimer::singleShot(testTimeout, &application, [&application]() {
        qCritical("synapse-settings-gui: test timeout");
        application.exit(124);
      });
    }
  }

  return application.exec();
}
