// SPDX-License-Identifier: MIT
#ifndef SYNAPSE_SETTINGS_INTERNAL_H
#define SYNAPSE_SETTINGS_INTERNAL_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SETTINGS_RECORD_LIMIT 65536U
#define SETTINGS_FIELD_LIMIT 4096U
#define SETTINGS_LINE_LIMIT (64U * 1024U)
#define SETTINGS_CAPTURE_LIMIT ((size_t)1024U * 1024U)
#define SETTINGS_CAPTURE_TIMEOUT_MS 2000
#define SETTINGS_AUDIO_BROKER_STREAM_LIMIT 128U

int settings_audio_command(int argc, char **argv);
#ifdef SYNAPSE_SETTINGS_WITH_GOXLR_STATUS
int settings_audio_goxlr_status_command(int argc, char **argv);
int settings_audio_goxlr_control_command(int argc, char **argv);
#endif
int settings_audio_route_command(int argc, char **argv);
int settings_audio_policy_target(const char *direction, const char *requested,
                                 char *target, size_t target_size,
                                 int *available);
int settings_audio_stream_executable(const char *stream_id, char *direction,
                                     size_t direction_size, char *path,
                                     size_t path_size);

typedef struct {
  unsigned generation;
  int present;
  int matched;
  char device[32];
  char rule[24];
  char source[24];
} settings_audio_route_selection;

typedef struct {
  char status[16];
  char reason[48];
  unsigned policy_generation;
  size_t baseline_streams;
  int active;
  int enforcement_available;
} settings_audio_broker_status;

typedef struct {
  int directory_fd;
  int lock_fd;
  int socket_fd;
  int lock_held;
} settings_audio_broker_runtime;

typedef struct {
  char status[32];
  char reason[48];
  char stream[32];
  char direction[8];
  char device[32];
  char rule[24];
  char source[24];
  unsigned policy_generation;
  int changed;
  int routing_applied;
  int verified;
  int compensation_attempted;
  int compensation_verified;
} settings_audio_broker_receipt;

int settings_audio_route_select(const char *canonical_path,
                                const char *direction,
                                settings_audio_route_selection *selection);
int settings_audio_route_policy_state(unsigned *generation, int *present);
int settings_audio_broker_stream_ids(char ids[][32], size_t capacity,
                                     size_t *count, const char **reason);
int settings_audio_broker_apply_new(const char *stream_id,
                                    settings_audio_broker_receipt *receipt);
void settings_audio_broker_status_ready(settings_audio_broker_status *status,
                                        int active, unsigned generation,
                                        size_t baseline_count);
void settings_audio_broker_status_unavailable(
    settings_audio_broker_status *status, const char *reason,
    unsigned generation, size_t baseline_count);
int settings_audio_broker_status_print(
    const settings_audio_broker_status *status, const char *format);
int settings_audio_broker_status_query(settings_audio_broker_status *status);
int settings_audio_broker_status_command(int argc, char **argv);
int settings_audio_broker_runtime_acquire(
    settings_audio_broker_runtime *runtime, const char **reason);
int settings_audio_broker_runtime_listen(settings_audio_broker_runtime *runtime,
                                         const char **reason);
void settings_audio_broker_runtime_release(
    settings_audio_broker_runtime *runtime);
int settings_audio_broker_runtime_fd(
    const settings_audio_broker_runtime *runtime);
int settings_audio_broker_runtime_serve(settings_audio_broker_runtime *runtime,
                                        unsigned generation,
                                        size_t baseline_count);
int settings_audio_broker_command(int argc, char **argv);

#endif
