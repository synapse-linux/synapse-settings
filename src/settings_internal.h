// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SYNAPSE_SETTINGS_INTERNAL_H
#define SYNAPSE_SETTINGS_INTERNAL_H

#include <stddef.h>
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

#endif
