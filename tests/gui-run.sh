#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

gui=${1:?GUI fixture binary required}
backend=${2:?test backend required}
# Production deliberately strips these overrides. Reject the wrong artifact
# before starting a GUI rather than silently falling back to host Audio tools.
gui_strings=$(strings "$gui")
if ! grep -Fq SYNAPSE_AUDIO_FIXTURES <<<"$gui_strings"; then
  printf 'GUI smoke requires a separately compiled fixture GUI\n' >&2
  exit 64
fi
if [[ -n ${GUI_SMOKE_OUTPUT_DIR:-} ]]; then
  mkdir -m 0700 -- "$GUI_SMOKE_OUTPUT_DIR"
fi
work=$(mktemp -d)
finish() {
  local status=$?
  if [[ -n ${GUI_SMOKE_OUTPUT_DIR:-} ]]; then
    for artifact in "$work"/*.stdout "$work"/*.stderr "$work"/*.png; do
      if [[ -f $artifact ]]; then
        cp --update=none-fail -- "$artifact" "$GUI_SMOKE_OUTPUT_DIR/"
      fi
    done
  fi
  rm -rf "$work"
  return "$status"
}
trap finish EXIT
install -d -m 0755 "$work/stage" "$work/audio" "$work/config"
install -d -m 0755 "$work/config/synapse"
install -d -m 0700 "$work/runtime"
install -m 0755 "$gui" "$work/stage/synapse-settings-gui"
install -m 0755 "$backend" "$work/stage/synapse-settings-fixture"
cat >"$work/stage/synapse-settings" <<'SH'
#!/bin/sh
set -eu
# A missing test environment must fail before the C fixture can choose defaults.
[ "${SYNAPSE_PACTL-}" = "$HOME/pactl-fake" ] || exit 64
[ "${SYNAPSE_GOXLR-}" = "$HOME/goxlr-fake" ] || exit 64
[ "${SYNAPSE_AUDIO_FIXTURES-}" = "$HOME/audio" ] || exit 64
[ "${SYNAPSE_AUDIO_ROUTE_POLICY-}" = "$HOME/config/audio-route-policy-v1.json" ] || exit 64
exec "${0%/*}/synapse-settings-fixture" "$@"
SH
chmod 755 "$work/stage/synapse-settings"
cat >"$work/stage/synapse-theme" <<'SH'
#!/bin/sh
set -eu
[ "${1-} ${2-} ${3-}" = 'current --format json' ] || exit 64
state=${XDG_CONFIG_HOME:?}/synapse/theme-state-v2.json
[ -f "$state" ] || exit 69
exec /usr/bin/cat -- "$state"
SH
chmod 755 "$work/stage/synapse-theme"
cat >"$work/config/synapse/theme-state-v2.json" <<'JSON'
{"schema":"synapse.theme.current/v3","id":"matte-black","theme":{"id":"matte-black","name":"Matte Black","description":"Deterministic first-party GUI fixture.","source":"synapse-test-fixture","preview":"","wallpaper":"","backgrounds":[],"palette":{"background":"#121212","surface":"#0d0d0d","surfaceHover":"#1e1e1e","border":"#333333","accent":"#e68e0d","text":"#bebebe","muted":"#555555","urgent":"#d35f5f"}}}
JSON
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
printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v3","providerActive":true,"deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","profileModelReady":true,"generation":7,"stateAuthority":"provider-profile-model","hardwareReadback":false,"hardwareExactRollback":false,"popupCapabilities":{"faderAVolume":true,"faderBVolume":true,"faderCVolume":true,"faderDVolume":true,"faderAMute":true,"faderBMute":true,"faderCMute":true,"faderDMute":true,"coughMute":true,"headphonesVolume":true,"lineOutVolume":true},"faders":[{"fader":"A","channel":"Mic","volume":110,"muteState":"Unmuted"},{"fader":"B","channel":"Chat","volume":120,"muteState":"Unmuted"},{"fader":"C","channel":"Music","volume":130,"muteState":"MutedToAll"},{"fader":"D","channel":"System","volume":127,"muteState":"Unmuted"}],"cough":{"mode":"Toggle","muteState":"Unmuted"},"outputs":{"headphonesVolume":180,"lineOutVolume":200,"monitoredOutput":"Headphones"},"systemOutputSupported":true,"systemOutput":{"routeToLineOut":false,"systemVolume":127,"lineOutVolume":200,"systemFader":"D","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}}]}'
SH
chmod 755 "$work/goxlr-fake"

for locale in en_US it_IT ar; do
  screenshot="$work/$locale.png"
  env QT_QPA_PLATFORM=offscreen HOME="$work" XDG_CONFIG_HOME="$work/config" \
    XDG_RUNTIME_DIR="$work/runtime" SYNAPSE_PACTL="$work/pactl-fake" SYNAPSE_AUDIO_FIXTURES="$work/audio" \
    SYNAPSE_AUDIO_ROUTE_POLICY="$work/config/audio-route-policy-v1.json" \
    SYNAPSE_GOXLR="$work/goxlr-fake" \
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
  grep -Fq 'theme=matte-black theme-provider=valid' "$work/$locale.stderr"
done

if cmp -s "$work/en_US.png" "$work/ar.png"; then
  printf 'recognized RTL locale did not mirror the rendered layout\n' >&2
  exit 1
fi

env QT_QPA_PLATFORM=offscreen HOME="$work" XDG_CONFIG_HOME="$work/config" \
  XDG_RUNTIME_DIR="$work/runtime" SYNAPSE_PACTL="$work/pactl-fake" \
  SYNAPSE_AUDIO_FIXTURES="$work/audio" \
  SYNAPSE_AUDIO_ROUTE_POLICY="$work/config/audio-route-policy-v1.json" \
  SYNAPSE_GOXLR="$work/goxlr-fake" \
  "$work/stage/synapse-settings-gui" --locale zz_INVALID \
    --test-exit-after-load --test-ready-timeout 10000 \
    >"$work/fallback.stdout" 2>"$work/fallback.stderr"
grep -Fq 'audio=validated renderer=software' "$work/fallback.stderr"
grep -Fq 'theme=matte-black theme-provider=valid' "$work/fallback.stderr"

(
  sleep 0.5
  cat >"$work/config/synapse/theme-state-v2.json.next" <<'JSON'
{"schema":"synapse.theme.current/v3","id":"paper-white","theme":{"id":"paper-white","name":"Paper White","description":"Deterministic first-party reload fixture.","source":"synapse-test-fixture","preview":"","wallpaper":"","backgrounds":[],"palette":{"background":"#f4f1ea","surface":"#ffffff","surfaceHover":"#ece7dc","border":"#b7afa0","accent":"#2766c2","text":"#171717","muted":"#625f59","urgent":"#b42318"}}}
JSON
  mv "$work/config/synapse/theme-state-v2.json.next" \
    "$work/config/synapse/theme-state-v2.json"
) &
theme_writer=$!
env QT_QPA_PLATFORM=offscreen HOME="$work" XDG_CONFIG_HOME="$work/config" \
  XDG_RUNTIME_DIR="$work/runtime" SYNAPSE_PACTL="$work/pactl-fake" \
  SYNAPSE_AUDIO_FIXTURES="$work/audio" \
  SYNAPSE_AUDIO_ROUTE_POLICY="$work/config/audio-route-policy-v1.json" \
  SYNAPSE_GOXLR="$work/goxlr-fake" \
  "$work/stage/synapse-settings-gui" --locale en_US \
    --test-exit-after-load --test-ready-timeout 10000 \
    --test-settle-delay 1800 --test-window-size 900x640 \
    --test-screenshot "$work/theme-reload.png" \
    >"$work/theme-reload.stdout" 2>"$work/theme-reload.stderr"
wait "$theme_writer"
test -s "$work/theme-reload.png"
grep -Fq 'audio=validated renderer=software' "$work/theme-reload.stderr"
grep -Fq 'theme=paper-white theme-provider=valid' "$work/theme-reload.stderr"
if cmp -s "$work/en_US.png" "$work/theme-reload.png"; then
  printf 'theme reload did not change the rendered surface\n' >&2
  exit 1
fi

if env QT_QPA_PLATFORM=offscreen HOME="$work" \
  XDG_CONFIG_HOME="$work/config" XDG_RUNTIME_DIR="$work/runtime" \
  "$work/stage/synapse-settings-gui" --test-exit-after-load \
    --test-settle-delay 49 >"$work/invalid-settle.stdout" \
    2>"$work/invalid-settle.stderr"; then
  printf 'out-of-range test settle delay was accepted\n' >&2
  exit 1
fi
grep -Fq 'invalid test settle delay' "$work/invalid-settle.stderr"

test ! -e "$work/config/audio-route-policy-v1.json"
printf 'synapse-settings GUI adapter and fallback smoke: PASS\n'
