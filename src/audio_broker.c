// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L

#include "settings_internal.h"

#include <json-c/json.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef SYNAPSE_SETTINGS_VERSION
#define SYNAPSE_SETTINGS_VERSION "0.7.0-alpha.1"
#endif

#define BROKER_STREAM_LIMIT SETTINGS_AUDIO_BROKER_STREAM_LIMIT
#define BROKER_EVENT_LINE_LIMIT 511U

static volatile sig_atomic_t stop_requested;

static const char *broker_pactl_binary(void) {
#ifdef SYNAPSE_SETTINGS_TEST_HOOKS
  const char *override = getenv("SYNAPSE_PACTL");
  if (override && *override)
    return override;
#endif
  return "/usr/bin/pactl";
}

static void broker_usage(FILE *out) {
  fputs("Usage:\n"
        "  synapse-audio-route-broker --probe [--format text|json]\n"
        "  synapse-audio-route-broker --foreground\n"
        "  synapse-audio-route-broker --version\n",
        out);
}

static void handle_stop(int signal_number) {
  (void)signal_number;
  stop_requested = 1;
}

static int install_signal_handlers(void) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = handle_stop;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGTERM, &action, NULL) != 0 ||
      sigaction(SIGINT, &action, NULL) != 0)
    return -1;
  return 0;
}

static void emit_status(int active, const char *reason, unsigned generation,
                        size_t baseline_count) {
  settings_audio_broker_status status;
  if (reason)
    settings_audio_broker_status_unavailable(&status, reason, generation,
                                             baseline_count);
  else
    settings_audio_broker_status_ready(&status, active, generation,
                                       baseline_count);
  (void)settings_audio_broker_status_print(&status, "json");
}

static void print_receipt(const settings_audio_broker_receipt *receipt) {
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-route-broker-receipt/v1"));
  json_object_object_add(root, "status",
                         json_object_new_string(receipt->status));
  json_object_object_add(root, "reason",
                         receipt->reason[0]
                             ? json_object_new_string(receipt->reason)
                             : json_object_new_null());
  json_object_object_add(root, "mode",
                         json_object_new_string("new-streams-only"));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "stream",
                         json_object_new_string(receipt->stream));
  json_object_object_add(root, "direction",
                         json_object_new_string(receipt->direction));
  json_object_object_add(root, "device",
                         receipt->device[0]
                             ? json_object_new_string(receipt->device)
                             : json_object_new_null());
  json_object_object_add(root, "rule",
                         receipt->rule[0]
                             ? json_object_new_string(receipt->rule)
                             : json_object_new_null());
  json_object_object_add(root, "source",
                         receipt->source[0]
                             ? json_object_new_string(receipt->source)
                             : json_object_new_null());
  json_object_object_add(root, "policyGeneration",
                         json_object_new_int64(receipt->policy_generation));
  json_object_object_add(root, "changed",
                         json_object_new_boolean(receipt->changed));
  json_object_object_add(root, "routingApplied",
                         json_object_new_boolean(receipt->routing_applied));
  json_object_object_add(root, "verified",
                         json_object_new_boolean(receipt->verified));
  json_object_object_add(
      root, "compensationAttempted",
      json_object_new_boolean(receipt->compensation_attempted));
  json_object_object_add(
      root, "compensationVerified",
      json_object_new_boolean(receipt->compensation_verified));
  json_object_object_add(root, "persistentPidRule", json_object_new_boolean(0));
  json_object_object_add(root, "existingStreamMigration",
                         json_object_new_boolean(0));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  fflush(stdout);
  json_object_put(root);
}

static pid_t start_subscriber(int *read_fd) {
  int pipefd[2];
  if (pipe(pipefd) != 0)
    return -1;
  pid_t child = fork();
  if (child < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  if (child == 0) {
    struct sigaction reset;
    memset(&reset, 0, sizeof(reset));
    reset.sa_handler = SIG_DFL;
    sigemptyset(&reset.sa_mask);
    (void)sigaction(SIGTERM, &reset, NULL);
    (void)sigaction(SIGINT, &reset, NULL);
    if (dup2(pipefd[1], STDOUT_FILENO) < 0)
      _exit(126);
    int null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (null_fd >= 0) {
      (void)dup2(null_fd, STDERR_FILENO);
      close(null_fd);
    }
    close(pipefd[0]);
    close(pipefd[1]);
    (void)setenv("LC_ALL", "C", 1);
    (void)setenv("LANG", "C", 1);
    const char *pactl = broker_pactl_binary();
    if (strchr(pactl, '/'))
      execl(pactl, pactl, "subscribe", (char *)NULL);
    else
      execlp(pactl, pactl, "subscribe", (char *)NULL);
    _exit(127);
  }
  close(pipefd[1]);
  int flags = fcntl(pipefd[0], F_GETFL);
  if (flags >= 0)
    (void)fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
  *read_fd = pipefd[0];
  return child;
}

static void stop_subscriber(pid_t child, int read_fd) {
  if (read_fd >= 0)
    close(read_fd);
  if (child <= 0)
    return;
  (void)kill(child, SIGTERM);
  int status = 0;
  for (unsigned attempt = 0; attempt < 20U; attempt++) {
    pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child || waited < 0)
      return;
    struct timespec delay = {0, 10L * 1000L * 1000L};
    (void)nanosleep(&delay, NULL);
  }
  (void)kill(child, SIGKILL);
  (void)waitpid(child, &status, 0);
}

static int tracked_find(char tracked[][32], size_t count,
                        const char *stream_id) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(tracked[i], stream_id) == 0)
      return (int)i;
  return -1;
}

static int tracked_add(char tracked[][32], size_t *count,
                       const char *stream_id) {
  if (tracked_find(tracked, *count, stream_id) >= 0)
    return 0;
  if (*count >= BROKER_STREAM_LIMIT)
    return -1;
  size_t length = strlen(stream_id);
  if (length >= sizeof(tracked[0]))
    return -1;
  memcpy(tracked[*count], stream_id, length + 1U);
  (*count)++;
  return 0;
}

static void tracked_remove(char tracked[][32], size_t *count,
                           const char *stream_id) {
  int found = tracked_find(tracked, *count, stream_id);
  if (found < 0)
    return;
  size_t index = (size_t)found;
  memmove(&tracked[index], &tracked[index + 1U],
          (*count - index - 1U) * sizeof(tracked[0]));
  (*count)--;
}

static int parse_event(const char *line, int *is_new, int *is_remove,
                       char *stream_id, size_t stream_size) {
  *is_new = 0;
  *is_remove = 0;
  const char *kind = NULL;
  const char *prefix = NULL;
  const char *digits = NULL;
  if (strncmp(line, "Event 'new' on ", 15U) == 0) {
    *is_new = 1;
    kind = line + 15U;
  } else if (strncmp(line, "Event 'remove' on ", 18U) == 0) {
    *is_remove = 1;
    kind = line + 18U;
  } else {
    return 0;
  }
  if (strncmp(kind, "sink-input #", 12U) == 0) {
    prefix = "playback";
    digits = kind + 12U;
  } else if (strncmp(kind, "source-output #", 15U) == 0) {
    prefix = "recording";
    digits = kind + 15U;
  } else {
    *is_new = 0;
    *is_remove = 0;
    return 0;
  }
  if (!*digits || (digits[0] == '0' && digits[1]))
    return -1;
  for (const char *cursor = digits; *cursor; cursor++)
    if (*cursor < '0' || *cursor > '9')
      return -1;
  errno = 0;
  char *end = NULL;
  long index = strtol(digits, &end, 10);
  if (errno != 0 || !end || *end || index < 0 || index > INT_MAX)
    return -1;
  int written = snprintf(stream_id, stream_size, "%s-%ld", prefix, index);
  return written > 0 && (size_t)written < stream_size ? 1 : -1;
}

static int probe_broker(const char *format) {
  settings_audio_broker_status runtime_status;
  if (settings_audio_broker_status_query(&runtime_status) != 0)
    return 1;
  if (runtime_status.active ||
      strcmp(runtime_status.status, "Unavailable") == 0) {
    (void)settings_audio_broker_status_print(&runtime_status, format);
    return runtime_status.active ? 0 : 1;
  }

  char streams[BROKER_STREAM_LIMIT][32];
  size_t stream_count = 0;
  const char *reason = NULL;
  unsigned generation = 0;
  int present = 0;
  if (settings_audio_broker_stream_ids(streams, BROKER_STREAM_LIMIT,
                                       &stream_count, &reason) != 0) {
    settings_audio_broker_status_unavailable(
        &runtime_status, reason ? reason : "audio-unavailable", 0, 0);
    (void)settings_audio_broker_status_print(&runtime_status, format);
    return 1;
  }
  if (settings_audio_route_policy_state(&generation, &present) != 0) {
    settings_audio_broker_status_unavailable(
        &runtime_status, "policy-unavailable", 0, stream_count);
    (void)settings_audio_broker_status_print(&runtime_status, format);
    return 1;
  }
  (void)present;
  settings_audio_broker_status_ready(&runtime_status, 0, generation,
                                     stream_count);
  (void)settings_audio_broker_status_print(&runtime_status, format);
  return 0;
}

static int run_broker(size_t test_event_limit) {
  if (install_signal_handlers() != 0)
    return 1;
  settings_audio_broker_runtime runtime;
  const char *reason = NULL;
  if (settings_audio_broker_runtime_acquire(&runtime, &reason) != 0) {
    emit_status(0, reason ? reason : "runtime-unavailable", 0, 0);
    return 1;
  }
  int subscriber_fd = -1;
  pid_t subscriber = start_subscriber(&subscriber_fd);
  if (subscriber < 0) {
    emit_status(0, "subscriber-unavailable", 0, 0);
    settings_audio_broker_runtime_release(&runtime);
    return 1;
  }
  char tracked[BROKER_STREAM_LIMIT][32];
  size_t tracked_count = 0;
  if (settings_audio_broker_stream_ids(tracked, BROKER_STREAM_LIMIT,
                                       &tracked_count, &reason) != 0) {
    emit_status(0, reason ? reason : "audio-unavailable", 0, 0);
    settings_audio_broker_runtime_release(&runtime);
    stop_subscriber(subscriber, subscriber_fd);
    return 1;
  }
  const size_t baseline_count = tracked_count;
  unsigned generation = 0;
  int present = 0;
  if (settings_audio_route_policy_state(&generation, &present) != 0) {
    emit_status(0, "policy-unavailable", 0, baseline_count);
    settings_audio_broker_runtime_release(&runtime);
    stop_subscriber(subscriber, subscriber_fd);
    return 1;
  }
  (void)present;
  if (settings_audio_broker_runtime_listen(&runtime, &reason) != 0) {
    emit_status(0, reason ? reason : "runtime-state-invalid", generation,
                baseline_count);
    settings_audio_broker_runtime_release(&runtime);
    stop_subscriber(subscriber, subscriber_fd);
    return 1;
  }
  emit_status(1, NULL, generation, baseline_count);

  char line[BROKER_EVENT_LINE_LIMIT + 1U] = {0};
  size_t line_used = 0;
  size_t processed = 0;
  int result = 1;
  while (!stop_requested) {
    struct pollfd descriptors[2] = {
        {subscriber_fd, POLLIN | POLLHUP, 0},
        {settings_audio_broker_runtime_fd(&runtime), POLLIN, 0},
    };
    int ready = poll(descriptors, 2, 250);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready < 0)
      break;
    if (ready == 0)
      continue;
    if (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
      emit_status(0, "runtime-state-invalid", generation, baseline_count);
      goto done;
    }
    if (descriptors[1].revents & POLLIN) {
      if (settings_audio_broker_runtime_serve(&runtime, generation,
                                              baseline_count) != 0) {
        emit_status(0, "runtime-state-invalid", generation, baseline_count);
        goto done;
      }
    }
    if (!(descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)))
      continue;
    char chunk[256];
    ssize_t count = read(subscriber_fd, chunk, sizeof(chunk));
    if (count < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
      continue;
    if (count <= 0)
      break;
    for (ssize_t i = 0; i < count; i++) {
      unsigned char byte = (unsigned char)chunk[i];
      if (byte == '\n') {
        line[line_used] = '\0';
        int is_new = 0;
        int is_remove = 0;
        char stream_id[32] = {0};
        int parsed = parse_event(line, &is_new, &is_remove, stream_id,
                                 sizeof(stream_id));
        line_used = 0;
        if (parsed < 0) {
          emit_status(0, "invalid-subscriber-event", generation,
                      baseline_count);
          goto done;
        }
        if (parsed > 0 && is_remove)
          tracked_remove(tracked, &tracked_count, stream_id);
        if (parsed > 0 && is_new &&
            tracked_find(tracked, tracked_count, stream_id) < 0) {
          if (tracked_add(tracked, &tracked_count, stream_id) != 0) {
            emit_status(0, "stream-limit", generation, baseline_count);
            goto done;
          }
          settings_audio_broker_receipt receipt;
          if (settings_audio_broker_apply_new(stream_id, &receipt) != 0) {
            emit_status(0, "broker-internal-error", generation, baseline_count);
            goto done;
          }
          print_receipt(&receipt);
          if (receipt.source[0])
            generation = receipt.policy_generation;
          processed++;
          if (test_event_limit && processed >= test_event_limit) {
            result = 0;
            goto done;
          }
        }
        continue;
      }
      if (byte < 0x20U || byte == 0x7fU ||
          line_used >= BROKER_EVENT_LINE_LIMIT) {
        emit_status(0, "invalid-subscriber-event", generation, baseline_count);
        goto done;
      }
      line[line_used++] = (char)byte;
    }
  }
  if (stop_requested)
    result = 0;
  else
    emit_status(0, "subscriber-ended", generation, baseline_count);

done:
  settings_audio_broker_runtime_release(&runtime);
  stop_subscriber(subscriber, subscriber_fd);
  return result;
}

int settings_audio_broker_command(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    puts("synapse-audio-route-broker " SYNAPSE_SETTINGS_VERSION);
    return 0;
  }
  if (argc == 2 &&
      (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    broker_usage(stdout);
    return 0;
  }
  if (argc >= 2 && strcmp(argv[1], "--probe") == 0) {
    const char *format = "text";
    if (argc == 4 && strcmp(argv[2], "--format") == 0)
      format = argv[3];
    else if (argc != 2) {
      broker_usage(stderr);
      return 2;
    }
    if (strcmp(format, "text") != 0 && strcmp(format, "json") != 0) {
      broker_usage(stderr);
      return 2;
    }
    return probe_broker(format);
  }
  if (argc >= 2 && strcmp(argv[1], "--foreground") == 0) {
    size_t test_event_limit = 0;
#ifdef SYNAPSE_SETTINGS_TEST_HOOKS
    if (argc == 4 && strcmp(argv[2], "--test-exit-after-events") == 0) {
      errno = 0;
      char *end = NULL;
      unsigned long parsed = strtoul(argv[3], &end, 10);
      if (errno != 0 || !end || *end || parsed == 0 ||
          parsed > BROKER_STREAM_LIMIT) {
        broker_usage(stderr);
        return 2;
      }
      test_event_limit = (size_t)parsed;
    } else
#endif
        if (argc != 2) {
      broker_usage(stderr);
      return 2;
    }
    return run_broker(test_event_limit);
  }
  broker_usage(stderr);
  return 2;
}

int main(int argc, char **argv) {
  return settings_audio_broker_command(argc, argv);
}
