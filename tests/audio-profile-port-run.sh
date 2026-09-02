#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

binary=${1:?binary required}
fake=${2:?fake pactl required}
output_dir=${3:-}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fixtures="$work/fixtures"
mkdir -p "$fixtures/base"

cat >"$fixtures/cards.json" <<'EOF'
[
 {"index":40,"name":"card.alpha","description":"Primary audio card",
  "profiles":{
   "profile.pro":{"description":"Pro Audio"},
   "profile.disabled":{"description":"Disabled profile","available":false},
   "profile.hifi":{"description":"High Fidelity","available":true},
   "profile.off":{"description":"Off","available":true}},
  "active_profile":"profile.hifi"}
]
EOF
cat >"$fixtures/sinks.json" <<'EOF'
[
 {"index":10,"name":"sink.alpha","description":"Integrated output",
  "ports":[
   {"name":"port.headphones","description":"Headphones","availability":"availability unknown"},
   {"name":"port.blocked","description":"Unavailable jack","availability":"not available"},
   {"name":"port.speaker","description":"Speakers","availability":"available"}],
  "active_port":"port.speaker"},
 {"index":11,"name":"sink.usb","description":"USB output",
  "ports":[{"name":"port.usb","description":"USB output"}],
  "active_port":"port.usb"}
]
EOF
cat >"$fixtures/sources.json" <<'EOF'
[
 {"index":20,"name":"source.alpha","description":"Integrated input",
  "monitor_of_sink":null,"monitor_source":"",
  "ports":[
   {"name":"port.internal","description":"Internal microphone","availability":"available"},
   {"name":"port.mic","description":"External microphone","availability":"availability unknown"}],
  "active_port":"port.internal"},
 {"index":21,"name":"sink.alpha.monitor","description":"Monitor",
  "monitor_of_sink":10,"monitor_source":"sink.alpha",
  "ports":[{"name":"port.monitor","description":"Monitor","availability":"available"}],
  "active_port":"port.monitor"}
]
EOF
cp "$fixtures/cards.json" "$fixtures/base/cards.json"
cp "$fixtures/sinks.json" "$fixtures/base/sinks.json"
cp "$fixtures/sources.json" "$fixtures/base/sources.json"

log="$work/selection.log"
count="$work/selection.count"
ack=synapse-settings/audio-profile-port/v1
SELECTION_ENV=(env SYNAPSE_PACTL="$fake" \
  SYNAPSE_AUDIO_SELECTION_FIXTURES="$fixtures" \
  SYNAPSE_AUDIO_SELECTION_LOG="$log" \
  SYNAPSE_AUDIO_SELECTION_COUNT="$count")

restore_state() {
  cp "$fixtures/base/cards.json" "$fixtures/cards.json"
  cp "$fixtures/base/sinks.json" "$fixtures/sinks.json"
  cp "$fixtures/base/sources.json" "$fixtures/sources.json"
  rm -f "$log" "$count" "$fixtures/mutation-state.json" \
    "$fixtures/selection-read-count" "$fixtures/selection-list-count" \
    "$fixtures/fail-next-selection-list"
}

json_field() {
  python - "$1" "$2" <<'PY'
import json,sys
value=json.load(open(sys.argv[1],encoding='utf-8'))
print(value[sys.argv[2]])
PY
}

active_value() {
  python - "$fixtures/$1" "$2" "$3" <<'PY'
import json,sys
items=json.load(open(sys.argv[1],encoding='utf-8'))
selected=[item for item in items if item.get('name')==sys.argv[2]]
assert len(selected)==1
print(selected[0][sys.argv[3]])
PY
}

restore_state
before=$(sha256sum "$fixtures/cards.json" "$fixtures/sinks.json" \
  "$fixtures/sources.json")
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/inventory.json"
after=$(sha256sum "$fixtures/cards.json" "$fixtures/sinks.json" \
  "$fixtures/sources.json")
[[ $before == "$after" && ! -e $log ]]
python - "$work/inventory.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1],encoding='utf-8'))
assert set(v)=={'schema','stateAuthority','available','reason',
 'mutationAvailable','cards','endpoints','hardwareReadback',
 'hardwareExactRollback','bounded'}
assert v['schema']=='synapse.settings.audio-profile-port-inventory/v1'
assert v['stateAuthority']=='pipewire-pulse-model'
assert v['available'] and v['reason'] is None and v['mutationAvailable']
assert not v['hardwareReadback'] and not v['hardwareExactRollback'] and v['bounded']
assert len(v['cards'])==1
card=v['cards'][0]
assert set(card)=={'id','label','activeProfile','activeProfileLabel',
 'mutationAvailable','profiles'}
assert card['id'].startswith('card-') and len(card['id'])==21
assert card['label']=='Primary audio card'
assert card['activeProfile'].startswith('profile-')
assert card['activeProfileLabel']=='High Fidelity' and card['mutationAvailable']
assert [x['label'] for x in card['profiles']]==[
 'Disabled profile','High Fidelity','Off','Pro Audio']
assert [x['availability'] for x in card['profiles']]==[
 'unavailable','available','available','unknown']
assert len(v['endpoints'])==3
assert [x['direction'] for x in v['endpoints']]==['output','output','input']
assert all(x['activePort'].startswith('port-') for x in v['endpoints'])
assert all(x['mutationAvailable'] for x in v['endpoints'])
assert all(x['id'].startswith(x['direction']+'-') for x in v['endpoints'])
usb=next(x for x in v['endpoints'] if x['label']=='USB output')
assert [x['availability'] for x in usb['ports']]==['unknown']
text=open(sys.argv[1],encoding='utf-8').read()
for raw in ('card.alpha','profile.hifi','profile.pro','sink.alpha',
            'source.alpha','port.speaker','port.headphones'):
 assert raw not in text
PY

read -r card_id profile_hifi profile_pro profile_off profile_disabled \
  output_id port_speaker port_headphones port_blocked input_id port_internal port_mic \
  < <(python - "$work/inventory.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1],encoding='utf-8'))
card=v['cards'][0]
profiles={x['label']:x['id'] for x in card['profiles']}
endpoints={(x['direction'],x['label']):x for x in v['endpoints']}
out=endpoints[('output','Integrated output')]
inp=endpoints[('input','Integrated input')]
out_ports={x['label']:x['id'] for x in out['ports']}
in_ports={x['label']:x['id'] for x in inp['ports']}
print(card['id'],profiles['High Fidelity'],profiles['Pro Audio'],profiles['Off'],
      profiles['Disabled profile'],out['id'],out['activePort'],
      out_ports['Headphones'],out_ports['Unavailable jack'],inp['id'],
      inp['activePort'],in_ports['External microphone'])
PY
)
[[ $profile_hifi != "$profile_pro" && $profile_off != "$profile_hifi" ]]
[[ $port_speaker != "$port_headphones" && $port_blocked != "$port_speaker" ]]

# Missing presentation labels stay empty and never fall back to raw backend
# identity in inventories or plans.
restore_state
python - "$fixtures/cards.json" "$fixtures/sinks.json" \
    "$fixtures/sources.json" <<'PY'
import json,sys
for path in sys.argv[1:]:
 values=json.load(open(path,encoding='utf-8'))
 for value in values:
  value.pop('description',None)
  profiles=value.get('profiles',{})
  ports=value.get('ports',[])
  for option in (profiles.values() if isinstance(profiles,dict) else []):
   option.pop('description',None)
  for option in (ports if isinstance(ports,list) else []):
   option.pop('description',None)
 open(path,'w',encoding='utf-8').write(json.dumps(values,separators=(',',':'))+'\n')
PY
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/unlabelled-inventory.json"
"${SELECTION_ENV[@]}" "$binary" audio plan-profile --card "$card_id" \
  --profile "$profile_pro" --format json >"$work/unlabelled-plan.json"
python - "$work/unlabelled-inventory.json" "$work/unlabelled-plan.json" <<'PY'
import json,sys
inventory=json.load(open(sys.argv[1],encoding='utf-8'))
plan=json.load(open(sys.argv[2],encoding='utf-8'))
assert all(not card['label'] and not card['activeProfileLabel'] and
           all(not option['label'] for option in card['profiles'])
           for card in inventory['cards'])
assert all(not endpoint['label'] and not endpoint['activePortLabel'] and
           all(not option['label'] for option in endpoint['ports'])
           for endpoint in inventory['endpoints'])
assert not plan['targetLabel'] and not plan['originalLabel'] and not plan['requestedLabel']
text=open(sys.argv[1],encoding='utf-8').read()+open(sys.argv[2],encoding='utf-8').read()
for raw in ('card.alpha','profile.hifi','profile.pro','sink.alpha',
            'source.alpha','port.speaker','port.headphones'):
 assert raw not in text
PY
[[ ! -e $log ]]

# Omitted collections advertise no choices, but remain valid when the same
# target advertises no active choice.
restore_state
python - "$fixtures/cards.json" "$fixtures/sinks.json" <<'PY'
import json,sys
for path,choices,active in ((sys.argv[1],'profiles','active_profile'),
                            (sys.argv[2],'ports','active_port')):
 values=json.load(open(path,encoding='utf-8'))
 values[0].pop(choices)
 values[0].pop(active)
 open(path,'w',encoding='utf-8').write(json.dumps(values,separators=(',',':'))+'\n')
PY
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/omitted-choices-inventory.json"
python - "$work/omitted-choices-inventory.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1],encoding='utf-8'))
assert value['available']
card=value['cards'][0]
output=next(item for item in value['endpoints']
            if item['direction']=='output' and item['label']=='Integrated output')
assert card['profiles']==[] and card['activeProfile'] is None
assert output['ports']==[] and output['activePort'] is None
assert not card['mutationAvailable'] and not output['mutationAvailable']
PY

# A missing or explicit null active choice is valid read-only state but cannot
# authorize mutation for that target.
restore_state
python - "$fixtures/cards.json" "$fixtures/sinks.json" <<'PY'
import json,sys
cards=json.load(open(sys.argv[1],encoding='utf-8'))
cards[0].pop('active_profile')
open(sys.argv[1],'w',encoding='utf-8').write(
 json.dumps(cards,separators=(',',':'))+'\n')
sinks=json.load(open(sys.argv[2],encoding='utf-8'))
sinks[0]['active_port']=None
open(sys.argv[2],'w',encoding='utf-8').write(
 json.dumps(sinks,separators=(',',':'))+'\n')
PY
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/null-active-inventory.json"
python - "$work/null-active-inventory.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1],encoding='utf-8'))
assert value['available']
card=value['cards'][0]
output=next(item for item in value['endpoints'] if item['direction']=='output')
assert card['activeProfile'] is None and card['activeProfileLabel'] is None
assert output['activePort'] is None and output['activePortLabel'] is None
assert not card['mutationAvailable'] and not output['mutationAvailable']
PY

# Profile planning is read-only and binds only opaque target/selection tokens.
restore_state
before=$(sha256sum "$fixtures/cards.json")
"${SELECTION_ENV[@]}" "$binary" audio plan-profile --card "$card_id" \
  --profile "$profile_pro" --format json >"$work/profile-plan.json"
after=$(sha256sum "$fixtures/cards.json")
[[ $before == "$after" && ! -e $log ]]
profile_cohort=$(json_field "$work/profile-plan.json" cohort)
python - "$work/profile-plan.json" "$card_id" "$profile_hifi" "$profile_pro" <<'PY'
import json,sys
v=json.load(open(sys.argv[1],encoding='utf-8'))
assert set(v)=={'schema','status','selection','target','targetType','targetLabel',
 'originalSelection','originalLabel','requestedSelection','requestedLabel',
 'requestedAvailability','cohort','changed','stateAuthority',
 'requiresAcknowledgement','singleTarget','postflightRequired',
 'rollbackOnUnverified','graphMayChange','signalPathMayChange',
 'playbackStarted','captureStarted','defaultChanged','policyChanged','audibilityVerified',
 'hardwareReadback','hardwareExactRollback','applied','bounded'}
assert v['schema']=='synapse.settings.audio-profile-port-plan/v1'
assert v['status']=='Planned' and v['selection']=='profile'
assert v['target']==sys.argv[2] and v['targetType']=='card'
assert v['originalSelection']==sys.argv[3] and v['requestedSelection']==sys.argv[4]
assert v['originalLabel']=='High Fidelity' and v['requestedLabel']=='Pro Audio'
assert v['requestedAvailability']=='unknown' and v['changed']
assert v['cohort'].startswith('selection-') and len(v['cohort'])==26
assert v['stateAuthority']=='pipewire-pulse-model'
assert v['requiresAcknowledgement']=='synapse-settings/audio-profile-port/v1'
assert v['singleTarget'] and v['postflightRequired'] and v['rollbackOnUnverified']
assert v['graphMayChange'] and v['signalPathMayChange'] and v['bounded']
assert not any(v[x] for x in ('playbackStarted','captureStarted',
                              'defaultChanged','policyChanged',
                              'audibilityVerified','hardwareReadback',
                              'hardwareExactRollback','applied'))
PY

# Labels are mutable presentation metadata: drift after planning neither changes
# the cohort nor blocks the exact private/public identity transaction. A fresh
# inventory supersedes the stale plan labels after acknowledgement.
python - "$fixtures/cards.json" <<'PY'
import json,sys
path=sys.argv[1];values=json.load(open(path,encoding='utf-8'))
values[0]['description']='Renamed audio card'
values[0]['profiles']['profile.hifi']['description']='Renamed High Fidelity'
values[0]['profiles']['profile.pro']['description']='Renamed Pro Audio'
open(path,'w',encoding='utf-8').write(json.dumps(values,separators=(',',':'))+'\n')
PY

# One fixed profile setter is followed by same-card, exact-value verification.
: >"$log"
"${SELECTION_ENV[@]}" "$binary" audio set-profile --card "$card_id" \
  --from-profile "$profile_hifi" --profile "$profile_pro" \
  --cohort "$profile_cohort" --ack "$ack" --format json \
  >"$work/profile-applied.json"
python - "$work/profile-applied.json" "$card_id" "$profile_hifi" "$profile_pro" <<'PY'
import json,sys
v=json.load(open(sys.argv[1],encoding='utf-8'))
assert v['schema']=='synapse.settings.audio-profile-port-receipt/v1'
assert v['status']=='Applied' and v['reason'] is None
assert v['selection']=='profile' and v['target']==sys.argv[2]
assert v['targetType']=='card' and v['originalSelection']==sys.argv[3]
assert v['requestedSelection']==sys.argv[4]
assert v['changed'] and v['mutationAttempted'] and v['verified']
assert not v['rollbackAttempted'] and not v['rollbackVerified']
assert v['profileChanged'] and not v['portChanged']
assert v['graphMayChange'] and v['signalPathMayChange']
assert not v['playbackStarted'] and not v['captureStarted']
assert not v['defaultChanged'] and not v['policyChanged']
assert not v['audibilityVerified']
assert not v['hardwareReadback'] and not v['hardwareExactRollback'] and v['bounded']
PY
[[ $(cat "$log") == $'set-card-profile\tcard.alpha\tprofile.pro' ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.pro ]]
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/profile-label-drift-inventory.json"
python - "$work/profile-label-drift-inventory.json" "$card_id" "$profile_hifi" \
    "$profile_pro" <<'PY'
import json,sys
value=json.load(open(sys.argv[1],encoding='utf-8'))
card=next(item for item in value['cards'] if item['id']==sys.argv[2])
assert card['label']=='Renamed audio card'
labels={item['id']:item['label'] for item in card['profiles']}
assert labels[sys.argv[3]]=='Renamed High Fidelity'
assert labels[sys.argv[4]]=='Renamed Pro Audio'
PY

# A submitted same-selection transaction verifies without a setter.
"${SELECTION_ENV[@]}" "$binary" audio plan-profile --card "$card_id" \
  --profile "$profile_pro" --format json >"$work/profile-same-plan.json"
same_cohort=$(json_field "$work/profile-same-plan.json" cohort)
: >"$log"
"${SELECTION_ENV[@]}" "$binary" audio set-profile --card "$card_id" \
  --from-profile "$profile_pro" --profile "$profile_pro" \
  --cohort "$same_cohort" --ack "$ack" --format json \
  >"$work/profile-same.json"
python - "$work/profile-same-plan.json" "$work/profile-same.json" <<'PY'
import json,sys
plan=json.load(open(sys.argv[1]));receipt=json.load(open(sys.argv[2]))
assert not plan['changed'] and not plan['applied']
assert receipt['status']=='AlreadySet' and receipt['reason'] is None
assert not receipt['changed'] and not receipt['mutationAttempted']
assert receipt['verified'] and not receipt['profileChanged']
PY
[[ ! -s $log ]]

# Output and input ports use distinct fixed operations and the shared strict
# profile/port contract without exposing raw backend names.
restore_state
"${SELECTION_ENV[@]}" "$binary" audio plan-port --direction output \
  --device "$output_id" --port "$port_headphones" --format json \
  >"$work/output-port-plan.json"
port_cohort=$(json_field "$work/output-port-plan.json" cohort)
: >"$log"
"${SELECTION_ENV[@]}" "$binary" audio set-port --direction output \
  --device "$output_id" --from-port "$port_speaker" \
  --port "$port_headphones" --cohort "$port_cohort" --ack "$ack" \
  --format json >"$work/output-port-applied.json"
python - "$work/output-port-plan.json" "$work/output-port-applied.json" <<'PY'
import json,sys
plan=json.load(open(sys.argv[1]));receipt=json.load(open(sys.argv[2]))
assert plan['selection']=='port' and plan['targetType']=='output'
assert plan['requestedAvailability']=='unknown'
assert not plan['graphMayChange'] and plan['signalPathMayChange']
assert receipt['status']=='Applied' and receipt['selection']=='port'
assert receipt['targetType']=='output' and receipt['portChanged']
assert not receipt['profileChanged'] and not receipt['graphMayChange']
PY
[[ $(cat "$log") == $'set-sink-port\tsink.alpha\tport.headphones' ]]
[[ $(active_value sinks.json sink.alpha active_port) == port.headphones ]]

restore_state
"${SELECTION_ENV[@]}" "$binary" audio plan-port --direction input \
  --device "$input_id" --port "$port_mic" --format json \
  >"$work/input-port-plan.json"
input_cohort=$(json_field "$work/input-port-plan.json" cohort)
: >"$log"
"${SELECTION_ENV[@]}" "$binary" audio set-port --direction input \
  --device "$input_id" --from-port "$port_internal" --port "$port_mic" \
  --cohort "$input_cohort" --ack "$ack" --format json \
  >"$work/input-port-applied.json"
[[ $(cat "$log") == $'set-source-port\tsource.alpha\tport.mic' ]]
[[ $(active_value sources.json source.alpha active_port) == port.mic ]]

# Unavailable choices and malformed/cross-kind tokens fail before mutation.
restore_state
: >"$log"
set +e
"${SELECTION_ENV[@]}" "$binary" audio plan-profile --card "$card_id" \
  --profile "$profile_disabled" --format json >/dev/null 2>&1
unavailable_status=$?
"${SELECTION_ENV[@]}" "$binary" audio plan-port --direction output \
  --device "$output_id" --port "$port_blocked" --format json >/dev/null 2>&1
port_unavailable_status=$?
set -e
[[ $unavailable_status == 1 && $port_unavailable_status == 1 && ! -s $log ]]
for args in \
  "audio plan-profile --card card.alpha --profile $profile_pro" \
  "audio plan-profile --card $card_id --profile $port_headphones" \
  "audio set-profile --card $card_id --from-profile $profile_hifi --profile $profile_pro --cohort $profile_cohort --ack wrong" \
  "audio set-port --direction output --device $output_id --port $port_headphones --cohort $port_cohort --ack $ack" \
  "audio plan-port --direction input --device $output_id --port $port_headphones"; do
  set +e
  # Reviewed vectors contain no glob metacharacters.
  # shellcheck disable=SC2086
  "${SELECTION_ENV[@]}" "$binary" $args >/dev/null 2>&1
  status=$?
  set -e
  [[ $status == 2 ]]
done
[[ ! -s $log ]]

# Original-state and cohort drift are typed refusals before any setter.
restore_state
"${SELECTION_ENV[@]}" "$binary" audio plan-profile --card "$card_id" \
  --profile "$profile_pro" --format json >"$work/stale-plan.json"
stale_cohort=$(json_field "$work/stale-plan.json" cohort)
python - "$fixtures/cards.json" <<'PY'
import json,sys
p=sys.argv[1];v=json.load(open(p));v[0]['active_profile']='profile.off'
open(p,'w').write(json.dumps(v,separators=(',',':'))+'\n')
PY
: >"$log"
set +e
"${SELECTION_ENV[@]}" "$binary" audio set-profile --card "$card_id" \
  --from-profile "$profile_hifi" --profile "$profile_pro" \
  --cohort "$stale_cohort" --ack "$ack" --format json \
  >"$work/original-mismatch.json"
status=$?
set -e
[[ $status == 1 && ! -s $log ]]
python - "$work/original-mismatch.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Refused' and v['reason']=='original-selection-mismatch'
assert not v['mutationAttempted'] and not v['rollbackAttempted']
PY

restore_state
bad_cohort=selection-0000000000000000
[[ $bad_cohort != "$stale_cohort" ]] || bad_cohort=selection-0000000000000001
: >"$log"
set +e
"${SELECTION_ENV[@]}" "$binary" audio set-profile --card "$card_id" \
  --from-profile "$profile_hifi" --profile "$profile_pro" \
  --cohort "$bad_cohort" --ack "$ack" --format json \
  >"$work/cohort-mismatch.json"
status=$?
set -e
[[ $status == 1 && ! -s $log ]]
python - "$work/cohort-mismatch.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Refused' and v['reason']=='selection-cohort-changed'
assert not v['mutationAttempted']
PY

# A change between the two apply preflights invalidates the cohort.
restore_state
"${SELECTION_ENV[@]}" "$binary" audio plan-profile --card "$card_id" \
  --profile "$profile_pro" --format json >"$work/double-plan.json"
double_cohort=$(json_field "$work/double-plan.json" cohort)
drift='{"file":"cards.json","target":"card.alpha","field":"active_profile","options":"profiles","original":"profile.hifi","requested":"profile.off"}'
: >"$log"
set +e
"${SELECTION_ENV[@]}" SYNAPSE_AUDIO_SELECTION_MODE=change-before-second \
  SYNAPSE_AUDIO_SELECTION_DRIFT_STATE="$drift" \
  "$binary" audio set-profile --card "$card_id" \
  --from-profile "$profile_hifi" --profile "$profile_pro" \
  --cohort "$double_cohort" --ack "$ack" --format json \
  >"$work/double-refused.json"
status=$?
set -e
[[ $status == 1 && ! -s $log ]]
python - "$work/double-refused.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Refused' and v['reason']=='selection-cohort-changed'
assert not v['mutationAttempted']
PY

run_profile_failure() {
  local mode=$1 expected_reason=$2 rollback_attempted=$3 rollback_verified=$4
  local option_alias=${5:-}
  restore_state
  "${SELECTION_ENV[@]}" "$binary" audio plan-profile --card "$card_id" \
    --profile "$profile_pro" --format json >"$work/$mode-plan.json"
  local cohort
  cohort=$(json_field "$work/$mode-plan.json" cohort)
  : >"$log"
  local start elapsed
  start=$(date +%s)
  set +e
  "${SELECTION_ENV[@]}" SYNAPSE_AUDIO_SELECTION_MODE="$mode" \
    SYNAPSE_AUDIO_SELECTION_TEST_OPTION_ALIAS="$option_alias" \
    "$binary" audio set-profile --card "$card_id" \
    --from-profile "$profile_hifi" --profile "$profile_pro" \
    --cohort "$cohort" --ack "$ack" --format json >"$work/$mode.json"
  local status=$?
  set -e
  elapsed=$(( $(date +%s) - start ))
  [[ $status == 1 ]]
  [[ $mode != timeout-after-mutate ]] || (( elapsed < 5 ))
  python - "$work/$mode.json" "$expected_reason" "$rollback_attempted" "$rollback_verified" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));attempted=sys.argv[3]=='true';verified=sys.argv[4]=='true'
assert v['status']=='Failed' and v['reason']==sys.argv[2]
assert not v['changed'] and v['mutationAttempted'] and not v['verified']
assert v['rollbackAttempted'] is attempted
assert v['rollbackVerified'] is verified
assert not v['profileChanged'] and not v['portChanged']
PY
}

# False success without a state change is unverified and needs no rollback.
run_profile_failure no-mutate-success verification-failed false false
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.hifi ]]

# A failed setter with no observed change is explicit and not compensated.
run_profile_failure fail mutation-failed false false
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.hifi ]]

# Failure/timeout after a visible requested state triggers one exact
# software-model restoration, never a requested retry.
for mode in fail-after-mutate timeout-after-mutate; do
  reason=mutation-failed
  [[ $mode != timeout-after-mutate ]] || reason=mutation-timeout
  run_profile_failure "$mode" "$reason" true true
  [[ $(cat "$log") == $'set-card-profile\tcard.alpha\tprofile.pro\nset-card-profile\tcard.alpha\tprofile.hifi' ]]
  [[ $(active_value cards.json card.alpha active_profile) == profile.hifi ]]
done

# A third state observed immediately after the setter is not overwritten.
run_profile_failure unexpected-success verification-failed false false
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.off ]]

# Fresh compensation preflight avoids overwriting external restoration or an
# intervening third value.
run_profile_failure external-restore-before-rollback mutation-failed false false
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.hifi ]]
run_profile_failure intervene-before-rollback verification-failed false false
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.off ]]
run_profile_failure original-unavailable-before-rollback verification-unavailable false false
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.pro ]]

# A colliding public token never substitutes a different private raw option in
# postflight, compensation preflight, or restored-state verification. These
# aliases exist only through a compile-time test hook.
requested_alias=$'profile.alias\tprofile.pro'
original_alias=$'profile.alias\tprofile.hifi'
run_profile_failure requested-alias-after-mutate verification-unavailable false false "$requested_alias"
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.alias ]]
run_profile_failure requested-alias-before-rollback verification-unavailable false false "$requested_alias"
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.alias ]]
run_profile_failure original-alias-before-rollback verification-unavailable false false "$original_alias"
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.pro ]]
run_profile_failure original-alias-after-rollback rollback-failed true false "$original_alias"
[[ $(wc -l <"$log") == 2 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.alias ]]

# Failed restoration, target identity change, target loss, and postflight loss
# are never represented as success or hardware-exact rollback.
run_profile_failure rollback-fail rollback-failed true false
[[ $(wc -l <"$log") == 2 ]]
[[ $(active_value cards.json card.alpha active_profile) == profile.pro ]]
run_profile_failure identity-change target-identity-changed false false
[[ $(wc -l <"$log") == 1 ]]
run_profile_failure vanish-after-selection target-vanished false false
[[ $(wc -l <"$log") == 1 ]]
run_profile_failure postflight-unavailable verification-unavailable false false
[[ $(wc -l <"$log") == 1 ]]

run_output_port_failure() {
  local mode=$1 expected_reason=$2 rollback_attempted=$3 rollback_verified=$4
  restore_state
  "${SELECTION_ENV[@]}" "$binary" audio plan-port --direction output \
    --device "$output_id" --port "$port_headphones" --format json \
    >"$work/port-$mode-plan.json"
  local cohort
  cohort=$(json_field "$work/port-$mode-plan.json" cohort)
  : >"$log"
  set +e
  "${SELECTION_ENV[@]}" SYNAPSE_AUDIO_SELECTION_MODE="$mode" \
    "$binary" audio set-port --direction output --device "$output_id" \
    --from-port "$port_speaker" --port "$port_headphones" \
    --cohort "$cohort" --ack "$ack" --format json \
    >"$work/port-$mode.json"
  local status=$?
  set -e
  [[ $status == 1 ]]
  python - "$work/port-$mode.json" "$expected_reason" \
      "$rollback_attempted" "$rollback_verified" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));attempted=sys.argv[3]=='true';verified=sys.argv[4]=='true'
assert v['status']=='Failed' and v['reason']==sys.argv[2]
assert v['selection']=='port' and v['targetType']=='output'
assert v['mutationAttempted'] and not v['verified'] and not v['changed']
assert v['rollbackAttempted'] is attempted
assert v['rollbackVerified'] is verified
assert not v['profileChanged'] and not v['portChanged']
assert not v['graphMayChange'] and v['signalPathMayChange']
PY
}

# Array-backed endpoint ports use the same exact-original compensation guard as
# profiles, but retain their distinct fixed setter and graph-change semantics.
run_output_port_failure fail-after-mutate mutation-failed true true
[[ $(cat "$log") == $'set-sink-port\tsink.alpha\tport.headphones\nset-sink-port\tsink.alpha\tport.speaker' ]]
[[ $(active_value sinks.json sink.alpha active_port) == port.speaker ]]
run_output_port_failure original-unavailable-before-rollback \
  verification-unavailable false false
[[ $(wc -l <"$log") == 1 ]]
[[ $(active_value sinks.json sink.alpha active_port) == port.headphones ]]
run_output_port_failure rollback-fail rollback-failed true false
[[ $(wc -l <"$log") == 2 ]]
[[ $(active_value sinks.json sink.alpha active_port) == port.headphones ]]

# Choice identities are owner-scoped. A syntactically valid token belonging to
# a second card or endpoint cannot be used against the first owner.
restore_state
python - "$fixtures/cards.json" "$fixtures/sinks.json" \
    "$fixtures/sources.json" <<'PY'
import json,sys
cards_path,sinks_path,sources_path=sys.argv[1:]
cards=json.load(open(cards_path));cards.append({
 'index':41,'name':'card.beta','description':'Secondary audio card',
 'profiles':{
  'profile.hifi':{'description':'Secondary HiFi','available':True},
  'profile.pro':{'description':'Secondary Pro','available':True}},
 'active_profile':'profile.hifi'})
open(cards_path,'w').write(json.dumps(cards,separators=(',',':'))+'\n')
sinks=json.load(open(sinks_path));sinks[1]['ports'].append({
 'name':'port.speaker','description':'USB speaker','availability':'available'})
open(sinks_path,'w').write(json.dumps(sinks,separators=(',',':'))+'\n')
sources=json.load(open(sources_path));sources.append({
 'index':22,'name':'sink.usb','description':'Same raw name, input direction',
 'monitor_of_sink':None,'monitor_source':'',
 'ports':[{'name':'port.speaker','description':'Input speaker token',
           'availability':'available'}],
 'active_port':'port.speaker'})
open(sources_path,'w').write(json.dumps(sources,separators=(',',':'))+'\n')
PY
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/owner-inventory.json"
read -r second_profile second_port cross_direction_port < <(python - "$work/owner-inventory.json" \
    "$profile_pro" "$port_speaker" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));first_profile=sys.argv[2];first_port=sys.argv[3]
second_card=[x for x in v['cards'] if x['label']=='Secondary audio card'][0]
second_profile=[x['id'] for x in second_card['profiles'] if x['label']=='Secondary Pro'][0]
second_endpoint=[x for x in v['endpoints'] if x['label']=='USB output'][0]
second_port=[x['id'] for x in second_endpoint['ports'] if x['label']=='USB speaker'][0]
cross_direction_endpoint=[x for x in v['endpoints'] if x['label']=='Same raw name, input direction'][0]
cross_direction_port=cross_direction_endpoint['ports'][0]['id']
assert len({first_port,second_port,cross_direction_port}) == 3
assert second_profile != first_profile
print(second_profile,second_port,cross_direction_port)
PY
)
: >"$log"
for args in \
  "audio plan-profile --card $card_id --profile $second_profile" \
  "audio plan-port --direction output --device $output_id --port $second_port" \
  "audio plan-port --direction output --device $output_id --port $cross_direction_port"; do
  set +e
  # Reviewed vectors contain no glob metacharacters.
  # shellcheck disable=SC2086
  "${SELECTION_ENV[@]}" "$binary" $args --format json >/dev/null 2>&1
  status=$?
  set -e
  [[ $status == 1 ]]
done
[[ ! -s $log ]]

# Malformed, inconsistent, duplicate and over-bound profile/port data always
# produces one typed read-only absence and clears every partially parsed target.
make_invalid_inventory_case() {
  local case_name=$1
  restore_state
  python - "$case_name" "$fixtures/cards.json" "$fixtures/sinks.json" \
      "$fixtures/sources.json" <<'PY'
import json,sys
case,cards_path,sinks_path,sources_path=sys.argv[1:]
cards=json.load(open(cards_path));sinks=json.load(open(sinks_path));sources=json.load(open(sources_path))
if case == 'bad-availability':
 cards[0]['profiles']['profile.hifi']['available']='maybe'
elif case == 'null-availability':
 cards[0]['profiles']['profile.hifi']['available']=None
elif case == 'profile-entry-not-object':
 cards[0]['profiles']['profile.hifi']=True
elif case == 'unknown-active-profile':
 cards[0]['active_profile']='profile.missing'
elif case == 'unknown-active-port':
 sinks[0]['active_port']='port.missing'
elif case == 'nonstring-active-profile':
 cards[0]['active_profile']=40
elif case == 'nonstring-active-port':
 sinks[0]['active_port']=10
elif case == 'duplicate-card-name':
 duplicate=dict(cards[0]);duplicate['index']=41;cards.append(duplicate)
elif case == 'duplicate-card-index':
 duplicate=dict(cards[0]);duplicate['name']='card.beta';cards.append(duplicate)
elif case == 'duplicate-output-index':
 duplicate=dict(sinks[0]);duplicate['name']='sink.beta';sinks.append(duplicate)
elif case == 'profiles-not-object':
 cards[0]['profiles']=[]
elif case == 'null-profiles':
 cards[0]['profiles']=None
elif case == 'ports-not-object':
 sinks[0]['ports']={}
elif case == 'null-ports':
 sinks[0]['ports']=None
elif case == 'port-entry-not-object':
 sinks[0]['ports'][0]='port.speaker'
elif case == 'missing-port-name':
 sinks[0]['ports'][0].pop('name')
elif case == 'null-port-name':
 sinks[0]['ports'][0]['name']=None
elif case == 'nonstring-port-name':
 sinks[0]['ports'][0]['name']=10
elif case == 'empty-port-name':
 sinks[0]['ports'][0]['name']=''
elif case == 'port-name-too-long':
 sinks[0]['ports'][0]['name']='p'*256
elif case == 'embedded-nul-port-name':
 sinks[0]['ports'][0]['name']='port\x00suffix'
elif case == 'duplicate-port-name':
 duplicate=dict(sinks[0]['ports'][0]);sinks[0]['ports'].append(duplicate)
elif case == 'bad-port-availability':
 sinks[0]['ports'][0]['availability']='sometimes'
elif case == 'boolean-port-availability':
 sinks[0]['ports'][0]['availability']=True
elif case == 'too-many-profiles':
 cards[0]['profiles']={f'profile.{i:03d}':{'description':f'Profile {i}','available':True} for i in range(65)}
 cards[0]['active_profile']='profile.000'
elif case == 'too-many-ports':
 sinks[0]['ports']=[{'name':f'port.{i:03d}','description':f'Port {i}',
                     'availability':'available'} for i in range(65)]
 sinks[0]['active_port']='port.000'
elif case == 'too-many-total-profiles':
 cards=[]
 for card_index in range(9):
  profiles={f'profile.{i:03d}':{'description':f'Profile {i}','available':True} for i in range(64)}
  cards.append({'index':card_index,'name':f'card.{card_index:02d}',
   'description':f'Card {card_index}','profiles':profiles,
   'active_profile':'profile.000'})
elif case == 'too-many-total-ports':
 sinks=[]
 for endpoint_index in range(9):
  ports=[{'name':f'port.{i:03d}','description':f'Port {i}',
          'availability':'available'} for i in range(64)]
  sinks.append({'index':endpoint_index,'name':f'sink.{endpoint_index:02d}',
   'description':f'Output {endpoint_index}','ports':ports,
   'active_port':'port.000'})
elif case == 'too-many-cards':
 cards=[]
 for card_index in range(33):
  cards.append({'index':card_index,'name':f'card.{card_index:02d}',
   'description':f'Card {card_index}',
   'profiles':{'profile.hifi':{'description':'HiFi','available':True}},
   'active_profile':'profile.hifi'})
elif case == 'too-many-outputs':
 sinks=[]
 for endpoint_index in range(65):
  sinks.append({'index':endpoint_index,'name':f'sink.{endpoint_index:02d}',
   'description':f'Output {endpoint_index}',
   'ports':[{'name':'port.main','description':'Main','availability':'available'}],
   'active_port':'port.main'})
elif case == 'too-many-inputs':
 sources=[]
 for endpoint_index in range(65):
  sources.append({'index':endpoint_index,'name':f'source.{endpoint_index:02d}',
   'description':f'Input {endpoint_index}',
   'ports':[{'name':'port.main','description':'Main','availability':'available'}],
   'active_port':'port.main'})
elif case == 'too-many-monitor-inputs':
 sources=[]
 for endpoint_index in range(65):
  sources.append({'index':endpoint_index,'name':f'sink.{endpoint_index:02d}.monitor',
   'description':f'Monitor {endpoint_index}',
   'monitor_of_sink':endpoint_index,'monitor_source':f'sink.{endpoint_index:02d}',
   'ports':[],'active_port':''})
elif case == 'raw-name-too-long':
 cards[0]['name']='c'*256
elif case == 'label-too-long':
 cards[0]['description']='L'*256
elif case == 'null-card-label':
 cards[0]['description']=None
elif case == 'null-output-label':
 sinks[0]['description']=None
elif case == 'embedded-nul-name':
 cards[0]['name']='card\x00suffix'
elif case == 'option-name-too-long':
 cards[0]['profiles']={'p'*256:{'description':'Profile','available':True}}
 cards[0]['active_profile']='p'*256
elif case == 'option-label-too-long':
 cards[0]['profiles']['profile.hifi']['description']='L'*256
elif case == 'null-profile-label':
 cards[0]['profiles']['profile.hifi']['description']=None
elif case == 'nonstring-profile-label':
 cards[0]['profiles']['profile.hifi']['description']=40
elif case == 'null-port-label':
 next(x for x in sinks[0]['ports'] if x['name']=='port.speaker')['description']=None
elif case == 'nonstring-port-label':
 sinks[0]['ports'][0]['description']=10
elif case == 'embedded-nul-option-name':
 cards[0]['profiles']={'profile\x00suffix':{'description':'Profile','available':True}}
 cards[0]['active_profile']='profile\x00suffix'
elif case == 'negative-target-index':
 sinks[0]['index']=-1
elif case == 'malformed-monitor':
 sources[0]['monitor_of_sink']='not-an-index'
elif case == 'negative-monitor-index':
 sources[0]['monitor_of_sink']=-1
elif case == 'duplicate-monitor-index':
 sources[1]['index']=sources[0]['index']
elif case == 'duplicate-monitor-name':
 duplicate=dict(sources[1]);duplicate['index']=99;sources.append(duplicate)
elif case == 'overlong-monitor-name':
 sources[1]['name']='m'*256
elif case == 'embedded-nul-monitor-name':
 sources[1]['name']='monitor\x00suffix'
elif case == 'overlong-monitor-source':
 sources[0]['monitor_source']='s'*256
elif case == 'embedded-nul-monitor-source':
 sources[0]['monitor_source']='sink\x00suffix'
elif case == 'null-monitor-source':
 sources[1]['monitor_source']=None
elif case == 'overlong-monitor-label':
 sources[1]['description']='L'*256
elif case == 'null-monitor-label':
 sources[1]['description']=None
elif case == 'monitor-ports-not-object':
 sources[1]['ports']={}
elif case == 'null-monitor-ports':
 sources[1]['ports']=None
elif case == 'bad-monitor-port-availability':
 sources[1]['ports'][0]['availability']='sometimes'
elif case == 'null-monitor-port-availability':
 sources[1]['ports'][0]['availability']=None
elif case == 'unknown-monitor-active-port':
 sources[1]['active_port']='port.absent'
elif case == 'too-many-monitor-ports':
 sources=[]
 for endpoint_index in range(9):
  ports=[{'name':f'port.{i:03d}','description':f'Port {i}',
          'availability':'available'} for i in range(64)]
  sources.append({'index':endpoint_index,
   'name':f'sink.{endpoint_index:02d}.monitor',
   'description':f'Monitor {endpoint_index}',
   'monitor_of_sink':endpoint_index,
   'monitor_source':f'sink.{endpoint_index:02d}',
   'ports':ports,'active_port':'port.000'})
else:
 raise AssertionError(case)
for path,value in ((cards_path,cards),(sinks_path,sinks),(sources_path,sources)):
 open(path,'w').write(json.dumps(value,separators=(',',':'))+'\n')
PY
  "${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
    >"$work/invalid-$case_name.json"
  python - "$work/invalid-$case_name.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert not v['available'] and v['reason']=='invalid-response'
assert not v['mutationAvailable'] and v['cards']==[] and v['endpoints']==[]
assert not v['hardwareReadback'] and not v['hardwareExactRollback'] and v['bounded']
PY
  [[ ! -e $log ]]
}

for invalid_case in bad-availability null-availability profile-entry-not-object \
    unknown-active-profile unknown-active-port nonstring-active-profile \
    nonstring-active-port duplicate-card-name duplicate-card-index \
    duplicate-output-index profiles-not-object null-profiles ports-not-object \
    null-ports port-entry-not-object missing-port-name null-port-name \
    nonstring-port-name empty-port-name port-name-too-long \
    embedded-nul-port-name duplicate-port-name bad-port-availability \
    boolean-port-availability too-many-profiles too-many-ports \
    too-many-total-profiles too-many-total-ports too-many-cards \
    too-many-outputs too-many-inputs too-many-monitor-inputs \
    raw-name-too-long label-too-long null-card-label null-output-label \
    embedded-nul-name option-name-too-long option-label-too-long \
    null-profile-label nonstring-profile-label null-port-label \
    nonstring-port-label embedded-nul-option-name \
    negative-target-index \
    malformed-monitor negative-monitor-index duplicate-monitor-index \
    duplicate-monitor-name overlong-monitor-name embedded-nul-monitor-name \
    overlong-monitor-source embedded-nul-monitor-source null-monitor-source \
    overlong-monitor-label null-monitor-label \
    monitor-ports-not-object null-monitor-ports bad-monitor-port-availability \
    null-monitor-port-availability unknown-monitor-active-port \
    too-many-monitor-ports; do
  make_invalid_inventory_case "$invalid_case"
done

# Exactly 64 validated monitor entries are accepted within the raw input bound
# and omitted from the physical endpoint projection.
restore_state
python - "$fixtures/sources.json" <<'PY'
import json,sys
sources=[]
for index in range(64):
 sources.append({'index':index,'name':f'sink.{index:02d}.monitor',
  'description':f'Monitor {index}','monitor_of_sink':index,
  'monitor_source':f'sink.{index:02d}','ports':[],'active_port':''})
open(sys.argv[1],'w').write(json.dumps(sources,separators=(',',':'))+'\n')
PY
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/monitor-limit.json"
python - "$work/monitor-limit.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1],encoding='utf-8'))
assert value['available'] and value['reason'] is None
assert len(value['cards'])==1
assert len([item for item in value['endpoints'] if item['direction']=='output'])==2
assert not [item for item in value['endpoints'] if item['direction']=='input']
PY

# Exact configured aggregate maxima remain valid: 512 profiles and 512 ports.
restore_state
python - "$fixtures/cards.json" "$fixtures/sinks.json" "$fixtures/sources.json" <<'PY'
import json,sys
cards=[];sinks=[]
for owner in range(8):
 profiles={f'profile.{i:03d}':{'description':f'Profile {i}','available':True} for i in range(64)}
 cards.append({'index':owner,'name':f'card.{owner:02d}',
  'description':f'Card {owner}','profiles':profiles,'active_profile':'profile.000'})
 ports=[{'name':f'port.{i:03d}','description':f'Port {i}',
         'availability':'available'} for i in range(64)]
 sinks.append({'index':owner,'name':f'sink.{owner:02d}',
  'description':f'Output {owner}','ports':ports,'active_port':'port.000'})
for path,value in zip(sys.argv[1:],(cards,sinks,[])):
 open(path,'w').write(json.dumps(value,separators=(',',':'))+'\n')
PY
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/exact-aggregate-maxima.json"
python - "$work/exact-aggregate-maxima.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['available'] and v['mutationAvailable']
assert len(v['cards'])==8 and len(v['endpoints'])==8
assert sum(len(card['profiles']) for card in v['cards'])==512
assert sum(len(endpoint['ports']) for endpoint in v['endpoints'])==512
PY
[[ ! -e $log ]]
restore_state

# Valid UTF-8 backend identities remain opaque but usable; they are not limited
# to an ASCII presentation or command spelling.
python - "$fixtures/cards.json" "$fixtures/sinks.json" \
    "$fixtures/sources.json" <<'PY'
import json,sys
cards=json.load(open(sys.argv[1],encoding='utf-8'))
sinks=json.load(open(sys.argv[2],encoding='utf-8'))
sources=json.load(open(sys.argv[3],encoding='utf-8'))
cards[0]['name']='carta.áudio'
cards[0]['profiles']['profilo.😀']=cards[0]['profiles'].pop('profile.pro')
sinks[0]['name']='uscita.日本'
next(port for port in sinks[0]['ports']
     if port['name']=='port.headphones')['name']='porta.é'
sources[1]['name']='monitor.日本'
sources[1]['monitor_source']='uscita.日本'
for path,value in zip(sys.argv[1:],(cards,sinks,sources)):
 open(path,'w',encoding='utf-8').write(
  json.dumps(value,ensure_ascii=False,separators=(',',':'))+'\n')
PY
"${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/unicode-identities.json"
read -r unicode_card unicode_profile < <(python - "$work/unicode-identities.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1],encoding='utf-8'))
assert v['available'] and v['mutationAvailable'] and len(v['endpoints'])==3
serialized=json.dumps(v,ensure_ascii=False)
for raw in ('carta.áudio','profilo.😀','uscita.日本','porta.é','monitor.日本'):
 assert raw not in serialized
card=v['cards'][0]
requested=next(p['id'] for p in card['profiles'] if p['label']=='Pro Audio')
print(card['id'],requested)
PY
)
"${SELECTION_ENV[@]}" "$binary" audio plan-profile --card "$unicode_card" \
  --profile "$unicode_profile" --format json >"$work/unicode-plan.json"
python - "$work/unicode-plan.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1],encoding='utf-8'))
assert v['status']=='Planned' and v['selection']=='profile' and v['changed']
PY
[[ ! -e $log ]]
restore_state

expect_transport_invalid() {
  local mode=$1
  restore_state
  SYNAPSE_AUDIO_SELECTION_MODE="$mode" \
    "${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
    >"$work/invalid-$mode.json"
  python - "$work/invalid-$mode.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert not v['available'] and v['reason']=='invalid-response'
assert not v['mutationAvailable'] and v['cards']==[] and v['endpoints']==[]
assert not v['hardwareReadback'] and not v['hardwareExactRollback'] and v['bounded']
PY
  [[ ! -e $log ]]
}
for invalid_mode in duplicate-card-key escaped-duplicate-card-key \
    duplicate-option-key trailing-json trailing-comma raw-nul \
    escaped-nul-ignored invalid-primitive leading-zero invalid-escape \
    unpaired-surrogate invalid-utf8 oversized depth-over-limit \
    object-key-over-limit document-key-over-limit; do
  expect_transport_invalid "$invalid_mode"
done
for valid_mode in exact-limit depth-limit object-key-limit \
    escaped-surrogate-pair; do
  restore_state
  SYNAPSE_AUDIO_SELECTION_MODE="$valid_mode" \
    "${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
    >"$work/$valid_mode-inventory.json"
  python - "$work/$valid_mode-inventory.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['available'] and v['mutationAvailable'] and v['bounded']
PY
done
cp "$work/invalid-bad-availability.json" "$work/invalid-inventory.json"

# Every child receives the fixed locale, isolated process group, null standard
# input/error, and captured standard output even if the CLI inherited no
# standard descriptors.
restore_state
envelope_log="$work/envelope.log"
set +e
(
  exec 0>&- 1>&- 2>&-
  "${SELECTION_ENV[@]}" SYNAPSE_AUDIO_SELECTION_MODE=verify-envelope \
    SYNAPSE_AUDIO_SELECTION_ENVELOPE_LOG="$envelope_log" \
    "$binary" audio plan-profile --card "$card_id" --profile "$profile_pro" \
    --format json
)
envelope_status=$?
set -e
[[ $envelope_status == 0 ]]
python - "$envelope_log" <<'PY'
import json,sys
entries=[json.loads(line) for line in open(sys.argv[1],encoding='ascii')]
assert len(entries)==1 and entries[0]['valid']
PY

# A failed pactl child cannot leave a same-group descendant running after it
# closes the capture descriptors and exits.
restore_state
error_child_pid_file="$work/error-child.pid"
error_survivor="$work/error-descendant-survived"
SYNAPSE_AUDIO_SELECTION_MODE=error-with-descendant \
SYNAPSE_AUDIO_SELECTION_CHILD_PID="$error_child_pid_file" \
SYNAPSE_AUDIO_SELECTION_SURVIVOR="$error_survivor" \
  "${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/error-with-descendant.json"
[[ -s $error_child_pid_file ]]
python - "$work/error-with-descendant.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1],encoding='utf-8'))
assert not value['available'] and value['reason']=='unavailable'
PY
sleep 1.2
[[ ! -e $error_survivor ]]
error_child_pid=$(<"$error_child_pid_file")
[[ $error_child_pid =~ ^[1-9][0-9]*$ ]]
error_child_state=$(ps -o stat= -p "$error_child_pid" 2>/dev/null || true)
[[ -z $error_child_state || $error_child_state == Z* ]]

# Killing the capturing CLI cannot leave its pactl descendant running.
restore_state
child_pid_file="$work/pdeath-child.pid"
SYNAPSE_AUDIO_SELECTION_MODE=hang-with-pid \
SYNAPSE_AUDIO_SELECTION_CHILD_PID="$child_pid_file" \
  "${SELECTION_ENV[@]}" "$binary" audio profile-port-inventory --format json \
  >"$work/pdeath-parent.json" 2>/dev/null &
parent_pid=$!
for _ in $(seq 1 100); do
  [[ -s $child_pid_file ]] && break
  kill -0 "$parent_pid" 2>/dev/null
  sleep 0.01
done
[[ -s $child_pid_file ]]
child_pid=$(<"$child_pid_file")
[[ $child_pid =~ ^[1-9][0-9]*$ ]]
kill -KILL "$parent_pid"
set +e
wait "$parent_pid" 2>/dev/null
parent_status=$?
set -e
[[ $parent_status == 137 ]]
child_terminated=false
for _ in $(seq 1 200); do
  if ! kill -0 "$child_pid" 2>/dev/null; then
    child_terminated=true
    break
  fi
  child_state=$(ps -o stat= -p "$child_pid" 2>/dev/null || true)
  if [[ -z $child_state || $child_state == Z* ]]; then
    child_terminated=true
    break
  fi
  sleep 0.01
done
if [[ $child_terminated != true ]]; then
  kill -KILL "$child_pid" 2>/dev/null || true
  false
fi

if [[ -n $output_dir ]]; then
  mkdir -p "$output_dir"
  for document in inventory profile-plan profile-applied profile-same-plan \
      profile-same output-port-plan output-port-applied input-port-plan \
      input-port-applied original-mismatch cohort-mismatch double-refused \
      no-mutate-success fail fail-after-mutate timeout-after-mutate \
      unexpected-success external-restore-before-rollback \
      intervene-before-rollback original-unavailable-before-rollback \
      requested-alias-after-mutate requested-alias-before-rollback \
      original-alias-before-rollback original-alias-after-rollback \
      rollback-fail identity-change \
      vanish-after-selection postflight-unavailable invalid-inventory; do
    cp "$work/$document.json" "$output_dir/profile-port-$document.json"
  done
fi

printf '%s\n' 'synapse-settings guarded Audio profiles and ports: PASS'
