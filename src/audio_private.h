// SPDX-License-Identifier: MIT
#ifndef SYNAPSE_SETTINGS_AUDIO_PRIVATE_H
#define SYNAPSE_SETTINGS_AUDIO_PRIVATE_H

// Private C composition, not an installed API or a Qt/presentation contract.
#include "settings_internal.h"
#include <json-c/json.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define AUDIO_ENDPOINT_LIMIT 64U
#define AUDIO_CARD_LIMIT 32U
#define AUDIO_LABEL_LIMIT 255U
#define AUDIO_CONTROL_ACK "synapse-settings/audio-control/v1"
#define AUDIO_SAFE_VOLUME_MAXIMUM 100

// Capture buffer ownership transfers to the caller; release with
// audio_capture_free. Raw capture/exit status alone is not a verified
// transaction outcome.
typedef struct {
  char *data;
  size_t length;
  int status;
  int timed_out;
  int too_large;
  int failed;
} audio_capture_result;

// Value snapshots only. Inventory arrays and process inspection stay in
// audio.c. Raw endpoint/process fields are private transaction/cohort inputs,
// never GUI data.
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
  int volume_percent;
  int muted;
  int requested_available;
} audio_broker_stream_state;

typedef struct {
  char target[32];
  char target_type[16];
  char raw_name[SETTINGS_FIELD_LIMIT + 1U];
  int backend_index;
  int volume_percent;
  int muted;
  int stream_target;
  audio_broker_stream_state stream;
} audio_control_state;

// Existing bounded primitives: original preconditions and return values apply.
// JSON objects returned by audio_pactl_json are owned references
// (json_object_put).
const char *audio_pactl_binary(void);
void audio_usage(FILE *out);
int audio_copy_bounded(char *target, size_t size, const char *value);
int audio_raw_identity_valid(const char *value);
const char *audio_json_bounded_string(json_object *object, const char *key,
                                      int allow_missing, size_t maximum,
                                      int *valid);
const char *audio_json_optional_bounded_string(json_object *object,
                                               const char *key, size_t maximum,
                                               int *valid);
int audio_json_index(json_object *object, const char *key, int *index);
int audio_source_is_monitor(json_object *object, int *is_monitor);
int audio_copy_label(char *target, size_t size, const char *value);
void audio_capture_free(audio_capture_result *result);
audio_capture_result audio_capture_command(char *const argv[]);
json_object *audio_pactl_json(const char *pactl, const char *first,
                              const char *second, const char *third,
                              const char **reason);
void audio_fnv1a64_field(uint64_t *hash, const char *text);
uint64_t audio_fnv1a64(const char *text);
int audio_profile_token_for_raw(const char *card_raw, const char *profile_raw,
                                char *token, size_t token_size);
int audio_parse_format(int argc, char **argv, int start, const char **format);
int audio_endpoint_token_shape(const char *direction, const char *device);

// Snapshot acquisition does not authorize a write. Result: 0 loaded, 1
// vanished, 2 unprovable process identity, -1 unavailable/invalid; reason is
// borrowed.
int audio_control_target_shape(const char *target, char *target_type,
                               size_t target_type_size, int *stream_target);
int audio_load_control_state(const char *target, audio_control_state *state,
                             const char **reason);
int audio_control_identity_equal(const audio_control_state *left,
                                 const audio_control_state *right);

// Called only after audio command dispatch has selected this family.
int settings_audio_control_command(int argc, char **argv);
#ifdef SYNAPSE_SETTINGS_WITH_PROFILE_PORT
int settings_audio_profile_port_inventory_command(int argc, char **argv);
int settings_audio_selection_command(int argc, char **argv);
#endif

#endif
