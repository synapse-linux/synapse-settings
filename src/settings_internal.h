// SPDX-License-Identifier: GPL-3.0-or-later
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

int settings_audio_command(int argc, char **argv);
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
int settings_audio_broker_command(int argc, char **argv);

#endif
