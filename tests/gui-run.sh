#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

gui=${1:?GUI binary required}
backend=${2:?test backend required}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
install -d -m 0755 "$work/stage" "$work/audio" "$work/config"
install -d -m 0700 "$work/runtime"
install -m 0755 "$gui" "$work/stage/synapse-settings-gui"
install -m 0755 "$backend" "$work/stage/synapse-settings"
cat >"$work/audio/sinks.json" <<'JSON'
[{"index":10,"name":"sink.a","description":"Integrated audio","mute":false,"volume":{"left":{"value":32768}}}]
JSON
cat >"$work/audio/sources.json" <<'JSON'
[{"index":20,"name":"source.a","description":"Built-in microphone","monitor_of_sink":null,"monitor_source":"","mute":false,"volume":{"mono":{"value":32768}}},{"index":21,"name":"sink.a.monitor","description":"Monitor","monitor_of_sink":null,"monitor_source":"sink.a","mute":false,"volume":{"mono":{"value":65536}}}]
JSON
printf '[]\n' >"$work/audio/sink-inputs.json"
printf '[]\n' >"$work/audio/source-outputs.json"
cat >"$work/audio/cards.json" <<'JSON'
[{"index":40,"name":"card.a","description":"Primary audio card","active_profile":"HiFi"}]
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

for locale in en_US it_IT; do
  screenshot="$work/$locale.png"
  env QT_QPA_PLATFORM=offscreen HOME="$work" XDG_CONFIG_HOME="$work/config" \
    XDG_RUNTIME_DIR="$work/runtime" SYNAPSE_PACTL="$work/pactl-fake" SYNAPSE_AUDIO_FIXTURES="$work/audio" \
    SYNAPSE_AUDIO_ROUTE_POLICY="$work/config/audio-route-policy-v1.json" \
    "$work/stage/synapse-settings-gui" --locale "$locale" \
      --test-exit-after-load --test-ready-timeout 10000 \
      --test-window-size 900x640 --test-screenshot "$screenshot" \
      >"$work/$locale.stdout" 2>"$work/$locale.stderr"
  test -s "$screenshot"
  file "$screenshot" | grep -Fq 'PNG image data'
  if grep -Eiq 'qml=|binding loop|referenceerror|typeerror' "$work/$locale.stderr"; then
    exit 1
  fi
  grep -Fq 'audio=validated renderer=software' "$work/$locale.stderr"
done

test ! -e "$work/config/audio-route-policy-v1.json"
printf 'synapse-settings GUI adapter smoke: PASS\n'
