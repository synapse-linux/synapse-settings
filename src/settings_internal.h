// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SYNAPSE_SETTINGS_INTERNAL_H
#define SYNAPSE_SETTINGS_INTERNAL_H

#include <stdio.h>

#define SETTINGS_RECORD_LIMIT 65536U
#define SETTINGS_FIELD_LIMIT 4096U
#define SETTINGS_LINE_LIMIT (64U * 1024U)
#define SETTINGS_CAPTURE_LIMIT (1024U * 1024U)
#define SETTINGS_CAPTURE_TIMEOUT_MS 2000

int settings_audio_command(int argc, char **argv);
int settings_graphics_command(int argc, char **argv);

#endif
