#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

binary=${1:?binary required}
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
process_pid=
trap 'if [[ -n "$process_pid" ]]; then kill "$process_pid" 2>/dev/null || true; wait "$process_pid" 2>/dev/null || true; fi; rm -rf "$work"' EXIT
composition="$work/composition"
pacman="$work/pacman"
mkdir -p "$composition" "$pacman/base-1" "$pacman/extra-2" "$work/bin"
cat >"$composition/layers.tsv" <<'EOF'
layer	type	version
base	base	1.0
config.development	configuration	1.0
EOF
cat >"$composition/layer-packages.tsv" <<'EOF'
layer	package
base	base
config.development	openssh
EOF
cat >"$composition/package-lock.tsv" <<'EOF'
base 1-1
openssh 9.9-1
EOF
cat >"$pacman/base-1/desc" <<'EOF'
%NAME%
base

%VERSION%
1-1
EOF
cat >"$pacman/extra-2/desc" <<'EOF'
%NAME%
extra-tool

%VERSION%
2-1
EOF
cat >"$work/bin/docker-ok" <<'EOF'
#!/usr/bin/env bash
printf 'builder\timage:test\tUp 5 minutes\n'
EOF
cat >"$work/bin/docker-timeout" <<'EOF'
#!/usr/bin/env bash
exec 1>&-
sleep 5
EOF
chmod 755 "$work/bin"/*

env SYNAPSE_COMPOSITION_DIR="$composition" SYNAPSE_PACMAN_LOCAL="$pacman" \
  SYNAPSE_DOCKER="$work/bin/docker-ok" "$binary" layers --format json >"$work/layers.json"
python - "$work/layers.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1]))
assert value['schema']=='synapse.settings.layers/v2'
assert [x['id'] for x in value['layers']]==['base','config.development','unlayered']
assert value['semanticLayerCount']==2
assert value['layers'][0]['packages']==[{'name':'base','version':'1-1'}]
assert value['layers'][1]['packages']==[{'name':'openssh','version':'9.9-1'}]
assert value['layers'][2]['generated'] is True
assert value['layers'][2]['packages']==[{'name':'extra-tool','version':'2-1'}]
assert value['packages']=={'backend':'pacman','installedCount':2,
                           'factoryLockedCount':2,'unlayeredCount':1}
assert value['docker']['available'] is True
assert value['docker']['activeCount']==1
PY

env SYNAPSE_COMPOSITION_DIR="$composition" SYNAPSE_PACMAN_LOCAL="$pacman" \
  SYNAPSE_DOCKER="$work/missing-docker" "$binary" layers --format json >"$work/no-docker.json"
python - "$work/no-docker.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1]))
assert value['docker']=={'available':False,'reason':'not-installed','active':[],
                         'activeCount':0}
PY

env SYNAPSE_COMPOSITION_DIR="$composition" SYNAPSE_PACMAN_LOCAL="$pacman" \
  SYNAPSE_DOCKER="$work/bin/docker-ok" "$binary" layers --format text >"$work/layers.txt"
grep -Fq 'Non layerizzato' "$work/layers.txt"
grep -Fq 'extra-tool 2-1' "$work/layers.txt"

"$binary" sections --format json >"$work/sections.json"
python - "$work/sections.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1]))
assert value['schema']=='synapse.settings.sections/v2'
assert [x['id'] for x in value['sections']]==['layers','audio','input','themes']
assert all(x['available'] and x['icon'] and x['lazy'] for x in value['sections'])
PY
"$binary" sections --format text | grep -Fq $'audio\tAudio\taudio-card\tavailable'
[[ $($binary --version) == 'synapse-settings 0.5.0-alpha.1' ]]
"$binary" --help | grep -Fq 'synapse-settings audio policy set-rule'

# Broker-status observation is independently read-only when no broker or policy
# exists: it creates neither configuration nor runtime state and runs no pactl.
install -d -m 0700 "$work/status-home" "$work/status-runtime"
env HOME="$work/status-home" XDG_CONFIG_HOME="$work/status-home/config" \
  XDG_RUNTIME_DIR="$work/status-runtime" SYNAPSE_PACTL="$work/missing-pactl" \
  "$binary" audio broker-status --format json >"$work/status-inactive.json"
python - "$work/status-inactive.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1]))
assert value['schema']=='synapse.settings.audio-route-broker-status/v1'
assert value['status']=='Ready' and value['capable'] and not value['active']
assert not value['enforcementAvailable'] and value['reason']=='broker-not-running'
assert value['policyGeneration']==0 and value['baselineStreams']==0
PY
[[ ! -e "$work/status-home/config" && ! -e "$work/status-runtime/synapse" ]]

start=$(date +%s)
env SYNAPSE_COMPOSITION_DIR="$composition" SYNAPSE_PACMAN_LOCAL="$pacman" \
  SYNAPSE_DOCKER="$work/bin/docker-timeout" "$binary" layers --format json >"$work/timeout.json"
elapsed=$(( $(date +%s) - start ))
(( elapsed < 4 ))
python - "$work/timeout.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1]))
assert value['docker']['available'] is False
assert value['docker']['reason']=='timeout'
PY

# Typed audio inventory and exact default mutation through fixed pactl argv.
audio="$work/audio"
mkdir -p "$audio"
cp "$(command -v sleep)" "$work/process-app"
"$work/process-app" 300 &
process_pid=$!
cat >"$audio/sinks.json" <<'EOF'
[
 {"index":10,"name":"sink.a","description":"Integrated audio","mute":false,"volume":{"front-left":{"value":32768},"front-right":{"value":32768}}},
 {"index":11,"name":"sink.b","description":"USB headset","mute":false,"volume":{"front-left":{"value":65536},"front-right":{"value":65536}}}
]
EOF
cat >"$audio/sources.json" <<'EOF'
[
 {"index":20,"name":"source.a","description":"Built-in microphone","monitor_of_sink":null,"monitor_source":"","mute":false,"volume":{"mono":{"value":32768}}},
 {"index":21,"name":"source.b","description":"USB microphone","monitor_of_sink":null,"monitor_source":"","mute":true,"volume":{"mono":{"value":65536}}},
 {"index":22,"name":"sink.a.monitor","description":"Modern monitor","monitor_of_sink":null,"monitor_source":"sink.a","mute":false,"volume":{"mono":{"value":65536}}},
 {"index":23,"name":"sink.b.monitor","description":"Legacy monitor","monitor_of_sink":11,"mute":false,"volume":{"mono":{"value":65536}}}
]
EOF
cat >"$audio/sink-inputs.json" <<EOF
[{"index":30,"sink":11,"mute":false,"volume":{"front-left":{"value":49152},"front-right":{"value":49152}},"properties":{"application.name":"Game","application.process.id":"$process_pid"}}]
EOF
printf '[]\n' >"$audio/source-outputs.json"
cat >"$audio/cards.json" <<'EOF'
[{"index":40,"name":"card.a","description":"Primary audio card","active_profile":"HiFi"}]
EOF
printf 'sink.a\n' >"$audio/default-sink"
printf 'source.a\n' >"$audio/default-source"
cat >"$work/bin/pactl-fake" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "${1-} ${2-} ${3-}" in
  '--format=json info ')
    printf '{"default_sink_name":"%s","default_source_name":"%s"}\n' "$(cat "$SYNAPSE_AUDIO_FIXTURES/default-sink")" "$(cat "$SYNAPSE_AUDIO_FIXTURES/default-source")" ;;
  '--format=json list sinks') cat "$SYNAPSE_AUDIO_FIXTURES/sinks.json" ;;
  '--format=json list sources') cat "$SYNAPSE_AUDIO_FIXTURES/sources.json" ;;
  '--format=json list sink-inputs') cat "$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json" ;;
  '--format=json list source-outputs') cat "$SYNAPSE_AUDIO_FIXTURES/source-outputs.json" ;;
  '--format=json list cards') cat "$SYNAPSE_AUDIO_FIXTURES/cards.json" ;;
  'set-default-sink sink.b ')
    printf 'sink.b\n' >"$SYNAPSE_AUDIO_FIXTURES/default-sink" ;;
  'set-default-source source.b ')
    printf 'source.b\n' >"$SYNAPSE_AUDIO_FIXTURES/default-source" ;;
  *) exit 64 ;;
esac
EOF
chmod 755 "$work/bin/pactl-fake"
AUDIO_ENV=(env SYNAPSE_PACTL="$work/bin/pactl-fake" SYNAPSE_AUDIO_FIXTURES="$audio")
"${AUDIO_ENV[@]}" "$binary" audio inventory --format json >"$work/audio-inventory.json"
python - "$work/audio-inventory.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['schema']=='synapse.settings.audio-inventory/v1'
assert v['available'] and v['mutationAvailable'] and v['reason'] is None
assert v['stateAuthority']=='pipewire-pulse-model'
assert [(x['label'],x['default'],x['volumePercent']) for x in v['outputs']]==[
 ('Integrated audio',True,50),('USB headset',False,100)]
assert all(x['id'].startswith('output-') and len(x['id'])==23 for x in v['outputs'])
assert [(x['default'],x['muted']) for x in v['inputs']]==[(True,False),(False,True)]
assert all(x['id'].startswith('input-') and len(x['id'])==22 for x in v['inputs'])
assert len(v['streams'])==1 and v['streams'][0]['id']=='playback-30'
assert v['streams'][0]['target']==v['outputs'][1]['id'] and v['streams'][0]['volumePercent']==75
assert v['streams'][0]['processRuleAvailable'] is True
assert len(v['cards'])==1 and v['cards'][0]['id'].startswith('card-')
assert v['cards'][0]['label']=='Primary audio card' and v['cards'][0]['activeProfile']=='HiFi'
PY
read -r integrated_output headset_output < <(python - "$work/audio-inventory.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));print(v['outputs'][0]['id'],v['outputs'][1]['id'])
PY
)
"${AUDIO_ENV[@]}" "$binary" audio plan-default --direction output --device "$headset_output" --format json >"$work/audio-plan.json"
python - "$work/audio-plan.json" "$headset_output" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['schema']=='synapse.settings.audio-default-plan/v1'
assert v['device']==sys.argv[2] and v['changed'] is True and v['applied'] is False
PY
"${AUDIO_ENV[@]}" "$binary" audio set-default --direction output --device "$headset_output" \
  --ack synapse-settings/audio-default/v1 --format json >"$work/audio-receipt.json"
python - "$work/audio-receipt.json" "$headset_output" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['schema']=='synapse.settings.audio-default-receipt/v1'
assert v['status']=='Applied' and v['device']==sys.argv[2] and v['changed'] and v['applied']
PY
"${AUDIO_ENV[@]}" "$binary" audio inventory --format json >"$work/audio-after.json"
python - "$work/audio-after.json" "$headset_output" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert [x['id'] for x in v['outputs'] if x['default']]==[sys.argv[2]]
PY
env SYNAPSE_PACTL="$work/bin/missing-pactl" "$binary" audio inventory --format json >"$work/audio-unavailable.json"
python - "$work/audio-unavailable.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['available'] is False and v['reason']=='unavailable'
assert v['outputs']==[] and v['inputs']==[] and v['mutationAvailable'] is False
PY
cat >"$work/bin/pactl-timeout" <<'EOF'
#!/usr/bin/env bash
exec 1>&-
sleep 5
EOF
chmod 755 "$work/bin/pactl-timeout"
start=$(date +%s)
env SYNAPSE_PACTL="$work/bin/pactl-timeout" "$binary" audio inventory --format json >"$work/audio-timeout.json"
elapsed=$(( $(date +%s) - start ))
(( elapsed < 4 ))
python - "$work/audio-timeout.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['available'] is False and v['reason']=='timeout'
PY
cp "$audio/sinks.json" "$audio/sinks.valid.json"
python - "$audio/sinks.json" <<'PY'
import json,sys
p=sys.argv[1];v=json.load(open(p));v.append(v[0]);json.dump(v,open(p,'w'))
PY
"${AUDIO_ENV[@]}" "$binary" audio inventory --format json >"$work/audio-duplicate.json"
python - "$work/audio-duplicate.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['available'] is False and v['reason']=='invalid-response'
PY
mv "$audio/sinks.valid.json" "$audio/sinks.json"
cp "$audio/sink-inputs.json" "$audio/sink-inputs.valid.json"
python - "$audio/sink-inputs.json" <<'PY'
import json,sys
p=sys.argv[1];v=json.load(open(p));v.append(dict(v[0]));json.dump(v,open(p,'w'))
PY
"${AUDIO_ENV[@]}" "$binary" audio inventory --format json >"$work/audio-duplicate-stream.json"
python - "$work/audio-duplicate-stream.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['available'] is False and v['reason']=='invalid-response'
PY
cp "$audio/sink-inputs.valid.json" "$audio/sink-inputs.json"
python - "$audio/sink-inputs.json" <<'PY'
import json,sys
p=sys.argv[1];v=json.load(open(p));v[0]['index']=-1;json.dump(v,open(p,'w'))
PY
"${AUDIO_ENV[@]}" "$binary" audio inventory --format json >"$work/audio-negative-stream.json"
python - "$work/audio-negative-stream.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['available'] is False and v['reason']=='invalid-response'
PY
mv "$audio/sink-inputs.valid.json" "$audio/sink-inputs.json"
for args in \
  "audio set-default --direction output --device $integrated_output --ack wrong" \
  'audio set-default --direction output --device raw-device --ack synapse-settings/audio-default/v1' \
  "audio plan-default --direction output --device $integrated_output --ack synapse-settings/audio-default/v1"; do
  set +e
  # The fixture cases are reviewed argument vectors encoded without glob bytes.
  # shellcheck disable=SC2086
  "${AUDIO_ENV[@]}" "$binary" $args >/dev/null 2>&1
  status=$?
  set -e
  [[ $status != 0 ]]
done

# Private deterministic per-executable and directory audio routing policy.
read -r usb_input < <(python - "$work/audio-after.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));print(v['inputs'][1]['id'])
PY
)
mkdir -p "$work/steam/library/special" "$work/steam2"
for executable in \
  "$work/steam/library/game" \
  "$work/steam/library/special/game" \
  "$work/steam/library/special/tool" \
  "$work/steam2/game"; do
  printf '#!/usr/bin/env sh\nexit 0\n' >"$executable"
  chmod 755 "$executable"
done
policy="$work/config/synapse/audio-route-policy-v1.json"
ROUTE_ENV=(env HOME="$work" SYNAPSE_PACTL="$work/bin/pactl-fake" \
  SYNAPSE_AUDIO_FIXTURES="$audio" SYNAPSE_AUDIO_ROUTE_POLICY="$policy")

"${ROUTE_ENV[@]}" "$binary" audio policy show --format json >"$work/route-empty.json"
python - "$work/route-empty.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['schema']=='synapse.settings.audio-route-policy-view/v1'
assert v['present'] is False and v['generation']==0 and v['rules']==[]
assert v['systemDefaultFallback'] is True and not v['enforcementAvailable']
assert v['enforcementReason']=='audio-route-broker-not-integrated'
assert not v['persistentPidRules'] and not v['existingStreamMigration']
PY
[[ ! -e "$policy" ]]

set_rule() {
  local match=$1 path=$2 direction=$3 device=$4 output=$5
  "${ROUTE_ENV[@]}" "$binary" audio policy set-rule \
    --match "$match" --path "$path" --direction "$direction" --device "$device" \
    --ack synapse-settings/audio-route-policy/v1 --format json >"$output"
}
set_rule directory "$work/steam/library" output "$integrated_output" "$work/route-set-1.json"
set_rule directory "$work/steam/library/special" output "$headset_output" "$work/route-set-2.json"
set_rule executable "$work/steam/library/special/game" output "$integrated_output" "$work/route-set-3.json"
set_rule directory "$work/steam/library" input "$usb_input" "$work/route-set-4.json"
"${ROUTE_ENV[@]}" "$binary" audio policy set-process-rule --stream playback-30 \
  --device "$headset_output" --ack synapse-settings/audio-route-policy/v1 \
  --format json >"$work/route-set-process.json"
[[ $(stat -c %a "$policy") == 600 ]]
python - "$work/route-set-1.json" "$work/route-set-4.json" "$work/route-set-process.json" <<'PY'
import json,sys
first=json.load(open(sys.argv[1]));last=json.load(open(sys.argv[2]));process=json.load(open(sys.argv[3]))
assert first['schema']=='synapse.settings.audio-route-policy-receipt/v1'
assert first['action']=='set-rule' and first['generation']==1 and first['changed']
assert first['policyApplied'] and not first['routingApplied'] and not first['enforcementAvailable']
assert last['generation']==4 and last['rule']=='rule-0004'
assert process['action']=='set-process-rule' and process['generation']==5
assert process['rule']=='rule-0005' and not process['routingApplied']
PY

# Setting the same typed key is an idempotent upsert; changing its device updates it.
set_rule directory "$work/steam/library" output "$integrated_output" "$work/route-unchanged.json"
python - "$work/route-unchanged.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['generation']==5 and v['changed'] is False
assert v['rule']=='rule-0001'
PY
ln -s "$work/steam/library" "$work/steam-link"
set_rule directory "$work/steam-link" output "$headset_output" "$work/route-updated.json"
python - "$work/route-updated.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['generation']==6 and v['changed'] is True
assert v['rule']=='rule-0001'
PY
# Restore the base directory target for deterministic precedence checks.
set_rule directory "$work/steam/library" output "$integrated_output" "$work/route-restored.json"

"${ROUTE_ENV[@]}" "$binary" audio policy show --format json >"$work/route-view.json"
python - "$work/route-view.json" "$integrated_output" "$headset_output" "$usb_input" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['present'] and v['generation']==7
assert len(v['rules'])==5
assert all('displayPath' in r['match'] and 'path' not in r['match'] for r in v['rules'])
keys={(r['match']['type'],r['match']['displayPath'],r['direction']):r['device'] for r in v['rules']}
assert keys[('directory','~/steam/library','output')]==sys.argv[2]
assert keys[('directory','~/steam/library/special','output')]==sys.argv[3]
assert keys[('executable','~/steam/library/special/game','output')]==sys.argv[2]
assert keys[('directory','~/steam/library','input')]==sys.argv[4]
assert keys[('executable','~/process-app','output')]==sys.argv[3]
PY

resolve_route() {
  local executable=$1 direction=$2 output=$3
  "${ROUTE_ENV[@]}" "$binary" audio resolve --path "$executable" \
    --direction "$direction" --format json >"$output"
}
resolve_route "$work/steam/library/special/game" output "$work/resolve-exact.json"
resolve_route "$work/steam/library/special/tool" output "$work/resolve-longest.json"
resolve_route "$work/steam/library/game" output "$work/resolve-directory.json"
resolve_route "$work/steam2/game" output "$work/resolve-default.json"
resolve_route "$work/steam/library/special/game" input "$work/resolve-input.json"
resolve_route "$work/process-app" output "$work/resolve-process.json"
python - "$work" "$integrated_output" "$headset_output" "$usb_input" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]);integrated,headset,usb=sys.argv[2:]
def load(name): return json.loads((p/name).read_text())
exact=load('resolve-exact.json');assert exact['source']=='exact-executable' and exact['selectedDevice']==integrated and exact['rule']=='rule-0003'
longest=load('resolve-longest.json');assert longest['source']=='directory-prefix' and longest['selectedDevice']==headset and longest['rule']=='rule-0002'
directory=load('resolve-directory.json');assert directory['source']=='directory-prefix' and directory['selectedDevice']==integrated and directory['rule']=='rule-0001'
default=load('resolve-default.json');assert default['source']=='system-default' and default['selectedDevice']==headset and default['rule'] is None
input_rule=load('resolve-input.json');assert input_rule['source']=='directory-prefix' and input_rule['selectedDevice']==usb and input_rule['rule']=='rule-0004'
process=load('resolve-process.json');assert process['source']=='exact-executable' and process['selectedDevice']==headset and process['rule']=='rule-0005'
assert all(not load(x)['enforcementAvailable'] for x in ['resolve-exact.json','resolve-longest.json','resolve-directory.json','resolve-default.json','resolve-input.json','resolve-process.json'])
PY

# Unknown/mismatched devices, unstable PID rules, noncanonical kinds and bad acks fail closed.
for args in \
  "audio policy set-process-rule --stream playback-999 --device $integrated_output --ack synapse-settings/audio-route-policy/v1" \
  "audio policy set-process-rule --stream playback-30 --device $usb_input --ack synapse-settings/audio-route-policy/v1" \
  "audio policy set-rule --match process --path $work/steam/library/game --direction output --device $integrated_output --ack synapse-settings/audio-route-policy/v1" \
  "audio policy set-rule --match pid --path /123 --direction output --device $integrated_output --ack synapse-settings/audio-route-policy/v1" \
  "audio policy set-rule --match executable --path $work/steam/library/game --direction output --device $usb_input --ack synapse-settings/audio-route-policy/v1" \
  "audio policy set-rule --match executable --path $work/steam/library/game --direction output --device output-0000000000000000 --ack synapse-settings/audio-route-policy/v1" \
  "audio policy set-rule --match executable --path $work/missing --direction output --device $integrated_output --ack synapse-settings/audio-route-policy/v1" \
  "audio policy set-rule --match executable --path $work/steam/library/game --direction output --device $integrated_output --ack wrong" \
  "audio resolve --path $work/steam/library --direction output"; do
  set +e
  # The fixture cases are reviewed argument vectors encoded without glob bytes.
  # shellcheck disable=SC2086
  "${ROUTE_ENV[@]}" "$binary" $args >/dev/null 2>&1
  status=$?
  set -e
  [[ $status != 0 ]]
done

# Strict canonical parsing, ownership/mode checks and no-follow policy loading.
cp "$policy" "$work/policy.valid"
printf ' ' >>"$policy"
set +e
"${ROUTE_ENV[@]}" "$binary" audio policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status != 0 ]]
cp "$work/policy.valid" "$policy"
python - "$policy" <<'PY'
import json,sys
p=sys.argv[1];v=json.load(open(p));v['unknown']=True
open(p,'w').write(json.dumps(v,separators=(',',':'))+'\n')
PY
set +e
"${ROUTE_ENV[@]}" "$binary" audio policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status != 0 ]]
cp "$work/policy.valid" "$policy"
python - "$policy" <<'PY'
import json,sys
p=sys.argv[1];v=json.load(open(p));duplicate=dict(v['rules'][0]);duplicate['id']=v['rules'][0]['id'];v['rules'].append(duplicate)
open(p,'w').write(json.dumps(v,separators=(',',':'))+'\n')
PY
set +e
"${ROUTE_ENV[@]}" "$binary" audio policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status != 0 ]]
cp "$work/policy.valid" "$policy"
chmod 644 "$policy"
set +e
"${ROUTE_ENV[@]}" "$binary" audio policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status != 0 ]]
chmod 600 "$policy"
mv "$policy" "$work/policy.target"
ln -s "$work/policy.target" "$policy"
set +e
"${ROUTE_ENV[@]}" "$binary" audio policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status != 0 ]]
rm "$policy"
mv "$work/policy.target" "$policy"

# Removal is typed, acknowledged and leaves no ambiguous match.
"${ROUTE_ENV[@]}" "$binary" audio policy remove-rule --rule rule-0003 \
  --ack synapse-settings/audio-route-policy/v1 --format json >"$work/route-remove.json"
python - "$work/route-remove.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['action']=='remove-rule' and v['rule']=='rule-0003'
assert v['generation']==8 and v['changed'] and not v['routingApplied']
PY
resolve_route "$work/steam/library/special/game" output "$work/resolve-after-remove.json"
python - "$work/resolve-after-remove.json" "$headset_output" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['source']=='directory-prefix'
assert v['selectedDevice']==sys.argv[2] and v['rule']=='rule-0002'
PY

# All emitted public contracts validate against the pinned schemas.
"$binary" sections --format json >"$work/sections-final.json"
python - "$root" "$work" "$policy" <<'PY'
import json,sys
from pathlib import Path
from jsonschema import Draft202012Validator
root=Path(sys.argv[1]);work=Path(sys.argv[2]);policy=Path(sys.argv[3])
pairs=[
 ('sections-v2.schema.json','sections-final.json'),
 ('audio-inventory-v1.schema.json','audio-inventory.json'),
 ('audio-route-broker-status-v1.schema.json','status-inactive.json'),
 ('audio-default-plan-v1.schema.json','audio-plan.json'),
 ('audio-default-receipt-v1.schema.json','audio-receipt.json'),
 ('audio-route-policy-view-v1.schema.json','route-view.json'),
 ('audio-route-policy-receipt-v1.schema.json','route-set-1.json'),
 ('audio-route-policy-receipt-v1.schema.json','route-set-process.json'),
 ('audio-route-policy-receipt-v1.schema.json','route-remove.json'),
 ('audio-route-resolution-v1.schema.json','resolve-exact.json'),
 ('audio-route-resolution-v1.schema.json','resolve-default.json'),
 ('audio-route-resolution-v1.schema.json','resolve-after-remove.json'),
]
for schema_name,document_name in pairs:
 schema=json.loads((root/'schemas'/schema_name).read_text())
 Draft202012Validator.check_schema(schema)
 Draft202012Validator(schema).validate(json.loads((work/document_name).read_text()))
Draft202012Validator(json.loads((root/'schemas/audio-route-policy-v1.schema.json').read_text())).validate(json.loads(policy.read_text()))
print(f'schema validations: {len(pairs)+1}')
PY

bash -n "$root/tests/run.sh"
printf 'synapse-settings audio routing tests: PASS\n'
