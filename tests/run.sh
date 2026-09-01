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
[[ $($binary --version) == 'synapse-settings 0.7.0-alpha.1' ]]
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

move_stream() {
  local file=$1 index=$2 field=$3 raw_target=$4
  local mode=${SYNAPSE_AUDIO_MOVE_MODE:-success}
  if [[ -n ${SYNAPSE_AUDIO_MOVE_LOG:-} ]]; then
    printf '%s\t%s\t%s\n' "$field" "$index" "$raw_target" >>"$SYNAPSE_AUDIO_MOVE_LOG"
  fi
  case "$mode" in
    fail) exit 65 ;;
    timeout) exec 1>&-; sleep 5; exit 65 ;;
    no-mutate-success) exit 0 ;;
  esac
  python - "$file" "$index" "$field" "$raw_target" <<'PY'
import json,sys
path,index,field,target=sys.argv[1],int(sys.argv[2]),sys.argv[3],sys.argv[4]
raw_to_index={'sink.a':10,'sink.b':11,'source.a':20,'source.b':21}
value=json.load(open(path))
selected=[entry for entry in value if entry.get('index')==index]
if len(selected)!=1 or target not in raw_to_index:
    raise SystemExit(64)
selected[0][field]=raw_to_index[target]
open(path,'w').write(json.dumps(value,separators=(',',':'))+'\n')
PY
  if [[ $mode == identity-change ]]; then
    python - "$file" "$index" <<'PY'
import json,sys
path,index=sys.argv[1],int(sys.argv[2]);value=json.load(open(path))
for entry in value:
    if entry.get('index')==index:
        entry.setdefault('properties',{})['application.process.id']='2147483647'
open(path,'w').write(json.dumps(value,separators=(',',':'))+'\n')
PY
  elif [[ $mode == vanish-after-move ]]; then
    python - "$file" "$index" <<'PY'
import json,sys
path,index=sys.argv[1],int(sys.argv[2]);value=json.load(open(path))
value=[entry for entry in value if entry.get('index')!=index]
open(path,'w').write(json.dumps(value,separators=(',',':'))+'\n')
PY
  elif [[ $mode == postflight-unavailable ]]; then
    : >"$SYNAPSE_AUDIO_FIXTURES/fail-next-stream-list"
  elif [[ $mode == mutate-fail ]]; then
    exit 65
  fi
}

case "${1-} ${2-} ${3-}" in
  '--format=json info ')
    printf '{"default_sink_name":"%s","default_source_name":"%s"}\n' "$(cat "$SYNAPSE_AUDIO_FIXTURES/default-sink")" "$(cat "$SYNAPSE_AUDIO_FIXTURES/default-source")" ;;
  '--format=json list sinks') cat "$SYNAPSE_AUDIO_FIXTURES/sinks.json" ;;
  '--format=json list sources') cat "$SYNAPSE_AUDIO_FIXTURES/sources.json" ;;
  '--format=json list sink-inputs')
    if [[ -e $SYNAPSE_AUDIO_FIXTURES/fail-next-stream-list ]]; then
      rm -f "$SYNAPSE_AUDIO_FIXTURES/fail-next-stream-list"
      exit 65
    fi
    if [[ ${SYNAPSE_AUDIO_MOVE_MODE:-} == change-before-second ]]; then
      count=0
      [[ ! -e $SYNAPSE_AUDIO_FIXTURES/stream-list-count ]] || read -r count <"$SYNAPSE_AUDIO_FIXTURES/stream-list-count"
      count=$((count + 1))
      printf '%s\n' "$count" >"$SYNAPSE_AUDIO_FIXTURES/stream-list-count"
      if [[ $count == 2 ]]; then
        sed 's/"sink":11/"sink":10/' "$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json" >"$SYNAPSE_AUDIO_FIXTURES/sink-inputs.next"
        mv "$SYNAPSE_AUDIO_FIXTURES/sink-inputs.next" "$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json"
      fi
    fi
    cat "$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json" ;;
  '--format=json list source-outputs')
    if [[ -e $SYNAPSE_AUDIO_FIXTURES/fail-next-stream-list ]]; then
      rm -f "$SYNAPSE_AUDIO_FIXTURES/fail-next-stream-list"
      exit 65
    fi
    cat "$SYNAPSE_AUDIO_FIXTURES/source-outputs.json" ;;
  '--format=json list cards') cat "$SYNAPSE_AUDIO_FIXTURES/cards.json" ;;
  'set-default-sink sink.b ')
    printf 'sink.b\n' >"$SYNAPSE_AUDIO_FIXTURES/default-sink" ;;
  'set-default-source source.b ')
    printf 'source.b\n' >"$SYNAPSE_AUDIO_FIXTURES/default-source" ;;
  'move-sink-input '*)
    move_stream "$SYNAPSE_AUDIO_FIXTURES/sink-inputs.json" "$2" sink "$3" ;;
  'move-source-output '*)
    move_stream "$SYNAPSE_AUDIO_FIXTURES/source-outputs.json" "$2" source "$3" ;;
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

# Existing-stream movement is a separate, explicitly acknowledged one-stream
# transaction. Planning is read-only and binds opaque stream/process/endpoint
# identity; apply rechecks it twice, verifies the result, and only restores the
# exact original raw endpoint while the same identity remains provable.
read -r builtin_input usb_input < <(python - "$work/audio-after.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));print(v['inputs'][0]['id'],v['inputs'][1]['id'])
PY
)
cp "$audio/sink-inputs.json" "$work/sink-inputs.base.json"
restore_playback_stream() {
  cp "$work/sink-inputs.base.json" "$audio/sink-inputs.json"
  rm -f "$audio/fail-next-stream-list" "$audio/stream-list-count"
}
move_cohort() {
  python - "$1" <<'PY'
import json,sys
print(json.load(open(sys.argv[1]))['cohort'])
PY
}
apply_stream_move() {
  local output=$1
  shift
  "$@" >"$output"
}

before_plan=$(sha256sum "$audio/sink-inputs.json")
"${AUDIO_ENV[@]}" "$binary" audio plan-stream-move --stream playback-30 \
  --device "$integrated_output" --format json >"$work/stream-move-plan.json"
after_plan=$(sha256sum "$audio/sink-inputs.json")
[[ $before_plan == "$after_plan" ]]
playback_cohort=$(move_cohort "$work/stream-move-plan.json")
python - "$work/stream-move-plan.json" "$headset_output" "$integrated_output" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['schema']=='synapse.settings.audio-existing-stream-move-plan/v1'
assert v['status']=='Planned' and v['stream']=='playback-30' and v['direction']=='output'
assert v['originalDevice']==sys.argv[2] and v['requestedDevice']==sys.argv[3]
assert v['cohort'].startswith('move-') and len(v['cohort'])==21 and v['changed']
assert v['stateAuthority']=='pipewire-pulse-model'
assert v['requiresAcknowledgement']=='synapse-settings/audio-existing-stream-move/v1'
assert v['singleStream'] and v['postflightRequired'] and v['rollbackOnUnverified']
assert not v['applied'] and v['bounded']
PY

: >"$work/stream-move.log"
apply_stream_move "$work/stream-move-applied.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_LOG="$work/stream-move.log" \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$playback_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
python - "$work/stream-move-applied.json" "$headset_output" "$integrated_output" "$audio/sink-inputs.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));state=json.load(open(sys.argv[4]))
assert v['schema']=='synapse.settings.audio-existing-stream-move-receipt/v1'
assert v['status']=='Applied' and v['reason'] is None
assert v['stream']=='playback-30' and v['direction']=='output'
assert v['originalDevice']==sys.argv[2] and v['requestedDevice']==sys.argv[3]
assert v['changed'] and v['moveApplied'] and v['verified']
assert not v['rollbackAttempted'] and not v['rollbackVerified']
assert v['singleStream'] and not v['policyApplied'] and not v['persistentRuleCreated']
assert v['existingStreamMovement'] and v['bounded']
assert state[0]['sink']==10
PY
[[ $(cat "$work/stream-move.log") == $'sink\t30\tsink.a' ]]

# A same-target plan remains read-only and an explicitly submitted no-op is a
# verified AlreadyRouted receipt, never a policy operation.
"${AUDIO_ENV[@]}" "$binary" audio plan-stream-move --stream playback-30 \
  --device "$integrated_output" --format json >"$work/stream-move-plan-same.json"
same_cohort=$(move_cohort "$work/stream-move-plan-same.json")
apply_stream_move "$work/stream-move-already.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream playback-30 \
  --from-device "$integrated_output" --device "$integrated_output" \
  --cohort "$same_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
python - "$work/stream-move-plan-same.json" "$work/stream-move-already.json" <<'PY'
import json,sys
plan=json.load(open(sys.argv[1]));receipt=json.load(open(sys.argv[2]))
assert not plan['changed'] and not plan['applied']
assert receipt['status']=='AlreadyRouted' and receipt['reason'] is None
assert not receipt['changed'] and not receipt['moveApplied'] and receipt['verified']
assert not receipt['rollbackAttempted'] and not receipt['rollbackVerified']
PY

# Stale cohorts, mismatched original devices, and unavailable targets refuse
# before mutation. The opaque cohort cannot be replaced by a raw endpoint.
restore_playback_stream
"${AUDIO_ENV[@]}" "$binary" audio plan-stream-move --stream playback-30 \
  --device "$integrated_output" --format json >"$work/stream-move-plan-stale.json"
stale_cohort=$(move_cohort "$work/stream-move-plan-stale.json")
bad_cohort=move-0000000000000000
[[ $bad_cohort != "$stale_cohort" ]] || bad_cohort=move-0000000000000001
set +e
apply_stream_move "$work/stream-move-stale.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$bad_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
stale_status=$?
set -e
[[ $stale_status == 1 ]]
python - "$work/stream-move-stale.json" "$audio/sink-inputs.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));state=json.load(open(sys.argv[2]))
assert v['status']=='Refused' and v['reason']=='stream-cohort-changed'
assert not any(v[x] for x in ['changed','moveApplied','verified','rollbackAttempted','rollbackVerified'])
assert state[0]['sink']==11
PY
set +e
apply_stream_move "$work/stream-move-original-mismatch.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream playback-30 \
  --from-device "$integrated_output" --device "$integrated_output" \
  --cohort "$stale_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
mismatch_status=$?
apply_stream_move "$work/stream-move-target-unavailable.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device output-0000000000000000 \
  --cohort move-0000000000000000 \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
target_status=$?
set -e
[[ $mismatch_status == 1 && $target_status == 1 ]]
python - "$work/stream-move-original-mismatch.json" "$work/stream-move-target-unavailable.json" <<'PY'
import json,sys
mismatch=json.load(open(sys.argv[1]));target=json.load(open(sys.argv[2]))
assert mismatch['status']=='Refused' and mismatch['reason']=='original-target-mismatch'
assert target['status']=='Refused' and target['reason']=='target-unavailable'
assert not mismatch['moveApplied'] and not target['moveApplied']
PY

# A cohort change between the two apply preflights is refused before the fixed
# move argv. The fixture changes inventory externally on the second read.
restore_playback_stream
: >"$work/stream-move-double-preflight.log"
set +e
apply_stream_move "$work/stream-move-double-preflight.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_MODE=change-before-second \
  SYNAPSE_AUDIO_MOVE_LOG="$work/stream-move-double-preflight.log" \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$stale_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
double_preflight_status=$?
set -e
[[ $double_preflight_status == 1 && ! -s $work/stream-move-double-preflight.log ]]
python - "$work/stream-move-double-preflight.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Refused' and v['reason']=='stream-cohort-changed'
assert not v['moveApplied'] and not v['rollbackAttempted']
PY

# Unsafe or unavailable preflight state is represented by typed refusal receipts
# before a mutation command: provider loss, a vanished stream, an unavailable
# process identity, and an unknown current endpoint are distinct.
restore_playback_stream
touch "$audio/fail-next-stream-list"
set +e
apply_stream_move "$work/stream-move-audio-unavailable.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$stale_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
audio_unavailable_status=$?
set -e
[[ $audio_unavailable_status == 1 ]]

printf '[]\n' >"$audio/sink-inputs.json"
set +e
apply_stream_move "$work/stream-move-vanished-preflight.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$stale_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
vanished_preflight_status=$?
set -e
[[ $vanished_preflight_status == 1 ]]

restore_playback_stream
python - "$audio/sink-inputs.json" <<'PY'
import json,sys
path=sys.argv[1];value=json.load(open(path))
value[0]['properties']['application.process.id']='2147483647'
open(path,'w').write(json.dumps(value,separators=(',',':'))+'\n')
PY
set +e
apply_stream_move "$work/stream-move-process-unavailable.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$stale_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
process_unavailable_status=$?
set -e
[[ $process_unavailable_status == 1 ]]

restore_playback_stream
python - "$audio/sink-inputs.json" <<'PY'
import json,sys
path=sys.argv[1];value=json.load(open(path));value[0]['sink']=999
open(path,'w').write(json.dumps(value,separators=(',',':'))+'\n')
PY
set +e
apply_stream_move "$work/stream-move-current-unavailable.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$stale_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
current_unavailable_status=$?
set -e
[[ $current_unavailable_status == 1 ]]
python - "$work/stream-move-audio-unavailable.json" \
  "$work/stream-move-vanished-preflight.json" \
  "$work/stream-move-process-unavailable.json" \
  "$work/stream-move-current-unavailable.json" <<'PY'
import json,sys
expected=['audio-unavailable','stream-vanished','process-unavailable',
          'current-target-unavailable']
for path,reason in zip(sys.argv[1:],expected):
 value=json.load(open(path))
 assert value['status']=='Refused' and value['reason']==reason
 assert not any(value[key] for key in
                ['changed','moveApplied','verified','rollbackAttempted',
                 'rollbackVerified'])
PY

# Backend failure without a target change needs no rollback.
restore_playback_stream
"${AUDIO_ENV[@]}" "$binary" audio plan-stream-move --stream playback-30 \
  --device "$integrated_output" --format json >"$work/stream-move-plan-fail.json"
fail_cohort=$(move_cohort "$work/stream-move-plan-fail.json")
set +e
apply_stream_move "$work/stream-move-failed.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_MODE=fail \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$fail_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
failed_status=$?
set -e
[[ $failed_status == 1 ]]
python - "$work/stream-move-failed.json" "$audio/sink-inputs.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));state=json.load(open(sys.argv[2]))
assert v['status']=='Failed' and v['reason']=='move-failed'
assert not v['changed'] and not v['moveApplied'] and not v['verified']
assert not v['rollbackAttempted'] and not v['rollbackVerified']
assert state[0]['sink']==11
PY

# A false backend success with no target change is verification failure, while
# a backend failure that nonetheless moved is rolled back to the exact original
# raw endpoint and verified under the same stream identity.
restore_playback_stream
set +e
apply_stream_move "$work/stream-move-unverified.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_MODE=no-mutate-success \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$fail_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
unverified_status=$?
set -e
[[ $unverified_status == 1 ]]
python - "$work/stream-move-unverified.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='verification-failed'
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY
restore_playback_stream
: >"$work/stream-move-rollback.log"
set +e
apply_stream_move "$work/stream-move-rollback.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_MODE=mutate-fail \
  SYNAPSE_AUDIO_MOVE_LOG="$work/stream-move-rollback.log" \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$fail_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
rollback_status=$?
set -e
[[ $rollback_status == 1 ]]
python - "$work/stream-move-rollback.json" "$audio/sink-inputs.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));state=json.load(open(sys.argv[2]))
assert v['status']=='Failed' and v['reason']=='move-failed'
assert not v['changed'] and not v['moveApplied'] and not v['verified']
assert v['rollbackAttempted'] and v['rollbackVerified']
assert state[0]['sink']==11
PY
[[ $(cat "$work/stream-move-rollback.log") == $'sink\t30\tsink.a\nsink\t30\tsink.b' ]]

# Identity or inventory loss after the backend call blocks unsafe rollback.
restore_playback_stream
set +e
apply_stream_move "$work/stream-move-identity-changed.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_MODE=identity-change \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$fail_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
identity_status=$?
set -e
[[ $identity_status == 1 ]]
python - "$work/stream-move-identity-changed.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='stream-identity-changed'
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY
restore_playback_stream
set +e
apply_stream_move "$work/stream-move-vanished-postflight.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_MODE=vanish-after-move \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$fail_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
vanished_postflight_status=$?
set -e
[[ $vanished_postflight_status == 1 ]]
python - "$work/stream-move-vanished-postflight.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='stream-vanished'
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY
restore_playback_stream
set +e
apply_stream_move "$work/stream-move-verification-unavailable.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_MODE=postflight-unavailable \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$fail_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
unavailable_status=$?
set -e
[[ $unavailable_status == 1 ]]
python - "$work/stream-move-verification-unavailable.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='verification-unavailable'
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY

# Move execution is bounded even when the backend stalls.
restore_playback_stream
start=$(date +%s)
set +e
apply_stream_move "$work/stream-move-timeout.json" \
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_MODE=timeout \
  "$binary" audio move-stream --stream playback-30 \
  --from-device "$headset_output" --device "$integrated_output" \
  --cohort "$fail_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
move_timeout_status=$?
set -e
elapsed=$(( $(date +%s) - start ))
[[ $move_timeout_status == 1 ]]
(( elapsed < 4 ))
python - "$work/stream-move-timeout.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='move-timeout'
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY

# Input movement uses the separate source-output backend operation and the same
# typed one-stream receipt contract.
cat >"$audio/source-outputs.json" <<EOF
[{"index":31,"source":21,"mute":false,"volume":{"mono":{"value":32768}},"properties":{"application.name":"PodMic","application.process.id":"$process_pid"}}]
EOF
"${AUDIO_ENV[@]}" "$binary" audio plan-stream-move --stream recording-31 \
  --device "$builtin_input" --format json >"$work/input-stream-move-plan.json"
input_cohort=$(move_cohort "$work/input-stream-move-plan.json")
apply_stream_move "$work/input-stream-move-applied.json" \
  "${AUDIO_ENV[@]}" "$binary" audio move-stream --stream recording-31 \
  --from-device "$usb_input" --device "$builtin_input" \
  --cohort "$input_cohort" \
  --ack synapse-settings/audio-existing-stream-move/v1 --format json
python - "$work/input-stream-move-applied.json" "$audio/source-outputs.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));state=json.load(open(sys.argv[2]))
assert v['status']=='Applied' and v['direction']=='input' and v['stream']=='recording-31'
assert v['changed'] and v['moveApplied'] and v['verified']
assert state[0]['source']==20
PY
printf '[]\n' >"$audio/source-outputs.json"
restore_playback_stream

# Tokens, direction, cohort, and the exact acknowledgement are structural CLI
# gates and fail before any pactl move invocation.
: >"$work/stream-move-invalid.log"
for args in \
  "audio move-stream --stream playback-30 --from-device $headset_output --device $integrated_output --cohort $fail_cohort --ack wrong" \
  "audio move-stream --stream playback-030 --from-device $headset_output --device $integrated_output --cohort $fail_cohort --ack synapse-settings/audio-existing-stream-move/v1" \
  "audio move-stream --stream playback-2147483648 --from-device $headset_output --device $integrated_output --cohort $fail_cohort --ack synapse-settings/audio-existing-stream-move/v1" \
  "audio move-stream --stream playback-30 --from-device $headset_output --device $usb_input --cohort $fail_cohort --ack synapse-settings/audio-existing-stream-move/v1" \
  "audio move-stream --stream playback-30 --from-device $headset_output --device $integrated_output --cohort raw-sink --ack synapse-settings/audio-existing-stream-move/v1" \
  "audio plan-stream-move --stream playback-30 --device $integrated_output --cohort $fail_cohort"; do
  set +e
  # Fixture vectors contain no glob metacharacters.
  # shellcheck disable=SC2086
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_MOVE_LOG="$work/stream-move-invalid.log" \
    "$binary" $args >/dev/null 2>&1
  status=$?
  set -e
  [[ $status == 2 ]]
done
[[ ! -s "$work/stream-move-invalid.log" ]]
[[ ! -e "$work/config/synapse/audio-route-policy-v1.json" ]]

# Private deterministic per-executable and directory audio routing policy.
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
 ('audio-existing-stream-move-plan-v1.schema.json','stream-move-plan.json'),
 ('audio-existing-stream-move-plan-v1.schema.json','stream-move-plan-same.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-applied.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-already.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-stale.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-double-preflight.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-audio-unavailable.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-vanished-preflight.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-process-unavailable.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-current-unavailable.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-failed.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-unverified.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-rollback.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-identity-changed.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-vanished-postflight.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-verification-unavailable.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','stream-move-timeout.json'),
 ('audio-existing-stream-move-receipt-v1.schema.json','input-stream-move-applied.json'),
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
