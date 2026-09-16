// SPDX-License-Identifier: MIT
#include "theme.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
constexpr qsizetype kMaximumThemeBytes = static_cast<qsizetype>(16) * 1024;
constexpr int kThemeTimeoutMs = 1200;

SettingsTheme::Palette fallbackPalette() {
  return {QStringLiteral("fallback"),        QColor(QStringLiteral("#111827")),
          QColor(QStringLiteral("#1f2937")), QColor(QStringLiteral("#374151")),
          QColor(QStringLiteral("#4b5563")), QColor(QStringLiteral("#67e8f9")),
          QColor(QStringLiteral("#f8fafc")), QColor(QStringLiteral("#94a3b8")),
          QColor(QStringLiteral("#fb7185"))};
}

bool exactKeys(const QJsonObject &object,
               std::initializer_list<const char *> expected) {
  if (object.size() != static_cast<qsizetype>(expected.size()))
    return false;
  return std::all_of(expected.begin(), expected.end(),
                     [&object](const char *key) {
                       return object.contains(QLatin1String(key));
                     });
}

bool boundedString(const QJsonObject &object, const char *key,
                   qsizetype minimum, qsizetype maximum) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (!value.isString())
    return false;
  const qsizetype size = value.toString().size();
  return size >= minimum && size <= maximum;
}

bool boundedIdentifier(const QJsonObject &object, const char *key) {
  if (!boundedString(object, key, 1, 64))
    return false;
  const QString value = object.value(QLatin1String(key)).toString();
  return std::all_of(value.cbegin(), value.cend(), [](QChar character) {
    const ushort code = character.unicode();
    return (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') ||
           (code >= '0' && code <= '9') || code == '-' || code == '_' ||
           code == '.';
  });
}

bool hasDuplicateObjectKeys(const QByteArray &payload) {
  struct Container {
    bool object = false;
    QSet<QString> keys;
  };
  QVector<Container> containers;
  for (qsizetype index = 0; index < payload.size(); ++index) {
    const char character = payload.at(index);
    if (character == '{') {
      containers.append(Container{true, {}});
      continue;
    }
    if (character == '[') {
      containers.append(Container{false, {}});
      continue;
    }
    if (character == '}' || character == ']') {
      if (containers.isEmpty())
        return true;
      containers.removeLast();
      continue;
    }
    if (character != '"')
      continue;

    const qsizetype start = index;
    bool escaped = false;
    for (++index; index < payload.size(); ++index) {
      const char stringCharacter = payload.at(index);
      if (escaped) {
        escaped = false;
      } else if (stringCharacter == '\\') {
        escaped = true;
      } else if (stringCharacter == '"') {
        break;
      }
    }
    if (index >= payload.size())
      return true;
    qsizetype next = index + 1;
    while (next < payload.size() &&
           (payload.at(next) == ' ' || payload.at(next) == '\t' ||
            payload.at(next) == '\r' || payload.at(next) == '\n'))
      ++next;
    if (next >= payload.size() || payload.at(next) != ':')
      continue;
    if (containers.isEmpty() || !containers.constLast().object)
      return true;

    QByteArray wrapped("[");
    wrapped.append(payload.constData() + start, index - start + 1);
    wrapped.append(']');
    QJsonParseError keyError;
    const QJsonDocument keyDocument =
        QJsonDocument::fromJson(wrapped, &keyError);
    if (keyError.error != QJsonParseError::NoError || !keyDocument.isArray() ||
        keyDocument.array().size() != 1 ||
        !keyDocument.array().at(0).isString())
      return true;
    const QString decodedKey = keyDocument.array().at(0).toString();
    if (containers.last().keys.contains(decodedKey))
      return true;
    containers.last().keys.insert(decodedKey);
  }
  return !containers.isEmpty();
}

bool parseColor(const QJsonObject &colors, const char *key, QColor *output) {
  const QJsonValue value = colors.value(QLatin1String(key));
  if (!value.isString())
    return false;
  const QString text = value.toString();
  if (text.size() != 7 || !text.startsWith(QLatin1Char('#')))
    return false;
  for (qsizetype i = 1; i < text.size(); ++i) {
    const QChar character = text.at(i);
    if (!character.isDigit() &&
        !(character >= QLatin1Char('a') && character <= QLatin1Char('f')) &&
        !(character >= QLatin1Char('A') && character <= QLatin1Char('F')))
      return false;
  }
  const QColor color(text);
  if (!color.isValid())
    return false;
  *output = color;
  return true;
}

QProcessEnvironment themeEnvironment() {
  QProcessEnvironment environment;
  const auto copy = [&environment](const char *name) {
    const QByteArray value = qgetenv(name);
    if (!value.isEmpty() && value.size() < 4096)
      environment.insert(QString::fromLatin1(name),
                         QString::fromLocal8Bit(value));
  };
  copy("HOME");
  copy("XDG_CONFIG_HOME");
  copy("XDG_STATE_HOME");
#ifdef SYNAPSE_SETTINGS_GUI_TEST_HOOKS
  copy("SYNAPSE_SETTINGS_THEME_FIXTURE");
#endif
  environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
  environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
  return environment;
}

} // namespace

SettingsTheme::SettingsTheme(QString providerPath, QObject *parent)
    : QObject(parent), providerPath_(std::move(providerPath)),
      palette_(fallbackPalette()) {
  reloadTimer_.setSingleShot(true);
  reloadTimer_.setInterval(120);
  connect(&reloadTimer_, &QTimer::timeout, this, &SettingsTheme::reload);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this,
          [this](const QString &) { scheduleReload(); });
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString &) { scheduleReload(); });
}

QString SettingsTheme::id() const { return palette_.id; }
QColor SettingsTheme::background() const { return palette_.background; }
QColor SettingsTheme::surface() const { return palette_.surface; }
QColor SettingsTheme::surfaceHover() const { return palette_.surfaceHover; }
QColor SettingsTheme::border() const { return palette_.border; }
QColor SettingsTheme::accent() const { return palette_.accent; }
QColor SettingsTheme::text() const { return palette_.text; }
QColor SettingsTheme::muted() const { return palette_.muted; }
QColor SettingsTheme::urgent() const { return palette_.urgent; }
QColor SettingsTheme::accentSoft() const {
  return translucent(palette_.accent, 46);
}
QColor SettingsTheme::onAccent() const {
  return contrastingText(palette_.accent);
}
QColor SettingsTheme::onUrgent() const {
  return contrastingText(palette_.urgent);
}
bool SettingsTheme::providerValid() const { return providerValid_; }
QString SettingsTheme::reasonId() const { return reasonId_; }
quint64 SettingsTheme::revision() const { return revision_; }

QColor SettingsTheme::contrastingText(const QColor &color) {
  const auto linearChannel = [](double value) {
    return value <= 0.04045 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
  };
  const double luminance = (0.2126 * linearChannel(color.redF())) +
                           (0.7152 * linearChannel(color.greenF())) +
                           (0.0722 * linearChannel(color.blueF()));
  const double blackContrast = (luminance + 0.05) / 0.05;
  const double whiteContrast = 1.05 / (luminance + 0.05);
  return blackContrast >= whiteContrast ? QColor(QStringLiteral("#000000"))
                                        : QColor(QStringLiteral("#ffffff"));
}

QColor SettingsTheme::translucent(const QColor &color, int alpha) {
  QColor result(color);
  result.setAlpha(alpha);
  return result;
}

bool SettingsTheme::parsePayload(const QByteArray &payload, Palette *palette,
                                 QString *reasonId) {
  if (!palette || payload.isEmpty() || payload.size() > kMaximumThemeBytes) {
    if (reasonId)
      *reasonId = QStringLiteral("provider-output-invalid");
    return false;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject() ||
      hasDuplicateObjectKeys(payload)) {
    if (reasonId)
      *reasonId = QStringLiteral("provider-output-invalid");
    return false;
  }
  const QJsonObject root = document.object();
  if (!exactKeys(root, {"schema", "id", "theme"}) ||
      root.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("synapse.theme.current/v3") ||
      !boundedIdentifier(root, "id") ||
      !root.value(QStringLiteral("theme")).isObject()) {
    if (reasonId)
      *reasonId = QStringLiteral("provider-contract-mismatch");
    return false;
  }
  const QString id = root.value(QStringLiteral("id")).toString();
  const QJsonObject theme = root.value(QStringLiteral("theme")).toObject();
  if (!exactKeys(theme, {"id", "name", "description", "source", "preview",
                         "wallpaper", "backgrounds", "palette"}) ||
      !boundedIdentifier(theme, "id") ||
      theme.value(QStringLiteral("id")).toString() != id ||
      !boundedString(theme, "name", 1, 256) ||
      !boundedString(theme, "description", 0, 4096) ||
      !boundedString(theme, "source", 1, 256) ||
      !boundedString(theme, "preview", 0, 4096) ||
      !boundedString(theme, "wallpaper", 0, 4096) ||
      !theme.value(QStringLiteral("backgrounds")).isArray() ||
      !theme.value(QStringLiteral("palette")).isObject()) {
    if (reasonId)
      *reasonId = QStringLiteral("provider-contract-mismatch");
    return false;
  }
  const QJsonArray backgrounds =
      theme.value(QStringLiteral("backgrounds")).toArray();
  if (backgrounds.size() > 64 ||
      !std::all_of(backgrounds.cbegin(), backgrounds.cend(),
                   [](const QJsonValue &value) {
                     return value.isString() && value.toString().size() <= 4096;
                   })) {
    if (reasonId)
      *reasonId = QStringLiteral("provider-contract-mismatch");
    return false;
  }
  const QJsonObject colors = theme.value(QStringLiteral("palette")).toObject();
  if (!exactKeys(colors, {"background", "surface", "surfaceHover", "border",
                          "accent", "text", "muted", "urgent"})) {
    if (reasonId)
      *reasonId = QStringLiteral("provider-contract-mismatch");
    return false;
  }
  Palette parsed;
  parsed.id = id;
  if (!parseColor(colors, "background", &parsed.background) ||
      !parseColor(colors, "surface", &parsed.surface) ||
      !parseColor(colors, "surfaceHover", &parsed.surfaceHover) ||
      !parseColor(colors, "border", &parsed.border) ||
      !parseColor(colors, "accent", &parsed.accent) ||
      !parseColor(colors, "text", &parsed.text) ||
      !parseColor(colors, "muted", &parsed.muted) ||
      !parseColor(colors, "urgent", &parsed.urgent)) {
    if (reasonId)
      *reasonId = QStringLiteral("provider-contract-mismatch");
    return false;
  }
  *palette = parsed;
  return true;
}

void SettingsTheme::setFailure(const QString &reasonId) {
  const bool oldValid = providerValid_;
  const QString oldReason = reasonId_;
  providerValid_ = false;
  reasonId_ = reasonId;
  if (oldValid != providerValid_ || oldReason != reasonId_)
    emit statusChanged();
}

bool SettingsTheme::reload() {
  QProcess process;
  process.setProgram(providerPath_);
  process.setArguments({QStringLiteral("current"), QStringLiteral("--format"),
                        QStringLiteral("json")});
  process.setProcessEnvironment(themeEnvironment());
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.setStandardInputFile(QProcess::nullDevice());
  process.setWorkingDirectory(QStringLiteral("/"));
  const pid_t parentProcess = ::getpid();
  process.setChildProcessModifier([parentProcess]() {
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 ||
        ::getppid() != parentProcess || ::setpgid(0, 0) != 0)
      _exit(126);
  });
  process.start(QIODevice::ReadOnly);
  if (!process.waitForStarted(250)) {
    setFailure(QStringLiteral("provider-unavailable"));
    refreshWatchPaths();
    return false;
  }

  const qint64 processGroup = process.processId();
  const auto killProcessGroup = [&]() {
    if (processGroup > 1)
      (void)::kill(-static_cast<pid_t>(processGroup), SIGKILL);
    if (process.state() != QProcess::NotRunning)
      process.kill();
    if (process.state() != QProcess::NotRunning)
      (void)process.waitForFinished(250);
  };

  QByteArray output;
  QByteArray diagnostics;
  bool outputOverflow = false;
  const auto drainChannel = [&](QProcess::ProcessChannel channel,
                                QByteArray *destination) {
    process.setReadChannel(channel);
    while (!outputOverflow && process.bytesAvailable() > 0) {
      const qsizetype remaining =
          kMaximumThemeBytes - output.size() - diagnostics.size();
      const qint64 requested = std::min<qint64>(remaining + 1, 4096);
      const QByteArray chunk = process.read(requested);
      if (chunk.isEmpty())
        break;
      if (chunk.size() > remaining) {
        destination->append(chunk.constData(), remaining);
        outputOverflow = true;
        break;
      }
      destination->append(chunk);
    }
  };
  const auto drainOutput = [&]() {
    drainChannel(QProcess::StandardOutput, &output);
    drainChannel(QProcess::StandardError, &diagnostics);
  };

  QElapsedTimer deadline;
  deadline.start();
  while (process.state() != QProcess::NotRunning && !outputOverflow &&
         deadline.elapsed() < kThemeTimeoutMs) {
    const int remaining =
        std::max(1, kThemeTimeoutMs - static_cast<int>(deadline.elapsed()));
    (void)process.waitForReadyRead(std::min(remaining, 10));
    drainOutput();
  }
  drainOutput();
  const bool deadlineExpired = deadline.elapsed() >= kThemeTimeoutMs;
  if (outputOverflow) {
    process.closeReadChannel(QProcess::StandardOutput);
    process.closeReadChannel(QProcess::StandardError);
    killProcessGroup();
    setFailure(QStringLiteral("provider-output-invalid"));
    refreshWatchPaths();
    return false;
  }
  if (deadlineExpired || process.state() != QProcess::NotRunning) {
    process.closeReadChannel(QProcess::StandardOutput);
    process.closeReadChannel(QProcess::StandardError);
    killProcessGroup();
    setFailure(QStringLiteral("provider-timeout"));
    refreshWatchPaths();
    return false;
  }
  killProcessGroup();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    setFailure(QStringLiteral("provider-failed"));
    refreshWatchPaths();
    return false;
  }
  Palette parsed;
  QString reason;
  if (!parsePayload(output, &parsed, &reason)) {
    setFailure(reason);
    refreshWatchPaths();
    return false;
  }
  const bool changed =
      parsed.id != palette_.id || parsed.background != palette_.background ||
      parsed.surface != palette_.surface ||
      parsed.surfaceHover != palette_.surfaceHover ||
      parsed.border != palette_.border || parsed.accent != palette_.accent ||
      parsed.text != palette_.text || parsed.muted != palette_.muted ||
      parsed.urgent != palette_.urgent;
  const bool statusChanged =
      !providerValid_ || reasonId_ != QStringLiteral("provider-ok");
  palette_ = parsed;
  providerValid_ = true;
  reasonId_ = QStringLiteral("provider-ok");
  if (changed) {
    ++revision_;
    emit paletteChanged();
  }
  if (statusChanged)
    emit this->statusChanged();
  refreshWatchPaths();
  return true;
}

void SettingsTheme::watchConfig(const QString &path,
                                const QStringList &statePaths) {
  watchPaths_.clear();
  const auto appendPath = [this](const QString &candidate) {
    if (candidate.isEmpty())
      return;
    const QString absolute = QFileInfo(candidate).absoluteFilePath();
    if (!watchPaths_.contains(absolute))
      watchPaths_.append(absolute);
  };
  appendPath(path);
  for (const QString &statePath : statePaths)
    appendPath(statePath);
  refreshWatchPaths();
}

void SettingsTheme::scheduleReload() {
  refreshWatchPaths();
  reloadTimer_.start();
}

void SettingsTheme::refreshWatchPaths() {
  const QStringList oldFiles = watcher_.files();
  if (!oldFiles.isEmpty())
    watcher_.removePaths(oldFiles);
  const QStringList oldDirectories = watcher_.directories();
  if (!oldDirectories.isEmpty())
    watcher_.removePaths(oldDirectories);
  QStringList files;
  QStringList directories;
  for (const QString &path : std::as_const(watchPaths_)) {
    const QFileInfo entry(path);
    if (entry.exists()) {
      if (entry.isDir()) {
        if (!directories.contains(entry.absoluteFilePath()))
          directories.append(entry.absoluteFilePath());
      } else if (entry.isFile() && !files.contains(entry.absoluteFilePath())) {
        files.append(entry.absoluteFilePath());
      }
    }
    const QString parent = entry.absolutePath();
    if (QFileInfo(parent).isDir() && !directories.contains(parent))
      directories.append(parent);
  }
  if (!files.isEmpty())
    watcher_.addPaths(files);
  if (!directories.isEmpty())
    watcher_.addPaths(directories);
}
