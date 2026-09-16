// SPDX-License-Identifier: MIT
#pragma once
#include <QElapsedTimer>
#include <QTest>
#include <signal.h>
#include <sys/wait.h>

// Test-owned forked child only; never process-name matching or global cleanup.
struct FixtureChild {
  pid_t pid = 0;
  bool reaped = false;
  int status = 0;
  bool reap() {
    if (!reaped && pid > 1) reaped = waitpid(pid, &status, WNOHANG) == pid;
    return reaped;
  }
  ~FixtureChild() {
    if (pid <= 1 || reaped) return;
    (void)kill(pid, SIGKILL);
    QElapsedTimer timer;
    timer.start();
    while (!reap() && timer.elapsed() < 2000) QTest::qWait(10);
  }
};
