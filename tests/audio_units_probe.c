// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "audio_private.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef SYNAPSE_SETTINGS_TEST_HOOKS
#error "Audio unit probe requires SYNAPSE_SETTINGS_TEST_HOOKS"
#endif

static unsigned checks;
#define CHECK(expression)                                                      \
  do {                                                                         \
    assert(expression);                                                        \
    checks++;                                                                  \
  } while (0)

int main(void) {
  char text[32];
  memset(text, '!', sizeof(text));
  CHECK(audio_copy_bounded(text, sizeof(text), "opaque") == 0);
  CHECK(strcmp(text, "opaque") == 0);
  CHECK(audio_copy_bounded(text, sizeof(text), NULL) == 0 && text[0] == '\0');
  strcpy(text, "keep");
  errno = 0;
  CHECK(audio_copy_bounded(text, 4, "four") == -1 && errno == E2BIG);
  CHECK(strcmp(text, "keep") == 0);
  CHECK(audio_copy_label(text, sizeof(text), "a\nb\tc\177") == 0);
  CHECK(strcmp(text, "a b c ") == 0);
  CHECK(audio_copy_label(text, sizeof(text), NULL) == 0 && text[0] == '\0');
  CHECK(!audio_raw_identity_valid(NULL));
  CHECK(!audio_raw_identity_valid(""));
  CHECK(audio_raw_identity_valid("device.\n\t\303\251"));
  char raw[257];
  memset(raw, 'a', 255);
  raw[255] = '\0';
  CHECK(audio_raw_identity_valid(raw));
  raw[255] = 'a';
  raw[256] = '\0';
  CHECK(!audio_raw_identity_valid(raw));
  for (unsigned byte = 1; byte < 256; byte++) {
    char one[] = {(char)byte, '\0'};
    CHECK(audio_raw_identity_valid(one) == (byte < 128));
  }
  const char *bad_utf8[] = {"\300\200",         "\302",
                            "\340\237\277",     "\355\240\200",
                            "\360\217\277\277", "\364\220\200\200",
                            "\365\200\200\200"};
  for (size_t i = 0; i < sizeof(bad_utf8) / sizeof(*bad_utf8); i++)
    CHECK(!audio_raw_identity_valid(bad_utf8[i]));
  const char *good_utf8[] = {
      "\302\200",     "\337\277",         "\340\240\200",    "\355\237\277",
      "\356\200\200", "\360\220\200\200", "\364\217\277\277"};
  for (size_t i = 0; i < sizeof(good_utf8) / sizeof(*good_utf8); i++)
    CHECK(audio_raw_identity_valid(good_utf8[i]));

  json_object *object =
      json_tokener_parse("{\"s\":\"ok\",\"null\":null,\"n\":7,\"nul\":"
                         "\"a\\u0000b\",\"index\":2147483647}");
  CHECK(object != NULL);
  int valid = 7;
  CHECK(strcmp(audio_json_bounded_string(object, "s", 0, 2, &valid), "ok") ==
            0 &&
        valid);
  CHECK(!audio_json_bounded_string(object, "s", 0, 1, &valid) && !valid);
  CHECK(!audio_json_bounded_string(object, "null", 1, 2, &valid) && valid);
  CHECK(!audio_json_bounded_string(object, "null", 0, 2, &valid) && !valid);
  CHECK(!audio_json_optional_bounded_string(object, "null", 2, &valid) &&
        !valid);
  CHECK(!audio_json_optional_bounded_string(object, "missing", 2, &valid) &&
        valid);
  CHECK(!audio_json_bounded_string(object, "missing", 0, 2, &valid) && !valid);
  CHECK(!audio_json_bounded_string(object, "missing", 1, 2, &valid) && valid);
  CHECK(!audio_json_bounded_string(object, "n", 0, 2, &valid) && !valid);
  CHECK(!audio_json_bounded_string(object, "nul", 0, 4, &valid) && !valid);
  int index = -7;
  CHECK(audio_json_index(object, "index", &index) == 0 && index == INT_MAX);
  CHECK(audio_json_index(object, "n", &index) == 0 && index == 7);
  CHECK(audio_json_index(object, "s", &index) == -1 && index == 7);
  CHECK(audio_json_index(object, "missing", &index) == -1 && index == 7);
  CHECK(audio_json_index(NULL, "n", &index) == -1 && index == 7);
  int monitor = -1;
  CHECK(audio_source_is_monitor(object, &monitor) == 0 && monitor == 0);
  json_object_object_add(object, "monitor_source",
                         json_object_new_string("sink.a"));
  CHECK(audio_source_is_monitor(object, &monitor) == 0 && monitor == 1);
  json_object_object_add(object, "monitor_source", json_object_new_string(""));
  json_object_object_add(object, "monitor_of_sink", json_object_new_int(0));
  CHECK(audio_source_is_monitor(object, &monitor) == 0 && monitor == 1);
  json_object_object_add(object, "monitor_of_sink", json_object_new_int(-1));
  CHECK(audio_source_is_monitor(object, &monitor) == -1);
  json_object_put(object);

  CHECK(audio_fnv1a64("") == UINT64_C(14695981039346656037));
  CHECK(audio_fnv1a64("a") == UINT64_C(0xaf63dc4c8601ec8c));
  uint64_t left = UINT64_C(14695981039346656037), right = left;
  audio_fnv1a64_field(&left, "a");
  audio_fnv1a64_field(&left, "bc");
  audio_fnv1a64_field(&right, "ab");
  audio_fnv1a64_field(&right, "c");
  CHECK(left != right);
  char token[32], second[32];
  CHECK(audio_profile_token_for_raw("card.a", "profile.a", token,
                                    sizeof(token)) == 0);
  CHECK(audio_profile_token_for_raw("card.b", "profile.a", second,
                                    sizeof(second)) == 0);
  CHECK(strcmp(token, second) != 0 && strlen(token) == 24);
  CHECK(audio_profile_token_for_raw("card.a", "profile.a", second, 24) == -1);
  CHECK(audio_endpoint_token_shape("output", "output-0123456789abcdef"));
  CHECK(audio_endpoint_token_shape("input", "input-0123456789abcdef"));
  CHECK(!audio_endpoint_token_shape("output", "input-0123456789abcdef"));
  CHECK(!audio_endpoint_token_shape("output", "output-0123456789abcdeF"));
  const char *targets[] = {"output-0123456789abcdef", "input-0123456789abcdef",
                           "playback-0", "recording-2147483647"};
  const char *types[] = {"output", "input", "playback", "recording"};
  for (size_t i = 0; i < 4; i++) {
    int stream = -1;
    CHECK(audio_control_target_shape(targets[i], text, sizeof(text), &stream) ==
          0);
    CHECK(strcmp(text, types[i]) == 0 && stream == (i >= 2));
  }
  const char *invalid[] = {
      "playback-01", "recording-2147483648", "playback--1", "playback-+1",
      "playback- 1", "playback-1x",          "playback-",   ""};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
    int stream = -1;
    CHECK(audio_control_target_shape(invalid[i], text, sizeof(text), &stream) ==
          -1);
  }
  audio_control_state a = {0}, b = {0};
  strcpy(a.target, targets[0]);
  strcpy(a.target_type, "output");
  strcpy(a.raw_name, "sink.a");
  a.backend_index = 10;
  b = a;
  b.volume_percent = 47;
  b.muted = 1;
  CHECK(audio_control_identity_equal(&a, &b));
  b.backend_index++;
  CHECK(!audio_control_identity_equal(&a, &b));
  b = a;
  strcpy(b.raw_name, "sink.b");
  CHECK(!audio_control_identity_equal(&a, &b));
  CHECK(!audio_control_identity_equal(NULL, &a));
  a.stream_target = 1;
  a.stream.process_pid = 100;
  a.stream.process_start_time = 123;
  strcpy(a.stream.executable, "/fixture/app");
  b = a;
  b.stream.volume_percent++;
  CHECK(audio_control_identity_equal(&a, &b));
  b.stream.process_start_time++;
  CHECK(!audio_control_identity_equal(&a, &b));
  b = a;
  b.stream.process_pid++;
  CHECK(!audio_control_identity_equal(&a, &b));
  printf("audio-private-units: PASS checks=%u no-process-no-GUI\n", checks);
  return 0;
}
