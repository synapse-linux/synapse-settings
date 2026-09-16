// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "audio_private.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  AUDIO_CONTROL_VOLUME = 0,
  AUDIO_CONTROL_MUTE = 1
} audio_control_kind;

typedef struct {
  char status[16];
  char reason[48];
  char target[32];
  char target_type[16];
  const char *control;
  int original_value;
  int requested_value;
  int changed;
  int mutation_attempted;
  int verified;
  int rollback_attempted;
  int rollback_verified;
} audio_control_receipt;

static const char *audio_control_name(audio_control_kind control) {
  return control == AUDIO_CONTROL_VOLUME ? "volume" : "mute";
}

static int audio_control_state_value(const audio_control_state *state,
                                     audio_control_kind control) {
  return control == AUDIO_CONTROL_VOLUME ? state->volume_percent : state->muted;
}

static int audio_control_cohort(const audio_control_state *state,
                                audio_control_kind control, int original_value,
                                int requested_value, char *cohort,
                                size_t cohort_size) {
  if (!state || !cohort || !cohort_size)
    return -1;
  uint64_t hash = UINT64_C(14695981039346656037);
  audio_fnv1a64_field(&hash, "synapse.settings.audio-control-cohort/v1");
  audio_fnv1a64_field(&hash, state->target);
  audio_fnv1a64_field(&hash, state->target_type);
  audio_fnv1a64_field(&hash, audio_control_name(control));
  char number[32];
  int written = 0;
  if (state->stream_target) {
    audio_fnv1a64_field(&hash, state->stream.executable);
    written = snprintf(number, sizeof(number), "%ld",
                       (long)state->stream.process_pid);
    if (written < 0 || (size_t)written >= sizeof(number))
      return -1;
    audio_fnv1a64_field(&hash, number);
    written = snprintf(number, sizeof(number), "%" PRIu64,
                       state->stream.process_start_time);
    if (written < 0 || (size_t)written >= sizeof(number))
      return -1;
    audio_fnv1a64_field(&hash, number);
  } else {
    audio_fnv1a64_field(&hash, state->raw_name);
  }
  written = snprintf(number, sizeof(number), "%d", state->backend_index);
  if (written < 0 || (size_t)written >= sizeof(number))
    return -1;
  audio_fnv1a64_field(&hash, number);
  written = snprintf(number, sizeof(number), "%d", original_value);
  if (written < 0 || (size_t)written >= sizeof(number))
    return -1;
  audio_fnv1a64_field(&hash, number);
  written = snprintf(number, sizeof(number), "%d", requested_value);
  if (written < 0 || (size_t)written >= sizeof(number))
    return -1;
  audio_fnv1a64_field(&hash, number);
  written = snprintf(cohort, cohort_size, "control-%016" PRIx64, hash);
  if (written < 0 || (size_t)written >= cohort_size)
    return -1;
  return 0;
}

static int audio_control_cohort_shape(const char *cohort) {
  if (!cohort || strncmp(cohort, "control-", 8U) != 0 ||
      strlen(cohort + 8U) != 16U)
    return 0;
  for (const char *cursor = cohort + 8U; *cursor; cursor++)
    if (!((*cursor >= '0' && *cursor <= '9') ||
          (*cursor >= 'a' && *cursor <= 'f')))
      return 0;
  return 1;
}

static int parse_audio_percent(const char *text, int maximum, int *value) {
  if (!text || !*text || !value || (text[0] == '0' && text[1]))
    return -1;
  for (const char *cursor = text; *cursor; cursor++)
    if (*cursor < '0' || *cursor > '9')
      return -1;
  errno = 0;
  char *end = NULL;
  long parsed = strtol(text, &end, 10);
  if (errno != 0 || !end || *end || parsed < 0 || parsed > maximum)
    return -1;
  *value = (int)parsed;
  return 0;
}

static int parse_audio_boolean(const char *text, int *value) {
  if (!text || !value)
    return -1;
  if (strcmp(text, "true") == 0)
    *value = 1;
  else if (strcmp(text, "false") == 0)
    *value = 0;
  else
    return -1;
  return 0;
}

static int parse_audio_control_options(int argc, char **argv, int apply,
                                       audio_control_kind control,
                                       const char **target,
                                       const char **original_value,
                                       const char **requested_value,
                                       const char **cohort, const char **ack,
                                       const char **format) {
  *target = NULL;
  *original_value = NULL;
  *requested_value = NULL;
  *cohort = NULL;
  *ack = NULL;
  *format = "text";
  int seen_format = 0;
  const char *requested_flag =
      control == AUDIO_CONTROL_VOLUME ? "--percent" : "--muted";
  const char *original_flag =
      control == AUDIO_CONTROL_VOLUME ? "--from-percent" : "--from-muted";
  for (int index = 2; index < argc; index++) {
    const char **destination = NULL;
    if (strcmp(argv[index], "--target") == 0)
      destination = target;
    else if (strcmp(argv[index], requested_flag) == 0)
      destination = requested_value;
    else if (strcmp(argv[index], original_flag) == 0)
      destination = original_value;
    else if (strcmp(argv[index], "--cohort") == 0)
      destination = cohort;
    else if (strcmp(argv[index], "--ack") == 0)
      destination = ack;
    else if (strcmp(argv[index], "--format") == 0 && !seen_format) {
      if (++index >= argc)
        return -1;
      *format = argv[index];
      seen_format = 1;
      continue;
    } else if (strcmp(argv[index], "--json") == 0 && !seen_format) {
      *format = "json";
      seen_format = 1;
      continue;
    } else {
      return -1;
    }
    if (++index >= argc || *destination)
      return -1;
    *destination = argv[index];
  }
  if (!*target || !*requested_value ||
      (strcmp(*format, "text") != 0 && strcmp(*format, "json") != 0))
    return -1;
  if (!apply && (*original_value || *cohort || *ack))
    return -1;
  if (apply && (!*original_value || !*cohort || !*ack))
    return -1;
  return 0;
}

static void add_audio_control_value(json_object *root, const char *key,
                                    audio_control_kind control, int value) {
  json_object_object_add(root, key,
                         control == AUDIO_CONTROL_VOLUME
                             ? json_object_new_int(value)
                             : json_object_new_boolean(value));
}

static void print_audio_control_plan(const audio_control_state *state,
                                     audio_control_kind control,
                                     int requested_value, const char *cohort,
                                     int json) {
  int original_value = audio_control_state_value(state, control);
  int changed = original_value != requested_value;
  if (!json) {
    if (control == AUDIO_CONTROL_VOLUME)
      printf("Plan %s volume: %d%% -> %d%%%s\n", state->target, original_value,
             requested_value, changed ? "" : " already set");
    else
      printf("Plan %s mute: %s -> %s%s\n", state->target,
             original_value ? "true" : "false",
             requested_value ? "true" : "false", changed ? "" : " already set");
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-control-plan/v1"));
  json_object_object_add(root, "status", json_object_new_string("Planned"));
  json_object_object_add(root, "target", json_object_new_string(state->target));
  json_object_object_add(root, "targetType",
                         json_object_new_string(state->target_type));
  json_object_object_add(root, "control",
                         json_object_new_string(audio_control_name(control)));
  add_audio_control_value(root, "originalValue", control, original_value);
  add_audio_control_value(root, "requestedValue", control, requested_value);
  json_object_object_add(root, "cohort", json_object_new_string(cohort));
  json_object_object_add(root, "changed", json_object_new_boolean(changed));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "requiresAcknowledgement",
                         json_object_new_string(AUDIO_CONTROL_ACK));
  json_object_object_add(root, "singleTarget", json_object_new_boolean(1));
  json_object_object_add(root, "safeVolumeMaximumPercent",
                         json_object_new_int(AUDIO_SAFE_VOLUME_MAXIMUM));
  json_object_object_add(root, "postflightRequired",
                         json_object_new_boolean(1));
  json_object_object_add(root, "rollbackOnUnverified",
                         json_object_new_boolean(1));
  json_object_object_add(root, "playbackStarted", json_object_new_boolean(0));
  json_object_object_add(root, "captureStarted", json_object_new_boolean(0));
  json_object_object_add(root, "profileChanged", json_object_new_boolean(0));
  json_object_object_add(root, "routingChanged", json_object_new_boolean(0));
  json_object_object_add(root, "applied", json_object_new_boolean(0));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static int initialize_audio_control_receipt(audio_control_receipt *receipt,
                                            const char *target,
                                            audio_control_kind control,
                                            int original_value,
                                            int requested_value) {
  memset(receipt, 0, sizeof(*receipt));
  int stream_target = 0;
  if (audio_copy_bounded(receipt->target, sizeof(receipt->target), target) !=
          0 ||
      audio_control_target_shape(target, receipt->target_type,
                                 sizeof(receipt->target_type),
                                 &stream_target) != 0)
    return -1;
  (void)stream_target;
  receipt->control = audio_control_name(control);
  receipt->original_value = original_value;
  receipt->requested_value = requested_value;
  return 0;
}

static void audio_control_receipt_status(audio_control_receipt *receipt,
                                         const char *status,
                                         const char *reason) {
  (void)audio_copy_bounded(receipt->status, sizeof(receipt->status), status);
  (void)audio_copy_bounded(receipt->reason, sizeof(receipt->reason),
                           reason ? reason : "");
}

static void print_audio_control_receipt(const audio_control_receipt *receipt,
                                        audio_control_kind control, int json) {
  if (!json) {
    printf("Audio %s %s: %s", receipt->control, receipt->target,
           receipt->status);
    if (receipt->reason[0])
      printf(" (%s)", receipt->reason);
    fputc('\n', stdout);
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-control-receipt/v1"));
  json_object_object_add(root, "status",
                         json_object_new_string(receipt->status));
  if (receipt->reason[0])
    json_object_object_add(root, "reason",
                           json_object_new_string(receipt->reason));
  else
    json_object_object_add(root, "reason", NULL);
  json_object_object_add(root, "target",
                         json_object_new_string(receipt->target));
  json_object_object_add(root, "targetType",
                         json_object_new_string(receipt->target_type));
  json_object_object_add(root, "control",
                         json_object_new_string(receipt->control));
  add_audio_control_value(root, "originalValue", control,
                          receipt->original_value);
  add_audio_control_value(root, "requestedValue", control,
                          receipt->requested_value);
  json_object_object_add(root, "changed",
                         json_object_new_boolean(receipt->changed));
  json_object_object_add(root, "mutationAttempted",
                         json_object_new_boolean(receipt->mutation_attempted));
  json_object_object_add(root, "verified",
                         json_object_new_boolean(receipt->verified));
  json_object_object_add(root, "rollbackAttempted",
                         json_object_new_boolean(receipt->rollback_attempted));
  json_object_object_add(root, "rollbackVerified",
                         json_object_new_boolean(receipt->rollback_verified));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "requiresAcknowledgement",
                         json_object_new_string(AUDIO_CONTROL_ACK));
  json_object_object_add(root, "singleTarget", json_object_new_boolean(1));
  json_object_object_add(root, "safeVolumeMaximumPercent",
                         json_object_new_int(AUDIO_SAFE_VOLUME_MAXIMUM));
  json_object_object_add(root, "playbackStarted", json_object_new_boolean(0));
  json_object_object_add(root, "captureStarted", json_object_new_boolean(0));
  json_object_object_add(root, "profileChanged", json_object_new_boolean(0));
  json_object_object_add(root, "routingChanged", json_object_new_boolean(0));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static int execute_audio_control(const audio_control_state *state,
                                 audio_control_kind control, int value,
                                 int *timed_out) {
  const char *operation = NULL;
  if (strcmp(state->target_type, "output") == 0)
    operation =
        control == AUDIO_CONTROL_VOLUME ? "set-sink-volume" : "set-sink-mute";
  else if (strcmp(state->target_type, "input") == 0)
    operation = control == AUDIO_CONTROL_VOLUME ? "set-source-volume"
                                                : "set-source-mute";
  else if (strcmp(state->target_type, "playback") == 0)
    operation = control == AUDIO_CONTROL_VOLUME ? "set-sink-input-volume"
                                                : "set-sink-input-mute";
  else
    operation = control == AUDIO_CONTROL_VOLUME ? "set-source-output-volume"
                                                : "set-source-output-mute";

  char index[24];
  const char *identity = state->raw_name;
  if (state->stream_target) {
    int written = snprintf(index, sizeof(index), "%d", state->backend_index);
    if (written < 0 || (size_t)written >= sizeof(index))
      return -1;
    identity = index;
  }
  char requested[24];
  int written = control == AUDIO_CONTROL_VOLUME
                    ? snprintf(requested, sizeof(requested), "%d%%", value)
                    : snprintf(requested, sizeof(requested), "%d", value);
  if (written < 0 || (size_t)written >= sizeof(requested))
    return -1;
  const char *pactl = audio_pactl_binary();
  char *argv[5] = {(char *)pactl, (char *)operation, (char *)identity,
                   requested, NULL};
  audio_capture_result result = audio_capture_command(argv);
  *timed_out = result.timed_out;
  int status = result.data && result.status == 0 ? 0 : -1;
  audio_capture_free(&result);
  return status;
}

static void apply_audio_control(const char *target, audio_control_kind control,
                                int original_value, int requested_value,
                                const char *expected_cohort,
                                audio_control_receipt *receipt) {
  audio_control_state first;
  const char *reason = NULL;
  int loaded = audio_load_control_state(target, &first, &reason);
  if (loaded != 0) {
    audio_control_receipt_status(receipt, "Refused",
                                 loaded == 2   ? "process-unavailable"
                                 : loaded == 1 ? "target-vanished"
                                               : "audio-unavailable");
    return;
  }
  if (audio_control_state_value(&first, control) != original_value) {
    audio_control_receipt_status(receipt, "Refused", "original-value-mismatch");
    return;
  }
  char first_cohort[32];
  if (audio_control_cohort(&first, control, original_value, requested_value,
                           first_cohort, sizeof(first_cohort)) != 0 ||
      strcmp(first_cohort, expected_cohort) != 0) {
    audio_control_receipt_status(receipt, "Refused", "control-cohort-changed");
    return;
  }

  audio_control_state planned;
  loaded = audio_load_control_state(target, &planned, &reason);
  if (loaded != 0) {
    audio_control_receipt_status(receipt, "Refused",
                                 loaded == 2   ? "process-unavailable"
                                 : loaded == 1 ? "target-vanished"
                                               : "audio-unavailable");
    return;
  }
  char planned_cohort[32];
  if (!audio_control_identity_equal(&first, &planned) ||
      audio_control_state_value(&planned, control) != original_value ||
      audio_control_cohort(&planned, control, original_value, requested_value,
                           planned_cohort, sizeof(planned_cohort)) != 0 ||
      strcmp(planned_cohort, expected_cohort) != 0) {
    audio_control_receipt_status(receipt, "Refused", "control-cohort-changed");
    return;
  }
  if (original_value == requested_value) {
    receipt->verified = 1;
    audio_control_receipt_status(receipt, "AlreadySet", NULL);
    return;
  }

  receipt->mutation_attempted = 1;
  int mutation_timed_out = 0;
  int mutation_status = execute_audio_control(
      &planned, control, requested_value, &mutation_timed_out);
  audio_control_state after;
  loaded = audio_load_control_state(target, &after, &reason);
  if (loaded != 0) {
    audio_control_receipt_status(receipt, "Failed",
                                 loaded == 1   ? "target-vanished"
                                 : loaded == 2 ? "target-identity-changed"
                                               : "verification-unavailable");
    return;
  }
  if (!audio_control_identity_equal(&planned, &after)) {
    audio_control_receipt_status(receipt, "Failed", "target-identity-changed");
    return;
  }
  int observed = audio_control_state_value(&after, control);
  if (mutation_status == 0 && observed == requested_value) {
    receipt->changed = 1;
    receipt->verified = 1;
    audio_control_receipt_status(receipt, "Applied", NULL);
    return;
  }

  const char *failure_reason = mutation_timed_out     ? "mutation-timeout"
                               : mutation_status != 0 ? "mutation-failed"
                                                      : "verification-failed";
  if (observed != original_value) {
    audio_control_state rollback_state;
    const char *rollback_reason = NULL;
    loaded =
        audio_load_control_state(target, &rollback_state, &rollback_reason);
    if (loaded != 0) {
      failure_reason = loaded == 1   ? "target-vanished"
                       : loaded == 2 ? "target-identity-changed"
                                     : "verification-unavailable";
    } else if (!audio_control_identity_equal(&planned, &rollback_state)) {
      failure_reason = "target-identity-changed";
    } else if (audio_control_state_value(&rollback_state, control) ==
               original_value) {
      /* The original value was restored externally; do not mutate again. */
    } else if (audio_control_state_value(&rollback_state, control) !=
               observed) {
      failure_reason = "verification-failed";
    } else {
      receipt->rollback_attempted = 1;
      int rollback_timed_out = 0;
      (void)execute_audio_control(&rollback_state, control, original_value,
                                  &rollback_timed_out);
      (void)rollback_timed_out;
      audio_control_state restored;
      const char *restore_reason = NULL;
      if (audio_load_control_state(target, &restored, &restore_reason) == 0 &&
          audio_control_identity_equal(&planned, &restored) &&
          audio_control_state_value(&restored, control) == original_value) {
        receipt->rollback_verified = 1;
      } else {
        failure_reason = "rollback-failed";
      }
    }
  }
  audio_control_receipt_status(receipt, "Failed", failure_reason);
}

int settings_audio_control_command(int argc, char **argv) {
  int apply =
      strcmp(argv[1], "set-volume") == 0 || strcmp(argv[1], "set-mute") == 0;
  audio_control_kind control =
      strcmp(argv[1], "plan-volume") == 0 || strcmp(argv[1], "set-volume") == 0
          ? AUDIO_CONTROL_VOLUME
          : AUDIO_CONTROL_MUTE;
  const char *target = NULL;
  const char *original_text = NULL;
  const char *requested_text = NULL;
  const char *cohort = NULL;
  const char *ack = NULL;
  const char *format = NULL;
  if (parse_audio_control_options(argc, argv, apply, control, &target,
                                  &original_text, &requested_text, &cohort,
                                  &ack, &format) != 0) {
    audio_usage(stderr);
    return 2;
  }
  char target_type[16];
  int stream_target = 0;
  if (audio_control_target_shape(target, target_type, sizeof(target_type),
                                 &stream_target) != 0) {
    fputs("synapse-settings: invalid Audio control target\n", stderr);
    return 2;
  }
  (void)target_type;
  (void)stream_target;
  int original_value = 0;
  int requested_value = 0;
  int parsed =
      control == AUDIO_CONTROL_VOLUME
          ? parse_audio_percent(requested_text, AUDIO_SAFE_VOLUME_MAXIMUM,
                                &requested_value)
          : parse_audio_boolean(requested_text, &requested_value);
  if (parsed != 0 ||
      (apply &&
       (control == AUDIO_CONTROL_VOLUME
            ? parse_audio_percent(original_text, 999, &original_value)
            : parse_audio_boolean(original_text, &original_value)) != 0)) {
    fputs("synapse-settings: invalid Audio control value\n", stderr);
    return 2;
  }
  if (apply && (strcmp(ack, AUDIO_CONTROL_ACK) != 0 ||
                !audio_control_cohort_shape(cohort))) {
    fputs("synapse-settings: exact Audio control acknowledgement and cohort "
          "required\n",
          stderr);
    return 2;
  }
  int json = strcmp(format, "json") == 0;
  if (!apply) {
    audio_control_state state;
    const char *reason = NULL;
    int loaded = audio_load_control_state(target, &state, &reason);
    if (loaded != 0) {
      fprintf(stderr,
              "synapse-settings: Audio control target unavailable: %s\n",
              loaded == 2   ? "process-unavailable"
              : loaded == 1 ? "target-vanished"
                            : "audio-unavailable");
      return 1;
    }
    original_value = audio_control_state_value(&state, control);
    char planned_cohort[32];
    if (audio_control_cohort(&state, control, original_value, requested_value,
                             planned_cohort, sizeof(planned_cohort)) != 0) {
      fputs("synapse-settings: cannot bind Audio control plan\n", stderr);
      return 1;
    }
    print_audio_control_plan(&state, control, requested_value, planned_cohort,
                             json);
    return 0;
  }

  audio_control_receipt receipt;
  if (initialize_audio_control_receipt(&receipt, target, control,
                                       original_value, requested_value) != 0) {
    fputs("synapse-settings: invalid Audio control request\n", stderr);
    return 2;
  }
  apply_audio_control(target, control, original_value, requested_value, cohort,
                      &receipt);
  print_audio_control_receipt(&receipt, control, json);
  return strcmp(receipt.status, "Applied") == 0 ||
                 strcmp(receipt.status, "AlreadySet") == 0
             ? 0
             : 1;
}
