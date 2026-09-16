// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "audio_private.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SYNAPSE_SETTINGS_WITH_PROFILE_PORT
#error "Profile/port unit requires SYNAPSE_SETTINGS_WITH_PROFILE_PORT"
#endif

#define AUDIO_SELECTION_ACK "synapse-settings/audio-profile-port/v1"
#define AUDIO_SELECTION_RAW_LIMIT 255U
#define AUDIO_SELECTION_PER_TARGET_LIMIT 64U
#define AUDIO_PROFILE_OPTION_LIMIT 512U
#define AUDIO_PORT_OPTION_LIMIT 512U
#define AUDIO_SELECTION_COHORT_PREFIX "selection-"

typedef enum {
  AUDIO_SELECTION_PROFILE = 0,
  AUDIO_SELECTION_PORT = 1
} audio_selection_kind;

typedef enum {
  AUDIO_SELECTION_AVAILABLE = 0,
  AUDIO_SELECTION_UNKNOWN = 1,
  AUDIO_SELECTION_UNAVAILABLE = 2
} audio_selection_availability;

typedef struct {
  char raw_name[AUDIO_SELECTION_RAW_LIMIT + 1U];
  char id[32];
  char label[AUDIO_LABEL_LIMIT + 1U];
  audio_selection_availability availability;
} audio_selection_option;

typedef struct {
  audio_selection_kind kind;
  char target[32];
  char target_type[8];
  char raw_name[AUDIO_SELECTION_RAW_LIMIT + 1U];
  char label[AUDIO_LABEL_LIMIT + 1U];
  int backend_index;
  char active_selection[32];
  char active_raw[AUDIO_SELECTION_RAW_LIMIT + 1U];
  char active_label[AUDIO_LABEL_LIMIT + 1U];
  int active_found;
  size_t option_offset;
  size_t option_count;
} audio_selection_target;

typedef struct {
  int available;
  const char *reason;
  audio_selection_target cards[AUDIO_CARD_LIMIT];
  size_t card_count;
  audio_selection_target outputs[AUDIO_ENDPOINT_LIMIT];
  size_t output_count;
  audio_selection_target inputs[AUDIO_ENDPOINT_LIMIT];
  size_t input_count;
  audio_selection_option profiles[AUDIO_PROFILE_OPTION_LIMIT];
  size_t profile_count;
  audio_selection_option ports[AUDIO_PORT_OPTION_LIMIT];
  size_t port_count;
} audio_profile_port_inventory;

typedef struct {
  audio_selection_kind kind;
  char target[32];
  char target_type[8];
  char raw_name[AUDIO_SELECTION_RAW_LIMIT + 1U];
  char label[AUDIO_LABEL_LIMIT + 1U];
  int backend_index;
  audio_selection_option original;
  audio_selection_option requested;
  int requested_found;
} audio_selection_state;

typedef struct {
  char status[16];
  char reason[48];
  char target[32];
  char target_type[8];
  const char *selection;
  char original_selection[32];
  char requested_selection[32];
  int changed;
  int mutation_attempted;
  int verified;
  int rollback_attempted;
  int rollback_verified;
} audio_selection_receipt;

static const char *audio_selection_name(audio_selection_kind kind) {
  return kind == AUDIO_SELECTION_PROFILE ? "profile" : "port";
}

static const char *
audio_selection_availability_name(audio_selection_availability availability) {
  switch (availability) {
  case AUDIO_SELECTION_AVAILABLE:
    return "available";
  case AUDIO_SELECTION_UNKNOWN:
    return "unknown";
  case AUDIO_SELECTION_UNAVAILABLE:
    return "unavailable";
  }
  return "unavailable";
}

static const char *audio_selection_json_string(json_object *object,
                                               const char *key,
                                               int allow_missing,
                                               size_t maximum, int *valid) {
  return audio_json_bounded_string(object, key, allow_missing, maximum, valid);
}

static int audio_selection_raw_valid(const char *value) {
  return audio_raw_identity_valid(value);
}

static int audio_selection_copy_raw(char *target, size_t target_size,
                                    const char *value) {
  if (!audio_selection_raw_valid(value) || !target || target_size == 0U ||
      strlen(value) >= target_size) {
    errno = EINVAL;
    return -1;
  }
  memcpy(target, value, strlen(value) + 1U);
  return 0;
}

static int audio_selection_index(json_object *object, int *index) {
  return audio_json_index(object, "index", index);
}

static int audio_selection_copy_label_from_object(json_object *object,
                                                  const char *fallback,
                                                  char *target,
                                                  size_t target_size) {
  int valid = 0;
  const char *description = audio_json_optional_bounded_string(
      object, "description", AUDIO_LABEL_LIMIT, &valid);
  if (!valid)
    return -1;
  return audio_copy_label(target, target_size,
                          description ? description : fallback);
}

static int audio_selection_option_token(audio_selection_kind kind,
                                        const char *direction,
                                        const char *owner_raw,
                                        const char *option_raw, char *token,
                                        size_t token_size) {
  const char *token_raw = option_raw;
#ifdef SYNAPSE_SETTINGS_TEST_HOOKS
  char mapped_raw[AUDIO_SELECTION_RAW_LIMIT + 1U];
  const char *mapping = getenv("SYNAPSE_AUDIO_SELECTION_TEST_OPTION_ALIAS");
  if (mapping && *mapping) {
    const char *separator = strchr(mapping, '\t');
    if (!separator || separator == mapping || !separator[1] ||
        strchr(separator + 1, '\t'))
      return -1;
    size_t alias_length = (size_t)(separator - mapping);
    if (strlen(option_raw) == alias_length &&
        memcmp(option_raw, mapping, alias_length) == 0) {
      if (!audio_selection_raw_valid(separator + 1) ||
          audio_selection_copy_raw(mapped_raw, sizeof(mapped_raw),
                                   separator + 1) != 0)
        return -1;
      token_raw = mapped_raw;
    }
  }
#endif
  if (kind == AUDIO_SELECTION_PROFILE)
    return audio_profile_token_for_raw(owner_raw, token_raw, token, token_size);
  uint64_t hash = UINT64_C(14695981039346656037);
  audio_fnv1a64_field(&hash, "synapse.settings.audio-port-token/v1");
  audio_fnv1a64_field(&hash, direction);
  audio_fnv1a64_field(&hash, owner_raw);
  audio_fnv1a64_field(&hash, token_raw);
  int written = snprintf(token, token_size, "port-%016" PRIx64, hash);
  if (written < 0 || (size_t)written >= token_size)
    return -1;
  return 0;
}

static int audio_selection_target_token(audio_selection_kind kind,
                                        const char *direction,
                                        const char *raw_name, char *token,
                                        size_t token_size) {
  const char *prefix = kind == AUDIO_SELECTION_PROFILE ? "card" : direction;
  if (!prefix)
    return -1;
  int written = snprintf(token, token_size, "%s-%016" PRIx64, prefix,
                         audio_fnv1a64(raw_name));
  return written < 0 || (size_t)written >= token_size ? -1 : 0;
}

static int
audio_selection_parse_availability(audio_selection_kind kind,
                                   json_object *object,
                                   audio_selection_availability *availability) {
  json_object *value = NULL;
  const char *key =
      kind == AUDIO_SELECTION_PROFILE ? "available" : "availability";
  if (!json_object_object_get_ex(object, key, &value)) {
    *availability = AUDIO_SELECTION_UNKNOWN;
    return 0;
  }
  if (kind == AUDIO_SELECTION_PROFILE) {
    if (!json_object_is_type(value, json_type_boolean))
      return -1;
    *availability = json_object_get_boolean(value)
                        ? AUDIO_SELECTION_AVAILABLE
                        : AUDIO_SELECTION_UNAVAILABLE;
    return 0;
  }
  int valid = 0;
  const char *text = audio_json_bounded_string(object, key, 0, 32U, &valid);
  if (!valid)
    return -1;
  if (strcmp(text, "available") == 0)
    *availability = AUDIO_SELECTION_AVAILABLE;
  else if (strcmp(text, "availability unknown") == 0)
    *availability = AUDIO_SELECTION_UNKNOWN;
  else if (strcmp(text, "not available") == 0)
    *availability = AUDIO_SELECTION_UNAVAILABLE;
  else
    return -1;
  return 0;
}

static int audio_selection_option_compare(const void *left, const void *right) {
  const audio_selection_option *a = left;
  const audio_selection_option *b = right;
  return strcmp(a->raw_name, b->raw_name);
}

static int audio_selection_target_compare(const void *left, const void *right) {
  const audio_selection_target *a = left;
  const audio_selection_target *b = right;
  return strcmp(a->raw_name, b->raw_name);
}

static int
audio_selection_option_id_exists(const audio_profile_port_inventory *inventory,
                                 audio_selection_kind kind, const char *id) {
  const audio_selection_option *items =
      kind == AUDIO_SELECTION_PROFILE ? inventory->profiles : inventory->ports;
  size_t count = kind == AUDIO_SELECTION_PROFILE ? inventory->profile_count
                                                 : inventory->port_count;
  /* The caller has reserved the current option at the final pool slot. */
  if (count > 0U)
    count--;
  for (size_t index = 0; index < count; index++)
    if (strcmp(items[index].id, id) == 0)
      return 1;
  return 0;
}

static int audio_selection_append_option(
    audio_selection_kind kind, const char *direction, const char *owner_raw,
    const char *raw_name, json_object *value,
    audio_profile_port_inventory *inventory, audio_selection_target *target) {
  audio_selection_option *pool =
      kind == AUDIO_SELECTION_PROFILE ? inventory->profiles : inventory->ports;
  size_t *pool_count = kind == AUDIO_SELECTION_PROFILE
                           ? &inventory->profile_count
                           : &inventory->port_count;
  const size_t pool_limit = kind == AUDIO_SELECTION_PROFILE
                                ? AUDIO_PROFILE_OPTION_LIMIT
                                : AUDIO_PORT_OPTION_LIMIT;
  if (target->option_count >= AUDIO_SELECTION_PER_TARGET_LIMIT ||
      *pool_count >= pool_limit || !audio_selection_raw_valid(raw_name) ||
      !json_object_is_type(value, json_type_object))
    return -1;
  audio_selection_option *option = &pool[(*pool_count)++];
  memset(option, 0, sizeof(*option));
  if (audio_selection_copy_raw(option->raw_name, sizeof(option->raw_name),
                               raw_name) != 0 ||
      audio_selection_copy_label_from_object(value, "", option->label,
                                             sizeof(option->label)) != 0 ||
      audio_selection_parse_availability(kind, value, &option->availability) !=
          0 ||
      audio_selection_option_token(kind, direction, owner_raw, raw_name,
                                   option->id, sizeof(option->id)) != 0 ||
      audio_selection_option_id_exists(inventory, kind, option->id))
    return -1;
  target->option_count++;
  return 0;
}

static int audio_selection_parse_options(
    json_object *target_object, const char *key, audio_selection_kind kind,
    const char *direction, const char *owner_raw,
    audio_profile_port_inventory *inventory, audio_selection_target *target) {
  json_object *options = NULL;
  target->option_count = 0U;
  target->option_offset = kind == AUDIO_SELECTION_PROFILE
                              ? inventory->profile_count
                              : inventory->port_count;
  if (!json_object_object_get_ex(target_object, key, &options))
    return 0;
  if (kind == AUDIO_SELECTION_PROFILE) {
    if (!json_object_is_type(options, json_type_object))
      return -1;
    json_object_object_foreach(options, raw_name, value) {
      if (audio_selection_append_option(kind, direction, owner_raw, raw_name,
                                        value, inventory, target) != 0)
        return -1;
    }
  } else {
    if (!json_object_is_type(options, json_type_array) ||
        json_object_array_length(options) > AUDIO_SELECTION_PER_TARGET_LIMIT)
      return -1;
    const size_t length = json_object_array_length(options);
    for (size_t index = 0; index < length; index++) {
      json_object *value = json_object_array_get_idx(options, index);
      if (!json_object_is_type(value, json_type_object))
        return -1;
      int valid = 0;
      const char *raw_name = audio_selection_json_string(
          value, "name", 0, AUDIO_SELECTION_RAW_LIMIT, &valid);
      if (!valid ||
          audio_selection_append_option(kind, direction, owner_raw, raw_name,
                                        value, inventory, target) != 0)
        return -1;
    }
  }
  audio_selection_option *pool =
      kind == AUDIO_SELECTION_PROFILE ? inventory->profiles : inventory->ports;
  qsort(pool + target->option_offset, target->option_count, sizeof(pool[0]),
        audio_selection_option_compare);
  for (size_t index = 1; index < target->option_count; index++) {
    const audio_selection_option *previous =
        &pool[target->option_offset + index - 1U];
    const audio_selection_option *current =
        &pool[target->option_offset + index];
    if (strcmp(previous->raw_name, current->raw_name) == 0 ||
        strcmp(previous->id, current->id) == 0)
      return -1;
  }
  return 0;
}

static int audio_selection_source_is_monitor(json_object *object,
                                             int *is_monitor) {
  return audio_source_is_monitor(object, is_monitor);
}

static int
audio_selection_finalize_active(audio_selection_kind kind, json_object *object,
                                audio_profile_port_inventory *inventory,
                                audio_selection_target *target) {
  int valid = 0;
  const char *key =
      kind == AUDIO_SELECTION_PROFILE ? "active_profile" : "active_port";
  const char *active = audio_selection_json_string(
      object, key, 1, AUDIO_SELECTION_RAW_LIMIT, &valid);
  if (!valid)
    return -1;
  if (!active || !*active)
    return 0;
  if (!audio_selection_raw_valid(active))
    return -1;
  audio_selection_option *pool =
      kind == AUDIO_SELECTION_PROFILE ? inventory->profiles : inventory->ports;
  for (size_t index = 0; index < target->option_count; index++) {
    audio_selection_option *option = &pool[target->option_offset + index];
    if (strcmp(option->raw_name, active) != 0)
      continue;
    if (audio_copy_bounded(target->active_selection,
                           sizeof(target->active_selection), option->id) != 0 ||
        audio_selection_copy_raw(target->active_raw, sizeof(target->active_raw),
                                 option->raw_name) != 0 ||
        audio_copy_label(target->active_label, sizeof(target->active_label),
                         option->label) != 0)
      return -1;
    target->active_found = 1;
    return 0;
  }
  return -1;
}

static int audio_selection_parse_target_array(
    json_object *array, audio_selection_kind kind, const char *direction,
    audio_profile_port_inventory *inventory) {
  if (!json_object_is_type(array, json_type_array))
    return -1;
  audio_selection_target *targets = NULL;
  size_t *target_count = NULL;
  size_t target_limit = 0U;
  if (kind == AUDIO_SELECTION_PROFILE) {
    targets = inventory->cards;
    target_count = &inventory->card_count;
    target_limit = AUDIO_CARD_LIMIT;
  } else if (strcmp(direction, "output") == 0) {
    targets = inventory->outputs;
    target_count = &inventory->output_count;
    target_limit = AUDIO_ENDPOINT_LIMIT;
  } else {
    targets = inventory->inputs;
    target_count = &inventory->input_count;
    target_limit = AUDIO_ENDPOINT_LIMIT;
  }
  size_t length = json_object_array_length(array);
  if (length > target_limit || *target_count > target_limit - length)
    return -1;
  int seen_indexes[AUDIO_ENDPOINT_LIMIT];
  char seen_raw_names[AUDIO_ENDPOINT_LIMIT][AUDIO_SELECTION_RAW_LIMIT + 1U];
  char seen_target_tokens[AUDIO_ENDPOINT_LIMIT][32];
  size_t seen_index_count = 0U;
  size_t seen_identity_count = 0U;
  for (size_t array_index = 0; array_index < length; array_index++) {
    json_object *object = json_object_array_get_idx(array, array_index);
    if (!json_object_is_type(object, json_type_object))
      return -1;
    int valid = 0;
    int backend_index = 0;
    const char *raw_name = audio_selection_json_string(
        object, "name", 0, AUDIO_SELECTION_RAW_LIMIT, &valid);
    if (!valid || !audio_selection_raw_valid(raw_name) ||
        audio_selection_index(object, &backend_index) != 0)
      return -1;
    for (size_t previous = 0; previous < seen_index_count; previous++)
      if (seen_indexes[previous] == backend_index)
        return -1;
    seen_indexes[seen_index_count++] = backend_index;
    int is_monitor = 0;
    if (kind == AUDIO_SELECTION_PORT && strcmp(direction, "input") == 0 &&
        audio_selection_source_is_monitor(object, &is_monitor) != 0)
      return -1;
    audio_selection_target parsed_target;
    memset(&parsed_target, 0, sizeof(parsed_target));
    parsed_target.kind = kind;
    parsed_target.backend_index = backend_index;
    if (audio_selection_copy_raw(parsed_target.raw_name,
                                 sizeof(parsed_target.raw_name),
                                 raw_name) != 0 ||
        audio_selection_copy_label_from_object(object, "", parsed_target.label,
                                               sizeof(parsed_target.label)) !=
            0 ||
        audio_copy_bounded(
            parsed_target.target_type, sizeof(parsed_target.target_type),
            kind == AUDIO_SELECTION_PROFILE ? "card" : direction) != 0 ||
        audio_selection_target_token(kind, direction, raw_name,
                                     parsed_target.target,
                                     sizeof(parsed_target.target)) != 0 ||
        audio_selection_parse_options(
            object, kind == AUDIO_SELECTION_PROFILE ? "profiles" : "ports",
            kind, direction, raw_name, inventory, &parsed_target) != 0 ||
        audio_selection_finalize_active(kind, object, inventory,
                                        &parsed_target) != 0)
      return -1;
    for (size_t previous = 0; previous < seen_identity_count; previous++)
      if (strcmp(seen_raw_names[previous], parsed_target.raw_name) == 0 ||
          strcmp(seen_target_tokens[previous], parsed_target.target) == 0)
        return -1;
    if (audio_selection_copy_raw(seen_raw_names[seen_identity_count],
                                 sizeof(seen_raw_names[0]),
                                 parsed_target.raw_name) != 0 ||
        audio_copy_bounded(seen_target_tokens[seen_identity_count],
                           sizeof(seen_target_tokens[0]),
                           parsed_target.target) != 0)
      return -1;
    seen_identity_count++;
    if (is_monitor)
      continue;
    targets[(*target_count)++] = parsed_target;
  }
  qsort(targets, *target_count, sizeof(targets[0]),
        audio_selection_target_compare);
  for (size_t index = 0; index < *target_count; index++) {
    for (size_t previous = 0; previous < index; previous++)
      if (strcmp(targets[previous].raw_name, targets[index].raw_name) == 0 ||
          strcmp(targets[previous].target, targets[index].target) == 0 ||
          targets[previous].backend_index == targets[index].backend_index)
        return -1;
  }
  return 0;
}

static int audio_selection_load_json(audio_selection_kind kind,
                                     const char *direction,
                                     audio_profile_port_inventory *inventory,
                                     const char **reason) {
  const char *category = kind == AUDIO_SELECTION_PROFILE    ? "cards"
                         : strcmp(direction, "output") == 0 ? "sinks"
                                                            : "sources";
  json_object *array =
      audio_pactl_json(audio_pactl_binary(), "list", category, NULL, reason);
  if (!array)
    return -1;
  int status =
      audio_selection_parse_target_array(array, kind, direction, inventory);
  json_object_put(array);
  if (status != 0) {
    *reason = "invalid-response";
    return -1;
  }
  return 0;
}

static int
load_audio_profile_port_inventory(audio_profile_port_inventory *inventory) {
  memset(inventory, 0, sizeof(*inventory));
  inventory->reason = "unavailable";
  const char *reason = "unavailable";
  if (audio_selection_load_json(AUDIO_SELECTION_PROFILE, NULL, inventory,
                                &reason) != 0 ||
      audio_selection_load_json(AUDIO_SELECTION_PORT, "output", inventory,
                                &reason) != 0 ||
      audio_selection_load_json(AUDIO_SELECTION_PORT, "input", inventory,
                                &reason) != 0) {
    inventory->available = 0;
    inventory->reason = reason;
    inventory->card_count = 0U;
    inventory->output_count = 0U;
    inventory->input_count = 0U;
    inventory->profile_count = 0U;
    inventory->port_count = 0U;
    return -1;
  }
  inventory->available = 1;
  inventory->reason = "";
  return 0;
}

static int audio_selection_target_mutation_available(
    const audio_selection_target *target,
    const audio_selection_option *options) {
  if (!target->active_found)
    return 0;
  for (size_t index = 0; index < target->option_count; index++)
    if (options[target->option_offset + index].availability !=
        AUDIO_SELECTION_UNAVAILABLE)
      return 1;
  return 0;
}

static json_object *
audio_selection_option_json(const audio_selection_option *option) {
  json_object *value = json_object_new_object();
  json_object_object_add(value, "id", json_object_new_string(option->id));
  json_object_object_add(value, "label", json_object_new_string(option->label));
  json_object_object_add(
      value, "availability",
      json_object_new_string(
          audio_selection_availability_name(option->availability)));
  return value;
}

static json_object *
audio_selection_target_json(const audio_selection_target *target,
                            const audio_selection_option *options) {
  json_object *value = json_object_new_object();
  json_object_object_add(value, "id", json_object_new_string(target->target));
  if (target->kind == AUDIO_SELECTION_PORT)
    json_object_object_add(value, "direction",
                           json_object_new_string(target->target_type));
  json_object_object_add(value, "label", json_object_new_string(target->label));
  const char *active_key =
      target->kind == AUDIO_SELECTION_PROFILE ? "activeProfile" : "activePort";
  const char *active_label_key = target->kind == AUDIO_SELECTION_PROFILE
                                     ? "activeProfileLabel"
                                     : "activePortLabel";
  if (target->active_found) {
    json_object_object_add(value, active_key,
                           json_object_new_string(target->active_selection));
    json_object_object_add(value, active_label_key,
                           json_object_new_string(target->active_label));
  } else {
    json_object_object_add(value, active_key, NULL);
    json_object_object_add(value, active_label_key, NULL);
  }
  json_object_object_add(
      value, "mutationAvailable",
      json_object_new_boolean(
          audio_selection_target_mutation_available(target, options)));
  const char *options_key =
      target->kind == AUDIO_SELECTION_PROFILE ? "profiles" : "ports";
  json_object *array = json_object_new_array_ext((int)target->option_count);
  for (size_t index = 0; index < target->option_count; index++)
    json_object_array_add(array, audio_selection_option_json(
                                     &options[target->option_offset + index]));
  json_object_object_add(value, options_key, array);
  return value;
}

static void print_audio_profile_port_inventory_json(
    const audio_profile_port_inventory *inventory) {
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string(
          "synapse.settings.audio-profile-port-inventory/v1"));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "available",
                         json_object_new_boolean(inventory->available));
  if (inventory->available)
    json_object_object_add(root, "reason", NULL);
  else
    json_object_object_add(root, "reason",
                           json_object_new_string(inventory->reason));
  int mutation_available = 0;
  if (inventory->available) {
    for (size_t index = 0; index < inventory->card_count; index++)
      mutation_available |= audio_selection_target_mutation_available(
          &inventory->cards[index], inventory->profiles);
    for (size_t index = 0; index < inventory->output_count; index++)
      mutation_available |= audio_selection_target_mutation_available(
          &inventory->outputs[index], inventory->ports);
    for (size_t index = 0; index < inventory->input_count; index++)
      mutation_available |= audio_selection_target_mutation_available(
          &inventory->inputs[index], inventory->ports);
  }
  json_object_object_add(root, "mutationAvailable",
                         json_object_new_boolean(mutation_available));
  json_object *cards = json_object_new_array_ext((int)inventory->card_count);
  for (size_t index = 0; index < inventory->card_count; index++)
    json_object_array_add(cards,
                          audio_selection_target_json(&inventory->cards[index],
                                                      inventory->profiles));
  json_object_object_add(root, "cards", cards);
  json_object *endpoints = json_object_new_array_ext(
      (int)(inventory->output_count + inventory->input_count));
  for (size_t index = 0; index < inventory->output_count; index++)
    json_object_array_add(
        endpoints, audio_selection_target_json(&inventory->outputs[index],
                                               inventory->ports));
  for (size_t index = 0; index < inventory->input_count; index++)
    json_object_array_add(endpoints,
                          audio_selection_target_json(&inventory->inputs[index],
                                                      inventory->ports));
  json_object_object_add(root, "endpoints", endpoints);
  json_object_object_add(root, "hardwareReadback", json_object_new_boolean(0));
  json_object_object_add(root, "hardwareExactRollback",
                         json_object_new_boolean(0));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static void print_audio_profile_port_inventory_text(
    const audio_profile_port_inventory *inventory) {
  if (!inventory->available) {
    printf("Audio profiles and ports unavailable: %s\n", inventory->reason);
    return;
  }
  printf("Audio profile targets: %zu\n", inventory->card_count);
  printf("Audio port targets: %zu\n",
         inventory->output_count + inventory->input_count);
}

int settings_audio_profile_port_inventory_command(int argc, char **argv) {
  const char *format = NULL;
  if (audio_parse_format(argc, argv, 2, &format) != 0) {
    audio_usage(stderr);
    return 2;
  }
  audio_profile_port_inventory *inventory = calloc(1U, sizeof(*inventory));
  if (!inventory) {
    fputs("synapse-settings: Audio profile and port inventory unavailable\n",
          stderr);
    return 1;
  }
  (void)load_audio_profile_port_inventory(inventory);
  if (strcmp(format, "json") == 0)
    print_audio_profile_port_inventory_json(inventory);
  else
    print_audio_profile_port_inventory_text(inventory);
  free(inventory);
  return 0;
}

static audio_selection_target *
audio_selection_find_target(audio_profile_port_inventory *inventory,
                            audio_selection_kind kind, const char *target_token,
                            const char *target_type) {
  audio_selection_target *targets = NULL;
  size_t count = 0U;
  if (kind == AUDIO_SELECTION_PROFILE) {
    targets = inventory->cards;
    count = inventory->card_count;
  } else if (strcmp(target_type, "output") == 0) {
    targets = inventory->outputs;
    count = inventory->output_count;
  } else {
    targets = inventory->inputs;
    count = inventory->input_count;
  }
  for (size_t index = 0; index < count; index++)
    if (strcmp(targets[index].target, target_token) == 0)
      return &targets[index];
  return NULL;
}

static audio_selection_option *
audio_selection_find_option(audio_profile_port_inventory *inventory,
                            const audio_selection_target *target,
                            audio_selection_kind kind,
                            const char *option_token) {
  audio_selection_option *options =
      kind == AUDIO_SELECTION_PROFILE ? inventory->profiles : inventory->ports;
  for (size_t index = 0; index < target->option_count; index++) {
    audio_selection_option *option = &options[target->option_offset + index];
    if (strcmp(option->id, option_token) == 0)
      return option;
  }
  return NULL;
}

static int
load_audio_selection_state(audio_selection_kind kind, const char *target_token,
                           const char *target_type, const char *requested_token,
                           audio_selection_state *state, const char **reason) {
  memset(state, 0, sizeof(*state));
  audio_profile_port_inventory *inventory = calloc(1U, sizeof(*inventory));
  if (!inventory) {
    *reason = "audio-unavailable";
    return -1;
  }
  const char *load_reason = "unavailable";
  const char *direction = kind == AUDIO_SELECTION_PROFILE ? NULL : target_type;
  if (audio_selection_load_json(kind, direction, inventory, &load_reason) !=
      0) {
    free(inventory);
    *reason = "audio-unavailable";
    return -1;
  }
  audio_selection_target *target =
      audio_selection_find_target(inventory, kind, target_token, target_type);
  if (!target) {
    free(inventory);
    *reason = "target-vanished";
    return 1;
  }
  if (!target->active_found) {
    free(inventory);
    *reason = "active-selection-unavailable";
    return 2;
  }
  audio_selection_option *active = audio_selection_find_option(
      inventory, target, kind, target->active_selection);
  audio_selection_option *requested =
      audio_selection_find_option(inventory, target, kind, requested_token);
  if (!active ||
      audio_copy_bounded(state->target, sizeof(state->target),
                         target->target) != 0 ||
      audio_copy_bounded(state->target_type, sizeof(state->target_type),
                         target->target_type) != 0 ||
      audio_selection_copy_raw(state->raw_name, sizeof(state->raw_name),
                               target->raw_name) != 0 ||
      audio_copy_label(state->label, sizeof(state->label), target->label) !=
          0) {
    free(inventory);
    *reason = "audio-unavailable";
    return -1;
  }
  state->kind = kind;
  state->backend_index = target->backend_index;
  state->original = *active;
  if (requested) {
    state->requested = *requested;
    state->requested_found = 1;
  }
  free(inventory);
  *reason = NULL;
  return 0;
}

static int audio_selection_identity_equal(const audio_selection_state *left,
                                          const audio_selection_state *right) {
  return left && right && left->kind == right->kind &&
         left->backend_index == right->backend_index &&
         strcmp(left->target, right->target) == 0 &&
         strcmp(left->target_type, right->target_type) == 0 &&
         strcmp(left->raw_name, right->raw_name) == 0;
}

static int
audio_selection_option_identity_equal(const audio_selection_option *left,
                                      const audio_selection_option *right) {
  return left && right && strcmp(left->id, right->id) == 0 &&
         strcmp(left->raw_name, right->raw_name) == 0;
}

static int audio_selection_cohort(const audio_selection_state *state,
                                  char *cohort, size_t cohort_size) {
  if (!state || !state->requested_found || !cohort || !cohort_size)
    return -1;
  uint64_t hash = UINT64_C(14695981039346656037);
  audio_fnv1a64_field(&hash, "synapse.settings.audio-profile-port-cohort/v1");
  audio_fnv1a64_field(&hash, audio_selection_name(state->kind));
  audio_fnv1a64_field(&hash, state->target);
  audio_fnv1a64_field(&hash, state->target_type);
  audio_fnv1a64_field(&hash, state->raw_name);
  char number[32];
  int written = snprintf(number, sizeof(number), "%d", state->backend_index);
  if (written < 0 || (size_t)written >= sizeof(number))
    return -1;
  audio_fnv1a64_field(&hash, number);
  audio_fnv1a64_field(&hash, state->original.id);
  audio_fnv1a64_field(&hash, state->original.raw_name);
  audio_fnv1a64_field(&hash, state->requested.id);
  audio_fnv1a64_field(&hash, state->requested.raw_name);
  audio_fnv1a64_field(
      &hash, audio_selection_availability_name(state->requested.availability));
  written = snprintf(cohort, cohort_size,
                     AUDIO_SELECTION_COHORT_PREFIX "%016" PRIx64, hash);
  return written < 0 || (size_t)written >= cohort_size ? -1 : 0;
}

static int audio_selection_hex_token(const char *value, const char *prefix) {
  if (!value || !prefix)
    return 0;
  size_t prefix_length = strlen(prefix);
  if (strncmp(value, prefix, prefix_length) != 0 ||
      strlen(value + prefix_length) != 16U)
    return 0;
  for (const char *cursor = value + prefix_length; *cursor; cursor++)
    if (!((*cursor >= '0' && *cursor <= '9') ||
          (*cursor >= 'a' && *cursor <= 'f')))
      return 0;
  return 1;
}

static int audio_selection_cohort_shape(const char *cohort) {
  return audio_selection_hex_token(cohort, AUDIO_SELECTION_COHORT_PREFIX);
}

static int audio_selection_target_shape(audio_selection_kind kind,
                                        const char *target_type,
                                        const char *target) {
  if (kind == AUDIO_SELECTION_PROFILE)
    return target_type && strcmp(target_type, "card") == 0 &&
           audio_selection_hex_token(target, "card-");
  return audio_endpoint_token_shape(target_type, target);
}

static int audio_selection_option_shape(audio_selection_kind kind,
                                        const char *selection) {
  return audio_selection_hex_token(
      selection, kind == AUDIO_SELECTION_PROFILE ? "profile-" : "port-");
}

static void print_audio_selection_plan(const audio_selection_state *state,
                                       const char *cohort, int json) {
  int changed = strcmp(state->original.id, state->requested.id) != 0;
  if (!json) {
    printf("Plan Audio %s %s: %s -> %s%s\n", audio_selection_name(state->kind),
           state->target, state->original.label, state->requested.label,
           changed ? "" : " already selected");
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-profile-port-plan/v1"));
  json_object_object_add(root, "status", json_object_new_string("Planned"));
  json_object_object_add(
      root, "selection",
      json_object_new_string(audio_selection_name(state->kind)));
  json_object_object_add(root, "target", json_object_new_string(state->target));
  json_object_object_add(root, "targetType",
                         json_object_new_string(state->target_type));
  json_object_object_add(root, "targetLabel",
                         json_object_new_string(state->label));
  json_object_object_add(root, "originalSelection",
                         json_object_new_string(state->original.id));
  json_object_object_add(root, "originalLabel",
                         json_object_new_string(state->original.label));
  json_object_object_add(root, "requestedSelection",
                         json_object_new_string(state->requested.id));
  json_object_object_add(root, "requestedLabel",
                         json_object_new_string(state->requested.label));
  json_object_object_add(
      root, "requestedAvailability",
      json_object_new_string(
          audio_selection_availability_name(state->requested.availability)));
  json_object_object_add(root, "cohort", json_object_new_string(cohort));
  json_object_object_add(root, "changed", json_object_new_boolean(changed));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "requiresAcknowledgement",
                         json_object_new_string(AUDIO_SELECTION_ACK));
  json_object_object_add(root, "singleTarget", json_object_new_boolean(1));
  json_object_object_add(root, "postflightRequired",
                         json_object_new_boolean(1));
  json_object_object_add(root, "rollbackOnUnverified",
                         json_object_new_boolean(1));
  json_object_object_add(
      root, "graphMayChange",
      json_object_new_boolean(state->kind == AUDIO_SELECTION_PROFILE));
  json_object_object_add(root, "signalPathMayChange",
                         json_object_new_boolean(1));
  json_object_object_add(root, "playbackStarted", json_object_new_boolean(0));
  json_object_object_add(root, "captureStarted", json_object_new_boolean(0));
  json_object_object_add(root, "defaultChanged", json_object_new_boolean(0));
  json_object_object_add(root, "policyChanged", json_object_new_boolean(0));
  json_object_object_add(root, "audibilityVerified",
                         json_object_new_boolean(0));
  json_object_object_add(root, "hardwareReadback", json_object_new_boolean(0));
  json_object_object_add(root, "hardwareExactRollback",
                         json_object_new_boolean(0));
  json_object_object_add(root, "applied", json_object_new_boolean(0));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static int initialize_audio_selection_receipt(audio_selection_receipt *receipt,
                                              audio_selection_kind kind,
                                              const char *target,
                                              const char *target_type,
                                              const char *original_selection,
                                              const char *requested_selection) {
  memset(receipt, 0, sizeof(*receipt));
  receipt->selection = audio_selection_name(kind);
  if (audio_copy_bounded(receipt->target, sizeof(receipt->target), target) !=
          0 ||
      audio_copy_bounded(receipt->target_type, sizeof(receipt->target_type),
                         target_type) != 0 ||
      audio_copy_bounded(receipt->original_selection,
                         sizeof(receipt->original_selection),
                         original_selection) != 0 ||
      audio_copy_bounded(receipt->requested_selection,
                         sizeof(receipt->requested_selection),
                         requested_selection) != 0)
    return -1;
  return 0;
}

static void audio_selection_receipt_status(audio_selection_receipt *receipt,
                                           const char *status,
                                           const char *reason) {
  (void)audio_copy_bounded(receipt->status, sizeof(receipt->status), status);
  (void)audio_copy_bounded(receipt->reason, sizeof(receipt->reason),
                           reason ? reason : "");
}

static void
print_audio_selection_receipt(const audio_selection_receipt *receipt,
                              int json) {
  if (!json) {
    printf("Audio %s %s: %s", receipt->selection, receipt->target,
           receipt->status);
    if (receipt->reason[0])
      printf(" (%s)", receipt->reason);
    fputc('\n', stdout);
    return;
  }
  int profile = strcmp(receipt->selection, "profile") == 0;
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-profile-port-receipt/v1"));
  json_object_object_add(root, "status",
                         json_object_new_string(receipt->status));
  if (receipt->reason[0])
    json_object_object_add(root, "reason",
                           json_object_new_string(receipt->reason));
  else
    json_object_object_add(root, "reason", NULL);
  json_object_object_add(root, "selection",
                         json_object_new_string(receipt->selection));
  json_object_object_add(root, "target",
                         json_object_new_string(receipt->target));
  json_object_object_add(root, "targetType",
                         json_object_new_string(receipt->target_type));
  json_object_object_add(root, "originalSelection",
                         json_object_new_string(receipt->original_selection));
  json_object_object_add(root, "requestedSelection",
                         json_object_new_string(receipt->requested_selection));
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
  json_object_object_add(root, "profileChanged",
                         json_object_new_boolean(profile && receipt->changed));
  json_object_object_add(root, "portChanged",
                         json_object_new_boolean(!profile && receipt->changed));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "requiresAcknowledgement",
                         json_object_new_string(AUDIO_SELECTION_ACK));
  json_object_object_add(root, "singleTarget", json_object_new_boolean(1));
  json_object_object_add(root, "graphMayChange",
                         json_object_new_boolean(profile));
  json_object_object_add(root, "signalPathMayChange",
                         json_object_new_boolean(1));
  json_object_object_add(root, "playbackStarted", json_object_new_boolean(0));
  json_object_object_add(root, "captureStarted", json_object_new_boolean(0));
  json_object_object_add(root, "defaultChanged", json_object_new_boolean(0));
  json_object_object_add(root, "policyChanged", json_object_new_boolean(0));
  json_object_object_add(root, "audibilityVerified",
                         json_object_new_boolean(0));
  json_object_object_add(root, "hardwareReadback", json_object_new_boolean(0));
  json_object_object_add(root, "hardwareExactRollback",
                         json_object_new_boolean(0));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static int execute_audio_selection(const audio_selection_state *state,
                                   const audio_selection_option *selection,
                                   int *timed_out) {
  const char *operation = NULL;
  if (state->kind == AUDIO_SELECTION_PROFILE)
    operation = "set-card-profile";
  else if (strcmp(state->target_type, "output") == 0)
    operation = "set-sink-port";
  else
    operation = "set-source-port";
  char *argv[5] = {(char *)audio_pactl_binary(), (char *)operation,
                   (char *)state->raw_name, (char *)selection->raw_name, NULL};
  audio_capture_result result = audio_capture_command(argv);
  *timed_out = result.timed_out;
  int status = result.data && !result.too_large && result.status == 0 ? 0 : -1;
  audio_capture_free(&result);
  return status;
}

static const char *audio_selection_load_reason(int loaded) {
  return loaded == 1   ? "target-vanished"
         : loaded == 2 ? "active-selection-unavailable"
                       : "audio-unavailable";
}

static void apply_audio_selection(audio_selection_kind kind, const char *target,
                                  const char *target_type,
                                  const char *original_selection,
                                  const char *requested_selection,
                                  const char *expected_cohort,
                                  audio_selection_receipt *receipt) {
  audio_selection_state first;
  const char *reason = NULL;
  int loaded = load_audio_selection_state(kind, target, target_type,
                                          requested_selection, &first, &reason);
  if (loaded != 0) {
    audio_selection_receipt_status(receipt, "Refused",
                                   audio_selection_load_reason(loaded));
    return;
  }
  if (strcmp(first.original.id, original_selection) != 0) {
    audio_selection_receipt_status(receipt, "Refused",
                                   "original-selection-mismatch");
    return;
  }
  if (!first.requested_found ||
      first.requested.availability == AUDIO_SELECTION_UNAVAILABLE) {
    audio_selection_receipt_status(receipt, "Refused", "selection-unavailable");
    return;
  }
  char first_cohort[32];
  if (audio_selection_cohort(&first, first_cohort, sizeof(first_cohort)) != 0 ||
      strcmp(first_cohort, expected_cohort) != 0) {
    audio_selection_receipt_status(receipt, "Refused",
                                   "selection-cohort-changed");
    return;
  }

  audio_selection_state planned;
  loaded = load_audio_selection_state(kind, target, target_type,
                                      requested_selection, &planned, &reason);
  if (loaded != 0) {
    audio_selection_receipt_status(receipt, "Refused",
                                   audio_selection_load_reason(loaded));
    return;
  }
  char planned_cohort[32];
  if (!audio_selection_identity_equal(&first, &planned)) {
    audio_selection_receipt_status(receipt, "Refused",
                                   "target-identity-changed");
    return;
  }
  if (!planned.requested_found ||
      planned.requested.availability == AUDIO_SELECTION_UNAVAILABLE ||
      strcmp(planned.original.id, original_selection) != 0 ||
      !audio_selection_option_identity_equal(&first.original,
                                             &planned.original) ||
      !audio_selection_option_identity_equal(&first.requested,
                                             &planned.requested) ||
      first.requested.availability != planned.requested.availability ||
      audio_selection_cohort(&planned, planned_cohort,
                             sizeof(planned_cohort)) != 0 ||
      strcmp(planned_cohort, expected_cohort) != 0) {
    audio_selection_receipt_status(receipt, "Refused",
                                   "selection-cohort-changed");
    return;
  }
  if (strcmp(original_selection, requested_selection) == 0) {
    receipt->verified = 1;
    audio_selection_receipt_status(receipt, "AlreadySet", NULL);
    return;
  }

  receipt->mutation_attempted = 1;
  int mutation_timed_out = 0;
  int mutation_status = execute_audio_selection(&planned, &planned.requested,
                                                &mutation_timed_out);
  audio_selection_state after;
  loaded = load_audio_selection_state(kind, target, target_type,
                                      requested_selection, &after, &reason);
  if (loaded != 0) {
    audio_selection_receipt_status(receipt, "Failed",
                                   loaded == 1 ? "target-vanished"
                                               : "verification-unavailable");
    return;
  }
  if (!audio_selection_identity_equal(&planned, &after)) {
    audio_selection_receipt_status(receipt, "Failed",
                                   "target-identity-changed");
    return;
  }
  if (!after.requested_found ||
      !audio_selection_option_identity_equal(&planned.requested,
                                             &after.requested) ||
      (strcmp(after.original.id, requested_selection) == 0 &&
       !audio_selection_option_identity_equal(&planned.requested,
                                              &after.original)) ||
      (strcmp(after.original.id, original_selection) == 0 &&
       !audio_selection_option_identity_equal(&planned.original,
                                              &after.original))) {
    audio_selection_receipt_status(receipt, "Failed",
                                   "verification-unavailable");
    return;
  }
  const char *observed = after.original.id;
  if (mutation_status == 0 && strcmp(observed, requested_selection) == 0) {
    receipt->changed = 1;
    receipt->verified = 1;
    audio_selection_receipt_status(receipt, "Applied", NULL);
    return;
  }

  const char *failure_reason = mutation_timed_out     ? "mutation-timeout"
                               : mutation_status != 0 ? "mutation-failed"
                                                      : "verification-failed";
  if (strcmp(observed, original_selection) != 0 &&
      strcmp(observed, requested_selection) != 0) {
    /* Never overwrite an observed third choice. */
    failure_reason = "verification-failed";
  } else if (strcmp(observed, original_selection) != 0) {
    audio_selection_state rollback_state;
    const char *rollback_reason = NULL;
    loaded = load_audio_selection_state(kind, target, target_type,
                                        original_selection, &rollback_state,
                                        &rollback_reason);
    if (loaded != 0) {
      failure_reason =
          loaded == 1 ? "target-vanished" : "verification-unavailable";
    } else if (!audio_selection_identity_equal(&planned, &rollback_state)) {
      failure_reason = "target-identity-changed";
    } else if (!rollback_state.requested_found ||
               !audio_selection_option_identity_equal(
                   &planned.original, &rollback_state.requested)) {
      failure_reason = "verification-unavailable";
    } else if (strcmp(rollback_state.original.id, original_selection) == 0) {
      if (!audio_selection_option_identity_equal(&planned.original,
                                                 &rollback_state.original))
        failure_reason = "verification-unavailable";
      /* Otherwise another actor restored the original; issue no setter. */
    } else if (strcmp(rollback_state.original.id, observed) != 0) {
      failure_reason = "verification-failed";
    } else if (!audio_selection_option_identity_equal(
                   &planned.requested, &rollback_state.original) ||
               rollback_state.requested.availability ==
                   AUDIO_SELECTION_UNAVAILABLE) {
      failure_reason = "verification-unavailable";
    } else {
      receipt->rollback_attempted = 1;
      int rollback_timed_out = 0;
      (void)execute_audio_selection(&rollback_state, &rollback_state.requested,
                                    &rollback_timed_out);
      (void)rollback_timed_out;
      audio_selection_state restored;
      const char *restore_reason = NULL;
      if (load_audio_selection_state(kind, target, target_type,
                                     original_selection, &restored,
                                     &restore_reason) == 0 &&
          audio_selection_identity_equal(&planned, &restored) &&
          strcmp(restored.original.id, original_selection) == 0 &&
          audio_selection_option_identity_equal(&planned.original,
                                                &restored.original)) {
        receipt->rollback_verified = 1;
      } else {
        failure_reason = "rollback-failed";
      }
    }
  }
  audio_selection_receipt_status(receipt, "Failed", failure_reason);
}

static int parse_audio_selection_options(
    int argc, char **argv, int apply, audio_selection_kind kind,
    const char **target, const char **target_type,
    const char **original_selection, const char **requested_selection,
    const char **cohort, const char **ack, const char **format) {
  *target = NULL;
  *target_type = kind == AUDIO_SELECTION_PROFILE ? "card" : NULL;
  *original_selection = NULL;
  *requested_selection = NULL;
  *cohort = NULL;
  *ack = NULL;
  *format = "text";
  int seen_format = 0;
  const char *target_flag =
      kind == AUDIO_SELECTION_PROFILE ? "--card" : "--device";
  const char *requested_flag =
      kind == AUDIO_SELECTION_PROFILE ? "--profile" : "--port";
  const char *original_flag =
      kind == AUDIO_SELECTION_PROFILE ? "--from-profile" : "--from-port";
  for (int index = 2; index < argc; index++) {
    const char **destination = NULL;
    if (strcmp(argv[index], target_flag) == 0)
      destination = target;
    else if (strcmp(argv[index], requested_flag) == 0)
      destination = requested_selection;
    else if (strcmp(argv[index], original_flag) == 0)
      destination = original_selection;
    else if (kind == AUDIO_SELECTION_PORT &&
             strcmp(argv[index], "--direction") == 0)
      destination = target_type;
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
  if (!*target || !*target_type || !*requested_selection ||
      (strcmp(*format, "text") != 0 && strcmp(*format, "json") != 0))
    return -1;
  if (kind == AUDIO_SELECTION_PORT && strcmp(*target_type, "output") != 0 &&
      strcmp(*target_type, "input") != 0)
    return -1;
  if (!apply && (*original_selection || *cohort || *ack))
    return -1;
  if (apply && (!*original_selection || !*cohort || !*ack))
    return -1;
  return 0;
}

int settings_audio_selection_command(int argc, char **argv) {
  int profile = strcmp(argv[1], "plan-profile") == 0 ||
                strcmp(argv[1], "set-profile") == 0;
  int apply =
      strcmp(argv[1], "set-profile") == 0 || strcmp(argv[1], "set-port") == 0;
  audio_selection_kind kind =
      profile ? AUDIO_SELECTION_PROFILE : AUDIO_SELECTION_PORT;
  const char *target = NULL;
  const char *target_type = NULL;
  const char *original_selection = NULL;
  const char *requested_selection = NULL;
  const char *cohort = NULL;
  const char *ack = NULL;
  const char *format = NULL;
  if (parse_audio_selection_options(
          argc, argv, apply, kind, &target, &target_type, &original_selection,
          &requested_selection, &cohort, &ack, &format) != 0) {
    audio_usage(stderr);
    return 2;
  }
  if (!audio_selection_target_shape(kind, target_type, target) ||
      !audio_selection_option_shape(kind, requested_selection) ||
      (apply && (!audio_selection_option_shape(kind, original_selection) ||
                 !audio_selection_cohort_shape(cohort)))) {
    fputs("synapse-settings: invalid Audio profile or port token\n", stderr);
    return 2;
  }
  if (apply && strcmp(ack, AUDIO_SELECTION_ACK) != 0) {
    fputs("synapse-settings: exact Audio profile and port acknowledgement "
          "required\n",
          stderr);
    return 2;
  }
  int json = strcmp(format, "json") == 0;
  if (!apply) {
    audio_selection_state state;
    const char *reason = NULL;
    int loaded = load_audio_selection_state(
        kind, target, target_type, requested_selection, &state, &reason);
    if (loaded != 0) {
      fprintf(stderr, "synapse-settings: Audio %s target unavailable: %s\n",
              audio_selection_name(kind), audio_selection_load_reason(loaded));
      return 1;
    }
    if (!state.requested_found ||
        state.requested.availability == AUDIO_SELECTION_UNAVAILABLE) {
      fputs("synapse-settings: requested Audio selection unavailable\n",
            stderr);
      return 1;
    }
    char planned_cohort[32];
    if (audio_selection_cohort(&state, planned_cohort,
                               sizeof(planned_cohort)) != 0) {
      fputs("synapse-settings: cannot bind Audio profile or port plan\n",
            stderr);
      return 1;
    }
    print_audio_selection_plan(&state, planned_cohort, json);
    return 0;
  }

  audio_selection_receipt receipt;
  if (initialize_audio_selection_receipt(&receipt, kind, target, target_type,
                                         original_selection,
                                         requested_selection) != 0) {
    fputs("synapse-settings: invalid Audio profile or port request\n", stderr);
    return 2;
  }
  apply_audio_selection(kind, target, target_type, original_selection,
                        requested_selection, cohort, &receipt);
  print_audio_selection_receipt(&receipt, json);
  return strcmp(receipt.status, "Applied") == 0 ||
                 strcmp(receipt.status, "AlreadySet") == 0
             ? 0
             : 1;
}
