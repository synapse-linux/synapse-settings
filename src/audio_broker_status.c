// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "settings_internal.h"

#include <json-c/json.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define BROKER_RUNTIME_DIRECTORY "synapse"
#define BROKER_RUNTIME_LOCK "audio-route-broker-v1.lock"
#define BROKER_RUNTIME_SOCKET "audio-route-broker-v1.sock"
#define BROKER_STATUS_REQUEST "status-v1\n"
#define BROKER_STATUS_LIMIT 4096U
#define BROKER_IO_TIMEOUT_MS 250

static const char *const status_reasons[] = {
    "broker-not-running",    "audio-unavailable",
    "unavailable",           "timeout",
    "invalid-response",      "stream-limit",
    "policy-unavailable",    "subscriber-unavailable",
    "subscriber-ended",      "invalid-subscriber-event",
    "broker-internal-error", "runtime-unavailable",
    "runtime-state-invalid", "broker-already-running",
};

static void status_assign(settings_audio_broker_status *status,
                          const char *state, int active, const char *reason,
                          unsigned generation, size_t baseline_count) {
  memset(status, 0, sizeof(*status));
  (void)snprintf(status->status, sizeof(status->status), "%s", state);
  if (reason)
    (void)snprintf(status->reason, sizeof(status->reason), "%s", reason);
  status->active = active;
  status->enforcement_available = active;
  status->policy_generation = generation;
  status->baseline_streams = baseline_count;
}

void settings_audio_broker_status_ready(settings_audio_broker_status *status,
                                        int active, unsigned generation,
                                        size_t baseline_count) {
  status_assign(status, "Ready", active, active ? NULL : "broker-not-running",
                generation, baseline_count);
}

void settings_audio_broker_status_unavailable(
    settings_audio_broker_status *status, const char *reason,
    unsigned generation, size_t baseline_count) {
  status_assign(status, "Unavailable", 0,
                reason && *reason ? reason : "unavailable", generation,
                baseline_count);
}

static int reason_allowed(const char *reason) {
  if (!reason)
    return 0;
  for (size_t index = 0;
       index < sizeof(status_reasons) / sizeof(status_reasons[0]); index++)
    if (strcmp(reason, status_reasons[index]) == 0)
      return 1;
  return 0;
}

static int status_valid(const settings_audio_broker_status *status) {
  if (!status ||
      status->baseline_streams > SETTINGS_AUDIO_BROKER_STREAM_LIMIT ||
      (status->reason[0] && !reason_allowed(status->reason)))
    return 0;
  if (strcmp(status->status, "Ready") == 0) {
    if (status->active)
      return status->enforcement_available && !status->reason[0];
    return !status->enforcement_available &&
           strcmp(status->reason, "broker-not-running") == 0;
  }
  if (strcmp(status->status, "Unavailable") == 0)
    return !status->active && !status->enforcement_available &&
           status->reason[0] &&
           strcmp(status->reason, "broker-not-running") != 0;
  return 0;
}

static json_object *status_object(const settings_audio_broker_status *status) {
  if (!status_valid(status))
    return NULL;
  json_object *root = json_object_new_object();
  if (!root)
    return NULL;
  json_object_object_add(
      root, "schema",
      json_object_new_string("synapse.settings.audio-route-broker-status/v1"));
  json_object_object_add(root, "status",
                         json_object_new_string(status->status));
  json_object_object_add(root, "mode",
                         json_object_new_string("new-streams-only"));
  json_object_object_add(root, "stateAuthority",
                         json_object_new_string("pipewire-pulse-model"));
  json_object_object_add(root, "capable", json_object_new_boolean(1));
  json_object_object_add(root, "active",
                         json_object_new_boolean(status->active));
  json_object_object_add(
      root, "enforcementAvailable",
      json_object_new_boolean(status->enforcement_available));
  json_object_object_add(root, "reason",
                         status->reason[0]
                             ? json_object_new_string(status->reason)
                             : json_object_new_null());
  json_object_object_add(
      root, "policyGeneration",
      json_object_new_int64((int64_t)status->policy_generation));
  json_object_object_add(
      root, "baselineStreams",
      json_object_new_int64((int64_t)status->baseline_streams));
  json_object_object_add(root, "persistentPidRules",
                         json_object_new_boolean(0));
  json_object_object_add(root, "existingStreamMigration",
                         json_object_new_boolean(0));
  json_object_object_add(root, "bounded", json_object_new_boolean(1));
  return root;
}

static int status_serialize(const settings_audio_broker_status *status,
                            char *buffer, size_t size, size_t *length) {
  json_object *root = status_object(status);
  if (!root)
    return -1;
  const char *encoded =
      json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
  size_t encoded_length = encoded ? strlen(encoded) : 0;
  int result = -1;
  if (encoded && encoded_length > 0 && encoded_length < size &&
      encoded_length <= BROKER_STATUS_LIMIT) {
    memcpy(buffer, encoded, encoded_length + 1U);
    if (length)
      *length = encoded_length;
    result = 0;
  }
  json_object_put(root);
  return result;
}

int settings_audio_broker_status_print(
    const settings_audio_broker_status *status, const char *format) {
  if (!status_valid(status) || !format)
    return -1;
  if (strcmp(format, "json") == 0) {
    char buffer[BROKER_STATUS_LIMIT + 1U];
    if (status_serialize(status, buffer, sizeof(buffer), NULL) != 0)
      return -1;
    puts(buffer);
  } else if (strcmp(format, "text") == 0) {
    if (status->active)
      printf("Audio route broker active; baseline streams: %zu; policy "
             "generation: %u\n",
             status->baseline_streams, status->policy_generation);
    else if (strcmp(status->status, "Ready") == 0)
      puts("Audio route broker capable, inactive");
    else
      printf("Audio route broker unavailable: %s\n", status->reason);
  } else {
    return -1;
  }
  if (fflush(stdout) != 0)
    return -1;
  return ferror(stdout) ? -1 : 0;
}

static int safe_runtime_base(int *base_fd) {
  const char *path = getenv("XDG_RUNTIME_DIR");
  if (!path || path[0] != '/' || strlen(path) >= SETTINGS_FIELD_LIMIT)
    return -1;
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  if (fd < 0)
    return -1;
  struct stat state;
  if (fstat(fd, &state) != 0 || !S_ISDIR(state.st_mode) ||
      state.st_uid != getuid() || (state.st_mode & 0777U) != 0700U) {
    close(fd);
    errno = EPERM;
    return -1;
  }
  *base_fd = fd;
  return 0;
}

static int safe_runtime_directory(int base_fd, int create, int *directory_fd) {
  if (create && mkdirat(base_fd, BROKER_RUNTIME_DIRECTORY, 0700) != 0 &&
      errno != EEXIST)
    return -1;
  int fd = openat(base_fd, BROKER_RUNTIME_DIRECTORY,
                  O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  if (fd < 0)
    return -1;
  struct stat state;
  if (fstat(fd, &state) != 0 || !S_ISDIR(state.st_mode) ||
      state.st_uid != getuid() || (state.st_mode & 0777U) != 0700U) {
    close(fd);
    errno = EPERM;
    return -1;
  }
  *directory_fd = fd;
  return 0;
}

static int runtime_socket_path(char *path, size_t size) {
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (!runtime || runtime[0] != '/')
    return -1;
  int written = snprintf(path, size, "%s/%s/%s", runtime,
                         BROKER_RUNTIME_DIRECTORY, BROKER_RUNTIME_SOCKET);
  if (written < 0 || (size_t)written >= size ||
      (size_t)written >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

int settings_audio_broker_runtime_acquire(
    settings_audio_broker_runtime *runtime, const char **reason) {
  if (!runtime)
    return -1;
  memset(runtime, 0, sizeof(*runtime));
  runtime->directory_fd = -1;
  runtime->lock_fd = -1;
  runtime->socket_fd = -1;
  int base_fd = -1;
  if (safe_runtime_base(&base_fd) != 0 ||
      safe_runtime_directory(base_fd, 1, &runtime->directory_fd) != 0) {
    if (base_fd >= 0)
      close(base_fd);
    if (reason)
      *reason = "runtime-unavailable";
    return -1;
  }
  close(base_fd);
  runtime->lock_fd = openat(runtime->directory_fd, BROKER_RUNTIME_LOCK,
                            O_RDWR | O_CLOEXEC | O_CREAT | O_NOFOLLOW, 0600);
  if (runtime->lock_fd < 0) {
    if (reason)
      *reason = "runtime-state-invalid";
    settings_audio_broker_runtime_release(runtime);
    return -1;
  }
  struct stat state;
  if (fstat(runtime->lock_fd, &state) != 0 || !S_ISREG(state.st_mode) ||
      state.st_uid != getuid() || (state.st_mode & 0777U) != 0600U) {
    if (reason)
      *reason = "runtime-state-invalid";
    settings_audio_broker_runtime_release(runtime);
    return -1;
  }
  if (flock(runtime->lock_fd, LOCK_EX | LOCK_NB) != 0) {
    if (reason)
      *reason = errno == EWOULDBLOCK ? "broker-already-running"
                                     : "runtime-state-invalid";
    settings_audio_broker_runtime_release(runtime);
    return -1;
  }
  runtime->lock_held = 1;
  if (unlinkat(runtime->directory_fd, BROKER_RUNTIME_SOCKET, 0) != 0 &&
      errno != ENOENT) {
    if (reason)
      *reason = "runtime-state-invalid";
    settings_audio_broker_runtime_release(runtime);
    return -1;
  }
  return 0;
}

int settings_audio_broker_runtime_listen(settings_audio_broker_runtime *runtime,
                                         const char **reason) {
  if (!runtime || runtime->directory_fd < 0 || !runtime->lock_held ||
      runtime->socket_fd >= 0)
    return -1;
  char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
  if (runtime_socket_path(path, sizeof(path)) != 0) {
    if (reason)
      *reason = "runtime-unavailable";
    return -1;
  }
  int socket_fd =
      socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (socket_fd < 0) {
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  memcpy(address.sun_path, path, strlen(path) + 1U);
  socklen_t address_length =
      (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1U);
  if (bind(socket_fd, (const struct sockaddr *)&address, address_length) != 0 ||
      chmod(path, 0600) != 0 || listen(socket_fd, 4) != 0) {
    close(socket_fd);
    (void)unlinkat(runtime->directory_fd, BROKER_RUNTIME_SOCKET, 0);
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  struct stat state;
  if (fstatat(runtime->directory_fd, BROKER_RUNTIME_SOCKET, &state,
              AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISSOCK(state.st_mode) || state.st_uid != getuid() ||
      (state.st_mode & 0777U) != 0600U) {
    close(socket_fd);
    (void)unlinkat(runtime->directory_fd, BROKER_RUNTIME_SOCKET, 0);
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  runtime->socket_fd = socket_fd;
  return 0;
}

void settings_audio_broker_runtime_release(
    settings_audio_broker_runtime *runtime) {
  if (!runtime)
    return;
  if (runtime->socket_fd >= 0)
    close(runtime->socket_fd);
  runtime->socket_fd = -1;
  if (runtime->directory_fd >= 0 && runtime->lock_held)
    (void)unlinkat(runtime->directory_fd, BROKER_RUNTIME_SOCKET, 0);
  if (runtime->lock_fd >= 0) {
    if (runtime->lock_held)
      (void)flock(runtime->lock_fd, LOCK_UN);
    close(runtime->lock_fd);
  }
  runtime->lock_fd = -1;
  runtime->lock_held = 0;
  if (runtime->directory_fd >= 0)
    close(runtime->directory_fd);
  runtime->directory_fd = -1;
}

int settings_audio_broker_runtime_fd(
    const settings_audio_broker_runtime *runtime) {
  return runtime ? runtime->socket_fd : -1;
}

static int monotonic_milliseconds(int64_t *milliseconds) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    return -1;
  *milliseconds = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
  return 0;
}

static int wait_fd(int fd, short events, int64_t deadline) {
  for (;;) {
    int64_t now = 0;
    if (monotonic_milliseconds(&now) != 0)
      return -1;
    int64_t remaining = deadline - now;
    if (remaining <= 0) {
      errno = ETIMEDOUT;
      return -1;
    }
    struct pollfd descriptor = {fd, events, 0};
    int ready = poll(&descriptor, 1,
                     remaining > INT32_MAX ? INT32_MAX : (int)remaining);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready <= 0) {
      if (ready == 0)
        errno = ETIMEDOUT;
      return -1;
    }
    if (descriptor.revents & events)
      return 0;
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      errno = EIO;
      return -1;
    }
  }
}

static int send_all(int fd, const char *data, size_t length, int64_t deadline) {
  size_t sent = 0;
  while (sent < length) {
    if (wait_fd(fd, POLLOUT, deadline) != 0)
      return -1;
    ssize_t count = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return -1;
    sent += (size_t)count;
  }
  return 0;
}

static int receive_request(int fd, int64_t deadline) {
  char request[sizeof(BROKER_STATUS_REQUEST)];
  size_t used = 0;
  const size_t expected = strlen(BROKER_STATUS_REQUEST);
  while (used < expected) {
    if (wait_fd(fd, POLLIN, deadline) != 0)
      return -1;
    ssize_t count = recv(fd, request + used, expected - used, 0);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return -1;
    used += (size_t)count;
  }
  if (memcmp(request, BROKER_STATUS_REQUEST, expected) != 0)
    return -1;
  char extra = 0;
  ssize_t extra_count = recv(fd, &extra, 1, MSG_DONTWAIT | MSG_PEEK);
  if (extra_count > 0)
    return -1;
  if (extra_count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    return -1;
  return 0;
}

static int status_from_json(const char *payload, size_t length,
                            settings_audio_broker_status *status) {
  if (!payload || !status || length == 0 || length > BROKER_STATUS_LIMIT ||
      payload[0] != '{' || payload[length - 1U] != '}' ||
      memchr(payload, '\0', length) || memchr(payload, '\n', length) ||
      memchr(payload, '\r', length))
    return -1;
  struct json_tokener *tokener = json_tokener_new();
  if (!tokener)
    return -1;
  json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
  json_object *root = json_tokener_parse_ex(tokener, payload, (int)length);
  enum json_tokener_error parse_error = json_tokener_get_error(tokener);
  size_t parse_end = json_tokener_get_parse_end(tokener);
  json_tokener_free(tokener);
  if (parse_error != json_tokener_success || parse_end != length || !root ||
      !json_object_is_type(root, json_type_object) ||
      json_object_object_length(root) != 13U) {
    if (root)
      json_object_put(root);
    return -1;
  }

  json_object *schema = NULL;
  json_object *state = NULL;
  json_object *mode = NULL;
  json_object *authority = NULL;
  json_object *capable = NULL;
  json_object *active = NULL;
  json_object *enforcement = NULL;
  json_object *reason = NULL;
  json_object *generation = NULL;
  json_object *baseline = NULL;
  json_object *pid_rules = NULL;
  json_object *existing = NULL;
  json_object *bounded = NULL;
  int fields_valid =
      json_object_object_get_ex(root, "schema", &schema) &&
      json_object_object_get_ex(root, "status", &state) &&
      json_object_object_get_ex(root, "mode", &mode) &&
      json_object_object_get_ex(root, "stateAuthority", &authority) &&
      json_object_object_get_ex(root, "capable", &capable) &&
      json_object_object_get_ex(root, "active", &active) &&
      json_object_object_get_ex(root, "enforcementAvailable", &enforcement) &&
      json_object_object_get_ex(root, "reason", &reason) &&
      json_object_object_get_ex(root, "policyGeneration", &generation) &&
      json_object_object_get_ex(root, "baselineStreams", &baseline) &&
      json_object_object_get_ex(root, "persistentPidRules", &pid_rules) &&
      json_object_object_get_ex(root, "existingStreamMigration", &existing) &&
      json_object_object_get_ex(root, "bounded", &bounded) &&
      json_object_is_type(schema, json_type_string) &&
      strcmp(json_object_get_string(schema),
             "synapse.settings.audio-route-broker-status/v1") == 0 &&
      json_object_is_type(state, json_type_string) &&
      json_object_is_type(mode, json_type_string) &&
      strcmp(json_object_get_string(mode), "new-streams-only") == 0 &&
      json_object_is_type(authority, json_type_string) &&
      strcmp(json_object_get_string(authority), "pipewire-pulse-model") == 0 &&
      json_object_is_type(capable, json_type_boolean) &&
      json_object_get_boolean(capable) &&
      json_object_is_type(active, json_type_boolean) &&
      json_object_is_type(enforcement, json_type_boolean) &&
      json_object_is_type(generation, json_type_int) &&
      json_object_get_int64(generation) >= 0 &&
      (uint64_t)json_object_get_int64(generation) <= UINT32_MAX &&
      json_object_is_type(baseline, json_type_int) &&
      json_object_get_int64(baseline) >= 0 &&
      (uint64_t)json_object_get_int64(baseline) <=
          SETTINGS_AUDIO_BROKER_STREAM_LIMIT &&
      json_object_is_type(pid_rules, json_type_boolean) &&
      !json_object_get_boolean(pid_rules) &&
      json_object_is_type(existing, json_type_boolean) &&
      !json_object_get_boolean(existing) &&
      json_object_is_type(bounded, json_type_boolean) &&
      json_object_get_boolean(bounded);
  settings_audio_broker_status decoded;
  memset(&decoded, 0, sizeof(decoded));
  if (fields_valid) {
    const char *state_text = json_object_get_string(state);
    const char *reason_text = json_object_is_type(reason, json_type_string)
                                  ? json_object_get_string(reason)
                                  : NULL;
    if (json_object_is_type(reason, json_type_null) || reason_text) {
      status_assign(&decoded, state_text, json_object_get_boolean(active),
                    reason_text, (unsigned)json_object_get_int64(generation),
                    (size_t)json_object_get_int64(baseline));
      decoded.enforcement_available = json_object_get_boolean(enforcement);
    }
  }
  int valid = fields_valid && status_valid(&decoded);
  if (valid) {
    char canonical[BROKER_STATUS_LIMIT + 1U];
    size_t canonical_length = 0;
    valid = status_serialize(&decoded, canonical, sizeof(canonical),
                             &canonical_length) == 0 &&
            canonical_length == length &&
            memcmp(canonical, payload, length) == 0;
  }
  json_object_put(root);
  if (!valid)
    return -1;
  *status = decoded;
  return 0;
}

int settings_audio_broker_runtime_serve(settings_audio_broker_runtime *runtime,
                                        unsigned generation,
                                        size_t baseline_count) {
  if (!runtime || runtime->socket_fd < 0)
    return -1;
  int client =
      accept4(runtime->socket_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (client < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return 0;
  if (client < 0)
    return -1;
  struct ucred credentials;
  socklen_t credentials_size = sizeof(credentials);
  if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials,
                 &credentials_size) != 0 ||
      credentials_size != sizeof(credentials) || credentials.uid != getuid()) {
    close(client);
    return 0;
  }
  int64_t now = 0;
  if (monotonic_milliseconds(&now) != 0 ||
      receive_request(client, now + BROKER_IO_TIMEOUT_MS) != 0) {
    close(client);
    return 0;
  }
  settings_audio_broker_status status;
  unsigned fresh_generation = generation;
  int present = 0;
  if (settings_audio_route_policy_state(&fresh_generation, &present) != 0)
    settings_audio_broker_status_unavailable(&status, "policy-unavailable",
                                             generation, baseline_count);
  else
    settings_audio_broker_status_ready(&status, 1, fresh_generation,
                                       baseline_count);
  (void)present;
  char response[BROKER_STATUS_LIMIT + 2U];
  size_t response_length = 0;
  if (status_serialize(&status, response, sizeof(response) - 1U,
                       &response_length) == 0) {
    response[response_length++] = '\n';
    if (monotonic_milliseconds(&now) == 0)
      (void)send_all(client, response, response_length,
                     now + BROKER_IO_TIMEOUT_MS);
  }
  close(client);
  return 0;
}

static int connect_runtime_socket(int *socket_fd, int *absent,
                                  const char **reason) {
  *socket_fd = -1;
  *absent = 0;
  int base_fd = -1;
  if (safe_runtime_base(&base_fd) != 0) {
    if (reason)
      *reason = "runtime-unavailable";
    return -1;
  }
  int directory_fd = -1;
  if (safe_runtime_directory(base_fd, 0, &directory_fd) != 0) {
    int saved = errno;
    close(base_fd);
    if (saved == ENOENT) {
      *absent = 1;
      return 0;
    }
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  close(base_fd);
  struct stat state;
  if (fstatat(directory_fd, BROKER_RUNTIME_SOCKET, &state,
              AT_SYMLINK_NOFOLLOW) != 0) {
    int saved = errno;
    close(directory_fd);
    if (saved == ENOENT) {
      *absent = 1;
      return 0;
    }
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  if (!S_ISSOCK(state.st_mode) || state.st_uid != getuid() ||
      (state.st_mode & 0777U) != 0600U) {
    close(directory_fd);
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  close(directory_fd);
  char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
  if (runtime_socket_path(path, sizeof(path)) != 0) {
    if (reason)
      *reason = "runtime-unavailable";
    return -1;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  memcpy(address.sun_path, path, strlen(path) + 1U);
  socklen_t address_length =
      (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1U);
  int connect_result =
      connect(fd, (const struct sockaddr *)&address, address_length);
  int connect_error = connect_result == 0 ? 0 : errno;
  if (connect_result != 0 && connect_error != EINPROGRESS) {
    close(fd);
    if (connect_error == ENOENT || connect_error == ECONNREFUSED) {
      *absent = 1;
      return 0;
    }
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  if (connect_error == EINPROGRESS) {
    int64_t now = 0;
    if (monotonic_milliseconds(&now) != 0) {
      close(fd);
      if (reason)
        *reason = "runtime-state-invalid";
      return -1;
    }
    if (wait_fd(fd, POLLOUT, now + BROKER_IO_TIMEOUT_MS) != 0) {
      int saved = errno;
      close(fd);
      if (reason)
        *reason = saved == ETIMEDOUT ? "timeout" : "runtime-state-invalid";
      return -1;
    }
    int socket_error = 0;
    socklen_t error_size = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) != 0 ||
        socket_error != 0) {
      close(fd);
      if (socket_error == ENOENT || socket_error == ECONNREFUSED) {
        *absent = 1;
        return 0;
      }
      if (reason)
        *reason = "runtime-state-invalid";
      return -1;
    }
  }
  struct ucred credentials;
  socklen_t credentials_size = sizeof(credentials);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials,
                 &credentials_size) != 0 ||
      credentials_size != sizeof(credentials) || credentials.uid != getuid()) {
    close(fd);
    if (reason)
      *reason = "runtime-state-invalid";
    return -1;
  }
  *socket_fd = fd;
  return 0;
}

int settings_audio_broker_status_query(settings_audio_broker_status *status) {
  if (!status)
    return -1;
  settings_audio_broker_status_ready(status, 0, 0, 0);
  int fd = -1;
  int absent = 0;
  const char *reason = NULL;
  if (connect_runtime_socket(&fd, &absent, &reason) != 0) {
    settings_audio_broker_status_unavailable(
        status, reason ? reason : "runtime-state-invalid", 0, 0);
    return 0;
  }
  if (absent)
    return 0;
  int64_t now = 0;
  if (monotonic_milliseconds(&now) != 0) {
    close(fd);
    settings_audio_broker_status_unavailable(status, "invalid-response", 0, 0);
    return 0;
  }
  if (send_all(fd, BROKER_STATUS_REQUEST, strlen(BROKER_STATUS_REQUEST),
               now + BROKER_IO_TIMEOUT_MS) != 0) {
    int saved = errno;
    close(fd);
    settings_audio_broker_status_unavailable(
        status, saved == ETIMEDOUT ? "timeout" : "invalid-response", 0, 0);
    return 0;
  }
  char response[BROKER_STATUS_LIMIT + 2U];
  size_t used = 0;
  int complete = 0;
  if (monotonic_milliseconds(&now) != 0) {
    close(fd);
    settings_audio_broker_status_unavailable(status, "invalid-response", 0, 0);
    return 0;
  }
  int64_t deadline = now + BROKER_IO_TIMEOUT_MS;
  int timed_out = 0;
  while (used < sizeof(response) - 1U) {
    if (wait_fd(fd, POLLIN, deadline) != 0) {
      timed_out = errno == ETIMEDOUT;
      break;
    }
    ssize_t count = recv(fd, response + used, sizeof(response) - 1U - used, 0);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      break;
    size_t previous = used;
    used += (size_t)count;
    char *newline = memchr(response + previous, '\n', (size_t)count);
    if (newline) {
      if ((size_t)(newline - response) + 1U != used)
        used = sizeof(response);
      else
        complete = 1;
      break;
    }
  }
  close(fd);
  if (!complete || used < 2U || used > BROKER_STATUS_LIMIT + 1U ||
      response[used - 1U] != '\n' ||
      status_from_json(response, used - 1U, status) != 0 ||
      (strcmp(status->status, "Ready") == 0 && !status->active)) {
    settings_audio_broker_status_unavailable(
        status, timed_out ? "timeout" : "invalid-response", 0, 0);
  }
  return 0;
}

int settings_audio_broker_status_command(int argc, char **argv) {
  const char *format = "text";
  if (argc == 4 && strcmp(argv[2], "--format") == 0)
    format = argv[3];
  else if (argc != 2)
    return 2;
  if (strcmp(format, "text") != 0 && strcmp(format, "json") != 0)
    return 2;
  settings_audio_broker_status status;
  if (settings_audio_broker_status_query(&status) != 0 ||
      settings_audio_broker_status_print(&status, format) != 0)
    return 1;
  return 0;
}
