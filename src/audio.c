// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L

#include "settings_internal.h"

#include <json-c/json.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
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
        "  synapse-settings audio plan-default --direction output|input "
        "--device ID [--format text|json]\n"
        "  synapse-settings audio set-default --direction output|input "
        "--device ID --ack " AUDIO_ACK " [--format text|json]\n",
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
    if (skip_monitors &&
        json_object_object_get_ex(value, "monitor_of_sink", &monitor) &&
        monitor && !json_object_is_type(monitor, json_type_null))
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
    }
    if (copy_label(stream->label, sizeof(stream->label), label) != 0)
      return -1;
    int target_index = json_int_value(
        value, strcmp(direction, "playback") == 0 ? "sink" : "source", -1);
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
      parse_stream_array(playback, inventory, "playback") != 0 ||
      parse_stream_array(recording, inventory, "recording") != 0 ||
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
