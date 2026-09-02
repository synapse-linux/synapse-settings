#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
export LC_ALL=C LANG=C

settings=${1:?production settings binary required}
test_settings=${2:?fixture settings binary required}
broker=${3:?production broker binary required}
for binary in "$settings" "$test_settings" "$broker"; do
  [[ -f $binary && -x $binary ]]
done

production_strings=$(strings "$settings")
test_strings=$(strings "$test_settings")
broker_strings=$(strings "$broker")

for literal in \
    /usr/bin/pactl \
    synapse.settings.audio-inventory/v2 \
    synapse.settings.audio-profile-port-inventory/v1 \
    synapse.settings.audio-profile-port-plan/v1 \
    synapse.settings.audio-profile-port-receipt/v1 \
    synapse-settings/audio-profile-port/v1 \
    set-card-profile set-sink-port set-source-port; do
  grep -Fqx "$literal" <<<"$production_strings"
done

for hook in SYNAPSE_PACTL SYNAPSE_AUDIO_SELECTION_TEST_OPTION_ALIAS; do
  grep -Fqx "$hook" <<<"$test_strings"
  if grep -Fqx "$hook" <<<"$production_strings"; then
    printf 'production Settings contains fixture hook %s\n' "$hook" >&2
    exit 1
  fi
done

if grep -Fq 'profile-port-' <<<"$broker_strings" ||
   grep -Fq 'plan-profile' <<<"$broker_strings" ||
   grep -Fq 'set-profile' <<<"$broker_strings" ||
   grep -Fq 'plan-port' <<<"$broker_strings" ||
   grep -Fq 'set-port' <<<"$broker_strings" ||
   grep -Fq 'set-card-profile' <<<"$broker_strings" ||
   grep -Fq 'set-sink-port' <<<"$broker_strings" ||
   grep -Fq 'set-source-port' <<<"$broker_strings" ||
   grep -Fq 'synapse-settings/audio-profile-port/v1' <<<"$broker_strings" ||
   grep -Fq 'SYNAPSE_AUDIO_SELECTION_TEST_OPTION_ALIAS' <<<"$broker_strings"; then
  printf 'Audio broker contains the Settings-only profile/port capability\n' >&2
  exit 1
fi

printf 'synapse-settings profile/port production/test boundary: PASS\n'
