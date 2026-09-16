#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
export LC_ALL=C LANG=C

settings=${1:?production settings binary required}
test_settings=${2:?fixture settings binary required}
broker=${3:?production broker binary required}
gui=${4:?production GUI binary required}
test_gui=${5:?fixture GUI binary required}
for binary in "$settings" "$test_settings" "$broker" "$gui" "$test_gui"; do
  [[ -f $binary && -x $binary ]]
done

production_strings=$(strings "$settings")
test_strings=$(strings "$test_settings")
broker_strings=$(strings "$broker")
gui_ascii_strings=$(strings "$gui")
gui_utf16_strings=$(strings -el -n 3 "$gui")
test_gui_ascii_strings=$(strings "$test_gui")
grep -Fqx '/usr/bin/synapse-goxlr' <<<"$production_strings"
grep -Fqx 'synapse.settings.audio-goxlr-status/v2' <<<"$production_strings"
grep -Fqx 'synapse.goxlr.provider-status/v3' <<<"$production_strings"
grep -Fqx 'synapse.settings.audio-goxlr-control-plan/v1' <<<"$production_strings"
grep -Fqx 'synapse.settings.audio-goxlr-control-receipt/v1' <<<"$production_strings"
grep -Fqx 'synapse-settings/audio-goxlr-popup/v1' <<<"$production_strings"
grep -Fqx 'synapse-goxlr/popup-control/v1' <<<"$production_strings"
grep -Fqx 'SYNAPSE_GOXLR' <<<"$test_strings"
grep -Fqx '/usr/bin/synapse-goxlr-gui' <<<"$gui_utf16_strings"
grep -Fqx -- '--open-or-activate' <<<"$gui_utf16_strings"
grep -Fqx 'SYNAPSE_SETTINGS_GOXLR_APP_FIXTURE' <<<"$test_gui_ascii_strings"
if grep -Fqx 'SYNAPSE_GOXLR' <<<"$production_strings"; then
  printf 'production settings binary contains the GoXLR fixture hook\n' >&2
  exit 1
fi
if grep -Fqx 'SYNAPSE_SETTINGS_GOXLR_APP_FIXTURE' \
     <<<"$gui_ascii_strings"$'\n'"$gui_utf16_strings"; then
  printf 'production GUI contains the GoXLR application fixture hook\n' >&2
  exit 1
fi
if grep -Fqx 'synapse-goxlr/provider-start/v1' <<<"$production_strings" ||
   grep -Fqx 'synapse-goxlr/provider-start/v1' \
     <<<"$gui_ascii_strings"$'\n'"$gui_utf16_strings"; then
  printf 'Settings contains GoXLR provider-start authority\n' >&2
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
