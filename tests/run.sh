#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

binary=${1:?binary required}
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
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
assert [x['id'] for x in value['sections']]==['layers','audio','graphics','input','themes']
assert all(x['available'] and x['icon'] and x['lazy'] for x in value['sections'])
PY
"$binary" sections --format text | grep -Fq $'graphics\tGrafica\tvideo-display\tavailable'
[[ $($binary --version) == 'synapse-settings 0.2.0-alpha.1' ]]
"$binary" --help | grep -Fq 'synapse-settings graphics policy add-rule'

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
cat >"$audio/sinks.json" <<'EOF'
[
 {"index":10,"name":"sink.a","description":"Integrated audio","mute":false,"volume":{"front-left":{"value":32768},"front-right":{"value":32768}}},
 {"index":11,"name":"sink.b","description":"Gaming headset","mute":false,"volume":{"front-left":{"value":65536},"front-right":{"value":65536}}}
]
EOF
cat >"$audio/sources.json" <<'EOF'
[
 {"index":20,"name":"source.a","description":"Built-in microphone","monitor_of_sink":null,"mute":false,"volume":{"mono":{"value":32768}}},
 {"index":21,"name":"source.b","description":"USB microphone","monitor_of_sink":null,"mute":true,"volume":{"mono":{"value":65536}}},
 {"index":22,"name":"sink.a.monitor","description":"Monitor","monitor_of_sink":10,"mute":false,"volume":{"mono":{"value":65536}}}
]
EOF
cat >"$audio/sink-inputs.json" <<'EOF'
[{"index":30,"sink":11,"mute":false,"volume":{"front-left":{"value":49152},"front-right":{"value":49152}},"properties":{"application.name":"Game"}}]
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
 ('Integrated audio',True,50),('Gaming headset',False,100)]
assert all(x['id'].startswith('output-') and len(x['id'])==23 for x in v['outputs'])
assert [(x['default'],x['muted']) for x in v['inputs']]==[(True,False),(False,True)]
assert all(x['id'].startswith('input-') and len(x['id'])==22 for x in v['inputs'])
assert len(v['streams'])==1 and v['streams'][0]['id']=='playback-30'
assert v['streams'][0]['target']==v['outputs'][1]['id'] and v['streams'][0]['volumePercent']==75
assert len(v['cards'])==1 and v['cards'][0]['id'].startswith('card-')
assert v['cards'][0]['label']=='Primary audio card' and v['cards'][0]['activeProfile']=='HiFi'
PY
read -r integrated_output gaming_output < <(python - "$work/audio-inventory.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));print(v['outputs'][0]['id'],v['outputs'][1]['id'])
PY
)
"${AUDIO_ENV[@]}" "$binary" audio plan-default --direction output --device "$gaming_output" --format json >"$work/audio-plan.json"
python - "$work/audio-plan.json" "$gaming_output" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['schema']=='synapse.settings.audio-default-plan/v1'
assert v['device']==sys.argv[2] and v['changed'] is True and v['applied'] is False
PY
"${AUDIO_ENV[@]}" "$binary" audio set-default --direction output --device "$gaming_output" \
  --ack synapse-settings/audio-default/v1 --format json >"$work/audio-receipt.json"
python - "$work/audio-receipt.json" "$gaming_output" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['schema']=='synapse.settings.audio-default-receipt/v1'
assert v['status']=='Applied' and v['device']==sys.argv[2] and v['changed'] and v['applied']
PY
"${AUDIO_ENV[@]}" "$binary" audio inventory --format json >"$work/audio-after.json"
python - "$work/audio-after.json" "$gaming_output" <<'PY'
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
for args in \
  "audio set-default --direction output --device $integrated_output --ack wrong" \
  'audio set-default --direction output --device raw-device --ack synapse-settings/audio-default/v1' \
  "audio plan-default --direction output --device $integrated_output --ack synapse-settings/audio-default/v1"; do
  set +e
  "${AUDIO_ENV[@]}" "$binary" $args >/dev/null 2>&1
  status=$?
  set -e
  [[ $status != 0 ]]
done

# Bounded GPU inventory and private deterministic application policy.
drm="$work/drm"
mkdir -p "$drm/card0/device/drm/renderD128" "$drm/card1/device/drm/renderD129"
printf '0x8086\n' >"$drm/card0/device/vendor"
printf '0x1234\n' >"$drm/card0/device/device"
printf '1\n' >"$drm/card0/device/boot_vga"
printf 'PCI_SLOT_NAME=0000:00:02.0\n' >"$drm/card0/device/uevent"
ln -s ../../drivers/i915 "$drm/card0/device/driver"
printf '0x10de\n' >"$drm/card1/device/vendor"
printf '0x5678\n' >"$drm/card1/device/device"
printf '0\n' >"$drm/card1/device/boot_vga"
printf 'PCI_SLOT_NAME=0000:01:00.0\n' >"$drm/card1/device/uevent"
ln -s ../../drivers/nvidia "$drm/card1/device/driver"
mkdir -p "$work/config/synapse" "$work/steam/library" "$work/steam2"
chmod 700 "$work/config/synapse"
printf '#!/usr/bin/env sh\nexit 0\n' >"$work/steam/library/game"
printf '#!/usr/bin/env sh\nexit 0\n' >"$work/steam2/game"
chmod 755 "$work/steam/library/game" "$work/steam2/game"
policy="$work/config/synapse/graphics-policy-v1.json"
GRAPHICS_ENV=(env HOME="$work" SYNAPSE_DRM_CLASS="$drm" SYNAPSE_GRAPHICS_POLICY="$policy")
"${GRAPHICS_ENV[@]}" "$binary" graphics inventory --format json >"$work/graphics-inventory.json"
read -r intel nvidia < <(python - "$work/graphics-inventory.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['schema']=='synapse.settings.graphics-inventory/v1'
assert v['gpuCount']==2 and not v['launchBrokerAvailable'] and not v['runningProcessMigration']
g={x['vendorId']:x for x in v['gpus']}
assert g['0x8086']['displayOwner'] and not g['0x8086']['gamingCandidate']
assert not g['0x10de']['displayOwner'] and g['0x10de']['gamingCandidate']
assert g['0x10de']['strategy']=='nvidia-prime-offload'
print(g['0x8086']['id'],g['0x10de']['id'])
PY
)
"${GRAPHICS_ENV[@]}" "$binary" graphics policy show --format json >"$work/policy-empty.json"
python - "$work/policy-empty.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['schema']=='synapse.settings.graphics-policy-view/v1'
assert v['generation']==0 and v['defaultGpu']=='system'
assert v['rules']==[] and v['present'] is False and not v['enforcementAvailable']
PY
"${GRAPHICS_ENV[@]}" "$binary" graphics policy set-default --gpu "$intel" \
  --ack synapse-settings/graphics-policy/v1 --format json >"$work/set-default.json"
"${GRAPHICS_ENV[@]}" "$binary" graphics policy add-rule --match directory --path "$work/steam" --gpu "$nvidia" \
  --ack synapse-settings/graphics-policy/v1 --format json >"$work/add-directory.json"
"${GRAPHICS_ENV[@]}" "$binary" graphics resolve --path "$work/steam/library/game" --format json >"$work/resolve-directory.json"
python - "$work/resolve-directory.json" "$nvidia" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['selectedGpu']==sys.argv[2] and v['source']=='rule'
assert v['rule']=='rule-0001' and v['available'] and not v['enforcementAvailable']
PY
"${GRAPHICS_ENV[@]}" "$binary" graphics policy add-rule --match executable --path "$work/steam/library/game" --gpu "$intel" \
  --ack synapse-settings/graphics-policy/v1 --format json >"$work/add-executable.json"
"${GRAPHICS_ENV[@]}" "$binary" graphics resolve --path "$work/steam/library/game" --format json >"$work/resolve-exact.json"
python - "$work/resolve-exact.json" "$intel" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['selectedGpu']==sys.argv[2]
assert v['rule']=='rule-0002' and v['source']=='rule'
PY
"${GRAPHICS_ENV[@]}" "$binary" graphics resolve --path "$work/steam2/game" --format json >"$work/resolve-boundary.json"
python - "$work/resolve-boundary.json" "$intel" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['selectedGpu']==sys.argv[2]
assert v['source']=='default' and v['rule'] is None
PY
"${GRAPHICS_ENV[@]}" "$binary" graphics policy remove-rule --rule rule-0002 \
  --ack synapse-settings/graphics-policy/v1 --format json >"$work/remove-rule.json"
"${GRAPHICS_ENV[@]}" "$binary" graphics policy show --format json >"$work/policy-show.json"
python - "$work/policy-show.json" "$nvidia" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['generation']==4 and v['present']
assert len(v['rules'])==1 and v['rules'][0]['id']=='rule-0001'
assert v['rules'][0]['gpu']==sys.argv[2]
assert v['rules'][0]['match']['displayPath']=='~/steam'
PY
[[ $(stat -c %a "$policy") == 600 ]]
python - "$policy" <<'PY'
import json,sys
raw=open(sys.argv[1]).read();assert raw.endswith('\n') and len(raw.splitlines())==1
value=json.loads(raw);assert value['schema']=='synapse.settings.graphics-policy/v1'
PY

for command in \
  "graphics policy set-default --gpu missing --ack synapse-settings/graphics-policy/v1" \
  "graphics policy set-default --gpu $intel --ack wrong" \
  "graphics policy add-rule --match directory --path $work/steam --gpu $nvidia --ack synapse-settings/graphics-policy/v1" \
  "graphics policy add-rule --match process-name --path $work/steam --gpu $nvidia --ack synapse-settings/graphics-policy/v1"; do
  set +e
  "${GRAPHICS_ENV[@]}" "$binary" $command >/dev/null 2>&1
  status=$?
  set -e
  [[ $status != 0 ]]
done

bad="$work/config/synapse/bad.json"
printf '{"schema":"synapse.settings.graphics-policy/v1","generation":0,"defaultGpu":"system","rules":[],"unknown":true}\n' >"$bad"
chmod 600 "$bad"
set +e
env HOME="$work" SYNAPSE_DRM_CLASS="$drm" SYNAPSE_GRAPHICS_POLICY="$bad" \
  "$binary" graphics policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status == 1 ]]

loose="$work/config/synapse/loose.json"
cp "$policy" "$loose";chmod 644 "$loose"
set +e
env HOME="$work" SYNAPSE_DRM_CLASS="$drm" SYNAPSE_GRAPHICS_POLICY="$loose" \
  "$binary" graphics policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status == 1 ]]
link="$work/config/synapse/link.json"
ln -s "$policy" "$link"
set +e
env HOME="$work" SYNAPSE_DRM_CLASS="$drm" SYNAPSE_GRAPHICS_POLICY="$link" \
  "$binary" graphics policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status == 1 ]]
duplicate="$work/config/synapse/duplicate.json"
printf '{"schema":"synapse.settings.graphics-policy/v1","generation":0,"generation":1,"defaultGpu":"system","rules":[]}\n' >"$duplicate"
chmod 600 "$duplicate"
set +e
env HOME="$work" SYNAPSE_DRM_CLASS="$drm" SYNAPSE_GRAPHICS_POLICY="$duplicate" \
  "$binary" graphics policy show --format json >/dev/null 2>&1
status=$?
set -e
[[ $status == 1 ]]

python - "$root" "$work/sections.json" "$work/audio-inventory.json" "$work/audio-plan.json" \
  "$work/audio-receipt.json" "$work/graphics-inventory.json" "$work/policy-show.json" \
  "$work/set-default.json" "$work/add-directory.json" "$work/resolve-directory.json" "$policy" <<'PY'
import json,sys
from pathlib import Path
from jsonschema import Draft202012Validator
root=Path(sys.argv[1]); names=[
 'sections-v2.schema.json','audio-inventory-v1.schema.json','audio-default-plan-v1.schema.json',
 'audio-default-receipt-v1.schema.json','graphics-inventory-v1.schema.json',
 'graphics-policy-view-v1.schema.json','graphics-policy-receipt-v1.schema.json',
 'graphics-policy-receipt-v1.schema.json','graphics-resolution-v1.schema.json',
 'graphics-policy-v1.schema.json']
for schema_name,value_path in zip(names,sys.argv[2:]):
 schema=json.loads((root/'schemas'/schema_name).read_text());Draft202012Validator.check_schema(schema)
 Draft202012Validator(schema).validate(json.loads(Path(value_path).read_text()))
PY

for args in 'unknown' 'sections --format yaml' 'layers --unknown' 'audio unknown' 'graphics unknown'; do
  set +e
  "$binary" $args >/dev/null 2>&1
  status=$?
  set -e
  [[ $status == 2 ]]
done

cp -a "$composition" "$work/oversized"
python - "$work/oversized/layers.tsv" <<'PY'
import sys
with open(sys.argv[1],'a') as out: out.write('x'*65537+'\n')
PY
set +e
env SYNAPSE_COMPOSITION_DIR="$work/oversized" SYNAPSE_PACMAN_LOCAL="$pacman" \
  SYNAPSE_DOCKER="$work/missing" "$binary" layers --format json >/dev/null 2>&1
status=$?
set -e
[[ $status == 1 ]]

echo 'synapse-settings tests: PASS'
