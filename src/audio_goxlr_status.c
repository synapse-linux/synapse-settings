// SPDX-License-Identifier: MIT
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
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define AUDIO_GOXLR_RESPONSE_LIMIT 65536U
#define AUDIO_GOXLR_STATUS_TIMEOUT_MS 3000
#define AUDIO_GOXLR_APPLY_TIMEOUT_MS 12000
#define AUDIO_GOXLR_PROVIDER_SCHEMA "synapse.goxlr.provider-status/v3"
#define AUDIO_GOXLR_INVENTORY_SCHEMA "synapse.goxlr.inventory/v1"
#define AUDIO_GOXLR_STATUS_SCHEMA "synapse.settings.audio-goxlr-status/v2"
#define AUDIO_GOXLR_PLAN_SCHEMA "synapse.settings.audio-goxlr-control-plan/v1"
#define AUDIO_GOXLR_RECEIPT_SCHEMA                                                \
  "synapse.settings.audio-goxlr-control-receipt/v1"
#define AUDIO_GOXLR_PROVIDER_PLAN_SCHEMA                                          \
  "synapse.goxlr.popup-control-plan/v1"
#define AUDIO_GOXLR_PROVIDER_RECEIPT_SCHEMA                                       \
  "synapse.goxlr.popup-control-receipt/v1"
#define AUDIO_GOXLR_PROVIDER_ACK "synapse-goxlr/popup-control/v1"
#define AUDIO_GOXLR_SETTINGS_ACK "synapse-settings/audio-goxlr-popup/v1"
#define AUDIO_GOXLR_CONTROL_COUNT 11U
#define AUDIO_GOXLR_FADER_COUNT 4U

typedef struct {
  char fader[2];
  char channel[16];
  unsigned int volume;
  int muted;
} audio_goxlr_fader;

typedef struct {
  char model[16];
  int profile_model_ready;
  uint64_t generation;
  int capabilities[AUDIO_GOXLR_CONTROL_COUNT];
  audio_goxlr_fader faders[AUDIO_GOXLR_FADER_COUNT];
  char cough_mode[8];
  int cough_muted;
  unsigned int headphones_volume;
  unsigned int line_out_volume;
  char monitored_output[16];
  int system_output_supported;
} audio_goxlr_device;

typedef struct {
  audio_goxlr_device device;
  size_t device_count;
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

typedef struct {
  unsigned int original;
  unsigned int requested;
  char cohort[17];
} audio_goxlr_control_plan;

typedef struct {
  char status[16];
  unsigned int original;
  unsigned int requested;
  unsigned int observed;
  int changed;
  int rollback_attempted;
  int rollback_succeeded_present;
  int rollback_succeeded;
} audio_goxlr_control_receipt;

static const char *const audio_goxlr_controls[AUDIO_GOXLR_CONTROL_COUNT] = {
    "fader-a-volume", "fader-b-volume", "fader-c-volume",
    "fader-d-volume", "fader-a-mute",   "fader-b-mute",
    "fader-c-mute",   "fader-d-mute",   "cough-mute",
    "headphones-volume", "line-out-volume"};

static const char *const audio_goxlr_capability_keys[AUDIO_GOXLR_CONTROL_COUNT] = {
    "faderAVolume", "faderBVolume", "faderCVolume", "faderDVolume",
    "faderAMute",   "faderBMute",   "faderCMute",   "faderDMute",
    "coughMute",    "headphonesVolume", "lineOutVolume"};

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

static int audio_goxlr_u64(audio_goxlr_json_parser *parser, uint64_t *value) {
  audio_goxlr_skip_space(parser);
  if (parser->cursor >= parser->end || *parser->cursor < '0' ||
      *parser->cursor > '9')
    return 0;
  if (*parser->cursor == '0' && parser->cursor + 1 < parser->end &&
      parser->cursor[1] >= '0' && parser->cursor[1] <= '9')
    return 0;
  uint64_t result = 0U;
  do {
    unsigned int digit = (unsigned int)(*parser->cursor - '0');
    if (result > (UINT64_MAX - digit) / 10U)
      return 0;
    result = result * 10U + digit;
    parser->cursor++;
  } while (parser->cursor < parser->end && *parser->cursor >= '0' &&
           *parser->cursor <= '9');
  *value = result;
  return 1;
}

static int audio_goxlr_unsigned(audio_goxlr_json_parser *parser,
                                unsigned int *value) {
  uint64_t decoded = 0U;
  if (!audio_goxlr_u64(parser, &decoded) || decoded > UINT_MAX)
    return 0;
  *value = (unsigned int)decoded;
  return 1;
}

static int audio_goxlr_enum(audio_goxlr_json_parser *parser, char *output,
                            size_t capacity, const char *const *values,
                            size_t count) {
  char decoded[32];
  if (!audio_goxlr_string(parser, decoded, sizeof(decoded)))
    return 0;
  for (size_t index = 0; index < count; index++) {
    if (strcmp(decoded, values[index]) != 0)
      continue;
    if (output) {
      size_t length = strlen(decoded);
      if (!capacity || length >= capacity)
        return 0;
      memcpy(output, decoded, length + 1U);
    }
    return 1;
  }
  return 0;
}

static int audio_goxlr_channel(audio_goxlr_json_parser *parser, char *output,
                               size_t capacity) {
  static const char *const values[] = {
      "Mic",       "LineIn",     "Console", "System",
      "Game",      "Chat",       "Sample",  "Music",
      "Headphones", "MicMonitor", "LineOut"};
  return audio_goxlr_enum(parser, output, capacity, values,
                          sizeof(values) / sizeof(values[0]));
}

static int audio_goxlr_mute(audio_goxlr_json_parser *parser, int *muted) {
  static const char *const values[] = {"Unmuted", "MutedToAll"};
  char decoded[16];
  if (!audio_goxlr_enum(parser, decoded, sizeof(decoded), values, 2U))
    return 0;
  *muted = strcmp(decoded, "MutedToAll") == 0;
  return 1;
}

static int audio_goxlr_system_output(audio_goxlr_json_parser *parser) {
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  for (;;) {
    char key[32];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0U;
    unsigned int number = 0U;
    int boolean_value = 0;
    if (strcmp(key, "routeToLineOut") == 0) {
      bit = 1U;
      if (!audio_goxlr_bool(parser, &boolean_value))
        return 0;
    } else if (strcmp(key, "systemVolume") == 0) {
      bit = 2U;
      if (!audio_goxlr_unsigned(parser, &number) || number > 255U)
        return 0;
    } else if (strcmp(key, "lineOutVolume") == 0) {
      bit = 4U;
      if (!audio_goxlr_unsigned(parser, &number) || number > 255U)
        return 0;
    } else if (strcmp(key, "systemFader") == 0) {
      static const char *const values[] = {"A", "B", "C", "D"};
      bit = 8U;
      audio_goxlr_skip_space(parser);
      if (parser->cursor >= parser->end)
        return 0;
      if (*parser->cursor == 'n') {
        if (!audio_goxlr_literal(parser, "null"))
          return 0;
      } else if (!audio_goxlr_enum(parser, NULL, 0U, values, 4U)) {
        return 0;
      }
    } else if (strcmp(key, "systemMuteState") == 0) {
      bit = 16U;
      audio_goxlr_skip_space(parser);
      if (parser->cursor >= parser->end)
        return 0;
      if (*parser->cursor == 'n') {
        if (!audio_goxlr_literal(parser, "null"))
          return 0;
      } else if (!audio_goxlr_mute(parser, &boolean_value)) {
        return 0;
      }
    } else if (strcmp(key, "lineOutMix") == 0) {
      static const char *const values[] = {"A", "B"};
      bit = 32U;
      if (!audio_goxlr_enum(parser, NULL, 0U, values, 2U))
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

static int audio_goxlr_capabilities(audio_goxlr_json_parser *parser,
                                    int capabilities[]) {
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  for (;;) {
    char key[32];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    size_t index = AUDIO_GOXLR_CONTROL_COUNT;
    for (size_t candidate = 0U; candidate < AUDIO_GOXLR_CONTROL_COUNT;
         candidate++)
      if (strcmp(key, audio_goxlr_capability_keys[candidate]) == 0) {
        index = candidate;
        break;
      }
    if (index == AUDIO_GOXLR_CONTROL_COUNT ||
        (fields & (1U << index)) != 0U ||
        !audio_goxlr_bool(parser, &capabilities[index]))
      return 0;
    fields |= 1U << index;
    if (audio_goxlr_consume(parser, '}'))
      break;
    if (!audio_goxlr_consume(parser, ','))
      return 0;
  }
  return fields == ((1U << AUDIO_GOXLR_CONTROL_COUNT) - 1U);
}

static int audio_goxlr_parse_fader(audio_goxlr_json_parser *parser,
                                   audio_goxlr_fader *fader) {
  memset(fader, 0, sizeof(*fader));
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  for (;;) {
    char key[24];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0U;
    if (strcmp(key, "fader") == 0) {
      static const char *const values[] = {"A", "B", "C", "D"};
      bit = 1U;
      if (!audio_goxlr_enum(parser, fader->fader, sizeof(fader->fader), values,
                            4U))
        return 0;
    } else if (strcmp(key, "channel") == 0) {
      bit = 2U;
      if (!audio_goxlr_channel(parser, fader->channel,
                               sizeof(fader->channel)))
        return 0;
    } else if (strcmp(key, "volume") == 0) {
      bit = 4U;
      if (!audio_goxlr_unsigned(parser, &fader->volume) ||
          fader->volume > 255U)
        return 0;
    } else if (strcmp(key, "muteState") == 0) {
      bit = 8U;
      if (!audio_goxlr_mute(parser, &fader->muted))
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
  return fields == 15U;
}

static int audio_goxlr_faders(audio_goxlr_json_parser *parser,
                              audio_goxlr_fader faders[]) {
  if (!audio_goxlr_consume(parser, '['))
    return 0;
  unsigned int seen = 0U;
  size_t count = 0U;
  if (audio_goxlr_consume(parser, ']'))
    return 0;
  for (;;) {
    audio_goxlr_fader decoded;
    if (count >= AUDIO_GOXLR_FADER_COUNT ||
        !audio_goxlr_parse_fader(parser, &decoded))
      return 0;
    unsigned int index = (unsigned int)(decoded.fader[0] - 'A');
    if (index >= AUDIO_GOXLR_FADER_COUNT || (seen & (1U << index)) != 0U)
      return 0;
    faders[index] = decoded;
    seen |= 1U << index;
    count++;
    if (audio_goxlr_consume(parser, ']'))
      break;
    if (!audio_goxlr_consume(parser, ','))
      return 0;
  }
  return count == AUDIO_GOXLR_FADER_COUNT && seen == 15U;
}

static int audio_goxlr_cough(audio_goxlr_json_parser *parser,
                             audio_goxlr_device *device) {
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  for (;;) {
    char key[24];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0U;
    if (strcmp(key, "mode") == 0) {
      static const char *const values[] = {"Toggle", "Hold"};
      bit = 1U;
      if (!audio_goxlr_enum(parser, device->cough_mode,
                            sizeof(device->cough_mode), values, 2U))
        return 0;
    } else if (strcmp(key, "muteState") == 0) {
      bit = 2U;
      if (!audio_goxlr_mute(parser, &device->cough_muted))
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
  return fields == 3U;
}

static int audio_goxlr_outputs(audio_goxlr_json_parser *parser,
                               audio_goxlr_device *device) {
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  for (;;) {
    char key[32];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0U;
    if (strcmp(key, "headphonesVolume") == 0) {
      bit = 1U;
      if (!audio_goxlr_unsigned(parser, &device->headphones_volume) ||
          device->headphones_volume > 255U)
        return 0;
    } else if (strcmp(key, "lineOutVolume") == 0) {
      bit = 2U;
      if (!audio_goxlr_unsigned(parser, &device->line_out_volume) ||
          device->line_out_volume > 255U)
        return 0;
    } else if (strcmp(key, "monitoredOutput") == 0) {
      static const char *const values[] = {"Headphones", "BroadcastMix",
                                           "ChatMic",    "Sampler",
                                           "LineOut",    "StreamMix2"};
      bit = 4U;
      if (!audio_goxlr_enum(parser, device->monitored_output,
                            sizeof(device->monitored_output), values, 6U))
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
  return fields == 7U;
}

static int audio_goxlr_device_status(audio_goxlr_json_parser *parser,
                                     audio_goxlr_device *device) {
  memset(device, 0, sizeof(*device));
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  char id[16] = "";
  for (;;) {
    char key[40];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0U;
    int boolean_value = 0;
    if (strcmp(key, "id") == 0) {
      bit = 1U;
      if (!audio_goxlr_string(parser, id, sizeof(id)))
        return 0;
    } else if (strcmp(key, "model") == 0) {
      static const char *const values[] = {"GoXLR Mini", "GoXLR"};
      bit = 2U;
      if (!audio_goxlr_enum(parser, device->model, sizeof(device->model),
                            values, 2U))
        return 0;
    } else if (strcmp(key, "profileModelReady") == 0) {
      bit = 4U;
      if (!audio_goxlr_bool(parser, &device->profile_model_ready))
        return 0;
    } else if (strcmp(key, "generation") == 0) {
      bit = 8U;
      if (!audio_goxlr_u64(parser, &device->generation))
        return 0;
    } else if (strcmp(key, "stateAuthority") == 0) {
      bit = 16U;
      char authority[32];
      if (!audio_goxlr_string(parser, authority, sizeof(authority)) ||
          strcmp(authority, "provider-profile-model") != 0)
        return 0;
    } else if (strcmp(key, "hardwareReadback") == 0) {
      bit = 32U;
      if (!audio_goxlr_bool(parser, &boolean_value) || boolean_value)
        return 0;
    } else if (strcmp(key, "hardwareExactRollback") == 0) {
      bit = 64U;
      if (!audio_goxlr_bool(parser, &boolean_value) || boolean_value)
        return 0;
    } else if (strcmp(key, "popupCapabilities") == 0) {
      bit = 128U;
      if (!audio_goxlr_capabilities(parser, device->capabilities))
        return 0;
    } else if (strcmp(key, "faders") == 0) {
      bit = 256U;
      if (!audio_goxlr_faders(parser, device->faders))
        return 0;
    } else if (strcmp(key, "cough") == 0) {
      bit = 512U;
      if (!audio_goxlr_cough(parser, device))
        return 0;
    } else if (strcmp(key, "outputs") == 0) {
      bit = 1024U;
      if (!audio_goxlr_outputs(parser, device))
        return 0;
    } else if (strcmp(key, "systemOutputSupported") == 0) {
      bit = 2048U;
      if (!audio_goxlr_bool(parser, &device->system_output_supported))
        return 0;
    } else if (strcmp(key, "systemOutput") == 0) {
      bit = 4096U;
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
  if (fields != 8191U || strcmp(id, "goxlr-1") != 0 ||
      (device->profile_model_ready && device->generation == 0U) ||
      (strcmp(device->cough_mode, "Hold") == 0 &&
       device->capabilities[8]))
    return 0;
  for (size_t index = 0U; index < AUDIO_GOXLR_CONTROL_COUNT; index++)
    if (device->capabilities[index] && !device->profile_model_ready)
      return 0;
  return 1;
}

static int audio_goxlr_parse_provider(const char *json, size_t length,
                                      audio_goxlr_provider_status *status) {
  if (!json || !length || length > AUDIO_GOXLR_RESPONSE_LIMIT || !status)
    return 0;
  memset(status, 0, sizeof(*status));
  audio_goxlr_json_parser parser = {.cursor = json, .end = json + length};
  if (!audio_goxlr_consume(&parser, '{'))
    return 0;
  unsigned int fields = 0U;
  unsigned int declared_count = UINT_MAX;
  int active = 0;
  int truncated = 0;
  for (;;) {
    char key[32];
    if (!audio_goxlr_string(&parser, key, sizeof(key)) ||
        !audio_goxlr_consume(&parser, ':'))
      return 0;
    unsigned int bit = 0U;
    if (strcmp(key, "schema") == 0) {
      bit = 1U;
      char schema[48];
      if (!audio_goxlr_string(&parser, schema, sizeof(schema)) ||
          strcmp(schema, AUDIO_GOXLR_PROVIDER_SCHEMA) != 0)
        return 0;
    } else if (strcmp(key, "providerActive") == 0) {
      bit = 2U;
      if (!audio_goxlr_bool(&parser, &active) || !active)
        return 0;
    } else if (strcmp(key, "deviceCount") == 0) {
      bit = 4U;
      if (!audio_goxlr_unsigned(&parser, &declared_count) ||
          declared_count > 1U)
        return 0;
    } else if (strcmp(key, "truncated") == 0) {
      bit = 8U;
      if (!audio_goxlr_bool(&parser, &truncated) || truncated)
        return 0;
    } else if (strcmp(key, "devices") == 0) {
      bit = 16U;
      if (!audio_goxlr_consume(&parser, '['))
        return 0;
      if (audio_goxlr_consume(&parser, ']')) {
        status->device_count = 0U;
      } else {
        if (!audio_goxlr_device_status(&parser, &status->device) ||
            !audio_goxlr_consume(&parser, ']'))
          return 0;
        status->device_count = 1U;
      }
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
  return fields == 31U && parser.cursor == parser.end &&
         declared_count == status->device_count;
}

static int audio_goxlr_inventory_device(audio_goxlr_json_parser *parser) {
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  char id[16] = "";
  char model[16] = "";
  char usb_id[16] = "";
  int connected = 0;
  for (;;) {
    char key[24];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0U;
    if (strcmp(key, "id") == 0) {
      bit = 1U;
      if (!audio_goxlr_string(parser, id, sizeof(id)))
        return 0;
    } else if (strcmp(key, "model") == 0) {
      bit = 2U;
      if (!audio_goxlr_string(parser, model, sizeof(model)))
        return 0;
    } else if (strcmp(key, "usbId") == 0) {
      bit = 4U;
      if (!audio_goxlr_string(parser, usb_id, sizeof(usb_id)))
        return 0;
    } else if (strcmp(key, "connected") == 0) {
      bit = 8U;
      if (!audio_goxlr_bool(parser, &connected))
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
  size_t id_length = strlen(id);
  return fields == 15U && connected && id_length == 7U &&
         memcmp(id, "goxlr-", 6U) == 0 && id[6] >= '1' && id[6] <= '8' &&
         ((strcmp(model, "GoXLR Mini") == 0 &&
           strcmp(usb_id, "1220:8fe4") == 0) ||
          (strcmp(model, "GoXLR") == 0 &&
           strcmp(usb_id, "1220:8fe0") == 0));
}

static int audio_goxlr_parse_inventory(const char *json, size_t length,
                                       unsigned int *device_count) {
  if (!json || !length || length > AUDIO_GOXLR_RESPONSE_LIMIT || !device_count)
    return 0;
  audio_goxlr_json_parser parser = {.cursor = json, .end = json + length};
  if (!audio_goxlr_consume(&parser, '{'))
    return 0;
  unsigned int fields = 0U;
  unsigned int declared = UINT_MAX;
  unsigned int observed = 0U;
  for (;;) {
    char key[24];
    if (!audio_goxlr_string(&parser, key, sizeof(key)) ||
        !audio_goxlr_consume(&parser, ':'))
      return 0;
    unsigned int bit = 0U;
    if (strcmp(key, "schema") == 0) {
      bit = 1U;
      char schema[40];
      if (!audio_goxlr_string(&parser, schema, sizeof(schema)) ||
          strcmp(schema, AUDIO_GOXLR_INVENTORY_SCHEMA) != 0)
        return 0;
    } else if (strcmp(key, "deviceCount") == 0) {
      bit = 2U;
      if (!audio_goxlr_unsigned(&parser, &declared) || declared > 8U)
        return 0;
    } else if (strcmp(key, "truncated") == 0) {
      int truncated = 0;
      bit = 4U;
      if (!audio_goxlr_bool(&parser, &truncated) || truncated)
        return 0;
    } else if (strcmp(key, "devices") == 0) {
      bit = 8U;
      if (!audio_goxlr_consume(&parser, '['))
        return 0;
      if (!audio_goxlr_consume(&parser, ']')) {
        for (;;) {
          if (observed >= 8U || !audio_goxlr_inventory_device(&parser))
            return 0;
          observed++;
          if (audio_goxlr_consume(&parser, ']'))
            break;
          if (!audio_goxlr_consume(&parser, ','))
            return 0;
        }
      }
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
  if (fields != 15U || parser.cursor != parser.end || declared != observed)
    return 0;
  *device_count = observed;
  return 1;
}

static int audio_goxlr_now_ms(int64_t *value) {
  struct timespec now = {0};
  if (!value || clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
      now.tv_nsec < 0 || now.tv_nsec >= 1000000000L ||
      (uint64_t)now.tv_sec > (uint64_t)INT64_MAX / 1000U)
    return 0;
  int64_t milliseconds = (int64_t)now.tv_sec * 1000;
  int64_t fraction = now.tv_nsec / 1000000;
  if (milliseconds > INT64_MAX - fraction)
    return 0;
  *value = milliseconds + fraction;
  return 1;
}

static int audio_goxlr_pipe_above_standard(int descriptors[2]) {
  for (size_t index = 0U; index < 2U; index++) {
    if (descriptors[index] > STDERR_FILENO)
      continue;
    int replacement =
        fcntl(descriptors[index], F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    if (replacement < 0)
      return 0;
    close(descriptors[index]);
    descriptors[index] = replacement;
  }
  return 1;
}

static int audio_goxlr_pipe_close_on_exec(const int descriptors[2]) {
  for (size_t index = 0U; index < 2U; index++) {
    int flags = fcntl(descriptors[index], F_GETFD);
    if (flags < 0 ||
        fcntl(descriptors[index], F_SETFD, flags | FD_CLOEXEC) != 0)
      return 0;
  }
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

static audio_goxlr_capture
audio_goxlr_capture_command(const char *binary, char *const argv[],
                            int timeout_ms) {
  audio_goxlr_capture capture = {0};
  int64_t started = 0;
  if (!binary || !argv || timeout_ms < 1 || !audio_goxlr_now_ms(&started) ||
      started > INT64_MAX - timeout_ms) {
    capture.failed = 1;
    return capture;
  }
  const int64_t deadline = started + timeout_ms;
  int descriptors[2];
  int launch_descriptors[2];
  if (pipe(descriptors) != 0) {
    capture.failed = 1;
    return capture;
  }
  if (!audio_goxlr_pipe_above_standard(descriptors) ||
      !audio_goxlr_pipe_close_on_exec(descriptors) ||
      pipe(launch_descriptors) != 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    capture.failed = 1;
    return capture;
  }
  if (!audio_goxlr_pipe_above_standard(launch_descriptors) ||
      !audio_goxlr_pipe_close_on_exec(launch_descriptors)) {
    close(descriptors[0]);
    close(descriptors[1]);
    close(launch_descriptors[0]);
    close(launch_descriptors[1]);
    capture.failed = 1;
    return capture;
  }
  const pid_t parent = getpid();
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
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent ||
        setpgid(0, 0) != 0 || dup2(descriptors[1], STDOUT_FILENO) < 0)
      audio_goxlr_child_fail(launch_descriptors[1], 126);
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 ||
        dup2(null_fd, STDERR_FILENO) < 0)
      audio_goxlr_child_fail(launch_descriptors[1], 126);
    if (null_fd > STDERR_FILENO)
      close(null_fd);
    close(descriptors[0]);
    close(descriptors[1]);
    if (setenv("LC_ALL", "C", 1) != 0 || setenv("LANG", "C", 1) != 0)
      audio_goxlr_child_fail(launch_descriptors[1], 126);
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
    (void)kill(-child, SIGKILL);
  } else {
    capture.status = WEXITSTATUS(wait_status);
    if (capture.status != 0)
      (void)kill(-child, SIGKILL);
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

static void audio_goxlr_capture_free(audio_goxlr_capture *capture) {
  free(capture->data);
  memset(capture, 0, sizeof(*capture));
}

static int audio_goxlr_any_capability(const audio_goxlr_device *device) {
  for (size_t index = 0U; index < AUDIO_GOXLR_CONTROL_COUNT; index++)
    if (device->capabilities[index])
      return 1;
  return 0;
}

static json_object *audio_goxlr_project_device(const audio_goxlr_device *device,
                                               int controls_available) {
  json_object *item = json_object_new_object();
  json_object_object_add(item, "model", json_object_new_string(device->model));
  json_object_object_add(item, "profileModelReady",
                         json_object_new_boolean(device->profile_model_ready));
  json_object_object_add(item, "systemOutputSupported",
                         json_object_new_boolean(device->system_output_supported));
  json_object_object_add(item, "controlAvailable",
                         json_object_new_boolean(controls_available));
  json_object *faders = json_object_new_array_ext(4);
  for (size_t index = 0U; index < AUDIO_GOXLR_FADER_COUNT; index++) {
    json_object *fader = json_object_new_object();
    json_object_object_add(fader, "fader",
                           json_object_new_string(device->faders[index].fader));
    json_object_object_add(
        fader, "channel",
        json_object_new_string(device->faders[index].channel));
    json_object_object_add(fader, "volume",
                           json_object_new_int((int)device->faders[index].volume));
    json_object_object_add(fader, "muted",
                           json_object_new_boolean(device->faders[index].muted));
    json_object_object_add(
        fader, "volumeAvailable",
        json_object_new_boolean(controls_available && device->capabilities[index]));
    json_object_object_add(
        fader, "muteAvailable",
        json_object_new_boolean(controls_available &&
                                device->capabilities[4U + index]));
    json_object_array_add(faders, fader);
  }
  json_object_object_add(item, "faders", faders);
  json_object *cough = json_object_new_object();
  json_object_object_add(cough, "mode",
                         json_object_new_string(device->cough_mode));
  json_object_object_add(cough, "muted",
                         json_object_new_boolean(device->cough_muted));
  json_object_object_add(cough, "available",
                         json_object_new_boolean(controls_available &&
                                                 device->capabilities[8]));
  json_object_object_add(item, "cough", cough);
  json_object *outputs = json_object_new_object();
  json_object_object_add(outputs, "headphonesVolume",
                         json_object_new_int((int)device->headphones_volume));
  json_object_object_add(outputs, "lineOutVolume",
                         json_object_new_int((int)device->line_out_volume));
  json_object_object_add(outputs, "monitoredOutput",
                         json_object_new_string(device->monitored_output));
  json_object_object_add(outputs, "headphonesAvailable",
                         json_object_new_boolean(controls_available &&
                                                 device->capabilities[9]));
  json_object_object_add(outputs, "lineOutAvailable",
                         json_object_new_boolean(controls_available &&
                                                 device->capabilities[10]));
  json_object_object_add(item, "outputs", outputs);
  return item;
}

static void audio_goxlr_print_status(const char *status, const char *reason,
                                     int provider_active, int presence_known,
                                     int device_present,
                                     const audio_goxlr_provider_status *provider,
                                     const char *format) {
  int controls_available =
      provider_active && provider->device_count == 1U &&
      provider->device.profile_model_ready &&
      audio_goxlr_any_capability(&provider->device);
  if (strcmp(format, "text") == 0) {
    printf("GoXLR status: %s", status);
    if (reason)
      printf(" (%s)", reason);
    putchar('\n');
    printf("Device presence: %s\n",
           presence_known ? (device_present ? "present" : "absent")
                          : "unknown");
    if (provider_active && provider->device_count == 1U)
      printf("Provider device: %s; popup controls %s\n",
             provider->device.model,
             controls_available ? "available" : "unavailable");
    return;
  }
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
  json_object_object_add(root, "presenceKnown",
                         json_object_new_boolean(presence_known));
  json_object_object_add(root, "devicePresent",
                         json_object_new_boolean(device_present));
  size_t count = provider_active ? provider->device_count : 0U;
  json_object_object_add(root, "deviceCount",
                         json_object_new_int64((int64_t)count));
  json_object *devices = json_object_new_array_ext((int)count);
  if (count == 1U)
    json_object_array_add(
        devices, audio_goxlr_project_device(&provider->device,
                                            controls_available));
  json_object_object_add(root, "devices", devices);
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("provider-profile-model"));
  json_object_object_add(root, "hardwareReadback", json_object_new_boolean(0));
  json_object_object_add(root, "hardwareExactRollback",
                         json_object_new_boolean(0));
  json_object_object_add(root, "mutationAvailable",
                         json_object_new_boolean(controls_available));
  json_object_object_add(root, "inspectionReadOnly", json_object_new_boolean(1));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static void audio_goxlr_probe_presence(const char *binary, int *known,
                                       int *present) {
  *known = 0;
  *present = 0;
  char *const argv[] = {(char *)binary, "inventory", "--format", "json", NULL};
  audio_goxlr_capture capture = audio_goxlr_capture_command(
      binary, argv, AUDIO_GOXLR_STATUS_TIMEOUT_MS);
  unsigned int count = 0U;
  if (capture.data && !capture.failed && !capture.launch_failed &&
      !capture.too_large && !capture.timed_out && capture.status == 0 &&
      audio_goxlr_parse_inventory(capture.data, capture.size, &count)) {
    *known = 1;
    *present = count > 0U;
  }
  audio_goxlr_capture_free(&capture);
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
  int presence_known = 0;
  int device_present = 0;
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
    char *const status_argv[] = {(char *)binary, "provider-status", "--format",
                                 "json", NULL};
    audio_goxlr_capture capture = audio_goxlr_capture_command(
        binary, status_argv, AUDIO_GOXLR_STATUS_TIMEOUT_MS);
    if (!capture.data || capture.failed || capture.launch_failed) {
      status = "Failed";
      reason = "status-unavailable";
    } else if (capture.too_large) {
      status = "Failed";
      reason = "response-too-large";
    } else if (capture.timed_out) {
      status = "Failed";
      reason = "timeout";
    } else if (capture.status == 3) {
      status = "Inactive";
      reason = "provider-inactive";
    } else if (capture.status != 0) {
      status = "Failed";
      reason = "status-unavailable";
    } else if (!audio_goxlr_parse_provider(capture.data, capture.size,
                                           &provider)) {
      status = "Failed";
      reason = "invalid-response";
    } else {
      status = "Ready";
      reason = NULL;
      provider_active = 1;
      presence_known = 1;
      device_present = provider.device_count > 0U;
    }
    audio_goxlr_capture_free(&capture);
    if (!provider_active)
      audio_goxlr_probe_presence(binary, &presence_known, &device_present);
  } else if (*binary && binary_error != ENOENT && binary_error != ENOTDIR) {
    status = "Failed";
    reason = "status-unavailable";
  }

  audio_goxlr_print_status(status, reason, provider_active, presence_known,
                           device_present, &provider, format);
  return 0;
}

static int audio_goxlr_control_index(const char *control) {
  for (size_t index = 0U; index < AUDIO_GOXLR_CONTROL_COUNT; index++)
    if (strcmp(control, audio_goxlr_controls[index]) == 0)
      return (int)index;
  return -1;
}

static int audio_goxlr_canonical_unsigned(const char *text,
                                          unsigned int maximum,
                                          unsigned int *value) {
  if (!text || !*text || (*text == '0' && text[1] != '\0'))
    return 0;
  unsigned int decoded = 0U;
  for (const char *cursor = text; *cursor; cursor++) {
    if (*cursor < '0' || *cursor > '9')
      return 0;
    unsigned int digit = (unsigned int)(*cursor - '0');
    if (decoded > (UINT_MAX - digit) / 10U)
      return 0;
    decoded = decoded * 10U + digit;
  }
  if (decoded > maximum)
    return 0;
  *value = decoded;
  return 1;
}

static int audio_goxlr_cohort(const char *value) {
  if (!value || strlen(value) != 16U)
    return 0;
  for (size_t index = 0U; index < 16U; index++)
    if (!((value[index] >= '0' && value[index] <= '9') ||
          (value[index] >= 'a' && value[index] <= 'f')))
      return 0;
  return 1;
}

static int audio_goxlr_control_channel(const char *control,
                                       const char *channel) {
  static const char *const channels[] = {
      "Mic",       "LineIn",     "Console", "System",
      "Game",      "Chat",       "Sample",  "Music",
      "Headphones", "MicMonitor", "LineOut"};
  int index = audio_goxlr_control_index(control);
  if (index < 0)
    return 0;
  if (index < 8) {
    for (size_t candidate = 0U;
         candidate < sizeof(channels) / sizeof(channels[0]); candidate++)
      if (strcmp(channel, channels[candidate]) == 0)
        return 1;
    return 0;
  }
  return (index == 8 && strcmp(channel, "Mic") == 0) ||
         (index == 9 && strcmp(channel, "Headphones") == 0) ||
         (index == 10 && strcmp(channel, "LineOut") == 0);
}

static int audio_goxlr_parse_channel_string(audio_goxlr_json_parser *parser,
                                             const char *control) {
  char channel[16];
  if (!audio_goxlr_channel(parser, channel, sizeof(channel)))
    return 0;
  return audio_goxlr_control_channel(control, channel);
}

static int audio_goxlr_value(audio_goxlr_json_parser *parser, int mute,
                             unsigned int *value) {
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  char kind[8] = "";
  unsigned int decoded = 0U;
  for (;;) {
    char key[16];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0U;
    if (strcmp(key, "kind") == 0) {
      bit = 1U;
      if (!audio_goxlr_string(parser, kind, sizeof(kind)))
        return 0;
    } else if (strcmp(key, "value") == 0) {
      bit = 2U;
      if (!audio_goxlr_unsigned(parser, &decoded) ||
          decoded > (mute ? 1U : 255U))
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
  if (fields != 3U || strcmp(kind, mute ? "Mute" : "Volume") != 0)
    return 0;
  *value = decoded;
  return 1;
}

static int audio_goxlr_parse_provider_plan(const char *json, size_t length,
                                           const char *expected_control,
                                           unsigned int expected_requested,
                                           audio_goxlr_control_plan *plan) {
  if (!json || !length || length > AUDIO_GOXLR_RESPONSE_LIMIT || !plan)
    return 0;
  memset(plan, 0, sizeof(*plan));
  int control_index = audio_goxlr_control_index(expected_control);
  int mute = control_index >= 4 && control_index <= 8;
  audio_goxlr_json_parser parser = {.cursor = json, .end = json + length};
  if (!audio_goxlr_consume(&parser, '{'))
    return 0;
  unsigned int fields = 0U;
  for (;;) {
    char key[40];
    if (!audio_goxlr_string(&parser, key, sizeof(key)) ||
        !audio_goxlr_consume(&parser, ':'))
      return 0;
    unsigned int bit = 0U;
    char text[48];
    int boolean_value = 0;
    uint64_t generation = 0U;
    if (strcmp(key, "schema") == 0) {
      bit = 1U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, AUDIO_GOXLR_PROVIDER_PLAN_SCHEMA) != 0)
        return 0;
    } else if (strcmp(key, "device") == 0) {
      bit = 2U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, "goxlr-1") != 0)
        return 0;
    } else if (strcmp(key, "model") == 0) {
      bit = 4U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          (strcmp(text, "GoXLR Mini") != 0 && strcmp(text, "GoXLR") != 0))
        return 0;
    } else if (strcmp(key, "control") == 0) {
      bit = 8U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, expected_control) != 0)
        return 0;
    } else if (strcmp(key, "channel") == 0) {
      bit = 16U;
      if (!audio_goxlr_parse_channel_string(&parser, expected_control))
        return 0;
    } else if (strcmp(key, "generation") == 0) {
      bit = 32U;
      if (!audio_goxlr_u64(&parser, &generation) || generation == 0U)
        return 0;
    } else if (strcmp(key, "original") == 0) {
      bit = 64U;
      if (!audio_goxlr_value(&parser, mute, &plan->original))
        return 0;
    } else if (strcmp(key, "requested") == 0) {
      bit = 128U;
      if (!audio_goxlr_value(&parser, mute, &plan->requested))
        return 0;
    } else if (strcmp(key, "cohort") == 0) {
      bit = 256U;
      if (!audio_goxlr_string(&parser, plan->cohort, sizeof(plan->cohort)) ||
          !audio_goxlr_cohort(plan->cohort))
        return 0;
    } else if (strcmp(key, "requiresAcknowledgement") == 0) {
      bit = 512U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, AUDIO_GOXLR_PROVIDER_ACK) != 0)
        return 0;
    } else if (strcmp(key, "stateAuthority") == 0) {
      bit = 1024U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, "provider-profile-model") != 0)
        return 0;
    } else if (strcmp(key, "hardwareReadback") == 0) {
      bit = 2048U;
      if (!audio_goxlr_bool(&parser, &boolean_value) || boolean_value)
        return 0;
    } else if (strcmp(key, "hardwareExactRollback") == 0) {
      bit = 4096U;
      if (!audio_goxlr_bool(&parser, &boolean_value) || boolean_value)
        return 0;
    } else if (strcmp(key, "bounded") == 0) {
      bit = 8192U;
      if (!audio_goxlr_bool(&parser, &boolean_value) || !boolean_value)
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
  return fields == 16383U && parser.cursor == parser.end &&
         plan->requested == expected_requested;
}

static int audio_goxlr_parse_rollback(audio_goxlr_json_parser *parser,
                                      audio_goxlr_control_receipt *receipt) {
  if (!audio_goxlr_consume(parser, '{'))
    return 0;
  unsigned int fields = 0U;
  int available = 0;
  int hardware_exact = 0;
  for (;;) {
    char key[24];
    if (!audio_goxlr_string(parser, key, sizeof(key)) ||
        !audio_goxlr_consume(parser, ':'))
      return 0;
    unsigned int bit = 0U;
    char text[40];
    if (strcmp(key, "available") == 0) {
      bit = 1U;
      if (!audio_goxlr_bool(parser, &available) || !available)
        return 0;
    } else if (strcmp(key, "authority") == 0) {
      bit = 2U;
      if (!audio_goxlr_string(parser, text, sizeof(text)) ||
          strcmp(text, "provider-profile-model-only") != 0)
        return 0;
    } else if (strcmp(key, "hardwareExact") == 0) {
      bit = 4U;
      if (!audio_goxlr_bool(parser, &hardware_exact) || hardware_exact)
        return 0;
    } else if (strcmp(key, "attempted") == 0) {
      bit = 8U;
      if (!audio_goxlr_bool(parser, &receipt->rollback_attempted))
        return 0;
    } else if (strcmp(key, "succeeded") == 0) {
      bit = 16U;
      audio_goxlr_skip_space(parser);
      if (parser->cursor < parser->end && *parser->cursor == 'n') {
        if (!audio_goxlr_literal(parser, "null"))
          return 0;
      } else {
        receipt->rollback_succeeded_present = 1;
        if (!audio_goxlr_bool(parser, &receipt->rollback_succeeded))
          return 0;
      }
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
  return fields == 31U &&
         receipt->rollback_attempted == receipt->rollback_succeeded_present;
}

static int audio_goxlr_parse_provider_receipt(
    const char *json, size_t length, const char *expected_control,
    unsigned int expected_original, unsigned int expected_requested,
    audio_goxlr_control_receipt *receipt) {
  if (!json || !length || length > AUDIO_GOXLR_RESPONSE_LIMIT || !receipt)
    return 0;
  memset(receipt, 0, sizeof(*receipt));
  int control_index = audio_goxlr_control_index(expected_control);
  int mute = control_index >= 4 && control_index <= 8;
  audio_goxlr_json_parser parser = {.cursor = json, .end = json + length};
  if (!audio_goxlr_consume(&parser, '{'))
    return 0;
  unsigned int fields = 0U;
  for (;;) {
    char key[32];
    if (!audio_goxlr_string(&parser, key, sizeof(key)) ||
        !audio_goxlr_consume(&parser, ':'))
      return 0;
    unsigned int bit = 0U;
    char text[64];
    int boolean_value = 0;
    if (strcmp(key, "schema") == 0) {
      bit = 1U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, AUDIO_GOXLR_PROVIDER_RECEIPT_SCHEMA) != 0)
        return 0;
    } else if (strcmp(key, "device") == 0) {
      bit = 2U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, "goxlr-1") != 0)
        return 0;
    } else if (strcmp(key, "model") == 0) {
      bit = 4U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          (strcmp(text, "GoXLR Mini") != 0 && strcmp(text, "GoXLR") != 0))
        return 0;
    } else if (strcmp(key, "status") == 0) {
      static const char *const values[] = {
          "Applied", "AlreadyApplied", "Refused", "Drifted", "RolledBack",
          "RollbackFailed"};
      bit = 8U;
      if (!audio_goxlr_enum(&parser, receipt->status,
                            sizeof(receipt->status), values, 6U))
        return 0;
    } else if (strcmp(key, "control") == 0) {
      bit = 16U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, expected_control) != 0)
        return 0;
    } else if (strcmp(key, "channel") == 0) {
      bit = 32U;
      if (!audio_goxlr_parse_channel_string(&parser, expected_control))
        return 0;
    } else if (strcmp(key, "original") == 0) {
      bit = 64U;
      if (!audio_goxlr_value(&parser, mute, &receipt->original))
        return 0;
    } else if (strcmp(key, "requested") == 0) {
      bit = 128U;
      if (!audio_goxlr_value(&parser, mute, &receipt->requested))
        return 0;
    } else if (strcmp(key, "observed") == 0) {
      bit = 256U;
      if (!audio_goxlr_value(&parser, mute, &receipt->observed))
        return 0;
    } else if (strcmp(key, "changed") == 0) {
      bit = 512U;
      if (!audio_goxlr_bool(&parser, &receipt->changed))
        return 0;
    } else if (strcmp(key, "stateAuthority") == 0) {
      bit = 1024U;
      if (!audio_goxlr_string(&parser, text, sizeof(text)) ||
          strcmp(text, "provider-profile-model") != 0)
        return 0;
    } else if (strcmp(key, "hardwareReadback") == 0) {
      bit = 2048U;
      if (!audio_goxlr_bool(&parser, &boolean_value) || boolean_value)
        return 0;
    } else if (strcmp(key, "rollback") == 0) {
      bit = 4096U;
      if (!audio_goxlr_parse_rollback(&parser, receipt))
        return 0;
    } else if (strcmp(key, "playbackStarted") == 0) {
      bit = 8192U;
      if (!audio_goxlr_bool(&parser, &boolean_value) || boolean_value)
        return 0;
    } else if (strcmp(key, "captureStarted") == 0) {
      bit = 16384U;
      if (!audio_goxlr_bool(&parser, &boolean_value) || boolean_value)
        return 0;
    } else if (strcmp(key, "bounded") == 0) {
      bit = 32768U;
      if (!audio_goxlr_bool(&parser, &boolean_value) || !boolean_value)
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
  if (fields != 65535U || parser.cursor != parser.end ||
      receipt->original != expected_original ||
      receipt->requested != expected_requested)
    return 0;
  if ((strcmp(receipt->status, "Applied") == 0 &&
       (receipt->observed != expected_requested ||
        receipt->rollback_attempted)) ||
      (strcmp(receipt->status, "AlreadyApplied") == 0 &&
       (expected_original != expected_requested ||
        receipt->observed != expected_original || receipt->changed ||
        receipt->rollback_attempted)) ||
      (strcmp(receipt->status, "Refused") == 0 &&
       (receipt->observed != expected_original || receipt->changed ||
        receipt->rollback_attempted)) ||
      (strcmp(receipt->status, "RolledBack") == 0 &&
       (!receipt->rollback_attempted || !receipt->rollback_succeeded ||
        receipt->observed != expected_original || receipt->changed)) ||
      (strcmp(receipt->status, "RollbackFailed") == 0 &&
       (!receipt->rollback_attempted || receipt->rollback_succeeded)) ||
      (strcmp(receipt->status, "Drifted") == 0 &&
       receipt->rollback_attempted))
    return 0;
  return 1;
}

static void audio_goxlr_print_plan(const char *control,
                                   const audio_goxlr_control_plan *plan,
                                   const char *format) {
  if (strcmp(format, "text") == 0) {
    printf("GoXLR control plan: %s %u -> %u\n", control, plan->original,
           plan->requested);
    printf("Cohort: %s\n", plan->cohort);
    puts("State authority: provider-profile-model; hardware readback: no; applied: no");
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(root, "schema",
                         json_object_new_string(AUDIO_GOXLR_PLAN_SCHEMA));
  json_object_object_add(root, "status", json_object_new_string("Planned"));
  json_object_object_add(root, "control", json_object_new_string(control));
  json_object_object_add(root, "originalValue",
                         json_object_new_int((int)plan->original));
  json_object_object_add(root, "requestedValue",
                         json_object_new_int((int)plan->requested));
  json_object_object_add(root, "cohort", json_object_new_string(plan->cohort));
  json_object_object_add(
      root, "requiresAcknowledgement",
      json_object_new_string(AUDIO_GOXLR_SETTINGS_ACK));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("provider-profile-model"));
  json_object_object_add(root, "hardwareReadback", json_object_new_boolean(0));
  json_object_object_add(root, "hardwareExactRollback",
                         json_object_new_boolean(0));
  json_object_object_add(root, "applied", json_object_new_boolean(0));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static void audio_goxlr_print_receipt(
    const char *control, const audio_goxlr_control_receipt *receipt,
    const char *format) {
  if (strcmp(format, "text") == 0) {
    printf("GoXLR control result: %s (%s)\n", receipt->status, control);
    printf("Value: %u -> %u; observed %u; changed: %s\n", receipt->original,
           receipt->requested, receipt->observed,
           receipt->changed ? "yes" : "no");
    printf("Provider-model rollback attempted: %s\n",
           receipt->rollback_attempted ? "yes" : "no");
    puts("Hardware readback: no; hardware-exact rollback: no");
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(root, "schema",
                         json_object_new_string(AUDIO_GOXLR_RECEIPT_SCHEMA));
  json_object_object_add(root, "status",
                         json_object_new_string(receipt->status));
  json_object_object_add(root, "control", json_object_new_string(control));
  json_object_object_add(root, "originalValue",
                         json_object_new_int((int)receipt->original));
  json_object_object_add(root, "requestedValue",
                         json_object_new_int((int)receipt->requested));
  json_object_object_add(root, "observedValue",
                         json_object_new_int((int)receipt->observed));
  json_object_object_add(root, "changed",
                         json_object_new_boolean(receipt->changed));
  json_object_object_add(root, "rollbackAttempted",
                         json_object_new_boolean(receipt->rollback_attempted));
  if (receipt->rollback_succeeded_present)
    json_object_object_add(
        root, "rollbackSucceeded",
        json_object_new_boolean(receipt->rollback_succeeded));
  else
    json_object_object_add(root, "rollbackSucceeded", NULL);
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("provider-profile-model"));
  json_object_object_add(root, "hardwareReadback", json_object_new_boolean(0));
  json_object_object_add(root, "hardwareExactRollback",
                         json_object_new_boolean(0));
  json_object_object_add(root, "playbackStarted", json_object_new_boolean(0));
  json_object_object_add(root, "captureStarted", json_object_new_boolean(0));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

int settings_audio_goxlr_control_command(int argc, char **argv) {
  if (argc < 2 || (strcmp(argv[1], "plan-goxlr-control") != 0 &&
                   strcmp(argv[1], "set-goxlr-control") != 0))
    return 2;
  int apply = strcmp(argv[1], "set-goxlr-control") == 0;
  const char *control = NULL;
  const char *value_text = NULL;
  const char *original_text = NULL;
  const char *cohort = NULL;
  const char *ack = NULL;
  const char *format = "text";
  unsigned int fields = 0U;
  for (int index = 2; index < argc; index++) {
    unsigned int bit = 0U;
    const char **target = NULL;
    if (strcmp(argv[index], "--control") == 0) {
      bit = 1U;
      target = &control;
    } else if (strcmp(argv[index], "--value") == 0) {
      bit = 2U;
      target = &value_text;
    } else if (strcmp(argv[index], "--original") == 0) {
      bit = 4U;
      target = &original_text;
    } else if (strcmp(argv[index], "--cohort") == 0) {
      bit = 8U;
      target = &cohort;
    } else if (strcmp(argv[index], "--ack") == 0) {
      bit = 16U;
      target = &ack;
    } else if (strcmp(argv[index], "--format") == 0) {
      bit = 32U;
      target = &format;
    } else {
      return 2;
    }
    if ((fields & bit) != 0U || index + 1 >= argc)
      return 2;
    fields |= bit;
    *target = argv[++index];
  }
  if ((!apply && fields != (1U | 2U) &&
       fields != (1U | 2U | 32U)) ||
      (apply && fields != (1U | 2U | 4U | 8U | 16U) && fields != 63U) ||
      (strcmp(format, "json") != 0 && strcmp(format, "text") != 0) ||
      !control || audio_goxlr_control_index(control) < 0)
    return 2;
  int control_index = audio_goxlr_control_index(control);
  unsigned int maximum = control_index >= 4 && control_index <= 8 ? 1U : 255U;
  unsigned int requested = 0U;
  unsigned int original = 0U;
  if (!audio_goxlr_canonical_unsigned(value_text, maximum, &requested) ||
      (apply &&
       (!audio_goxlr_canonical_unsigned(original_text, maximum, &original) ||
        !audio_goxlr_cohort(cohort) ||
        strcmp(ack, AUDIO_GOXLR_SETTINGS_ACK) != 0)))
    return 2;

  const char *binary = audio_goxlr_binary();
  if (!*binary || access(binary, X_OK) != 0) {
    fputs("Error: goxlr-adapter-unavailable.\n", stderr);
    return 1;
  }
  if (!apply) {
    char *const child_argv[] = {(char *)binary,
                                "plan-popup-control",
                                "--control",
                                (char *)control,
                                "--value",
                                (char *)value_text,
                                "--format",
                                "json",
                                NULL};
    audio_goxlr_capture capture = audio_goxlr_capture_command(
        binary, child_argv, AUDIO_GOXLR_STATUS_TIMEOUT_MS);
    audio_goxlr_control_plan plan;
    int valid = capture.data && !capture.failed && !capture.launch_failed &&
                !capture.too_large && !capture.timed_out &&
                capture.status == 0 &&
                audio_goxlr_parse_provider_plan(capture.data, capture.size,
                                                control, requested, &plan);
    audio_goxlr_capture_free(&capture);
    if (!valid) {
      fputs("Error: goxlr-control-plan-failed.\n", stderr);
      return 1;
    }
    audio_goxlr_print_plan(control, &plan, format);
    return 0;
  }

  char *const child_argv[] = {
      (char *)binary, "apply-popup-control", "--control", (char *)control,
      "--value",      (char *)value_text,     "--cohort", (char *)cohort,
      "--ack",        AUDIO_GOXLR_PROVIDER_ACK, "--format", "json", NULL};
  audio_goxlr_capture capture = audio_goxlr_capture_command(
      binary, child_argv, AUDIO_GOXLR_APPLY_TIMEOUT_MS);
  audio_goxlr_control_receipt receipt;
  int valid = capture.data && !capture.failed && !capture.launch_failed &&
              !capture.too_large && !capture.timed_out &&
              (capture.status == 0 || capture.status == 1) &&
              audio_goxlr_parse_provider_receipt(
                  capture.data, capture.size, control, original, requested,
                  &receipt);
  int provider_status = capture.status;
  audio_goxlr_capture_free(&capture);
  if (!valid) {
    fputs("Error: goxlr-control-apply-failed.\n", stderr);
    return 1;
  }
  int success = strcmp(receipt.status, "Applied") == 0 ||
                strcmp(receipt.status, "AlreadyApplied") == 0;
  if ((success && provider_status != 0) || (!success && provider_status != 1)) {
    fputs("Error: goxlr-control-contract-invalid.\n", stderr);
    return 1;
  }
  audio_goxlr_print_receipt(control, &receipt, format);
  return success ? 0 : 1;
}
