// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>

// Finite command only, including the finite GUI activation client. The
// independently persistent GUI is never owned/cancelled by this runner.
class AudioCommand final : public QObject {
public:
  enum class Environment { Audio, GuiActivation };
  using Callback =
      std::function<void(int, const QByteArray &, const QString &)>;
  AudioCommand(const QString &program, const QStringList &arguments,
               int outputLimit, int timeoutMilliseconds, Callback callback,
               QObject *parent,
               Environment environment = Environment::Audio);
  ~AudioCommand() override;
  void start();
  void cancel();

private:
  QString drainStandardOutput();
  void killProcessGroup();
  void complete(int exitCode, const QString &errorId);
  QProcess process_;
  QTimer timer_;
  QByteArray output_;
  qsizetype outputLimit_ = 0;
  int timeoutMilliseconds_ = 0;
  qint64 processGroup_ = 0;
  Callback callback_;
  bool finished_ = false;
};
