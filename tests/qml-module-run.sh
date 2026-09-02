#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

qmltestrunner=${1:?qmltestrunner required}
module_root=${2:?QML module root required}
backend=${3:?test backend required}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
install -d -m 0755 "$work/audio" "$work/config" "$work/cache"
install -d -m 0700 "$work/runtime"
cat >"$work/audio/sinks.json" <<'JSON'
[{"index":10,"name":"sink.a","description":"Integrated audio","mute":false,"volume":{"left":{"value":32768}},"active_port":"port.speaker","ports":[{"name":"port.speaker","description":"Speakers","availability":"available"},{"name":"port.headphones","description":"Headphones","availability":"availability unknown"}]}]
JSON
cat >"$work/audio/sources.json" <<'JSON'
[{"index":20,"name":"source.a","description":"Built-in microphone","monitor_of_sink":null,"monitor_source":"","mute":false,"volume":{"mono":{"value":32768}},"active_port":"port.mic","ports":[{"name":"port.mic","description":"Microphone","availability":"available"}]},{"index":21,"name":"sink.a.monitor","description":"Monitor","monitor_of_sink":null,"monitor_source":"sink.a","mute":false,"volume":{"mono":{"value":65536}},"active_port":null,"ports":[]}]
JSON
printf '[]\n' >"$work/audio/sink-inputs.json"
printf '[]\n' >"$work/audio/source-outputs.json"
cat >"$work/audio/cards.json" <<'JSON'
[{"index":40,"name":"card.a","description":"Primary audio card","active_profile":"profile.hifi","profiles":{"profile.hifi":{"description":"High Fidelity","available":true},"profile.pro":{"description":"Pro Audio"}}}]
JSON
printf 'sink.a\n' >"$work/audio/default-sink"
printf 'source.a\n' >"$work/audio/default-source"
cat >"$work/pactl-fake" <<'SH'
#!/bin/sh
set -eu
case "${1-} ${2-} ${3-}" in
  '--format=json info ') printf '{"default_sink_name":"%s","default_source_name":"%s"}\n' "$(cat "$SYNAPSE_AUDIO_FIXTURES/default-sink")" "$(cat "$SYNAPSE_AUDIO_FIXTURES/default-source")" ;;
  '--format=json list sinks') cat "$SYNAPSE_AUDIO_FIXTURES/sinks.json" ;;
  '--format=json list sources') cat "$SYNAPSE_AUDIO_FIXTURES/sources.json" ;;
  '--format=json list sink-inputs') cat "$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json" ;;
  '--format=json list source-outputs') cat "$SYNAPSE_AUDIO_FIXTURES/source-outputs.json" ;;
  '--format=json list cards') cat "$SYNAPSE_AUDIO_FIXTURES/cards.json" ;;
  *) exit 64 ;;
esac
SH
chmod 755 "$work/pactl-fake"
cat >"$work/goxlr-fake" <<'SH'
#!/bin/sh
set -eu
[ "${1-} ${2-} ${3-}" = 'provider-status --format json' ] || exit 64
printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":254,"lineOutVolume":255,"systemFader":"D","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}}]}'
SH
chmod 755 "$work/goxlr-fake"

for locale in en_US it_IT; do
  if ! env LC_ALL="${locale}.utf8" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
      HOME="$work" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache" \
      XDG_RUNTIME_DIR="$work/runtime" SYNAPSE_SETTINGS_TEST_BACKEND="$backend" \
      SYNAPSE_PACTL="$work/pactl-fake" SYNAPSE_AUDIO_FIXTURES="$work/audio" \
      SYNAPSE_AUDIO_ROUTE_POLICY="$work/config/audio-route-policy-v1.json" \
      SYNAPSE_GOXLR="$work/goxlr-fake" \
      "$qmltestrunner" -import "$module_root" \
        -input tests/qml/tst_audio_module.qml \
        -o "$work/${locale}.log,txt" >"$work/${locale}.stdout" \
        2>"$work/${locale}.stderr"; then
    cat "$work/${locale}.stdout" "$work/${locale}.stderr" \
      "$work/${locale}.log" >&2
    exit 1
  fi
  grep -Fq '0 failed' "$work/${locale}.log"
  if grep -Eiq 'module .* is not installed|plugin cannot be loaded|referenceerror|typeerror|binding loop' \
      "$work/${locale}.stderr"; then
    cat "$work/${locale}.stderr" >&2
    exit 1
  fi
done

test ! -e "$work/config/audio-route-policy-v1.json"
test -z "$(find "$work/runtime" -mindepth 1 -print -quit)"
printf 'synapse-settings Audio QML module smoke: PASS\n'
