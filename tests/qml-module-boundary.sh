#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
export LC_ALL=C LANG=C

module=${1:?staged production QML module required}
plugin=$module/libsynapse_settings_audio_qml.so
[[ -f $plugin && -x $plugin ]]
for file in qmldir synapse-settings-audio.qmltypes AudioSettings.qml \
            AudioSettingsSection.qml AudioShellHost.qml; do
  [[ -f $module/$file ]]
done
grep -Fqx 'module Synapse.Settings.Audio' "$module/qmldir"
grep -Fqx 'plugin synapse_settings_audio_qml' "$module/qmldir"
grep -Fq 'isSingleton: true' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'exports: ["AudioBackend 1.0"]' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "setAudioVolume"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "setAudioMuted"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "planAudioProfile"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "planAudioPort"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "confirmAudioSelection"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "audioProfileCards"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "audioPortEndpoints"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "audioGoxlrStatus"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'name: "audioGoxlrDevices"' "$module/synapse-settings-audio.qmltypes"
grep -Fq 'readonly property string goxlrStatus:' "$module/AudioShellHost.qml"
if grep -Eiq 'name: "(set|plan|apply)Goxlr|stateAuthority|hardwareReadback|systemVolume|lineOutVolume|systemFader|systemMuteState|lineOutMix|submixEnabled' \
    "$module/synapse-settings-audio.qmltypes" "$module"/*.qml ||
   grep -Eiq 'goxlr-[1-8]|(^|[^[:alnum:]_])controlAvailable([^[:alnum:]_]|$)' \
    "$module/synapse-settings-audio.qmltypes" "$module"/*.qml; then
  printf 'forbidden GoXLR authority or raw provider state in feature QML\n' >&2
  exit 1
fi

while IFS= read -r import_line; do
  case "$import_line" in
    'import QtQuick'|'import QtQuick.Controls'|'import QtQuick.Layouts') ;;
    *) printf 'unexpected feature import: %s\n' "$import_line" >&2; exit 1 ;;
  esac
done < <(grep -h '^import ' "$module"/*.qml)
if grep -Eiq 'Quickshell|hyprctl|pactl|/proc|SO_PEERCRED|status-v1|cohort|acknowledgement|requiresAcknowledgement|raw(Name|Profile|Port)|set-(card-profile|sink-port|source-port)|SYNAPSE_|QProcess|JSON\.parse|environment|argv' \
    "$module"/*.qml; then
  printf 'forbidden transport or authority detail in feature QML\n' >&2
  exit 1
fi

ascii_strings=$(strings "$plugin")
utf16_strings=$(strings -el "$plugin")
header=$(readelf -h "$plugin")
notes=$(readelf -n "$plugin")
program_headers=$(readelf -W -l "$plugin")
dynamic=$(readelf -d "$plugin")

grep -Fqx '/usr/bin/synapse-settings' <<<"$utf16_strings"
if grep -Eq 'SYNAPSE_SETTINGS_TEST_BACKEND|/run/user/1000' \
    <<<"$ascii_strings"$'\n'"$utf16_strings"; then
  printf 'production QML plugin contains a fixture or UID-specific hook\n' >&2
  exit 1
fi
grep -Eq 'Type:[[:space:]]+DYN \(Shared object file\)' <<<"$header"
grep -Fq 'x86 ISA needed: x86-64-baseline' <<<"$notes"
grep -Fq 'GNU_RELRO' <<<"$program_headers"
grep -Eq '\(FLAGS\).*BIND_NOW' <<<"$dynamic"
if grep -Eq '\((RPATH|RUNPATH)\)' <<<"$dynamic"; then
  printf 'production QML plugin contains RPATH/RUNPATH\n' >&2
  exit 1
fi
printf 'synapse-settings Audio QML production boundary: PASS\n'
