// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "settings_internal.h"

#include <json-c/json.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef O_PATH
#define O_PATH 010000000
#endif

#define AUDIO_ENDPOINT_LIMIT 64U
#define AUDIO_STREAM_LIMIT 128U
#define AUDIO_CARD_LIMIT 32U
#define AUDIO_LABEL_LIMIT 255U
#define AUDIO_ACK "synapse-settings/audio-default/v1"

typedef struct {
  char raw_name[SETTINGS_FIELD_LIMIT + 1U];
  char id[32];
  char label[AUDIO_LABEL_LIMIT + 1U];
  int index;
  int volume_percent;
  int muted;
  int is_default;
} audio_endpoint;

typedef struct {
  char id[32];
  char label[AUDIO_LABEL_LIMIT + 1U];
  char target[32];
  int volume_percent;
  int muted;
  pid_t process_pid;
  int backend_index;
  int target_index;
  int process_rule_available;
  const char *direction;
} audio_stream;

typedef struct {
  char raw_name[SETTINGS_FIELD_LIMIT + 1U];
  char id[32];
  char label[AUDIO_LABEL_LIMIT + 1U];
  char active_profile[AUDIO_LABEL_LIMIT + 1U];
} audio_card;

typedef struct {
  int available;
  const char *reason;
  audio_endpoint outputs[AUDIO_ENDPOINT_LIMIT];
  size_t output_count;
  audio_endpoint inputs[AUDIO_ENDPOINT_LIMIT];
  size_t input_count;
  audio_endpoint backend_sources[AUDIO_ENDPOINT_LIMIT];
  size_t backend_source_count;
  audio_stream streams[AUDIO_STREAM_LIMIT];
  size_t stream_count;
  audio_card cards[AUDIO_CARD_LIMIT];
  size_t card_count;
} audio_inventory;

typedef struct {
  char *data;
  int status;
  int timed_out;
} capture_result;

static const char *pactl_binary(void) {
#ifdef SYNAPSE_SETTINGS_TEST_HOOKS
  const char *override = getenv("SYNAPSE_PACTL");
  if (override && *override)
    return override;
#endif
  return "/usr/bin/pactl";
}

static void audio_usage(FILE *out) {
  fputs("Usage:\n"
        "  synapse-settings audio inventory [--format text|json]\n"
        "  synapse-settings audio broker-status [--format text|json]\n"
        "  synapse-settings audio plan-default --direction output|input "
        "--device ID [--format text|json]\n"
        "  synapse-settings audio set-default --direction output|input "
        "--device ID --ack " AUDIO_ACK " [--format text|json]\n"
        "  synapse-settings audio policy show [--format text|json]\n"
        "  synapse-settings audio policy set-rule --match "
        "executable|directory --path PATH --direction output|input "
        "--device ID --ack synapse-settings/audio-route-policy/v1 "
        "[--format text|json]\n"
        "  synapse-settings audio policy set-process-rule --stream ID "
        "--device ID --ack synapse-settings/audio-route-policy/v1 "
        "[--format text|json]\n"
        "  synapse-settings audio policy remove-rule --rule ID --ack "
        "synapse-settings/audio-route-policy/v1 [--format text|json]\n"
        "  synapse-settings audio resolve --path EXECUTABLE --direction "
        "output|input [--format text|json]\n",
        out);
}

static int copy_bounded(char *target, size_t size, const char *value) {
  if (!value)
    value = "";
  size_t length = strlen(value);
  if (length >= size || length > SETTINGS_FIELD_LIMIT) {
    errno = E2BIG;
    return -1;
  }
  memcpy(target, value, length + 1U);
  return 0;
}

static int copy_label(char *target, size_t size, const char *value) {
  if (!value)
    value = "";
  size_t written = 0;
  for (const unsigned char *cursor = (const unsigned char *)value; *cursor;
       cursor++) {
    unsigned char byte = *cursor;
    if (written + 1U >= size) {
      errno = E2BIG;
      return -1;
    }
    target[written++] = byte < 0x20U || byte == 0x7fU ? ' ' : (char)byte;
  }
  target[written] = '\0';
  return 0;
}

static const char *json_string_value(json_object *object, const char *key,
                                     const char *fallback) {
  json_object *value = NULL;
  if (!object || !json_object_object_get_ex(object, key, &value) ||
      !json_object_is_type(value, json_type_string))
    return fallback;
  const char *text = json_object_get_string(value);
  return text ? text : fallback;
}

static int json_int_value(json_object *object, const char *key, int fallback) {
  json_object *value = NULL;
  if (!object || !json_object_object_get_ex(object, key, &value) ||
      !json_object_is_type(value, json_type_int))
    return fallback;
  return json_object_get_int(value);
}

static int json_bool_value(json_object *object, const char *key, int fallback) {
  json_object *value = NULL;
  if (!object || !json_object_object_get_ex(object, key, &value) ||
      !json_object_is_type(value, json_type_boolean))
    return fallback;
  return json_object_get_boolean(value) ? 1 : 0;
}

static int volume_percent(json_object *object) {
  json_object *volume = NULL;
  if (!json_object_object_get_ex(object, "volume", &volume) ||
      !json_object_is_type(volume, json_type_object))
    return 0;
  int64_t total = 0;
  size_t count = 0;
  json_object_object_foreach(volume, channel, item) {
    (void)channel;
    json_object *value = NULL;
    if (json_object_is_type(item, json_type_object) &&
        json_object_object_get_ex(item, "value", &value) &&
        json_object_is_type(value, json_type_int)) {
      total += json_object_get_int64(value);
      count++;
    }
  }
  if (!count)
    return 0;
  int64_t rounded =
      (total * 100 + (int64_t)count * 32768) / ((int64_t)count * 65536);
  if (rounded < 0)
    rounded = 0;
  if (rounded > 999)
    rounded = 999;
  return (int)rounded;
}

static void capture_free(capture_result *result) {
  free(result->data);
  memset(result, 0, sizeof(*result));
}

static capture_result capture_command(char *const argv[]) {
  capture_result result = {0};
  int pipefd[2];
  if (pipe(pipefd) != 0)
    return result;
  pid_t child = fork();
  if (child < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return result;
  }
  if (child == 0) {
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
    if (strchr(argv[0], '/'))
      execv(argv[0], argv);
    else
      execvp(argv[0], argv);
    _exit(127);
  }
  close(pipefd[1]);
  int flags = fcntl(pipefd[0], F_GETFL);
  if (flags >= 0)
    (void)fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
  result.data = malloc(4097U);
  if (!result.data) {
    close(pipefd[0]);
    kill(child, SIGKILL);
    (void)waitpid(child, NULL, 0);
    return result;
  }
  size_t capacity = 4096U;
  size_t used = 0;
  int elapsed = 0;
  int eof = 0;
  while (elapsed < SETTINGS_CAPTURE_TIMEOUT_MS && !eof) {
    struct pollfd descriptor = {pipefd[0], POLLIN | POLLHUP, 0};
    int ready = poll(&descriptor, 1, 50);
    if (ready < 0 && errno == EINTR)
      continue;
    elapsed += 50;
    if (ready < 0)
      break;
    if (descriptor.revents & (POLLIN | POLLHUP)) {
      for (;;) {
        if (used == capacity) {
          if (capacity >= SETTINGS_CAPTURE_LIMIT) {
            errno = E2BIG;
            eof = 1;
            break;
          }
          size_t next = capacity * 2U;
          if (next > SETTINGS_CAPTURE_LIMIT)
            next = SETTINGS_CAPTURE_LIMIT;
          char *grown = realloc(result.data, next + 1U);
          if (!grown) {
            eof = 1;
            break;
          }
          result.data = grown;
          capacity = next;
        }
        ssize_t count = read(pipefd[0], result.data + used, capacity - used);
        if (count > 0) {
          used += (size_t)count;
          continue;
        }
        if (count == 0)
          eof = 1;
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR)
          eof = 1;
        break;
      }
    }
  }
  close(pipefd[0]);
  int wait_status = 0;
  pid_t waited = waitpid(child, &wait_status, WNOHANG);
  while (waited == 0 && elapsed < SETTINGS_CAPTURE_TIMEOUT_MS) {
    struct timespec delay = {0, 25L * 1000L * 1000L};
    (void)nanosleep(&delay, NULL);
    elapsed += 25;
    waited = waitpid(child, &wait_status, WNOHANG);
  }
  if (waited == 0) {
    result.timed_out = 1;
    kill(child, SIGKILL);
    (void)waitpid(child, &wait_status, 0);
    result.status = 124;
  } else if (waited < 0 || !WIFEXITED(wait_status))
    result.status = 1;
  else
    result.status = WEXITSTATUS(wait_status);
  result.data[used] = '\0';
  return result;
}

static json_object *pactl_json(const char *pactl, const char *first,
                               const char *second, const char *third,
                               const char **reason) {
  char *argv[7] = {(char *)pactl, "--format=json",
                   (char *)first, (char *)second,
                   (char *)third, NULL,
                   NULL};
  size_t write = 2U;
  if (first)
    argv[write++] = (char *)first;
  if (second)
    argv[write++] = (char *)second;
  if (third)
    argv[write++] = (char *)third;
  argv[write] = NULL;
  capture_result result = capture_command(argv);
  if (!result.data || result.status != 0) {
    *reason = result.timed_out ? "timeout" : "unavailable";
    capture_free(&result);
    return NULL;
  }
  json_tokener *tokener = json_tokener_new_ex(32);
  if (!tokener) {
    capture_free(&result);
    *reason = "invalid-response";
    return NULL;
  }
  json_object *value =
      json_tokener_parse_ex(tokener, result.data, (int)strlen(result.data));
  enum json_tokener_error error = json_tokener_get_error(tokener);
  json_tokener_free(tokener);
  capture_free(&result);
  if (error != json_tokener_success || !value) {
    if (value)
      json_object_put(value);
    *reason = "invalid-response";
    return NULL;
  }
  return value;
}

static uint64_t audio_fnv1a64(const char *text) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char *cursor = (const unsigned char *)text; *cursor;
       cursor++) {
    hash ^= *cursor;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static int endpoint_compare(const void *left, const void *right) {
  const audio_endpoint *a = left;
  const audio_endpoint *b = right;
  return strcmp(a->raw_name, b->raw_name);
}

static int parse_endpoints(json_object *array, audio_endpoint *items,
                           size_t *count, const char *default_name,
                           const char *prefix, int skip_monitors) {
  if (!json_object_is_type(array, json_type_array))
    return -1;
  size_t length = json_object_array_length(array);
  for (size_t i = 0; i < length; i++) {
    json_object *value = json_object_array_get_idx(array, i);
    if (!json_object_is_type(value, json_type_object))
      return -1;
    json_object *monitor = NULL;
    json_object *monitor_source = NULL;
    int monitor_of_sink =
        json_object_object_get_ex(value, "monitor_of_sink", &monitor) &&
        monitor && !json_object_is_type(monitor, json_type_null);
    int named_monitor =
        json_object_object_get_ex(value, "monitor_source", &monitor_source) &&
        json_object_is_type(monitor_source, json_type_string) &&
        json_object_get_string_len(monitor_source) > 0;
    if (skip_monitors && (monitor_of_sink || named_monitor))
      continue;
    if (*count >= AUDIO_ENDPOINT_LIMIT)
      return -1;
    audio_endpoint *item = &items[(*count)++];
    memset(item, 0, sizeof(*item));
    const char *name = json_string_value(value, "name", NULL);
    const char *description = json_string_value(value, "description", prefix);
    if (!name ||
        copy_bounded(item->raw_name, sizeof(item->raw_name), name) != 0 ||
        copy_label(item->label, sizeof(item->label), description) != 0)
      return -1;
    item->index = json_int_value(value, "index", -1);
    item->volume_percent = volume_percent(value);
    item->muted = json_bool_value(value, "mute", 0);
    item->is_default = default_name && strcmp(default_name, name) == 0;
  }
  qsort(items, *count, sizeof(*items), endpoint_compare);
  for (size_t i = 0; i < *count; i++) {
    if (i > 0 && strcmp(items[i - 1U].raw_name, items[i].raw_name) == 0)
      return -1;
    int written = snprintf(items[i].id, sizeof(items[i].id), "%s-%016" PRIx64,
                           prefix, audio_fnv1a64(items[i].raw_name));
    if (written < 0 || (size_t)written >= sizeof(items[i].id))
      return -1;
    for (size_t j = 0; j < i; j++)
      if (strcmp(items[j].id, items[i].id) == 0)
        return -1;
  }
  return 0;
}

static const char *endpoint_id_for_index(const audio_endpoint *items,
                                         size_t count, int index) {
  for (size_t i = 0; i < count; i++)
    if (items[i].index == index)
      return items[i].id;
  return NULL;
}

static pid_t stream_process_pid(json_object *properties) {
  json_object *value = NULL;
  if (!properties ||
      !json_object_object_get_ex(properties, "application.process.id", &value))
    return 0;
  int64_t parsed = 0;
  if (json_object_is_type(value, json_type_int)) {
    parsed = json_object_get_int64(value);
  } else if (json_object_is_type(value, json_type_string)) {
    const char *text = json_object_get_string(value);
    if (!text || !*text)
      return 0;
    errno = 0;
    char *end = NULL;
    long long number = strtoll(text, &end, 10);
    if (errno != 0 || !end || *end)
      return 0;
    parsed = number;
  } else {
    return 0;
  }
  return parsed > 0 && parsed <= INT_MAX ? (pid_t)parsed : 0;
}

static int process_rule_available(pid_t process_pid) {
  if (process_pid <= 0)
    return 0;
  char path[64];
  int written = snprintf(path, sizeof(path), "/proc/%ld", (long)process_pid);
  if (written < 0 || (size_t)written >= sizeof(path))
    return 0;
  int process_fd = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  if (process_fd < 0)
    return 0;
  struct stat process;
  int available =
      fstat(process_fd, &process) == 0 && process.st_uid == geteuid();
  int executable_fd =
      available ? openat(process_fd, "exe", O_PATH | O_CLOEXEC) : -1;
  close(process_fd);
  if (executable_fd < 0)
    return 0;
  struct stat executable;
  available =
      fstat(executable_fd, &executable) == 0 && S_ISREG(executable.st_mode);
  close(executable_fd);
  return available;
}

static int parse_stream_array(json_object *array, audio_inventory *inventory,
                              const char *direction) {
  if (!json_object_is_type(array, json_type_array))
    return -1;
  size_t length = json_object_array_length(array);
  for (size_t i = 0; i < length; i++) {
    if (inventory->stream_count >= AUDIO_STREAM_LIMIT)
      return -1;
    json_object *value = json_object_array_get_idx(array, i);
    if (!json_object_is_type(value, json_type_object))
      return -1;
    audio_stream *stream = &inventory->streams[inventory->stream_count++];
    memset(stream, 0, sizeof(*stream));
    stream->direction = direction;
    int index = json_int_value(value, "index", -1);
    if (index < 0)
      return -1;
    stream->backend_index = index;
    int written = snprintf(
        stream->id, sizeof(stream->id), "%s-%d",
        strcmp(direction, "playback") == 0 ? "playback" : "recording", index);
    if (written < 0 || (size_t)written >= sizeof(stream->id))
      return -1;
    json_object *properties = NULL;
    const char *label = "Application";
    if (json_object_object_get_ex(value, "properties", &properties) &&
        json_object_is_type(properties, json_type_object)) {
      label =
          json_string_value(properties, "application.name",
                            json_string_value(properties, "media.name", label));
      stream->process_pid = stream_process_pid(properties);
      stream->process_rule_available =
          process_rule_available(stream->process_pid);
    }
    if (copy_label(stream->label, sizeof(stream->label), label) != 0)
      return -1;
    int target_index = json_int_value(
        value, strcmp(direction, "playback") == 0 ? "sink" : "source", -1);
    stream->target_index = target_index;
    const char *target =
        strcmp(direction, "playback") == 0
            ? endpoint_id_for_index(inventory->outputs, inventory->output_count,
                                    target_index)
            : endpoint_id_for_index(inventory->inputs, inventory->input_count,
                                    target_index);
    if (copy_bounded(stream->target, sizeof(stream->target),
                     target ? target : "unavailable") != 0)
      return -1;
    stream->volume_percent = volume_percent(value);
    stream->muted = json_bool_value(value, "mute", 0);
  }
  return 0;
}

static int stream_compare(const void *left, const void *right) {
  const audio_stream *a = left;
  const audio_stream *b = right;
  return strcmp(a->id, b->id);
}

static int validate_stream_identities(audio_inventory *inventory) {
  qsort(inventory->streams, inventory->stream_count,
        sizeof(inventory->streams[0]), stream_compare);
  for (size_t i = 1; i < inventory->stream_count; i++)
    if (strcmp(inventory->streams[i - 1U].id, inventory->streams[i].id) == 0)
      return -1;
  return 0;
}

static int card_compare(const void *left, const void *right) {
  const audio_card *a = left;
  const audio_card *b = right;
  return strcmp(a->raw_name, b->raw_name);
}

static int parse_cards(json_object *array, audio_inventory *inventory) {
  if (!json_object_is_type(array, json_type_array))
    return -1;
  size_t length = json_object_array_length(array);
  for (size_t i = 0; i < length; i++) {
    if (inventory->card_count >= AUDIO_CARD_LIMIT)
      return -1;
    json_object *value = json_object_array_get_idx(array, i);
    if (!json_object_is_type(value, json_type_object))
      return -1;
    audio_card *card = &inventory->cards[inventory->card_count++];
    memset(card, 0, sizeof(*card));
    const char *name = json_string_value(value, "name", NULL);
    const char *label = json_string_value(value, "description", "Audio card");
    const char *profile =
        json_string_value(value, "active_profile", "unavailable");
    if (!name ||
        copy_bounded(card->raw_name, sizeof(card->raw_name), name) != 0 ||
        copy_label(card->label, sizeof(card->label), label) != 0 ||
        copy_label(card->active_profile, sizeof(card->active_profile),
                   profile) != 0)
      return -1;
  }
  qsort(inventory->cards, inventory->card_count, sizeof(inventory->cards[0]),
        card_compare);
  for (size_t i = 0; i < inventory->card_count; i++) {
    if (i > 0 && strcmp(inventory->cards[i - 1U].raw_name,
                        inventory->cards[i].raw_name) == 0)
      return -1;
    int written = snprintf(inventory->cards[i].id,
                           sizeof(inventory->cards[i].id), "card-%016" PRIx64,
                           audio_fnv1a64(inventory->cards[i].raw_name));
    if (written < 0 || (size_t)written >= sizeof(inventory->cards[i].id))
      return -1;
    for (size_t j = 0; j < i; j++)
      if (strcmp(inventory->cards[j].id, inventory->cards[i].id) == 0)
        return -1;
  }
  return 0;
}

static int load_audio_inventory(audio_inventory *inventory) {
  memset(inventory, 0, sizeof(*inventory));
  inventory->reason = "unavailable";
  const char *pactl = pactl_binary();
  const char *reason = "unavailable";
  json_object *info = NULL;
  json_object *sinks = NULL;
  json_object *sources = NULL;
  json_object *playback = NULL;
  json_object *recording = NULL;
  json_object *cards = NULL;
  info = pactl_json(pactl, "info", NULL, NULL, &reason);
  if (!info)
    goto done;
  sinks = pactl_json(pactl, "list", "sinks", NULL, &reason);
  if (!sinks)
    goto done;
  sources = pactl_json(pactl, "list", "sources", NULL, &reason);
  if (!sources)
    goto done;
  playback = pactl_json(pactl, "list", "sink-inputs", NULL, &reason);
  if (!playback)
    goto done;
  recording = pactl_json(pactl, "list", "source-outputs", NULL, &reason);
  if (!recording)
    goto done;
  cards = pactl_json(pactl, "list", "cards", NULL, &reason);
  if (!cards)
    goto done;
  if (!json_object_is_type(info, json_type_object)) {
    reason = "invalid-response";
    goto done;
  }
  const char *default_output =
      json_string_value(info, "default_sink_name", NULL);
  const char *default_input =
      json_string_value(info, "default_source_name", NULL);
  if (parse_endpoints(sinks, inventory->outputs, &inventory->output_count,
                      default_output, "output", 0) != 0 ||
      parse_endpoints(sources, inventory->inputs, &inventory->input_count,
                      default_input, "input", 1) != 0 ||
      parse_endpoints(sources, inventory->backend_sources,
                      &inventory->backend_source_count, default_input, "input",
                      0) != 0 ||
      parse_stream_array(playback, inventory, "playback") != 0 ||
      parse_stream_array(recording, inventory, "recording") != 0 ||
      validate_stream_identities(inventory) != 0 ||
      parse_cards(cards, inventory) != 0) {
    reason = "invalid-response";
    goto done;
  }
  inventory->available = 1;
  inventory->reason = "";
done:
  if (!inventory->available)
    inventory->reason = reason;
  if (info)
    json_object_put(info);
  if (sinks)
    json_object_put(sinks);
  if (sources)
    json_object_put(sources);
  if (playback)
    json_object_put(playback);
  if (recording)
    json_object_put(recording);
  if (cards)
    json_object_put(cards);
  return inventory->available ? 0 : -1;
}

static json_object *endpoint_json(const audio_endpoint *endpoint) {
  json_object *value = json_object_new_object();
  json_object_object_add(value, "id", json_object_new_string(endpoint->id));
  json_object_object_add(value, "label",
                         json_object_new_string(endpoint->label));
  json_object_object_add(value, "default",
                         json_object_new_boolean(endpoint->is_default));
  json_object_object_add(value, "volumePercent",
                         json_object_new_int(endpoint->volume_percent));
  json_object_object_add(value, "muted",
                         json_object_new_boolean(endpoint->muted));
  return value;
}

static void print_audio_inventory_json(const audio_inventory *inventory) {
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-inventory/v1"));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "available",
                         json_object_new_boolean(inventory->available));
  if (inventory->available)
    json_object_object_add(root, "reason", NULL);
  else
    json_object_object_add(root, "reason",
                           json_object_new_string(inventory->reason));
  json_object_object_add(root, "mutationAvailable",
                         json_object_new_boolean(inventory->available));
  json_object *outputs =
      json_object_new_array_ext((int)inventory->output_count);
  for (size_t i = 0; i < inventory->output_count; i++)
    json_object_array_add(outputs, endpoint_json(&inventory->outputs[i]));
  json_object_object_add(root, "outputs", outputs);
  json_object *inputs = json_object_new_array_ext((int)inventory->input_count);
  for (size_t i = 0; i < inventory->input_count; i++)
    json_object_array_add(inputs, endpoint_json(&inventory->inputs[i]));
  json_object_object_add(root, "inputs", inputs);
  json_object *streams =
      json_object_new_array_ext((int)inventory->stream_count);
  for (size_t i = 0; i < inventory->stream_count; i++) {
    const audio_stream *stream = &inventory->streams[i];
    json_object *value = json_object_new_object();
    json_object_object_add(value, "id", json_object_new_string(stream->id));
    json_object_object_add(value, "direction",
                           json_object_new_string(stream->direction));
    json_object_object_add(value, "label",
                           json_object_new_string(stream->label));
    json_object_object_add(value, "target",
                           json_object_new_string(stream->target));
    json_object_object_add(value, "volumePercent",
                           json_object_new_int(stream->volume_percent));
    json_object_object_add(value, "muted",
                           json_object_new_boolean(stream->muted));
    json_object_object_add(
        value, "processRuleAvailable",
        json_object_new_boolean(stream->process_rule_available));
    json_object_array_add(streams, value);
  }
  json_object_object_add(root, "streams", streams);
  json_object *cards = json_object_new_array_ext((int)inventory->card_count);
  for (size_t i = 0; i < inventory->card_count; i++) {
    json_object *value = json_object_new_object();
    json_object_object_add(value, "id",
                           json_object_new_string(inventory->cards[i].id));
    json_object_object_add(value, "label",
                           json_object_new_string(inventory->cards[i].label));
    json_object_object_add(
        value, "activeProfile",
        json_object_new_string(inventory->cards[i].active_profile));
    json_object_array_add(cards, value);
  }
  json_object_object_add(root, "cards", cards);
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static void print_audio_inventory_text(const audio_inventory *inventory) {
  if (!inventory->available) {
    printf("Audio unavailable: %s\n", inventory->reason);
    return;
  }
  puts("Audio outputs");
  for (size_t i = 0; i < inventory->output_count; i++)
    printf("  %s  %s  %d%%  %s%s\n", inventory->outputs[i].id,
           inventory->outputs[i].label, inventory->outputs[i].volume_percent,
           inventory->outputs[i].muted ? "muted" : "active",
           inventory->outputs[i].is_default ? "  default" : "");
  puts("Audio inputs");
  for (size_t i = 0; i < inventory->input_count; i++)
    printf("  %s  %s  %d%%  %s%s\n", inventory->inputs[i].id,
           inventory->inputs[i].label, inventory->inputs[i].volume_percent,
           inventory->inputs[i].muted ? "muted" : "active",
           inventory->inputs[i].is_default ? "  default" : "");
  printf("Streams: %zu, cards: %zu\n", inventory->stream_count,
         inventory->card_count);
}

static int parse_format(int argc, char **argv, int start, const char **format) {
  *format = "text";
  for (int i = start; i < argc; i++) {
    if (strcmp(argv[i], "--format") == 0 && i + 1 < argc)
      *format = argv[++i];
    else if (strcmp(argv[i], "--json") == 0)
      *format = "json";
    else
      return -1;
  }
  return strcmp(*format, "text") == 0 || strcmp(*format, "json") == 0 ? 0 : -1;
}

static int parse_default_options(int argc, char **argv, const char **direction,
                                 const char **device, const char **ack,
                                 const char **format) {
  *direction = NULL;
  *device = NULL;
  *ack = NULL;
  *format = "text";
  int seen_format = 0;
  for (int i = 2; i < argc; i++) {
    const char **target = NULL;
    if (strcmp(argv[i], "--direction") == 0)
      target = direction;
    else if (strcmp(argv[i], "--device") == 0)
      target = device;
    else if (strcmp(argv[i], "--ack") == 0)
      target = ack;
    else if (strcmp(argv[i], "--format") == 0 && !seen_format) {
      if (i + 1 >= argc)
        return -1;
      *format = argv[++i];
      seen_format = 1;
      continue;
    } else if (strcmp(argv[i], "--json") == 0 && !seen_format) {
      *format = "json";
      seen_format = 1;
      continue;
    } else
      return -1;
    if (i + 1 >= argc || *target)
      return -1;
    *target = argv[++i];
  }
  if (!*direction || !*device)
    return -1;
  if (strcmp(*direction, "output") != 0 && strcmp(*direction, "input") != 0)
    return -1;
  return strcmp(*format, "text") == 0 || strcmp(*format, "json") == 0 ? 0 : -1;
}

static audio_endpoint *find_endpoint(audio_inventory *inventory,
                                     const char *direction, const char *id) {
  audio_endpoint *items =
      strcmp(direction, "output") == 0 ? inventory->outputs : inventory->inputs;
  size_t count = strcmp(direction, "output") == 0 ? inventory->output_count
                                                  : inventory->input_count;
  for (size_t i = 0; i < count; i++)
    if (strcmp(items[i].id, id) == 0)
      return &items[i];
  return NULL;
}

static void print_default_contract(const char *schema, const char *status,
                                   const char *direction,
                                   const audio_endpoint *endpoint, int changed,
                                   int applied, int json) {
  if (!json) {
    printf("%s %s: %s (%s)%s\n", applied ? "Set default" : "Plan default",
           direction, endpoint->label, endpoint->id,
           changed ? "" : " already selected");
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(root, "schema", json_object_new_string(schema));
  json_object_object_add(root, "status", json_object_new_string(status));
  json_object_object_add(root, "direction", json_object_new_string(direction));
  json_object_object_add(root, "device", json_object_new_string(endpoint->id));
  json_object_object_add(root, "label",
                         json_object_new_string(endpoint->label));
  json_object_object_add(root, "changed", json_object_new_boolean(changed));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "requiresAcknowledgement",
                         json_object_new_string(AUDIO_ACK));
  json_object_object_add(root, "applied", json_object_new_boolean(applied));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

int settings_audio_policy_target(const char *direction, const char *requested,
                                 char *target, size_t target_size,
                                 int *available) {
  if (!direction || !target || !target_size || !available ||
      (strcmp(direction, "output") != 0 && strcmp(direction, "input") != 0)) {
    errno = EINVAL;
    return -1;
  }
  audio_inventory inventory;
  if (load_audio_inventory(&inventory) != 0)
    return -1;
  audio_endpoint *items =
      strcmp(direction, "output") == 0 ? inventory.outputs : inventory.inputs;
  size_t count = strcmp(direction, "output") == 0 ? inventory.output_count
                                                  : inventory.input_count;
  *available = 0;
  target[0] = '\0';
  if (requested) {
    if (copy_bounded(target, target_size, requested) != 0)
      return -1;
    for (size_t i = 0; i < count; i++)
      if (strcmp(items[i].id, requested) == 0) {
        *available = 1;
        break;
      }
    return 0;
  }
  for (size_t i = 0; i < count; i++)
    if (items[i].is_default) {
      if (copy_bounded(target, target_size, items[i].id) != 0)
        return -1;
      *available = 1;
      break;
    }
  return 0;
}

static const audio_stream *find_stream(const audio_inventory *inventory,
                                       const char *stream_id) {
  for (size_t i = 0; i < inventory->stream_count; i++)
    if (strcmp(inventory->streams[i].id, stream_id) == 0)
      return &inventory->streams[i];
  return NULL;
}

static const audio_endpoint *find_endpoint_by_index(const audio_endpoint *items,
                                                    size_t count, int index) {
  for (size_t i = 0; i < count; i++)
    if (items[i].index == index)
      return &items[i];
  return NULL;
}

static int read_process_start_time(int process_fd, uint64_t *start_time) {
  int stat_fd = openat(process_fd, "stat", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (stat_fd < 0)
    return -1;
  char data[4097];
  size_t used = 0;
  while (used < sizeof(data) - 1U) {
    ssize_t count = read(stat_fd, data + used, sizeof(data) - 1U - used);
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0) {
      int saved = errno;
      close(stat_fd);
      errno = saved;
      return -1;
    }
    if (count == 0)
      break;
    used += (size_t)count;
  }
  if (used >= sizeof(data) - 1U) {
    close(stat_fd);
    errno = E2BIG;
    return -1;
  }
  close(stat_fd);
  data[used] = '\0';
  char *cursor = strrchr(data, ')');
  if (!cursor) {
    errno = EINVAL;
    return -1;
  }
  cursor++;
  for (unsigned field = 3U; field <= 22U; field++) {
    while (*cursor == ' ')
      cursor++;
    if (!*cursor) {
      errno = EINVAL;
      return -1;
    }
    char *end = cursor;
    while (*end && *end != ' ' && *end != '\n')
      end++;
    if (field == 22U) {
      char saved = *end;
      *end = '\0';
      errno = 0;
      char *parsed_end = NULL;
      unsigned long long parsed = strtoull(cursor, &parsed_end, 10);
      int valid = errno == 0 && parsed_end && *parsed_end == '\0';
      *end = saved;
      if (!valid) {
        errno = EINVAL;
        return -1;
      }
      *start_time = (uint64_t)parsed;
      return 0;
    }
    cursor = end;
  }
  errno = EINVAL;
  return -1;
}

static int trusted_process_executable(pid_t process_pid, char *path,
                                      size_t path_size, uint64_t *start_time) {
  if (process_pid <= 0 || !path || !path_size || !start_time) {
    errno = EINVAL;
    return -1;
  }
  char process_directory[64];
  int written = snprintf(process_directory, sizeof(process_directory),
                         "/proc/%ld", (long)process_pid);
  if (written < 0 || (size_t)written >= sizeof(process_directory)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  int process_fd =
      open(process_directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  if (process_fd < 0)
    return -1;
  struct stat owner;
  if (fstat(process_fd, &owner) != 0) {
    int saved = errno;
    close(process_fd);
    errno = saved;
    return -1;
  }
  if (owner.st_uid != geteuid()) {
    close(process_fd);
    errno = EPERM;
    return -1;
  }
  if (read_process_start_time(process_fd, start_time) != 0) {
    int saved = errno;
    close(process_fd);
    errno = saved;
    return -1;
  }
  int executable_fd = openat(process_fd, "exe", O_PATH | O_CLOEXEC);
  int open_error = errno;
  close(process_fd);
  if (executable_fd < 0) {
    errno = open_error;
    return -1;
  }
  char executable_link[64];
  written = snprintf(executable_link, sizeof(executable_link),
                     "/proc/self/fd/%d", executable_fd);
  if (written < 0 || (size_t)written >= sizeof(executable_link)) {
    close(executable_fd);
    errno = ENAMETOOLONG;
    return -1;
  }
  char *resolved = realpath(executable_link, NULL);
  int resolve_error = errno;
  close(executable_fd);
  if (!resolved) {
    errno = resolve_error;
    return -1;
  }
  struct stat value;
  int status = stat(resolved, &value);
  if (status == 0 && (!S_ISREG(value.st_mode) || access(resolved, X_OK) != 0)) {
    errno = EINVAL;
    status = -1;
  }
  if (status == 0)
    status = copy_bounded(path, path_size, resolved);
  free(resolved);
  return status;
}

int settings_audio_stream_executable(const char *stream_id, char *direction,
                                     size_t direction_size, char *path,
                                     size_t path_size) {
  if (!stream_id || !direction || !direction_size || !path || !path_size) {
    errno = EINVAL;
    return -1;
  }
  audio_inventory inventory;
  if (load_audio_inventory(&inventory) != 0)
    return -1;
  const audio_stream *selected = find_stream(&inventory, stream_id);
  if (!selected || selected->process_pid <= 0) {
    errno = ENOENT;
    return -1;
  }
  uint64_t start_time = 0;
  if (trusted_process_executable(selected->process_pid, path, path_size,
                                 &start_time) != 0)
    return -1;
  (void)start_time;
  return copy_bounded(direction, direction_size,
                      strcmp(selected->direction, "playback") == 0 ? "output"
                                                                   : "input");
}

typedef struct {
  char stream[32];
  char direction[8];
  char executable[PATH_MAX];
  char current_device[32];
  char current_raw[SETTINGS_FIELD_LIMIT + 1U];
  char requested_raw[SETTINGS_FIELD_LIMIT + 1U];
  pid_t process_pid;
  uint64_t process_start_time;
  int backend_index;
  int requested_available;
} audio_broker_stream_state;

static int parse_broker_stream_token(const char *stream_id, char *direction,
                                     size_t direction_size, int *index) {
  if (!stream_id || !direction || !direction_size || !index)
    return -1;
  const char *digits = NULL;
  const char *mapped_direction = NULL;
  if (strncmp(stream_id, "playback-", 9U) == 0) {
    digits = stream_id + 9U;
    mapped_direction = "output";
  } else if (strncmp(stream_id, "recording-", 10U) == 0) {
    digits = stream_id + 10U;
    mapped_direction = "input";
  } else {
    return -1;
  }
  if (!*digits || (digits[0] == '0' && digits[1]))
    return -1;
  for (const char *cursor = digits; *cursor; cursor++)
    if (*cursor < '0' || *cursor > '9')
      return -1;
  errno = 0;
  char *end = NULL;
  long parsed = strtol(digits, &end, 10);
  if (errno != 0 || !end || *end || parsed < 0 || parsed > INT_MAX ||
      copy_bounded(direction, direction_size, mapped_direction) != 0)
    return -1;
  *index = (int)parsed;
  return 0;
}

static int load_broker_stream_state(const char *stream_id,
                                    const char *requested_device,
                                    audio_broker_stream_state *state,
                                    const char **reason) {
  memset(state, 0, sizeof(*state));
  int token_index = -1;
  if (parse_broker_stream_token(stream_id, state->direction,
                                sizeof(state->direction), &token_index) != 0) {
    *reason = "invalid-stream-token";
    return -1;
  }
  audio_inventory inventory;
  if (load_audio_inventory(&inventory) != 0) {
    *reason = inventory.reason;
    return -2;
  }
  const audio_stream *stream = find_stream(&inventory, stream_id);
  if (!stream || stream->backend_index != token_index) {
    *reason = "stream-vanished";
    return 1;
  }
  if ((strcmp(stream->direction, "playback") == 0) !=
      (strcmp(state->direction, "output") == 0)) {
    *reason = "stream-identity-changed";
    return 1;
  }
  state->process_pid = stream->process_pid;
  state->backend_index = stream->backend_index;
  if (copy_bounded(state->stream, sizeof(state->stream), stream_id) != 0 ||
      trusted_process_executable(stream->process_pid, state->executable,
                                 sizeof(state->executable),
                                 &state->process_start_time) != 0) {
    *reason = "process-unavailable";
    return 2;
  }
  const audio_endpoint *current =
      strcmp(state->direction, "output") == 0
          ? find_endpoint_by_index(inventory.outputs, inventory.output_count,
                                   stream->target_index)
          : find_endpoint_by_index(inventory.backend_sources,
                                   inventory.backend_source_count,
                                   stream->target_index);
  if (current &&
      (copy_bounded(state->current_device, sizeof(state->current_device),
                    current->id) != 0 ||
       copy_bounded(state->current_raw, sizeof(state->current_raw),
                    current->raw_name) != 0))
    return -1;
  if (requested_device) {
    audio_endpoint *target =
        find_endpoint(&inventory, state->direction, requested_device);
    if (target) {
      state->requested_available = 1;
      if (copy_bounded(state->requested_raw, sizeof(state->requested_raw),
                       target->raw_name) != 0)
        return -1;
    }
  }
  *reason = NULL;
  return 0;
}

static int broker_identity_equal(const audio_broker_stream_state *left,
                                 const audio_broker_stream_state *right) {
  return left->backend_index == right->backend_index &&
         left->process_pid == right->process_pid &&
         left->process_start_time == right->process_start_time &&
         strcmp(left->stream, right->stream) == 0 &&
         strcmp(left->direction, right->direction) == 0 &&
         strcmp(left->executable, right->executable) == 0;
}

static int broker_selection_equal(const settings_audio_route_selection *left,
                                  const settings_audio_route_selection *right) {
  return left->generation == right->generation &&
         left->matched == right->matched &&
         strcmp(left->device, right->device) == 0 &&
         strcmp(left->rule, right->rule) == 0 &&
         strcmp(left->source, right->source) == 0;
}

static void broker_receipt_status(settings_audio_broker_receipt *receipt,
                                  const char *status, const char *reason) {
  (void)copy_bounded(receipt->status, sizeof(receipt->status), status);
  (void)copy_bounded(receipt->reason, sizeof(receipt->reason),
                     reason ? reason : "");
}

static int execute_broker_move(const audio_broker_stream_state *state,
                               const char *raw_target, int *timed_out) {
  char index[24];
  int written = snprintf(index, sizeof(index), "%d", state->backend_index);
  if (written < 0 || (size_t)written >= sizeof(index))
    return -1;
  const char *pactl = pactl_binary();
  char *argv[5] = {(char *)pactl,
                   strcmp(state->direction, "output") == 0
                       ? "move-sink-input"
                       : "move-source-output",
                   index, (char *)raw_target, NULL};
  capture_result result = capture_command(argv);
  *timed_out = result.timed_out;
  int status = result.data && result.status == 0 ? 0 : -1;
  capture_free(&result);
  return status;
}

int settings_audio_broker_stream_ids(char ids[][32], size_t capacity,
                                     size_t *count, const char **reason) {
  if (!ids || !count || !reason || capacity > AUDIO_STREAM_LIMIT) {
    errno = EINVAL;
    return -1;
  }
  audio_inventory inventory;
  if (load_audio_inventory(&inventory) != 0) {
    *reason = inventory.reason;
    return -1;
  }
  if (inventory.stream_count > capacity) {
    *reason = "stream-limit";
    errno = E2BIG;
    return -1;
  }
  for (size_t i = 0; i < inventory.stream_count; i++)
    if (copy_bounded(ids[i], 32U, inventory.streams[i].id) != 0) {
      *reason = "invalid-response";
      return -1;
    }
  *count = inventory.stream_count;
  *reason = NULL;
  return 0;
}

int settings_audio_broker_apply_new(const char *stream_id,
                                    settings_audio_broker_receipt *receipt) {
  if (!stream_id || !receipt) {
    errno = EINVAL;
    return -1;
  }
  memset(receipt, 0, sizeof(*receipt));
  int token_index = -1;
  if (parse_broker_stream_token(stream_id, receipt->direction,
                                sizeof(receipt->direction),
                                &token_index) != 0 ||
      copy_bounded(receipt->stream, sizeof(receipt->stream), stream_id) != 0) {
    errno = EINVAL;
    return -1;
  }
  (void)token_index;
  audio_broker_stream_state first;
  audio_broker_stream_state planned;
  settings_audio_route_selection selection;
  const char *reason = NULL;
  int prepared = 0;
  for (unsigned attempt = 0; attempt < 2U; attempt++) {
    int loaded = load_broker_stream_state(stream_id, NULL, &first, &reason);
    if (loaded != 0) {
      broker_receipt_status(receipt, "Skipped", reason);
      return 0;
    }
    if (settings_audio_route_select(first.executable, first.direction,
                                    &selection) != 0) {
      broker_receipt_status(receipt, "Skipped", "policy-unavailable");
      return 0;
    }
    receipt->policy_generation = selection.generation;
    if (copy_bounded(receipt->source, sizeof(receipt->source),
                     selection.source) != 0)
      return -1;
    if (!selection.matched) {
      broker_receipt_status(receipt, "Skipped", "no-matching-rule");
      return 0;
    }
    if (copy_bounded(receipt->device, sizeof(receipt->device),
                     selection.device) != 0 ||
        copy_bounded(receipt->rule, sizeof(receipt->rule), selection.rule) != 0)
      return -1;
    loaded = load_broker_stream_state(stream_id, selection.device, &planned,
                                      &reason);
    if (loaded != 0) {
      broker_receipt_status(receipt, "Skipped", reason);
      return 0;
    }
    settings_audio_route_selection confirmed;
    if (settings_audio_route_select(planned.executable, planned.direction,
                                    &confirmed) != 0) {
      broker_receipt_status(receipt, "Skipped", "policy-unavailable");
      return 0;
    }
    if (broker_identity_equal(&first, &planned) &&
        broker_selection_equal(&selection, &confirmed)) {
      prepared = 1;
      break;
    }
  }
  if (!prepared) {
    broker_receipt_status(receipt, "Skipped", "cohort-changed");
    return 0;
  }
  if (!planned.requested_available) {
    broker_receipt_status(receipt, "Skipped", "target-unavailable");
    return 0;
  }
  if (!planned.current_raw[0]) {
    broker_receipt_status(receipt, "Skipped", "current-target-unavailable");
    return 0;
  }
  if (strcmp(planned.current_device, selection.device) == 0) {
    receipt->verified = 1;
    broker_receipt_status(receipt, "AlreadyRouted", NULL);
    return 0;
  }
  int move_timed_out = 0;
  int moved = execute_broker_move(&planned, planned.requested_raw,
                                  &move_timed_out) == 0;
  audio_broker_stream_state after;
  int after_status =
      load_broker_stream_state(stream_id, selection.device, &after, &reason);
  int same_identity =
      after_status == 0 && broker_identity_equal(&planned, &after);
  int at_requested =
      same_identity && strcmp(after.current_device, selection.device) == 0;
  if (moved && at_requested) {
    receipt->changed = 1;
    receipt->routing_applied = 1;
    receipt->verified = 1;
    broker_receipt_status(receipt, "Applied", NULL);
    return 0;
  }
  if (same_identity && at_requested) {
    receipt->compensation_attempted = 1;
    int compensation_timed_out = 0;
    (void)execute_broker_move(&after, planned.current_raw,
                              &compensation_timed_out);
    audio_broker_stream_state restored;
    const char *restore_reason = NULL;
    if (load_broker_stream_state(stream_id, NULL, &restored, &restore_reason) ==
            0 &&
        broker_identity_equal(&planned, &restored) &&
        strcmp(restored.current_device, planned.current_device) == 0)
      receipt->compensation_verified = 1;
  }
  if (after_status == 1)
    broker_receipt_status(receipt, "Failed", "stream-vanished");
  else if (after_status < 0)
    broker_receipt_status(receipt, "Failed", "verification-unavailable");
  else if (!same_identity && after_status == 0)
    broker_receipt_status(receipt, "Failed", "stream-identity-changed");
  else if (move_timed_out)
    broker_receipt_status(receipt, "Failed", "move-timeout");
  else if (!moved)
    broker_receipt_status(receipt, "Failed", "move-failed");
  else
    broker_receipt_status(receipt, "Failed", "verification-failed");
  return 0;
}

static int execute_default(const char *direction,
                           const audio_endpoint *endpoint) {
  const char *pactl = pactl_binary();
  char *argv[4] = {(char *)pactl,
                   strcmp(direction, "output") == 0 ? "set-default-sink"
                                                    : "set-default-source",
                   (char *)endpoint->raw_name, NULL};
  capture_result result = capture_command(argv);
  int status = result.data && result.status == 0 ? 0 : -1;
  capture_free(&result);
  return status;
}

int settings_audio_command(int argc, char **argv) {
  if (argc < 2 || strcmp(argv[1], "--help") == 0 ||
      strcmp(argv[1], "-h") == 0) {
    audio_usage(argc < 2 ? stderr : stdout);
    return argc < 2 ? 2 : 0;
  }
  if (strcmp(argv[1], "policy") == 0 || strcmp(argv[1], "resolve") == 0)
    return settings_audio_route_command(argc, argv);
  if (strcmp(argv[1], "broker-status") == 0)
    return settings_audio_broker_status_command(argc, argv);
  if (strcmp(argv[1], "inventory") == 0) {
    const char *format = NULL;
    if (parse_format(argc, argv, 2, &format) != 0) {
      audio_usage(stderr);
      return 2;
    }
    audio_inventory inventory;
    (void)load_audio_inventory(&inventory);
    if (strcmp(format, "json") == 0)
      print_audio_inventory_json(&inventory);
    else
      print_audio_inventory_text(&inventory);
    return 0;
  }
  if (strcmp(argv[1], "plan-default") != 0 &&
      strcmp(argv[1], "set-default") != 0) {
    audio_usage(stderr);
    return 2;
  }
  const char *direction = NULL;
  const char *device = NULL;
  const char *ack = NULL;
  const char *format = NULL;
  if (parse_default_options(argc, argv, &direction, &device, &ack, &format) !=
      0) {
    audio_usage(stderr);
    return 2;
  }
  int apply = strcmp(argv[1], "set-default") == 0;
  if ((apply && (!ack || strcmp(ack, AUDIO_ACK) != 0)) || (!apply && ack)) {
    fputs("synapse-settings: exact audio acknowledgement required only for "
          "apply\n",
          stderr);
    return 2;
  }
  audio_inventory inventory;
  if (load_audio_inventory(&inventory) != 0) {
    fprintf(stderr, "synapse-settings: audio unavailable: %s\n",
            inventory.reason);
    return 1;
  }
  audio_endpoint *endpoint = find_endpoint(&inventory, direction, device);
  if (!endpoint) {
    fputs("synapse-settings: unknown audio device token\n", stderr);
    return 1;
  }
  int changed = !endpoint->is_default;
  if (!apply) {
    print_default_contract("synapse.settings.audio-default-plan/v1", "Planned",
                           direction, endpoint, changed, 0,
                           strcmp(format, "json") == 0);
    return 0;
  }
  if (changed && execute_default(direction, endpoint) != 0) {
    fputs("synapse-settings: default audio mutation failed\n", stderr);
    return 1;
  }
  audio_inventory after;
  if (load_audio_inventory(&after) != 0) {
    fputs("synapse-settings: cannot verify default audio mutation\n", stderr);
    return 1;
  }
  audio_endpoint *verified = find_endpoint(&after, direction, device);
  if (!verified || !verified->is_default) {
    fputs("synapse-settings: default audio verification failed\n", stderr);
    return 1;
  }
  print_default_contract("synapse.settings.audio-default-receipt/v1", "Applied",
                         direction, verified, changed, 1,
                         strcmp(format, "json") == 0);
  return 0;
}
