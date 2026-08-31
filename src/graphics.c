// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "settings_internal.h"

#include <json-c/json.h>
#include <synapse/core.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
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

#define GRAPHICS_GPU_LIMIT 32U
#define GRAPHICS_RULE_LIMIT 128U
#define GRAPHICS_POLICY_LIMIT (64U * 1024U)
#define GRAPHICS_ACK "synapse-settings/graphics-policy/v1"
#define GRAPHICS_SCHEMA "synapse.settings.graphics-policy/v1"

typedef struct {
  char id[32];
  char label[128];
  char vendor[16];
  char device[16];
  char driver[64];
  char strategy[32];
  int display_owner;
  int gaming_candidate;
  int render_available;
} graphics_gpu;

typedef struct {
  graphics_gpu items[GRAPHICS_GPU_LIMIT];
  size_t count;
} graphics_inventory;

typedef struct {
  char id[24];
  char type[16];
  char path[PATH_MAX];
  char gpu[32];
  int enabled;
} graphics_rule;

typedef struct {
  unsigned generation;
  char default_gpu[32];
  graphics_rule rules[GRAPHICS_RULE_LIMIT];
  size_t rule_count;
} graphics_policy;

static void graphics_usage(FILE *out) {
  fputs(
      "Usage:\n"
      "  synapse-settings graphics inventory [--format text|json]\n"
      "  synapse-settings graphics policy show [--format text|json]\n"
      "  synapse-settings graphics policy set-default --gpu ID|system "
      "--ack " GRAPHICS_ACK " [--format text|json]\n"
      "  synapse-settings graphics policy add-rule --match "
      "executable|directory --path PATH --gpu ID|system --ack " GRAPHICS_ACK
      " [--format text|json]\n"
      "  synapse-settings graphics policy remove-rule --rule ID "
      "--ack " GRAPHICS_ACK " [--format text|json]\n"
      "  synapse-settings graphics resolve --path PATH [--format text|json]\n",
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

static int read_small_file(const char *path, char *buffer, size_t size) {
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return -1;
  ssize_t count = read(fd, buffer, size - 1U);
  int saved = errno;
  close(fd);
  errno = saved;
  if (count < 0 || (size_t)count >= size - 1U)
    return -1;
  buffer[count] = '\0';
  while (count > 0 && (buffer[count - 1] == '\n' || buffer[count - 1] == '\r'))
    buffer[--count] = '\0';
  return 0;
}

static int card_entry(const char *name) {
  if (strncmp(name, "card", 4) != 0 || !name[4])
    return 0;
  for (const char *cursor = name + 4; *cursor; cursor++)
    if (*cursor < '0' || *cursor > '9')
      return 0;
  return 1;
}

static uint64_t fnv1a64(const char *text) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char *cursor = (const unsigned char *)text; *cursor;
       cursor++) {
    hash ^= *cursor;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static const char *vendor_label(const char *vendor) {
  if (strcmp(vendor, "0x8086") == 0)
    return "Intel";
  if (strcmp(vendor, "0x1002") == 0)
    return "AMD";
  if (strcmp(vendor, "0x10de") == 0)
    return "NVIDIA";
  return "GPU";
}

static const char *gpu_strategy(const char *vendor) {
  if (strcmp(vendor, "0x10de") == 0)
    return "nvidia-prime-offload";
  if (strcmp(vendor, "0x8086") == 0 || strcmp(vendor, "0x1002") == 0)
    return "mesa-dri-prime";
  return "unsupported";
}

static int render_available(const char *device_path) {
  char path[PATH_MAX];
  int written = snprintf(path, sizeof(path), "%s/drm", device_path);
  if (written < 0 || (size_t)written >= sizeof(path))
    return 0;
  DIR *directory = opendir(path);
  if (!directory)
    return 0;
  int available = 0;
  struct dirent *entry;
  while ((entry = readdir(directory))) {
    if (strncmp(entry->d_name, "renderD", 7) == 0) {
      available = 1;
      break;
    }
  }
  closedir(directory);
  return available;
}

static void driver_name(const char *device_path, char *target, size_t size) {
  char path[PATH_MAX];
  int written = snprintf(path, sizeof(path), "%s/driver", device_path);
  if (written < 0 || (size_t)written >= sizeof(path)) {
    (void)copy_text(target, size, "unavailable");
    return;
  }
  char link[PATH_MAX];
  ssize_t length = readlink(path, link, sizeof(link) - 1U);
  if (length <= 0) {
    (void)copy_text(target, size, "unavailable");
    return;
  }
  link[length] = '\0';
  const char *name = strrchr(link, '/');
  (void)copy_text(target, size, name ? name + 1 : link);
}

static void pci_slot(const char *device_path, char *target, size_t size) {
  target[0] = '\0';
  char path[PATH_MAX];
  int written = snprintf(path, sizeof(path), "%s/uevent", device_path);
  if (written < 0 || (size_t)written >= sizeof(path))
    return;
  size_t length = 0;
  char *contents = synapse_read_file(path, 32U * 1024U, &length);
  if (!contents)
    return;
  char *save = NULL;
  for (char *line = strtok_r(contents, "\n", &save); line;
       line = strtok_r(NULL, "\n", &save)) {
    if (strncmp(line, "PCI_SLOT_NAME=", 14) == 0) {
      (void)copy_text(target, size, line + 14);
      break;
    }
  }
  free(contents);
}

static int gpu_compare(const void *left, const void *right) {
  const graphics_gpu *a = left;
  const graphics_gpu *b = right;
  return strcmp(a->id, b->id);
}

static int scan_graphics(graphics_inventory *inventory) {
  memset(inventory, 0, sizeof(*inventory));
  const char *root = "/sys/class/drm";
#ifdef SYNAPSE_SETTINGS_TEST_HOOKS
  const char *override = getenv("SYNAPSE_DRM_CLASS");
  if (override && *override)
    root = override;
#endif
  DIR *directory = opendir(root);
  if (!directory)
    return -1;
  struct dirent *entry;
  int status = 0;
  while ((entry = readdir(directory))) {
    if (!card_entry(entry->d_name))
      continue;
    if (inventory->count >= GRAPHICS_GPU_LIMIT) {
      errno = E2BIG;
      status = -1;
      break;
    }
    char device_path[PATH_MAX];
    int written = snprintf(device_path, sizeof(device_path), "%s/%s/device",
                           root, entry->d_name);
    if (written < 0 || (size_t)written >= sizeof(device_path)) {
      status = -1;
      break;
    }
    struct stat device_stat;
    if (stat(device_path, &device_stat) != 0 || !S_ISDIR(device_stat.st_mode))
      continue;
    char path[PATH_MAX];
    char vendor[32];
    char device[32];
    char boot[32];
    written = snprintf(path, sizeof(path), "%s/vendor", device_path);
    if (written < 0 || (size_t)written >= sizeof(path) ||
        read_small_file(path, vendor, sizeof(vendor)) != 0)
      continue;
    written = snprintf(path, sizeof(path), "%s/device", device_path);
    if (written < 0 || (size_t)written >= sizeof(path) ||
        read_small_file(path, device, sizeof(device)) != 0)
      continue;
    int display_owner = 0;
    written = snprintf(path, sizeof(path), "%s/boot_vga", device_path);
    if (written > 0 && (size_t)written < sizeof(path) &&
        read_small_file(path, boot, sizeof(boot)) == 0)
      display_owner = strcmp(boot, "1") == 0;
    char slot[128];
    pci_slot(device_path, slot, sizeof(slot));
    char seed[512];
    written = snprintf(seed, sizeof(seed), "%s|%s|%s",
                       *slot ? slot : entry->d_name, vendor, device);
    if (written < 0 || (size_t)written >= sizeof(seed)) {
      status = -1;
      break;
    }
    graphics_gpu *gpu = &inventory->items[inventory->count++];
    memset(gpu, 0, sizeof(*gpu));
    written =
        snprintf(gpu->id, sizeof(gpu->id), "gpu-%016" PRIx64, fnv1a64(seed));
    if (written < 0 || (size_t)written >= sizeof(gpu->id) ||
        copy_text(gpu->vendor, sizeof(gpu->vendor), vendor) != 0 ||
        copy_text(gpu->device, sizeof(gpu->device), device) != 0 ||
        copy_text(gpu->strategy, sizeof(gpu->strategy), gpu_strategy(vendor)) !=
            0) {
      status = -1;
      break;
    }
    written = snprintf(gpu->label, sizeof(gpu->label), "%s graphics %s",
                       vendor_label(vendor), device);
    if (written < 0 || (size_t)written >= sizeof(gpu->label)) {
      status = -1;
      break;
    }
    driver_name(device_path, gpu->driver, sizeof(gpu->driver));
    gpu->display_owner = display_owner;
    gpu->gaming_candidate = !display_owner && (strcmp(vendor, "0x1002") == 0 ||
                                               strcmp(vendor, "0x10de") == 0);
    gpu->render_available = render_available(device_path);
  }
  closedir(directory);
  if (status == 0)
    qsort(inventory->items, inventory->count, sizeof(inventory->items[0]),
          gpu_compare);
  return status;
}

static const graphics_gpu *find_gpu(const graphics_inventory *inventory,
                                    const char *id) {
  if (strcmp(id, "system") == 0)
    return NULL;
  for (size_t i = 0; i < inventory->count; i++)
    if (strcmp(inventory->items[i].id, id) == 0)
      return &inventory->items[i];
  return NULL;
}

static int parse_format(int argc, char **argv, int start, const char **format) {
  *format = "text";
  int seen = 0;
  for (int i = start; i < argc; i++) {
    if (strcmp(argv[i], "--format") == 0 && i + 1 < argc && !seen) {
      *format = argv[++i];
      seen = 1;
    } else if (strcmp(argv[i], "--json") == 0 && !seen) {
      *format = "json";
      seen = 1;
    } else
      return -1;
  }
  return strcmp(*format, "text") == 0 || strcmp(*format, "json") == 0 ? 0 : -1;
}

static void print_graphics_inventory(const graphics_inventory *inventory,
                                     int json) {
  if (!json) {
    puts("Graphics devices");
    for (size_t i = 0; i < inventory->count; i++) {
      const graphics_gpu *gpu = &inventory->items[i];
      printf("  %s  %s  %s%s%s\n", gpu->id, gpu->label, gpu->driver,
             gpu->display_owner ? "  display-owner" : "",
             gpu->gaming_candidate ? "  gaming-candidate" : "");
    }
    puts("Policy applies to future broker-launched applications; running "
         "processes cannot be migrated.");
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.graphics-inventory/v1"));
  json_object *gpus = json_object_new_array_ext((int)inventory->count);
  for (size_t i = 0; i < inventory->count; i++) {
    const graphics_gpu *gpu = &inventory->items[i];
    json_object *value = json_object_new_object();
    json_object_object_add(value, "id", json_object_new_string(gpu->id));
    json_object_object_add(value, "label", json_object_new_string(gpu->label));
    json_object_object_add(value, "vendorId",
                           json_object_new_string(gpu->vendor));
    json_object_object_add(value, "deviceId",
                           json_object_new_string(gpu->device));
    json_object_object_add(value, "driver",
                           json_object_new_string(gpu->driver));
    json_object_object_add(value, "displayOwner",
                           json_object_new_boolean(gpu->display_owner));
    json_object_object_add(value, "gamingCandidate",
                           json_object_new_boolean(gpu->gaming_candidate));
    json_object_object_add(value, "renderNodeAvailable",
                           json_object_new_boolean(gpu->render_available));
    json_object_object_add(value, "strategy",
                           json_object_new_string(gpu->strategy));
    json_object_array_add(gpus, value);
  }
  json_object_object_add(root, "gpus", gpus);
  json_object_object_add(root, "gpuCount",
                         json_object_new_int64((int64_t)inventory->count));
  json_object_object_add(root, "policyAvailable", json_object_new_boolean(1));
  json_object_object_add(root, "launchBrokerAvailable",
                         json_object_new_boolean(0));
  json_object_object_add(root, "runningProcessMigration",
                         json_object_new_boolean(0));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static int policy_file_path(char *target, size_t size) {
#ifdef SYNAPSE_SETTINGS_TEST_HOOKS
  const char *override = getenv("SYNAPSE_GRAPHICS_POLICY");
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
    if (written < 0 || (size_t)written >= sizeof(fallback))
      return -1;
    config = fallback;
  }
  int written =
      snprintf(target, size, "%s/synapse/graphics-policy-v1.json", config);
  if (written < 0 || (size_t)written >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

static void policy_default(graphics_policy *policy) {
  memset(policy, 0, sizeof(*policy));
  (void)copy_text(policy->default_gpu, sizeof(policy->default_gpu), "system");
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

static int gpu_token_valid(const char *value) {
  if (strcmp(value, "system") == 0)
    return 1;
  if (strlen(value) != 20U || strncmp(value, "gpu-", 4) != 0)
    return 0;
  for (const char *cursor = value + 4; *cursor; cursor++)
    if (!(*cursor >= '0' && *cursor <= '9') &&
        !(*cursor >= 'a' && *cursor <= 'f'))
      return 0;
  return 1;
}

static int rule_token_valid(const char *value) {
  if (strncmp(value, "rule-", 5) != 0 || strlen(value) < 9U)
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

static int parse_policy_object(json_object *root, graphics_policy *policy) {
  if (!json_object_is_type(root, json_type_object) ||
      object_key_count(root) != 4U)
    return -1;
  char schema[64];
  if (json_exact_string(root, "schema", schema, sizeof(schema)) != 0 ||
      strcmp(schema, GRAPHICS_SCHEMA) != 0)
    return -1;
  json_object *generation = NULL;
  json_object *rules = NULL;
  if (!json_object_object_get_ex(root, "generation", &generation) ||
      !json_object_is_type(generation, json_type_int) ||
      json_object_get_int64(generation) < 0 ||
      json_object_get_int64(generation) > UINT_MAX ||
      json_exact_string(root, "defaultGpu", policy->default_gpu,
                        sizeof(policy->default_gpu)) != 0 ||
      !json_object_object_get_ex(root, "rules", &rules) ||
      !json_object_is_type(rules, json_type_array))
    return -1;
  policy->generation = (unsigned)json_object_get_int64(generation);
  if (!gpu_token_valid(policy->default_gpu))
    return -1;
  size_t length = json_object_array_length(rules);
  if (length > GRAPHICS_RULE_LIMIT)
    return -1;
  for (size_t i = 0; i < length; i++) {
    json_object *value = json_object_array_get_idx(rules, i);
    json_object *match = NULL;
    json_object *enabled = NULL;
    if (!json_object_is_type(value, json_type_object) ||
        object_key_count(value) != 4U ||
        json_exact_string(value, "id", policy->rules[i].id,
                          sizeof(policy->rules[i].id)) != 0 ||
        json_exact_string(value, "gpu", policy->rules[i].gpu,
                          sizeof(policy->rules[i].gpu)) != 0 ||
        !json_object_object_get_ex(value, "enabled", &enabled) ||
        !json_object_is_type(enabled, json_type_boolean) ||
        !json_object_object_get_ex(value, "match", &match) ||
        !json_object_is_type(match, json_type_object) ||
        object_key_count(match) != 2U ||
        json_exact_string(match, "type", policy->rules[i].type,
                          sizeof(policy->rules[i].type)) != 0 ||
        json_exact_string(match, "path", policy->rules[i].path,
                          sizeof(policy->rules[i].path)) != 0)
      return -1;
    if (strcmp(policy->rules[i].type, "executable") != 0 &&
        strcmp(policy->rules[i].type, "directory") != 0)
      return -1;
    if (!rule_token_valid(policy->rules[i].id) ||
        !gpu_token_valid(policy->rules[i].gpu) ||
        !normalized_policy_path(policy->rules[i].path))
      return -1;
    policy->rules[i].enabled = json_object_get_boolean(enabled) ? 1 : 0;
    for (size_t j = 0; j < i; j++)
      if (strcmp(policy->rules[j].id, policy->rules[i].id) == 0 ||
          (strcmp(policy->rules[j].type, policy->rules[i].type) == 0 &&
           strcmp(policy->rules[j].path, policy->rules[i].path) == 0))
        return -1;
  }
  policy->rule_count = length;
  return 0;
}

static json_object *policy_json(const graphics_policy *policy) {
  json_object *root = json_object_new_object();
  json_object_object_add(root, "schema",
                         json_object_new_string(GRAPHICS_SCHEMA));
  json_object_object_add(root, "generation",
                         json_object_new_int64(policy->generation));
  json_object_object_add(root, "defaultGpu",
                         json_object_new_string(policy->default_gpu));
  json_object *rules = json_object_new_array_ext((int)policy->rule_count);
  for (size_t i = 0; i < policy->rule_count; i++) {
    const graphics_rule *rule = &policy->rules[i];
    json_object *value = json_object_new_object();
    json_object_object_add(value, "id", json_object_new_string(rule->id));
    json_object *match = json_object_new_object();
    json_object_object_add(match, "type", json_object_new_string(rule->type));
    json_object_object_add(match, "path", json_object_new_string(rule->path));
    json_object_object_add(value, "match", match);
    json_object_object_add(value, "gpu", json_object_new_string(rule->gpu));
    json_object_object_add(value, "enabled",
                           json_object_new_boolean(rule->enabled));
    json_object_array_add(rules, value);
  }
  json_object_object_add(root, "rules", rules);
  return root;
}

static int load_policy(graphics_policy *policy, int *present) {
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
      stat_value.st_size <= 0 || stat_value.st_size > GRAPHICS_POLICY_LIMIT) {
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
    const char *canonical =
        json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    size_t canonical_length = strlen(canonical);
    size_t compare_size = size;
    if (compare_size && data[compare_size - 1U] == '\n')
      compare_size--;
    if (compare_size == canonical_length &&
        memcmp(data, canonical, canonical_length) == 0)
      status = 0;
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

static int save_policy(const graphics_policy *policy) {
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
  if (length + 1U > GRAPHICS_POLICY_LIMIT) {
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
  if (write_all(fd, serialized, length) != 0 || write_all(fd, "\n", 1U) != 0 ||
      fsync(fd) != 0)
    status = -1;
  int saved = errno;
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
    if (copy_text(parent, sizeof(parent), path) != 0)
      status = -1;
    else {
      char *slash = strrchr(parent, '/');
      if (!slash || slash == parent)
        status = -1;
      else {
        *slash = '\0';
        int directory_fd =
            open(parent, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        if (directory_fd < 0 || fsync(directory_fd) != 0)
          status = -1;
        if (directory_fd >= 0)
          close(directory_fd);
      }
    }
    if (status != 0)
      saved = errno;
  }
  if (status != 0)
    unlink(temporary);
  json_object_put(root);
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

static void print_policy(const graphics_policy *policy, int present, int json) {
  if (!json) {
    printf("Default application GPU: %s\n", policy->default_gpu);
    for (size_t i = 0; i < policy->rule_count; i++) {
      char shown[PATH_MAX];
      display_path(policy->rules[i].path, shown, sizeof(shown));
      printf("  %s  %s  %s -> %s\n", policy->rules[i].id, policy->rules[i].type,
             shown, policy->rules[i].gpu);
    }
    if (!present)
      puts("Policy file not created; system selection is active.");
    puts("Launch broker: not integrated");
    return;
  }
  json_object *root = policy_json(policy);
  json_object_object_del(root, "schema");
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.graphics-policy-view/v1"));
  json_object_object_add(root, "present", json_object_new_boolean(present));
  json_object_object_add(root, "enforcementAvailable",
                         json_object_new_boolean(0));
  json_object_object_add(
      root, "enforcementReason",
      json_object_new_string("launch-broker-not-integrated"));
  json_object_object_add(root, "runningProcessMigration",
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

static int gpu_valid(const graphics_inventory *inventory, const char *gpu) {
  return strcmp(gpu, "system") == 0 || find_gpu(inventory, gpu) != NULL;
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

static unsigned next_rule_number(const graphics_policy *policy) {
  unsigned maximum = 0;
  for (size_t i = 0; i < policy->rule_count; i++) {
    unsigned value = 0;
    char tail = '\0';
    if (sscanf(policy->rules[i].id, "rule-%u%c", &value, &tail) == 1 &&
        value > maximum)
      maximum = value;
  }
  return maximum + 1U;
}

static void print_policy_receipt(const char *action,
                                 const graphics_policy *policy, const char *gpu,
                                 const char *rule, int changed, int json) {
  if (!json) {
    printf("Graphics policy %s: %s%s\n", action,
           changed ? "applied" : "unchanged", rule ? rule : "");
    return;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.graphics-policy-receipt/v1"));
  json_object_object_add(root, "status", json_object_new_string("Applied"));
  json_object_object_add(root, "action", json_object_new_string(action));
  json_object_object_add(root, "generation",
                         json_object_new_int64(policy->generation));
  json_object_object_add(root, "changed", json_object_new_boolean(changed));
  if (gpu)
    json_object_object_add(root, "gpu", json_object_new_string(gpu));
  else
    json_object_object_add(root, "gpu", NULL);
  if (rule)
    json_object_object_add(root, "rule", json_object_new_string(rule));
  else
    json_object_object_add(root, "rule", NULL);
  json_object_object_add(root, "enforcementAvailable",
                         json_object_new_boolean(0));
  json_object_object_add(
      root, "enforcementReason",
      json_object_new_string("launch-broker-not-integrated"));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
}

static int path_is_within(const char *path, const char *directory) {
  size_t length = strlen(directory);
  if (strncmp(path, directory, length) != 0)
    return 0;
  if (directory[length - 1U] == '/')
    return 1;
  return path[length] == '/' || path[length] == '\0';
}

static int resolve_policy(int argc, char **argv) {
  const char *path = NULL;
  const char *format = "text";
  int seen_format = 0;
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--path") == 0 && i + 1 < argc && !path)
      path = argv[++i];
    else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc && !seen_format) {
      format = argv[++i];
      seen_format = 1;
    } else if (strcmp(argv[i], "--json") == 0 && !seen_format) {
      format = "json";
      seen_format = 1;
    } else
      return 2;
  }
  if (!path || (strcmp(format, "text") != 0 && strcmp(format, "json") != 0))
    return 2;
  char resolved[PATH_MAX];
  char *canonical = realpath(path, NULL);
  if (!canonical || copy_text(resolved, sizeof(resolved), canonical) != 0) {
    free(canonical);
    fputs("synapse-settings: path cannot be resolved\n", stderr);
    return 1;
  }
  free(canonical);
  graphics_policy policy;
  int present = 0;
  if (load_policy(&policy, &present) != 0) {
    fputs("synapse-settings: invalid graphics policy\n", stderr);
    return 1;
  }
  const graphics_rule *selected_rule = NULL;
  size_t selected_length = 0;
  for (size_t i = 0; i < policy.rule_count; i++) {
    const graphics_rule *rule = &policy.rules[i];
    if (!rule->enabled)
      continue;
    if (strcmp(rule->type, "executable") == 0 &&
        strcmp(rule->path, resolved) == 0) {
      selected_rule = rule;
      break;
    }
    if (strcmp(rule->type, "directory") == 0 &&
        path_is_within(resolved, rule->path)) {
      size_t length = strlen(rule->path);
      if (!selected_rule ||
          (selected_length != SIZE_MAX && length > selected_length)) {
        selected_rule = rule;
        selected_length = length;
      }
    }
  }
  const char *gpu_id = selected_rule ? selected_rule->gpu : policy.default_gpu;
  graphics_inventory inventory;
  if (scan_graphics(&inventory) != 0) {
    fputs("synapse-settings: graphics inventory unavailable\n", stderr);
    return 1;
  }
  const graphics_gpu *gpu = find_gpu(&inventory, gpu_id);
  int available = strcmp(gpu_id, "system") == 0 || gpu != NULL;
  const char *strategy = gpu ? gpu->strategy : "system-default";
  const char *source = selected_rule ? "rule" : "default";
  if (strcmp(format, "json") != 0) {
    printf("Selected GPU: %s (%s, %s)\n", gpu_id, source,
           available ? "available" : "unavailable");
    return 0;
  }
  json_object *root = json_object_new_object();
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.graphics-resolution/v1"));
  json_object_object_add(root, "selectedGpu", json_object_new_string(gpu_id));
  json_object_object_add(root, "source", json_object_new_string(source));
  if (selected_rule)
    json_object_object_add(root, "rule",
                           json_object_new_string(selected_rule->id));
  else
    json_object_object_add(root, "rule", NULL);
  json_object_object_add(root, "available", json_object_new_boolean(available));
  json_object_object_add(root, "strategy", json_object_new_string(strategy));
  json_object_object_add(root, "enforcementAvailable",
                         json_object_new_boolean(0));
  json_object_object_add(
      root, "enforcementReason",
      json_object_new_string("launch-broker-not-integrated"));
  json_object_object_add(root, "runningProcessMigration",
                         json_object_new_boolean(0));
  puts(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
  json_object_put(root);
  return 0;
}

static int policy_show(int argc, char **argv) {
  const char *format = NULL;
  if (parse_format(argc, argv, 3, &format) != 0)
    return 2;
  graphics_policy policy;
  int present = 0;
  if (load_policy(&policy, &present) != 0) {
    fputs("synapse-settings: invalid graphics policy\n", stderr);
    return 1;
  }
  print_policy(&policy, present, strcmp(format, "json") == 0);
  return 0;
}

static int policy_mutation(int argc, char **argv) {
  const char *action = argv[2];
  const char *gpu = NULL;
  const char *match = NULL;
  const char *path = NULL;
  const char *rule_id = NULL;
  const char *ack = NULL;
  const char *format = "text";
  int seen_format = 0;
  for (int i = 3; i < argc; i++) {
    const char **target = NULL;
    if (strcmp(argv[i], "--gpu") == 0)
      target = &gpu;
    else if (strcmp(argv[i], "--match") == 0)
      target = &match;
    else if (strcmp(argv[i], "--path") == 0)
      target = &path;
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
    } else
      return 2;
    if (i + 1 >= argc || *target)
      return 2;
    *target = argv[++i];
  }
  if (!ack || strcmp(ack, GRAPHICS_ACK) != 0 ||
      (strcmp(format, "text") != 0 && strcmp(format, "json") != 0))
    return 2;
  if (strcmp(action, "set-default") == 0) {
    if (!gpu || match || path || rule_id)
      return 2;
  } else if (strcmp(action, "add-rule") == 0) {
    if (!gpu || !match || !path || rule_id ||
        (strcmp(match, "executable") != 0 && strcmp(match, "directory") != 0))
      return 2;
  } else if (strcmp(action, "remove-rule") == 0) {
    if (!rule_id || gpu || match || path)
      return 2;
  } else
    return 2;

  graphics_inventory inventory;
  if (scan_graphics(&inventory) != 0) {
    fputs("synapse-settings: graphics inventory unavailable\n", stderr);
    return 1;
  }
  if (gpu && !gpu_valid(&inventory, gpu)) {
    fputs("synapse-settings: unknown GPU token\n", stderr);
    return 1;
  }
  graphics_policy policy;
  int present = 0;
  if (load_policy(&policy, &present) != 0) {
    fputs("synapse-settings: invalid graphics policy\n", stderr);
    return 1;
  }
  int changed = 0;
  const char *receipt_gpu = gpu;
  char receipt_rule[24];
  receipt_rule[0] = '\0';
  if (strcmp(action, "set-default") == 0) {
    if (!gpu)
      return 2;
    if (strcmp(policy.default_gpu, gpu) != 0) {
      if (copy_text(policy.default_gpu, sizeof(policy.default_gpu), gpu) != 0)
        return 1;
      changed = 1;
    }
  } else if (strcmp(action, "add-rule") == 0) {
    if (!gpu || !match || !path)
      return 2;
    if (policy.rule_count >= GRAPHICS_RULE_LIMIT) {
      fputs("synapse-settings: graphics rule limit reached\n", stderr);
      return 1;
    }
    graphics_rule *rule = &policy.rules[policy.rule_count];
    memset(rule, 0, sizeof(*rule));
    if (canonical_match_path(match, path, rule->path, sizeof(rule->path)) !=
        0) {
      fputs("synapse-settings: rule path must resolve to the requested type\n",
            stderr);
      return 1;
    }
    for (size_t i = 0; i < policy.rule_count; i++) {
      if (strcmp(policy.rules[i].type, match) == 0 &&
          strcmp(policy.rules[i].path, rule->path) == 0) {
        fputs("synapse-settings: duplicate graphics rule\n", stderr);
        return 1;
      }
    }
    unsigned number = next_rule_number(&policy);
    int written = snprintf(rule->id, sizeof(rule->id), "rule-%04u", number);
    if (written < 0 || (size_t)written >= sizeof(rule->id) ||
        copy_text(rule->type, sizeof(rule->type), match) != 0 ||
        copy_text(rule->gpu, sizeof(rule->gpu), gpu) != 0 ||
        copy_text(receipt_rule, sizeof(receipt_rule), rule->id) != 0)
      return 1;
    rule->enabled = 1;
    policy.rule_count++;
    changed = 1;
  } else {
    if (!rule_id)
      return 2;
    size_t found = policy.rule_count;
    for (size_t i = 0; i < policy.rule_count; i++)
      if (strcmp(policy.rules[i].id, rule_id) == 0)
        found = i;
    if (found == policy.rule_count) {
      fputs("synapse-settings: unknown graphics rule\n", stderr);
      return 1;
    }
    if (copy_text(receipt_rule, sizeof(receipt_rule), rule_id) != 0)
      return 1;
    receipt_gpu = policy.rules[found].gpu;
    memmove(&policy.rules[found], &policy.rules[found + 1U],
            (policy.rule_count - found - 1U) * sizeof(policy.rules[0]));
    policy.rule_count--;
    changed = 1;
  }
  if (changed) {
    if (policy.generation == UINT_MAX) {
      fputs("synapse-settings: graphics policy generation exhausted\n", stderr);
      return 1;
    }
    policy.generation++;
    if (save_policy(&policy) != 0) {
      perror("synapse-settings: save graphics policy");
      return 1;
    }
  }
  print_policy_receipt(action, &policy, receipt_gpu,
                       *receipt_rule ? receipt_rule : NULL, changed,
                       strcmp(format, "json") == 0);
  return 0;
}

int settings_graphics_command(int argc, char **argv) {
  if (argc < 2 || strcmp(argv[1], "--help") == 0 ||
      strcmp(argv[1], "-h") == 0) {
    graphics_usage(argc < 2 ? stderr : stdout);
    return argc < 2 ? 2 : 0;
  }
  if (strcmp(argv[1], "inventory") == 0) {
    const char *format = NULL;
    if (parse_format(argc, argv, 2, &format) != 0) {
      graphics_usage(stderr);
      return 2;
    }
    graphics_inventory inventory;
    if (scan_graphics(&inventory) != 0) {
      fputs("synapse-settings: graphics inventory unavailable\n", stderr);
      return 1;
    }
    print_graphics_inventory(&inventory, strcmp(format, "json") == 0);
    return 0;
  }
  if (strcmp(argv[1], "resolve") == 0) {
    int status = resolve_policy(argc, argv);
    if (status == 2)
      graphics_usage(stderr);
    return status;
  }
  if (strcmp(argv[1], "policy") != 0 || argc < 3) {
    graphics_usage(stderr);
    return 2;
  }
  if (strcmp(argv[2], "show") == 0) {
    int status = policy_show(argc, argv);
    if (status == 2)
      graphics_usage(stderr);
    return status;
  }
  int status = policy_mutation(argc, argv);
  if (status == 2)
    graphics_usage(stderr);
  return status;
}
