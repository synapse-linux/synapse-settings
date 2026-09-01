// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L

#include "settings_internal.h"

#include <json-c/json.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define AUDIO_GOXLR_DEVICE_LIMIT 8U
#define AUDIO_GOXLR_RESPONSE_LIMIT 65536U
#define AUDIO_GOXLR_TIMEOUT_MS 3000
#define AUDIO_GOXLR_PROVIDER_SCHEMA "synapse.goxlr.provider-status/v2"
#define AUDIO_GOXLR_STATUS_SCHEMA "synapse.settings.audio-goxlr-status/v1"

typedef struct {
  char id[16];
  char model[16];
  int system_output_supported;
} audio_goxlr_device;

typedef struct {
  audio_goxlr_device devices[AUDIO_GOXLR_DEVICE_LIMIT];
  size_t device_count;
  int truncated;
} audio_goxlr_provider_status;

typedef struct {
  const char *cursor;
  const char *end;
} audio_goxlr_json_parser;

typedef struct {
  char *data;
  size_t size;
  int status;
  int timed_out;
  int too_large;
  int failed;
  int launch_failed;
} audio_goxlr_capture;

static const char *audio_goxlr_binary(void) {
#ifdef SYNAPSE_SETTINGS_TEST_HOOKS
  const char *override = getenv("SYNAPSE_GOXLR");
  if (override && *override) {
    static char test_path[PATH_MAX];
    size_t length = strnlen(override, sizeof(test_path));
    if (length >= sizeof(test_path) || override[0] != '/')
      return "";
    memcpy(test_path, override, length + 1U);
    return test_path;
  }
#endif
  return "/usr/bin/synapse-goxlr";
}

static void audio_goxlr_skip_space(audio_goxlr_json_parser *parser) {
  while (parser->cursor < parser->end &&
         (*parser->cursor == ' ' || *parser->cursor == '\t' ||
          *parser->cursor == '\r' || *parser->cursor == '\n'))
    parser->cursor++;
}

static int audio_goxlr_consume(audio_goxlr_json_parser *parser, char expected) {
  audio_goxlr_skip_space(parser);
  if (parser->cursor >= parser->end || *parser->cursor != expected)
    return 0;
  parser->cursor++;
  return 1;
}

static int audio_goxlr_literal(audio_goxlr_json_parser *parser,
                               const char *value) {
  audio_goxlr_skip_space(parser);
  size_t length = strlen(value);
  if ((size_t)(parser->end - parser->cursor) < length ||
      memcmp(parser->cursor, value, length) != 0)
    return 0;
  parser->cursor += length;
  return 1;
}

static int audio_goxlr_string(audio_goxlr_json_parser *parser, char *output,
                              size_t capacity) {
  audio_goxlr_skip_space(parser);
  if (!capacity || parser->cursor >= parser->end || *parser->cursor != '"')
    return 0;
  parser->cursor++;
  size_t used = 0;
  while (parser->cursor < parser->end) {
    unsigned char value = (unsigned char)*parser->cursor++;
    if (value == '"') {
      output[used] = '\0';
      return 1;
    }
    if (value == '\\') {
      if (parser->cursor >= parser->end)
        return 0;
      unsigned char escaped = (unsigned char)*parser->cursor++;
      if (escaped == '"' || escaped == '\\' || escaped == '/')
        value = escaped;
      else if (escaped == 'b')
        value = '\b';
      else if (escaped == 'f')
        value = '\f';
      else if (escaped == 'n')
        value = '\n';
      else if (escaped == 'r')
        value = '\r';
      else if (escaped == 't')
        value = '\t';
      else
        return 0;
    } else if (value < 0x20U || value > 0x7eU) {
      return 0;
    }
    if (used + 1U >= capacity)
      return 0;
    output[used++] = (char)value;
  }
  return 0;
}

static int audio_goxlr_bool(audio_goxlr_json_parser *parser, int *value) {
  if (audio_goxlr_literal(parser, "true")) {
    *value = 1;
    return 1;
  }
  if (audio_goxlr_literal(parser, "false")) {
    *value = 0;
    return 1;
  }
  return 0;
}

static int audio_goxlr_unsigned(audio_goxlr_json_parser *parser,
                                unsigned int *value) {
  audio_goxlr_skip_space(parser);
  if (parser->cursor >= parser->end || *parser->cursor < '0' ||
      *parser->cursor > '9')
    return 0;
  if (*parser->cursor == '0' && parser->cursor + 1 < parser->end &&
      parser->cursor[1] >= '0' && parser->cursor[1] <= '9')
    return 0;
  unsigned int result = 0;
  do {
    unsigned int digit = (unsigned int)(*parser->cursor - '0');
    if (result > (UINT_MAX - digit) / 10U)
      return 0;
    result = result * 10U + digit;
    parser->cursor++;
  } while (parser->cursor < parser->end && *parser->cursor >= '0' &&
           *parser->cursor <= '9');
  *value = result;
  return 1;
}

static int audio_goxlr_nullable_enum(audio_goxlr_json_parser *parser,
                                     const char *first, const char *second,
                                     const char *third, const char *fourth) {
  audio_goxlr_skip_space(parser);
  if (parser->cursor < parser->end && *parser->cursor == 'n')
    return audio_goxlr_literal(parser, "null");
  char value[24];
  if (!audio_goxlr_string(parser, value, sizeof(value)))
    return 0;
  return strcmp(value, first) == 0 || (second && strcmp(value, second) == 0) ||
         (third && strcmp(value, third) == 0) ||
         (fourth && strcmp(value, fourth) == 0);
}

static int audio_goxlr_system_output(audio_goxlr_json_parser *parser) {
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0;
  if (audio_goxlr_consume(parser, '}'))
    return 0;
  for (;;) {
    char key[32];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0;
    int boolean_value = 0;
    unsigned int integer_value = 0;
    if (strcmp(key, "routeToLineOut") == 0) {
      bit = 1U;
      if (!audio_goxlr_bool(parser, &boolean_value))
        return 0;
    } else if (strcmp(key, "systemVolume") == 0) {
      bit = 2U;
      if (!audio_goxlr_unsigned(parser, &integer_value) || integer_value > 255U)
        return 0;
    } else if (strcmp(key, "lineOutVolume") == 0) {
      bit = 4U;
      if (!audio_goxlr_unsigned(parser, &integer_value) || integer_value > 255U)
        return 0;
    } else if (strcmp(key, "systemFader") == 0) {
      bit = 8U;
      if (!audio_goxlr_nullable_enum(parser, "A", "B", "C", "D"))
        return 0;
    } else if (strcmp(key, "systemMuteState") == 0) {
      bit = 16U;
      if (!audio_goxlr_nullable_enum(parser, "Unmuted", "MutedToX",
                                     "MutedToAll", NULL))
        return 0;
    } else if (strcmp(key, "lineOutMix") == 0) {
      bit = 32U;
      char mix[8];
      if (!audio_goxlr_string(parser, mix, sizeof(mix)) ||
          (strcmp(mix, "A") != 0 && strcmp(mix, "B") != 0))
        return 0;
    } else if (strcmp(key, "submixEnabled") == 0) {
      bit = 64U;
      if (!audio_goxlr_bool(parser, &boolean_value))
        return 0;
    } else {
      return 0;
    }
    if ((fields & bit) != 0U)
      return 0;
    fields |= bit;
    if (audio_goxlr_consume(parser, '}'))
      break;
    if (!audio_goxlr_consume(parser, ','))
      return 0;
  }
  return fields == 127U;
}

static int audio_goxlr_valid_id(const char *value) {
  return strlen(value) == 7U && memcmp(value, "goxlr-", 6U) == 0 &&
         value[6] >= '1' && value[6] <= '8';
}

static int audio_goxlr_valid_model(const char *value) {
  return strcmp(value, "GoXLR Mini") == 0 || strcmp(value, "GoXLR") == 0 ||
         strcmp(value, "Unknown") == 0;
}

static int audio_goxlr_device_status(audio_goxlr_json_parser *parser,
                                     audio_goxlr_device *device) {
  memset(device, 0, sizeof(*device));
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0;
  if (audio_goxlr_consume(parser, '}'))
    return 0;
  for (;;) {
    char key[32];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0;
    if (strcmp(key, "id") == 0) {
      bit = 1U;
      if (!audio_goxlr_string(parser, device->id, sizeof(device->id)))
        return 0;
    } else if (strcmp(key, "model") == 0) {
      bit = 2U;
      if (!audio_goxlr_string(parser, device->model, sizeof(device->model)))
        return 0;
    } else if (strcmp(key, "systemOutputSupported") == 0) {
      bit = 4U;
      if (!audio_goxlr_bool(parser, &device->system_output_supported))
        return 0;
    } else if (strcmp(key, "stateAuthority") == 0) {
      bit = 8U;
      char authority[40];
      if (!audio_goxlr_string(parser, authority, sizeof(authority)) ||
          strcmp(authority, "provider-profile-model") != 0)
        return 0;
    } else if (strcmp(key, "systemOutput") == 0) {
      bit = 16U;
      if (!audio_goxlr_system_output(parser))
        return 0;
    } else {
      return 0;
    }
    if ((fields & bit) != 0U)
      return 0;
    fields |= bit;
    if (audio_goxlr_consume(parser, '}'))
      break;
    if (!audio_goxlr_consume(parser, ','))
      return 0;
  }
  return fields == 31U && audio_goxlr_valid_id(device->id) &&
         audio_goxlr_valid_model(device->model);
}

static int audio_goxlr_devices(audio_goxlr_json_parser *parser,
                               audio_goxlr_provider_status *status) {
  if (!audio_goxlr_consume(parser, '['))
    return 0;
  status->device_count = 0;
  if (audio_goxlr_consume(parser, ']'))
    return 1;
  for (;;) {
    if (status->device_count >= AUDIO_GOXLR_DEVICE_LIMIT ||
        !audio_goxlr_device_status(parser,
                                   &status->devices[status->device_count]))
      return 0;
    for (size_t index = 0; index < status->device_count; index++)
      if (strcmp(status->devices[index].id,
                 status->devices[status->device_count].id) == 0)
        return 0;
    status->device_count++;
    if (audio_goxlr_consume(parser, ']'))
      return 1;
    if (!audio_goxlr_consume(parser, ','))
      return 0;
  }
}

static int audio_goxlr_device_compare(const void *left, const void *right) {
  const audio_goxlr_device *a = left;
  const audio_goxlr_device *b = right;
  return strcmp(a->id, b->id);
}

static int audio_goxlr_parse_provider(const char *json, size_t length,
                                      audio_goxlr_provider_status *status) {
  if (!json || !length || length > AUDIO_GOXLR_RESPONSE_LIMIT || !status)
    return 0;
  memset(status, 0, sizeof(*status));
  audio_goxlr_json_parser parser = {.cursor = json, .end = json + length};
  if (!audio_goxlr_consume(&parser, '{'))
    return 0;
  unsigned int fields = 0;
  unsigned int declared_count = UINT_MAX;
  if (audio_goxlr_consume(&parser, '}'))
    return 0;
  for (;;) {
    char key[32];
    if (!audio_goxlr_string(&parser, key, sizeof(key)) ||
        !audio_goxlr_consume(&parser, ':'))
      return 0;
    unsigned int bit = 0;
    if (strcmp(key, "schema") == 0) {
      bit = 1U;
      char schema[48];
      if (!audio_goxlr_string(&parser, schema, sizeof(schema)) ||
          strcmp(schema, AUDIO_GOXLR_PROVIDER_SCHEMA) != 0)
        return 0;
    } else if (strcmp(key, "deviceCount") == 0) {
      bit = 2U;
      if (!audio_goxlr_unsigned(&parser, &declared_count) ||
          declared_count > AUDIO_GOXLR_DEVICE_LIMIT)
        return 0;
    } else if (strcmp(key, "truncated") == 0) {
      bit = 4U;
      if (!audio_goxlr_bool(&parser, &status->truncated))
        return 0;
    } else if (strcmp(key, "devices") == 0) {
      bit = 8U;
      if (!audio_goxlr_devices(&parser, status))
        return 0;
    } else {
      return 0;
    }
    if ((fields & bit) != 0U)
      return 0;
    fields |= bit;
    if (audio_goxlr_consume(&parser, '}'))
      break;
    if (!audio_goxlr_consume(&parser, ','))
      return 0;
  }
  audio_goxlr_skip_space(&parser);
  if (fields != 15U || parser.cursor != parser.end ||
      declared_count != status->device_count ||
      (status->truncated && status->device_count != AUDIO_GOXLR_DEVICE_LIMIT))
    return 0;
  qsort(status->devices, status->device_count, sizeof(status->devices[0]),
        audio_goxlr_device_compare);
  return 1;
}

static void audio_goxlr_capture_free(audio_goxlr_capture *capture) {
  free(capture->data);
  memset(capture, 0, sizeof(*capture));
}

static int audio_goxlr_now_ms(int64_t *value) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    return 0;
  if (now.tv_sec < 0 || (uint64_t)now.tv_sec > (uint64_t)INT64_MAX / 1000U)
    return 0;
  *value = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
  return 1;
}

static void audio_goxlr_terminate_child(pid_t child) {
  (void)kill(-child, SIGKILL);
  (void)kill(child, SIGKILL);
  while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
  }
}

static void audio_goxlr_child_fail(int descriptor, int status) {
  const unsigned char marker = 1U;
  while (write(descriptor, &marker, sizeof(marker)) < 0 && errno == EINTR) {
  }
  _exit(status);
}

static audio_goxlr_capture audio_goxlr_capture_status(const char *binary) {
  audio_goxlr_capture capture = {0};
  int64_t started = 0;
  if (!audio_goxlr_now_ms(&started) ||
      started > INT64_MAX - AUDIO_GOXLR_TIMEOUT_MS) {
    capture.failed = 1;
    return capture;
  }
  const int64_t deadline = started + AUDIO_GOXLR_TIMEOUT_MS;

  int descriptors[2];
  if (pipe(descriptors) != 0) {
    capture.failed = 1;
    return capture;
  }
  int launch_descriptors[2];
  if (pipe(launch_descriptors) != 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    capture.failed = 1;
    return capture;
  }
  int launch_flags = fcntl(launch_descriptors[1], F_GETFD);
  if (launch_flags < 0 ||
      fcntl(launch_descriptors[1], F_SETFD, launch_flags | FD_CLOEXEC) < 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    close(launch_descriptors[0]);
    close(launch_descriptors[1]);
    capture.failed = 1;
    return capture;
  }
  pid_t child = fork();
  if (child < 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    close(launch_descriptors[0]);
    close(launch_descriptors[1]);
    capture.failed = 1;
    return capture;
  }
  if (child == 0) {
    close(launch_descriptors[0]);
    if (setpgid(0, 0) != 0 || dup2(descriptors[1], STDOUT_FILENO) < 0)
      audio_goxlr_child_fail(launch_descriptors[1], 126);
    int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 ||
        dup2(null_fd, STDERR_FILENO) < 0)
      audio_goxlr_child_fail(launch_descriptors[1], 126);
    close(null_fd);
    close(descriptors[0]);
    close(descriptors[1]);
    if (setenv("LC_ALL", "C", 1) != 0 || setenv("LANG", "C", 1) != 0)
      audio_goxlr_child_fail(launch_descriptors[1], 126);
    char *const argv[] = {(char *)binary, "provider-status", "--format", "json",
                          NULL};
    execv(binary, argv);
    audio_goxlr_child_fail(launch_descriptors[1], 127);
  }

  (void)setpgid(child, child);
  close(descriptors[1]);
  close(launch_descriptors[1]);
  int flags = fcntl(descriptors[0], F_GETFL);
  if (flags < 0 || fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) < 0) {
    close(descriptors[0]);
    close(launch_descriptors[0]);
    audio_goxlr_terminate_child(child);
    capture.failed = 1;
    return capture;
  }
  capture.data = malloc(AUDIO_GOXLR_RESPONSE_LIMIT + 2U);
  if (!capture.data) {
    close(descriptors[0]);
    close(launch_descriptors[0]);
    audio_goxlr_terminate_child(child);
    capture.failed = 1;
    return capture;
  }

  int eof = 0;
  while (!eof && !capture.too_large && !capture.failed) {
    int64_t now = 0;
    if (!audio_goxlr_now_ms(&now)) {
      capture.failed = 1;
      break;
    }
    if (now >= deadline) {
      capture.timed_out = 1;
      break;
    }
    int remaining = (int)(deadline - now);
    struct pollfd descriptor = {descriptors[0], POLLIN | POLLHUP, 0};
    int ready = poll(&descriptor, 1, remaining < 50 ? remaining : 50);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready < 0 || (descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
      capture.failed = 1;
      break;
    }
    if ((descriptor.revents & (POLLIN | POLLHUP)) == 0)
      continue;
    for (;;) {
      ssize_t count = read(descriptors[0], capture.data + capture.size,
                           AUDIO_GOXLR_RESPONSE_LIMIT + 1U - capture.size);
      if (count > 0) {
        capture.size += (size_t)count;
        if (capture.size > AUDIO_GOXLR_RESPONSE_LIMIT) {
          capture.too_large = 1;
          break;
        }
        continue;
      }
      if (count == 0)
        eof = 1;
      else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        capture.failed = 1;
      break;
    }
  }
  close(descriptors[0]);

  int wait_status = 0;
  pid_t waited = 0;
  while (!capture.timed_out && !capture.too_large && !capture.failed) {
    waited = waitpid(child, &wait_status, WNOHANG);
    if (waited == child)
      break;
    if (waited < 0 && errno == EINTR)
      continue;
    if (waited < 0) {
      capture.failed = 1;
      break;
    }
    int64_t now = 0;
    if (!audio_goxlr_now_ms(&now)) {
      capture.failed = 1;
      break;
    }
    if (now >= deadline) {
      capture.timed_out = 1;
      break;
    }
    int64_t delay_ms = deadline - now;
    if (delay_ms > 10)
      delay_ms = 10;
    struct timespec delay = {0, delay_ms * 1000L * 1000L};
    (void)nanosleep(&delay, NULL);
  }

  if (capture.timed_out || capture.too_large || capture.failed) {
    audio_goxlr_terminate_child(child);
    capture.status = capture.timed_out ? 124 : -1;
  } else if (waited != child || !WIFEXITED(wait_status)) {
    capture.status = -1;
  } else {
    capture.status = WEXITSTATUS(wait_status);
  }

  unsigned char launch_marker = 0U;
  ssize_t launch_count;
  do {
    launch_count =
        read(launch_descriptors[0], &launch_marker, sizeof(launch_marker));
  } while (launch_count < 0 && errno == EINTR);
  close(launch_descriptors[0]);
  if (launch_count > 0)
    capture.launch_failed = 1;
  else if (launch_count < 0 && !capture.timed_out && !capture.too_large)
    capture.failed = 1;
  capture.data[capture.size] = '\0';
  return capture;
}

static void
audio_goxlr_print_json(const char *status, const char *reason,
                       int provider_active,
                       const audio_goxlr_provider_status *provider) {
  json_object *root = json_object_new_object();
  json_object_object_add(root, "schema",
                         json_object_new_string(AUDIO_GOXLR_STATUS_SCHEMA));
  json_object_object_add(root, "status", json_object_new_string(status));
  if (reason)
    json_object_object_add(root, "reason", json_object_new_string(reason));
  else
    json_object_object_add(root, "reason", NULL);
  json_object_object_add(root, "providerActive",
                         json_object_new_boolean(provider_active));
  size_t device_count = provider_active ? provider->device_count : 0;
  json_object_object_add(root, "deviceCount",
                         json_object_new_int64((int64_t)device_count));
  json_object_object_add(
      root, "truncated",
      json_object_new_boolean(provider_active && provider->truncated));
  json_object *devices = json_object_new_array_ext((int)device_count);
  for (size_t index = 0; index < device_count; index++) {
    const audio_goxlr_device *device = &provider->devices[index];
    json_object *item = json_object_new_object();
    json_object_object_add(item, "id", json_object_new_string(device->id));
    json_object_object_add(item, "model",
                           json_object_new_string(device->model));
    json_object_object_add(
        item, "systemOutputSupported",
        json_object_new_boolean(device->system_output_supported));
    json_object_object_add(item, "controlAvailable",
                           json_object_new_boolean(0));
    json_object_array_add(devices, item);
  }
  json_object_object_add(root, "devices", devices);
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("provider-profile-model"));
  json_object_object_add(root, "hardwareReadback", json_object_new_boolean(0));
  json_object_object_add(root, "hardwareExactRollback",
                         json_object_new_boolean(0));
  json_object_object_add(root, "mutationAvailable", json_object_new_boolean(0));
  json_object_object_add(root, "readOnly", json_object_new_boolean(1));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static void
audio_goxlr_print_text(const char *status, const char *reason,
                       int provider_active,
                       const audio_goxlr_provider_status *provider) {
  if (!provider_active) {
    printf("GoXLR status: %s (%s)\n", status, reason);
    return;
  }
  printf("GoXLR provider ready: %zu device(s)%s\n", provider->device_count,
         provider->truncated ? " (truncated)" : "");
  for (size_t index = 0; index < provider->device_count; index++) {
    const audio_goxlr_device *device = &provider->devices[index];
    printf("  %s  %s  System output %s  control unavailable\n", device->id,
           device->model,
           device->system_output_supported ? "supported" : "unsupported");
  }
}

int settings_audio_goxlr_status_command(int argc, char **argv) {
  const char *format = "text";
  if (argc < 2 || strcmp(argv[1], "goxlr-status") != 0)
    return 2;
  for (int index = 2; index < argc; index++) {
    if (strcmp(argv[index], "--format") == 0 && index + 1 < argc)
      format = argv[++index];
    else if (strcmp(argv[index], "--json") == 0)
      format = "json";
    else
      return 2;
  }
  if (strcmp(format, "text") != 0 && strcmp(format, "json") != 0)
    return 2;

  const char *status = "Unavailable";
  const char *reason = "adapter-unavailable";
  int provider_active = 0;
  audio_goxlr_provider_status provider = {0};
  const char *binary = audio_goxlr_binary();
  int binary_available = 0;
  int binary_error = ENOENT;
  if (*binary) {
    if (access(binary, X_OK) == 0)
      binary_available = 1;
    else
      binary_error = errno;
  }
  if (binary_available) {
    audio_goxlr_capture capture = audio_goxlr_capture_status(binary);
    if (!capture.data || capture.failed || capture.launch_failed) {
      status = "Failed";
      reason = "status-unavailable";
    } else if (capture.too_large) {
      status = "Failed";
      reason = "response-too-large";
    } else if (capture.timed_out) {
      status = "Failed";
      reason = "timeout";
    } else if (capture.status < 0) {
      status = "Failed";
      reason = "status-unavailable";
    } else if (capture.status != 0) {
      status = "Inactive";
      reason = "provider-inactive";
    } else if (!audio_goxlr_parse_provider(capture.data, capture.size,
                                           &provider)) {
      status = "Failed";
      reason = "invalid-response";
    } else {
      status = "Ready";
      reason = NULL;
      provider_active = 1;
    }
    audio_goxlr_capture_free(&capture);
  } else if (*binary && binary_error != ENOENT && binary_error != ENOTDIR) {
    status = "Failed";
    reason = "status-unavailable";
  }

  if (strcmp(format, "json") == 0)
    audio_goxlr_print_json(status, reason, provider_active, &provider);
  else
    audio_goxlr_print_text(status, reason, provider_active, &provider);
  return 0;
}
