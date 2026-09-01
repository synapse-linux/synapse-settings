// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "settings_internal.h"

#include <json-c/json.h>
#include <synapse/core.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define AUDIO_ROUTE_RULE_LIMIT 128U
#define AUDIO_ROUTE_POLICY_LIMIT ((size_t)64U * 1024U)
#define AUDIO_ROUTE_ACK "synapse-settings/audio-route-policy/v1"
#define AUDIO_ROUTE_SCHEMA "synapse.settings.audio-route-policy/v1"

typedef struct {
  char id[24];
  char type[16];
  char path[PATH_MAX];
  char direction[8];
  char device[32];
  int enabled;
} audio_route_rule;

typedef struct {
  unsigned generation;
  audio_route_rule rules[AUDIO_ROUTE_RULE_LIMIT];
  size_t rule_count;
} audio_route_policy;

static void audio_route_usage(FILE *out) {
  fputs(
      "Usage:\n"
      "  synapse-settings audio policy show [--format text|json]\n"
      "  synapse-settings audio policy set-rule --match executable|directory "
      "--path PATH --direction output|input --device ID --ack " AUDIO_ROUTE_ACK
      " [--format text|json]\n"
      "  synapse-settings audio policy set-process-rule --stream ID "
      "--device ID --ack " AUDIO_ROUTE_ACK " [--format text|json]\n"
      "  synapse-settings audio policy remove-rule --rule ID "
      "--ack " AUDIO_ROUTE_ACK " [--format text|json]\n"
      "  synapse-settings audio resolve --path EXECUTABLE --direction "
      "output|input [--format text|json]\n",
      out);
}

static int copy_text(char *target, size_t size, const char *value) {
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

static int parse_format(int argc, char **argv, int first, const char **format) {
  *format = "text";
  int seen = 0;
  for (int i = first; i < argc; i++) {
    if (strcmp(argv[i], "--format") == 0 && i + 1 < argc && !seen) {
      *format = argv[++i];
      seen = 1;
    } else if (strcmp(argv[i], "--json") == 0 && !seen) {
      *format = "json";
      seen = 1;
    } else {
      return -1;
    }
  }
  return strcmp(*format, "text") == 0 || strcmp(*format, "json") == 0 ? 0 : -1;
}

static size_t object_key_count(json_object *object) {
  size_t count = 0;
  json_object_object_foreach(object, key, value) {
    (void)key;
    (void)value;
    count++;
  }
  return count;
}

static int json_exact_string(json_object *object, const char *key, char *target,
                             size_t size) {
  json_object *value = NULL;
  if (!json_object_object_get_ex(object, key, &value) ||
      !json_object_is_type(value, json_type_string))
    return -1;
  return copy_text(target, size, json_object_get_string(value));
}

static int direction_valid(const char *direction) {
  return strcmp(direction, "output") == 0 || strcmp(direction, "input") == 0;
}

static int device_token_valid(const char *direction, const char *device) {
  const char *prefix = strcmp(direction, "output") == 0 ? "output-" : "input-";
  size_t expected = strlen(prefix) + 16U;
  if (!direction_valid(direction) || strlen(device) != expected ||
      strncmp(device, prefix, strlen(prefix)) != 0)
    return 0;
  for (const char *cursor = device + strlen(prefix); *cursor; cursor++)
    if (!(*cursor >= '0' && *cursor <= '9') &&
        !(*cursor >= 'a' && *cursor <= 'f'))
      return 0;
  return 1;
}

static int rule_token_valid(const char *value) {
  if (strncmp(value, "rule-", 5) != 0 || strlen(value) != 9U)
    return 0;
  for (const char *cursor = value + 5; *cursor; cursor++)
    if (*cursor < '0' || *cursor > '9')
      return 0;
  return 1;
}

static int normalized_policy_path(const char *path) {
  size_t length = strlen(path);
  if (!length || path[0] != '/')
    return 0;
  for (const unsigned char *byte = (const unsigned char *)path; *byte; byte++)
    if (*byte < 0x20U || *byte == 0x7fU)
      return 0;
  if (length == 1U)
    return 1;
  if (path[length - 1U] == '/')
    return 0;
  const char *cursor = path + 1;
  while (*cursor) {
    const char *slash = strchr(cursor, '/');
    size_t segment = slash ? (size_t)(slash - cursor) : strlen(cursor);
    if (!segment || (segment == 1U && cursor[0] == '.') ||
        (segment == 2U && cursor[0] == '.' && cursor[1] == '.'))
      return 0;
    if (!slash)
      break;
    cursor = slash + 1;
  }
  return 1;
}

static int policy_file_path(char *target, size_t size) {
#ifdef SYNAPSE_SETTINGS_TEST_HOOKS
  const char *override = getenv("SYNAPSE_AUDIO_ROUTE_POLICY");
  if (override && *override)
    return copy_text(target, size, override);
#endif
  const char *config = getenv("XDG_CONFIG_HOME");
  char fallback[PATH_MAX];
  if (!config || !*config) {
    const char *home = getenv("HOME");
    if (!home || !*home) {
      errno = ENOENT;
      return -1;
    }
    int written = snprintf(fallback, sizeof(fallback), "%s/.config", home);
    if (written < 0 || (size_t)written >= sizeof(fallback)) {
      errno = ENAMETOOLONG;
      return -1;
    }
    config = fallback;
  }
  int written =
      snprintf(target, size, "%s/synapse/audio-route-policy-v1.json", config);
  if (written < 0 || (size_t)written >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

static void policy_default(audio_route_policy *policy) {
  memset(policy, 0, sizeof(*policy));
}

static int parse_policy_object(json_object *root, audio_route_policy *policy) {
  if (!json_object_is_type(root, json_type_object) ||
      object_key_count(root) != 3U)
    return -1;
  char schema[64];
  json_object *generation = NULL;
  json_object *rules = NULL;
  if (json_exact_string(root, "schema", schema, sizeof(schema)) != 0 ||
      strcmp(schema, AUDIO_ROUTE_SCHEMA) != 0 ||
      !json_object_object_get_ex(root, "generation", &generation) ||
      !json_object_is_type(generation, json_type_int) ||
      json_object_get_int64(generation) < 0 ||
      json_object_get_int64(generation) > UINT_MAX ||
      !json_object_object_get_ex(root, "rules", &rules) ||
      !json_object_is_type(rules, json_type_array))
    return -1;
  policy->generation = (unsigned)json_object_get_int64(generation);
  size_t length = json_object_array_length(rules);
  if (length > AUDIO_ROUTE_RULE_LIMIT)
    return -1;
  for (size_t i = 0; i < length; i++) {
    audio_route_rule *rule = &policy->rules[i];
    json_object *value = json_object_array_get_idx(rules, i);
    json_object *match = NULL;
    json_object *enabled = NULL;
    if (!json_object_is_type(value, json_type_object) ||
        object_key_count(value) != 5U ||
        json_exact_string(value, "id", rule->id, sizeof(rule->id)) != 0 ||
        json_exact_string(value, "direction", rule->direction,
                          sizeof(rule->direction)) != 0 ||
        json_exact_string(value, "device", rule->device,
                          sizeof(rule->device)) != 0 ||
        !json_object_object_get_ex(value, "enabled", &enabled) ||
        !json_object_is_type(enabled, json_type_boolean) ||
        !json_object_object_get_ex(value, "match", &match) ||
        !json_object_is_type(match, json_type_object) ||
        object_key_count(match) != 2U ||
        json_exact_string(match, "type", rule->type, sizeof(rule->type)) != 0 ||
        json_exact_string(match, "path", rule->path, sizeof(rule->path)) != 0)
      return -1;
    if ((strcmp(rule->type, "executable") != 0 &&
         strcmp(rule->type, "directory") != 0) ||
        !rule_token_valid(rule->id) ||
        !device_token_valid(rule->direction, rule->device) ||
        !normalized_policy_path(rule->path))
      return -1;
    rule->enabled = json_object_get_boolean(enabled) ? 1 : 0;
    for (size_t j = 0; j < i; j++)
      if (strcmp(policy->rules[j].id, rule->id) == 0 ||
          (strcmp(policy->rules[j].type, rule->type) == 0 &&
           strcmp(policy->rules[j].path, rule->path) == 0 &&
           strcmp(policy->rules[j].direction, rule->direction) == 0))
        return -1;
  }
  policy->rule_count = length;
  return 0;
}

static json_object *policy_json(const audio_route_policy *policy) {
  json_object *root = json_object_new_object();
  json_object_object_add(root, "schema",
                         json_object_new_string(AUDIO_ROUTE_SCHEMA));
  json_object_object_add(root, "generation",
                         json_object_new_int64(policy->generation));
  json_object *rules = json_object_new_array_ext((int)policy->rule_count);
  for (size_t i = 0; i < policy->rule_count; i++) {
    const audio_route_rule *rule = &policy->rules[i];
    json_object *value = json_object_new_object();
    json_object_object_add(value, "id", json_object_new_string(rule->id));
    json_object *match = json_object_new_object();
    json_object_object_add(match, "type", json_object_new_string(rule->type));
    json_object_object_add(match, "path", json_object_new_string(rule->path));
    json_object_object_add(value, "match", match);
    json_object_object_add(value, "direction",
                           json_object_new_string(rule->direction));
    json_object_object_add(value, "device",
                           json_object_new_string(rule->device));
    json_object_object_add(value, "enabled",
                           json_object_new_boolean(rule->enabled));
    json_object_array_add(rules, value);
  }
  json_object_object_add(root, "rules", rules);
  return root;
}

static int load_policy(audio_route_policy *policy, int *present) {
  policy_default(policy);
  *present = 0;
  char path[PATH_MAX];
  if (policy_file_path(path, sizeof(path)) != 0)
    return -1;
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0 && errno == ENOENT)
    return 0;
  if (fd < 0)
    return -1;
  struct stat stat_value;
  if (fstat(fd, &stat_value) != 0 || !S_ISREG(stat_value.st_mode) ||
      stat_value.st_uid != geteuid() || (stat_value.st_mode & 0077) != 0 ||
      stat_value.st_size <= 0 ||
      stat_value.st_size > (off_t)AUDIO_ROUTE_POLICY_LIMIT) {
    close(fd);
    errno = EINVAL;
    return -1;
  }
  size_t size = (size_t)stat_value.st_size;
  char *data = malloc(size + 1U);
  if (!data) {
    close(fd);
    return -1;
  }
  size_t used = 0;
  while (used < size) {
    ssize_t count = read(fd, data + used, size - used);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      break;
    used += (size_t)count;
  }
  close(fd);
  if (used != size) {
    free(data);
    errno = EIO;
    return -1;
  }
  data[size] = '\0';
  json_tokener *tokener = json_tokener_new_ex(32);
  if (!tokener) {
    free(data);
    return -1;
  }
  json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
  json_object *root = json_tokener_parse_ex(tokener, data, (int)size);
  enum json_tokener_error error = json_tokener_get_error(tokener);
  json_tokener_free(tokener);
  int status = -1;
  if (error == json_tokener_success && root &&
      parse_policy_object(root, policy) == 0) {
    json_object *canonical_root = policy_json(policy);
    if (canonical_root) {
      const char *canonical = json_object_to_json_string_ext(
          canonical_root, JSON_C_TO_STRING_PLAIN);
      size_t canonical_length = strlen(canonical);
      size_t compare_size = size;
      if (compare_size && data[compare_size - 1U] == '\n')
        compare_size--;
      if (compare_size == canonical_length &&
          memcmp(data, canonical, canonical_length) == 0)
        status = 0;
      json_object_put(canonical_root);
    }
  }
  if (root)
    json_object_put(root);
  free(data);
  if (status != 0) {
    errno = EINVAL;
    return -1;
  }
  *present = 1;
  return 0;
}

static int owned_directory(const char *path, int require_private) {
  struct stat value;
  if (lstat(path, &value) != 0 || !S_ISDIR(value.st_mode) ||
      S_ISLNK(value.st_mode) || value.st_uid != geteuid()) {
    errno = EPERM;
    return -1;
  }
  if (require_private && (value.st_mode & 0077) != 0 && chmod(path, 0700) != 0)
    return -1;
  return 0;
}

static int ensure_policy_parent(const char *path) {
  char parent[PATH_MAX];
  if (copy_text(parent, sizeof(parent), path) != 0)
    return -1;
  char *slash = strrchr(parent, '/');
  if (!slash || slash == parent) {
    errno = EINVAL;
    return -1;
  }
  *slash = '\0';
  struct stat value;
  if (lstat(parent, &value) != 0) {
    if (errno != ENOENT)
      return -1;
    char ancestor[PATH_MAX];
    if (copy_text(ancestor, sizeof(ancestor), parent) != 0)
      return -1;
    char *ancestor_slash = strrchr(ancestor, '/');
    if (!ancestor_slash || ancestor_slash == ancestor) {
      errno = EINVAL;
      return -1;
    }
    *ancestor_slash = '\0';
    if (lstat(ancestor, &value) != 0) {
      if (errno != ENOENT || mkdir(ancestor, 0700) != 0)
        return -1;
    }
    if (owned_directory(ancestor, 0) != 0 || mkdir(parent, 0700) != 0)
      return -1;
  }
  return owned_directory(parent, 1);
}

static int write_all(int fd, const char *data, size_t size) {
  size_t used = 0;
  while (used < size) {
    ssize_t count = write(fd, data + used, size - used);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return -1;
    used += (size_t)count;
  }
  return 0;
}

static int save_policy(const audio_route_policy *policy) {
  char path[PATH_MAX];
  if (policy_file_path(path, sizeof(path)) != 0 ||
      ensure_policy_parent(path) != 0)
    return -1;
  json_object *root = policy_json(policy);
  if (!root)
    return -1;
  const char *serialized =
      json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
  size_t length = strlen(serialized);
  if (length + 1U > AUDIO_ROUTE_POLICY_LIMIT) {
    json_object_put(root);
    errno = E2BIG;
    return -1;
  }
  char temporary[PATH_MAX];
  int written = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path,
                         (long)getpid());
  if (written < 0 || (size_t)written >= sizeof(temporary)) {
    json_object_put(root);
    errno = ENAMETOOLONG;
    return -1;
  }
  int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                0600);
  if (fd < 0) {
    json_object_put(root);
    return -1;
  }
  int status = 0;
  int saved = 0;
  if (write_all(fd, serialized, length) != 0 || write_all(fd, "\n", 1U) != 0 ||
      fsync(fd) != 0) {
    status = -1;
    saved = errno;
  }
  if (close(fd) != 0 && status == 0) {
    status = -1;
    saved = errno;
  }
  if (status == 0 && rename(temporary, path) != 0) {
    status = -1;
    saved = errno;
  }
  if (status == 0) {
    char parent[PATH_MAX];
    if (copy_text(parent, sizeof(parent), path) != 0) {
      status = -1;
      saved = errno;
    } else {
      char *slash = strrchr(parent, '/');
      if (!slash || slash == parent) {
        status = -1;
        saved = EINVAL;
      } else {
        *slash = '\0';
        int directory_fd =
            open(parent, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        if (directory_fd < 0) {
          status = -1;
          saved = errno;
        } else {
          if (fsync(directory_fd) != 0) {
            status = -1;
            saved = errno;
          }
          close(directory_fd);
        }
      }
    }
  }
  if (status != 0)
    unlink(temporary);
  json_object_put(root);
  if (status != 0)
    errno = saved;
  return status;
}

static void display_path(const char *path, char *target, size_t size) {
  const char *home = getenv("HOME");
  if (home && *home) {
    size_t length = strlen(home);
    if (strncmp(path, home, length) == 0 &&
        (path[length] == '/' || path[length] == '\0')) {
      int written = snprintf(target, size, "~%s", path + length);
      if (written > 0 && (size_t)written < size)
        return;
    }
  }
  (void)copy_text(target, size, path);
}

static int canonical_match_path(const char *type, const char *path,
                                char *target, size_t size) {
  char *resolved = realpath(path, NULL);
  if (!resolved)
    return -1;
  struct stat value;
  int status = stat(resolved, &value);
  if (status == 0 && strcmp(type, "directory") == 0 && !S_ISDIR(value.st_mode))
    status = -1;
  if (status == 0 && strcmp(type, "executable") == 0 &&
      (!S_ISREG(value.st_mode) || access(resolved, X_OK) != 0))
    status = -1;
  if (status == 0)
    status = copy_text(target, size, resolved);
  free(resolved);
  return status;
}

static int path_is_within(const char *path, const char *directory) {
  size_t length = strlen(directory);
  if (strncmp(path, directory, length) != 0)
    return 0;
  if (directory[length - 1U] == '/')
    return 1;
  return path[length] == '/' || path[length] == '\0';
}

static unsigned next_rule_number(const audio_route_policy *policy) {
  unsigned maximum = 0;
  for (size_t i = 0; i < policy->rule_count; i++) {
    const char *digits = policy->rules[i].id + 5;
    unsigned value = (unsigned)(digits[0] - '0') * 1000U +
                     (unsigned)(digits[1] - '0') * 100U +
                     (unsigned)(digits[2] - '0') * 10U +
                     (unsigned)(digits[3] - '0');
    if (value > maximum)
      maximum = value;
  }
  return maximum + 1U;
}

static void print_policy(const audio_route_policy *policy, int present,
                         int json) {
  if (!json) {
    puts("Application audio routing rules:");
    for (size_t i = 0; i < policy->rule_count; i++) {
      char shown[PATH_MAX];
      display_path(policy->rules[i].path, shown, sizeof(shown));
      printf("  %s  %s  %s  %s -> %s\n", policy->rules[i].id,
             policy->rules[i].type, shown, policy->rules[i].direction,
             policy->rules[i].device);
    }
    if (!present)
      puts("Policy file not created; system defaults are active.");
    puts("Audio route broker: not integrated");
    return;
  }
  json_object *root = policy_json(policy);
  json_object_object_del(root, "schema");
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-route-policy-view/v1"));
  json_object_object_add(root, "present", json_object_new_boolean(present));
  json_object_object_add(root, "systemDefaultFallback",
                         json_object_new_boolean(1));
  json_object_object_add(root, "enforcementAvailable",
                         json_object_new_boolean(0));
  json_object_object_add(
      root, "enforcementReason",
      json_object_new_string("audio-route-broker-not-integrated"));
  json_object_object_add(root, "persistentPidRules",
                         json_object_new_boolean(0));
  json_object_object_add(root, "existingStreamMigration",
                         json_object_new_boolean(0));
  json_object *rules = NULL;
  if (json_object_object_get_ex(root, "rules", &rules)) {
    for (size_t i = 0; i < policy->rule_count; i++) {
      json_object *value = json_object_array_get_idx(rules, i);
      json_object *match = NULL;
      if (json_object_object_get_ex(value, "match", &match)) {
        char shown[PATH_MAX];
        display_path(policy->rules[i].path, shown, sizeof(shown));
        json_object_object_del(match, "path");
        json_object_object_add(match, "displayPath",
                               json_object_new_string(shown));
      }
    }
  }
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static void print_policy_receipt(const char *action,
                                 const audio_route_policy *policy,
                                 const char *device, const char *rule,
                                 int changed, int json) {
  if (!json) {
    printf("Audio route policy %s: %s%s%s\n", action,
           changed ? "applied" : "unchanged", rule ? " " : "",
           rule ? rule : "");
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-route-policy-receipt/v1"));
  json_object_object_add(root, "status", json_object_new_string("Applied"));
  json_object_object_add(root, "action", json_object_new_string(action));
  json_object_object_add(root, "generation",
                         json_object_new_int64(policy->generation));
  json_object_object_add(root, "changed", json_object_new_boolean(changed));
  json_object_object_add(root, "device",
                         device ? json_object_new_string(device)
                                : json_object_new_null());
  json_object_object_add(root, "rule",
                         rule ? json_object_new_string(rule)
                              : json_object_new_null());
  json_object_object_add(root, "policyApplied", json_object_new_boolean(1));
  json_object_object_add(root, "routingApplied", json_object_new_boolean(0));
  json_object_object_add(root, "enforcementAvailable",
                         json_object_new_boolean(0));
  json_object_object_add(
      root, "enforcementReason",
      json_object_new_string("audio-route-broker-not-integrated"));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static int policy_show(int argc, char **argv) {
  const char *format = NULL;
  if (parse_format(argc, argv, 3, &format) != 0)
    return 2;
  audio_route_policy policy;
  int present = 0;
  if (load_policy(&policy, &present) != 0) {
    fputs("synapse-settings: invalid audio route policy\n", stderr);
    return 1;
  }
  print_policy(&policy, present, strcmp(format, "json") == 0);
  return 0;
}

static int resolve_policy(int argc, char **argv) {
  const char *path = NULL;
  const char *direction = NULL;
  const char *format = "text";
  int seen_format = 0;
  for (int i = 2; i < argc; i++) {
    const char **target = NULL;
    if (strcmp(argv[i], "--path") == 0)
      target = &path;
    else if (strcmp(argv[i], "--direction") == 0)
      target = &direction;
    else if (strcmp(argv[i], "--format") == 0 && !seen_format) {
      if (i + 1 >= argc)
        return 2;
      format = argv[++i];
      seen_format = 1;
      continue;
    } else if (strcmp(argv[i], "--json") == 0 && !seen_format) {
      format = "json";
      seen_format = 1;
      continue;
    } else {
      return 2;
    }
    if (i + 1 >= argc || *target)
      return 2;
    *target = argv[++i];
  }
  if (!path || !direction || !direction_valid(direction) ||
      (strcmp(format, "text") != 0 && strcmp(format, "json") != 0))
    return 2;
  char resolved[PATH_MAX];
  if (canonical_match_path("executable", path, resolved, sizeof(resolved)) !=
      0) {
    fputs("synapse-settings: executable path cannot be resolved\n", stderr);
    return 1;
  }
  audio_route_policy policy;
  int present = 0;
  if (load_policy(&policy, &present) != 0) {
    fputs("synapse-settings: invalid audio route policy\n", stderr);
    return 1;
  }
  const audio_route_rule *selected = NULL;
  size_t selected_length = 0;
  for (size_t i = 0; i < policy.rule_count; i++) {
    const audio_route_rule *rule = &policy.rules[i];
    if (!rule->enabled || strcmp(rule->direction, direction) != 0)
      continue;
    if (strcmp(rule->type, "executable") == 0 &&
        strcmp(rule->path, resolved) == 0) {
      selected = rule;
      break;
    }
    if (strcmp(rule->type, "directory") == 0 &&
        path_is_within(resolved, rule->path)) {
      size_t length = strlen(rule->path);
      if (!selected || length > selected_length) {
        selected = rule;
        selected_length = length;
      }
    }
  }
  char device[32];
  int available = 0;
  if (settings_audio_policy_target(direction,
                                   selected ? selected->device : NULL, device,
                                   sizeof(device), &available) != 0) {
    fputs("synapse-settings: audio inventory unavailable\n", stderr);
    return 1;
  }
  const char *source = selected ? (strcmp(selected->type, "executable") == 0
                                       ? "exact-executable"
                                       : "directory-prefix")
                                : "system-default";
  if (strcmp(format, "json") != 0) {
    printf("Selected %s: %s (%s, %s)\n", direction,
           *device ? device : "unavailable", source,
           available ? "available" : "unavailable");
    return 0;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-route-resolution/v1"));
  json_object_object_add(root, "direction", json_object_new_string(direction));
  json_object_object_add(root, "selectedDevice",
                         *device ? json_object_new_string(device)
                                 : json_object_new_null());
  json_object_object_add(root, "source", json_object_new_string(source));
  json_object_object_add(root, "rule",
                         selected ? json_object_new_string(selected->id)
                                  : json_object_new_null());
  json_object_object_add(root, "available", json_object_new_boolean(available));
  json_object_object_add(root, "enforcementAvailable",
                         json_object_new_boolean(0));
  json_object_object_add(
      root, "enforcementReason",
      json_object_new_string("audio-route-broker-not-integrated"));
  json_object_object_add(root, "persistentPidRule", json_object_new_boolean(0));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
  return 0;
}

static int policy_mutation(int argc, char **argv) {
  const char *action = argv[2];
  const char *match = NULL;
  const char *path = NULL;
  const char *direction = NULL;
  const char *device = NULL;
  const char *stream_id = NULL;
  const char *rule_id = NULL;
  const char *ack = NULL;
  const char *format = "text";
  int seen_format = 0;
  for (int i = 3; i < argc; i++) {
    const char **target = NULL;
    if (strcmp(argv[i], "--match") == 0)
      target = &match;
    else if (strcmp(argv[i], "--path") == 0)
      target = &path;
    else if (strcmp(argv[i], "--direction") == 0)
      target = &direction;
    else if (strcmp(argv[i], "--device") == 0)
      target = &device;
    else if (strcmp(argv[i], "--stream") == 0)
      target = &stream_id;
    else if (strcmp(argv[i], "--rule") == 0)
      target = &rule_id;
    else if (strcmp(argv[i], "--ack") == 0)
      target = &ack;
    else if (strcmp(argv[i], "--format") == 0 && !seen_format) {
      if (i + 1 >= argc)
        return 2;
      format = argv[++i];
      seen_format = 1;
      continue;
    } else if (strcmp(argv[i], "--json") == 0 && !seen_format) {
      format = "json";
      seen_format = 1;
      continue;
    } else {
      return 2;
    }
    if (i + 1 >= argc || *target)
      return 2;
    *target = argv[++i];
  }
  if (!ack || strcmp(ack, AUDIO_ROUTE_ACK) != 0 ||
      (strcmp(format, "text") != 0 && strcmp(format, "json") != 0))
    return 2;
  int setting_rule = strcmp(action, "set-rule") == 0 ||
                     strcmp(action, "set-process-rule") == 0;
  if (strcmp(action, "set-rule") == 0) {
    if (!match || !path || !direction || !device || stream_id || rule_id ||
        (strcmp(match, "executable") != 0 && strcmp(match, "directory") != 0) ||
        !direction_valid(direction) || !device_token_valid(direction, device))
      return 2;
  } else if (strcmp(action, "set-process-rule") == 0) {
    if (!stream_id || !device || match || path || direction || rule_id)
      return 2;
  } else if (strcmp(action, "remove-rule") == 0) {
    if (!rule_id || match || path || direction || device || stream_id ||
        !rule_token_valid(rule_id))
      return 2;
  } else {
    return 2;
  }

  char process_path[PATH_MAX] = "";
  char process_direction[8] = "";
  if (strcmp(action, "set-process-rule") == 0) {
    if (settings_audio_stream_executable(
            stream_id, process_direction, sizeof(process_direction),
            process_path, sizeof(process_path)) != 0) {
      fputs("synapse-settings: selected stream has no trusted executable\n",
            stderr);
      return 1;
    }
    match = "executable";
    path = process_path;
    direction = process_direction;
    if (!device_token_valid(direction, device))
      return 2;
  }

  if (device) {
    char resolved_device[32];
    int available = 0;
    if (settings_audio_policy_target(direction, device, resolved_device,
                                     sizeof(resolved_device),
                                     &available) != 0) {
      fputs("synapse-settings: audio inventory unavailable\n", stderr);
      return 1;
    }
    if (!available || strcmp(device, resolved_device) != 0) {
      fputs("synapse-settings: unknown audio device token\n", stderr);
      return 1;
    }
  }

  audio_route_policy policy;
  int present = 0;
  if (load_policy(&policy, &present) != 0) {
    fputs("synapse-settings: invalid audio route policy\n", stderr);
    return 1;
  }
  (void)present;
  int changed = 0;
  char receipt_rule[24] = "";
  char receipt_device_storage[32] = "";
  const char *receipt_device = device;
  if (setting_rule) {
    if (!match || !path || !direction || !device)
      return 2;
    char canonical[PATH_MAX];
    if (canonical_match_path(match, path, canonical, sizeof(canonical)) != 0) {
      fputs("synapse-settings: rule path must resolve to the requested type\n",
            stderr);
      return 1;
    }
    size_t found = policy.rule_count;
    for (size_t i = 0; i < policy.rule_count; i++)
      if (strcmp(policy.rules[i].type, match) == 0 &&
          strcmp(policy.rules[i].path, canonical) == 0 &&
          strcmp(policy.rules[i].direction, direction) == 0)
        found = i;
    audio_route_rule *rule = NULL;
    if (found < policy.rule_count) {
      rule = &policy.rules[found];
      if (strcmp(rule->device, device) != 0 || !rule->enabled) {
        if (copy_text(rule->device, sizeof(rule->device), device) != 0)
          return 1;
        rule->enabled = 1;
        changed = 1;
      }
    } else {
      if (policy.rule_count >= AUDIO_ROUTE_RULE_LIMIT) {
        fputs("synapse-settings: audio route rule limit reached\n", stderr);
        return 1;
      }
      rule = &policy.rules[policy.rule_count];
      memset(rule, 0, sizeof(*rule));
      unsigned number = next_rule_number(&policy);
      int written = snprintf(rule->id, sizeof(rule->id), "rule-%04u", number);
      if (number > 9999U || written < 0 ||
          (size_t)written >= sizeof(rule->id) ||
          copy_text(rule->type, sizeof(rule->type), match) != 0 ||
          copy_text(rule->path, sizeof(rule->path), canonical) != 0 ||
          copy_text(rule->direction, sizeof(rule->direction), direction) != 0 ||
          copy_text(rule->device, sizeof(rule->device), device) != 0)
        return 1;
      rule->enabled = 1;
      policy.rule_count++;
      changed = 1;
    }
    if (copy_text(receipt_rule, sizeof(receipt_rule), rule->id) != 0)
      return 1;
  } else {
    size_t found = policy.rule_count;
    for (size_t i = 0; i < policy.rule_count; i++)
      if (strcmp(policy.rules[i].id, rule_id) == 0)
        found = i;
    if (found == policy.rule_count) {
      fputs("synapse-settings: unknown audio route rule\n", stderr);
      return 1;
    }
    if (copy_text(receipt_rule, sizeof(receipt_rule), rule_id) != 0)
      return 1;
    if (copy_text(receipt_device_storage, sizeof(receipt_device_storage),
                  policy.rules[found].device) != 0)
      return 1;
    receipt_device = receipt_device_storage;
    memmove(&policy.rules[found], &policy.rules[found + 1U],
            (policy.rule_count - found - 1U) * sizeof(policy.rules[0]));
    policy.rule_count--;
    changed = 1;
  }
  if (changed) {
    if (policy.generation == UINT_MAX) {
      fputs("synapse-settings: audio route policy generation exhausted\n",
            stderr);
      return 1;
    }
    policy.generation++;
    if (save_policy(&policy) != 0) {
      perror("synapse-settings: save audio route policy");
      return 1;
    }
  }
  print_policy_receipt(action, &policy, receipt_device, receipt_rule, changed,
                       strcmp(format, "json") == 0);
  return 0;
}

int settings_audio_route_command(int argc, char **argv) {
  if (argc >= 2 && strcmp(argv[1], "resolve") == 0) {
    int status = resolve_policy(argc, argv);
    if (status == 2)
      audio_route_usage(stderr);
    return status;
  }
  if (argc < 3 || strcmp(argv[1], "policy") != 0) {
    audio_route_usage(stderr);
    return 2;
  }
  if (strcmp(argv[2], "show") == 0) {
    int status = policy_show(argc, argv);
    if (status == 2)
      audio_route_usage(stderr);
    return status;
  }
  int status = policy_mutation(argc, argv);
  if (status == 2)
    audio_route_usage(stderr);
  return status;
}
