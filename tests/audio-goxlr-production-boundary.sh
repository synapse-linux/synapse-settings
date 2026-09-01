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
grep -Fqx '/usr/bin/synapse-goxlr' <<<"$production_strings"
grep -Fqx 'synapse.settings.audio-goxlr-status/v1' <<<"$production_strings"
grep -Fqx 'synapse.goxlr.provider-status/v2' <<<"$production_strings"
grep -Fqx 'SYNAPSE_GOXLR' <<<"$test_strings"
if grep -Fqx 'SYNAPSE_GOXLR' <<<"$production_strings"; then
  printf 'production settings binary contains the GoXLR fixture hook\n' >&2
  exit 1
fi
if grep -Fqx '/usr/bin/synapse-goxlr' <<<"$broker_strings" ||
   grep -Fqx 'SYNAPSE_GOXLR' <<<"$broker_strings" ||
   grep -Fq 'goxlr-status' <<<"$broker_strings" ||
   grep -Fq 'synapse.goxlr' <<<"$broker_strings" ||
   grep -Fq 'audio-goxlr' <<<"$broker_strings"; then
  printf 'Audio broker contains the Settings-only GoXLR adapter\n' >&2
  exit 1
fi
printf 'synapse-settings GoXLR production/test boundary: PASS\n'
