// SPDX-License-Identifier: MIT
#include "audio_command_p.h"
#include <QProcessEnvironment>
#include <algorithm>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#include <utility>

namespace {
QProcessEnvironment audioCommandEnvironment(AudioCommand::Environment purpose) {
  QProcessEnvironment environment;
  const auto copy = [&environment](const char *name) {
    const QByteArray value = qgetenv(name);
    if (!value.isEmpty() && value.size() < 4096 && !value.contains('\0'))
      environment.insert(QString::fromLatin1(name),
                         QString::fromLocal8Bit(value));
  };
  // Policy files and owner-only broker/GoXLR IPC use HOME and XDG. pactl's
  // libpulse client also needs the selected user server/authentication context.
  for (const char *name : {"HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME",
                           "XDG_RUNTIME_DIR", "PULSE_SERVER", "PULSE_COOKIE",
                           "PULSE_RUNTIME_PATH", "DBUS_SESSION_BUS_ADDRESS"})
    copy(name);
#ifdef SYNAPSE_SETTINGS_GUI_TEST_HOOKS
  // Explicit test-only authorities, including the isolated staged C library.
  for (const char *name :
       {"LD_LIBRARY_PATH", "SYNAPSE_PACTL", "SYNAPSE_AUDIO_FIXTURES",
        "SYNAPSE_AUDIO_ROUTE_POLICY", "SYNAPSE_GOXLR",
        "SYNAPSE_GOXLR_GUI_TEST_LOG", "SYNAPSE_AUDIO_MOVE_LOG",
        "SYNAPSE_AUDIO_MOVE_MODE", "SYNAPSE_AUDIO_CONTROL_LOG",
        "SYNAPSE_AUDIO_CONTROL_MODE", "SYNAPSE_AUDIO_SELECTION_LOG",
        "SYNAPSE_AUDIO_SELECTION_MODE", "SYNAPSE_ADAPTER_REAL_BACKEND",
        "SYNAPSE_ADAPTER_OVERRIDE_RESPONSE", "SYNAPSE_ADAPTER_OVERRIDE_EXIT",
        "SYNAPSE_ADAPTER_WRAPPER_LOG"})
    copy(name);
#endif
  environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
  environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
  if (purpose == AudioCommand::Environment::GuiActivation) {
    // Presentation startup only. Never add display/locale authority to ordinary
    // Audio commands. JSON/diagnostics of the finite client remain en_US/neutral.
    // An absent LC_ALL must not override the inherited LANG/LC_MESSAGES.
    environment.remove(QStringLiteral("LC_ALL"));
    for (const char *name : {"XDG_SESSION_ID", "WAYLAND_DISPLAY", "DISPLAY",
                            "XDG_SESSION_TYPE", "XDG_CURRENT_DESKTOP", "XAUTHORITY",
                            "XDG_DATA_HOME", "XDG_CACHE_HOME", "LANG", "LC_ALL",
                            "LC_MESSAGES", "LANGUAGE"})
      copy(name);
#ifdef SYNAPSE_SETTINGS_GUI_TEST_HOOKS
    for (const char *name : {"SYNAPSE_SETTINGS_GOXLR_APP_LOG",
                            "SYNAPSE_SETTINGS_GOXLR_APP_MODE", "QT_QPA_PLATFORM"})
      copy(name);
#endif
  }
  return environment;
}

} // namespace

AudioCommand::AudioCommand(const QString &program, const QStringList &arguments,
                           int outputLimit, int timeoutMilliseconds,
                           Callback callback, QObject *parent,
                           Environment environment)
    : QObject(parent), outputLimit_(outputLimit),
      timeoutMilliseconds_(timeoutMilliseconds),
      callback_(std::move(callback)) {
  timer_.setSingleShot(true);
  process_.setProgram(program);
  process_.setArguments(arguments);
  process_.setProcessEnvironment(audioCommandEnvironment(environment));
  process_.setWorkingDirectory(QStringLiteral("/"));
  process_.setProcessChannelMode(QProcess::SeparateChannels);
  process_.setStandardInputFile(QProcess::nullDevice());
  process_.setStandardErrorFile(QProcess::nullDevice());
  const pid_t parentProcess = ::getpid();
  process_.setChildProcessModifier([parentProcess]() {
    if (::getppid() != parentProcess || ::setpgid(0, 0) != 0 ||
        ::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || ::getppid() != parentProcess)
      _exit(126);
  });
  connect(&process_, &QProcess::started, this, [this]() {
    const qint64 processId = process_.processId();
    if (processId > 0)
      processGroup_ = processId;
  });
  connect(&process_, &QProcess::readyReadStandardOutput, this, [this]() {
    const QString errorId = drainStandardOutput();
    if (!errorId.isEmpty())
      complete(-1, errorId);
  });
  connect(&process_, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
              complete(-1, QStringLiteral("start-failed"));
          });
  connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this](int exitCode, QProcess::ExitStatus status) {
            const QString errorId = drainStandardOutput();
            if (!errorId.isEmpty()) {
              complete(-1, errorId);
            } else if (status != QProcess::NormalExit) {
              complete(-1, QStringLiteral("process-crashed"));
            } else {
              complete(exitCode, QString());
            }
          });
  connect(&timer_, &QTimer::timeout, this,
          [this]() { complete(-1, QStringLiteral("timeout")); });
}

void AudioCommand::start() {
  timer_.start(timeoutMilliseconds_);
  process_.start();
}

AudioCommand::~AudioCommand() {
  finished_ = true;
  timer_.stop();
  QObject::disconnect(&process_, nullptr, this, nullptr);
  killProcessGroup();
}

QString AudioCommand::drainStandardOutput() {
  process_.setReadChannel(QProcess::StandardOutput);
  while (process_.bytesAvailable() > 0) {
    const qsizetype room = outputLimit_ + 1 - output_.size();
    if (room <= 0)
      return QStringLiteral("output-too-large");
    const qint64 requested = std::min<qint64>(process_.bytesAvailable(), room);
    QByteArray chunk;
    chunk.resize(static_cast<qsizetype>(requested));
    const qint64 received = process_.read(chunk.data(), requested);
    if (received <= 0)
      return QStringLiteral("output-read-failed");
    chunk.resize(static_cast<qsizetype>(received));
    output_.append(chunk);
    if (output_.size() > outputLimit_)
      return QStringLiteral("output-too-large");
  }
  return {};
}

void AudioCommand::killProcessGroup() {
  const qint64 processId =
      processGroup_ > 0 ? processGroup_ : process_.processId();
  if (processId <= 0 || ::kill(-static_cast<pid_t>(processId), SIGKILL) != 0)
    process_.kill();
  processGroup_ = 0;
  if (process_.state() != QProcess::NotRunning)
    (void)process_.waitForFinished(1000);
}

void AudioCommand::complete(int exitCode, const QString &errorId) {
  if (finished_)
    return;
  finished_ = true;
  timer_.stop();
  QObject::disconnect(&process_, nullptr, this, nullptr);
  killProcessGroup(); // normal leader exit does not discharge descendants
  Callback callback = std::move(callback_);
  QByteArray output = std::move(output_);
  deleteLater();
  callback(exitCode, output, errorId);
}

void AudioCommand::cancel() { complete(-1, QStringLiteral("cancelled")); }
