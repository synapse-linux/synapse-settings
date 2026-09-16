#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

binary=${1:?binary required}
pactl_fake=${2:?fake pactl required}
audio=${3:?Audio fixtures required}
work=${4:?work directory required}
output_id=${5:?output token required}
input_id=${6:?input token required}

AUDIO_ENV=(env SYNAPSE_PACTL="$pactl_fake" SYNAPSE_AUDIO_FIXTURES="$audio")
ack=synapse-settings/audio-control/v1
control_log="$work/audio-control.log"
control_count="$work/audio-control.count"
cp "$audio/sinks.json" "$work/control-sinks.base.json"
cp "$audio/sources.json" "$work/control-sources.base.json"
cp "$audio/sink-inputs.json" "$work/control-sink-inputs.base.json"
cp "$audio/source-outputs.json" "$work/control-source-outputs.base.json"

restore_control_state() {
  cp "$work/control-sinks.base.json" "$audio/sinks.json"
  cp "$work/control-sources.base.json" "$audio/sources.json"
  cp "$work/control-sink-inputs.base.json" "$audio/sink-inputs.json"
  cp "$work/control-source-outputs.base.json" "$audio/source-outputs.json"
  rm -f "$audio/fail-next-stream-list" "$audio/stream-list-count" \
    "$audio/control-read-count" "$control_log" "$control_count"
}

cohort_from() {
  python - "$1" <<'PY'
import json,sys
print(json.load(open(sys.argv[1]))['cohort'])
PY
}

fixture_value() {
  python - "$1" "$2" "$3" <<'PY'
import json,sys
path,selector,identity=sys.argv[1:]
value=json.load(open(path))
items=[item for item in value if str(item.get(selector))==identity]
assert len(items)==1
item=items[0]
channels=item['volume']
percent=(sum(int(channel['value']) for channel in channels.values())*100+
         len(channels)*32768)//(len(channels)*65536)
print(percent, str(bool(item['mute'])).lower())
PY
}

# Planning is read-only and returns only an opaque target token, bounded values,
# a cohort, and explicit non-audio side-effect guarantees.
restore_control_state
before=$(sha256sum "$audio/sinks.json" "$audio/sources.json" \
  "$audio/sink-inputs.json" "$audio/source-outputs.json")
"${AUDIO_ENV[@]}" "$binary" audio plan-volume --target "$output_id" \
  --percent 35 --format json >"$work/control-volume-plan.json"
after=$(sha256sum "$audio/sinks.json" "$audio/sources.json" \
  "$audio/sink-inputs.json" "$audio/source-outputs.json")
[[ $before == "$after" && ! -e $control_log ]]
volume_cohort=$(cohort_from "$work/control-volume-plan.json")
python - "$work/control-volume-plan.json" "$output_id" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert set(v)=={'schema','status','target','targetType','control','originalValue',
 'requestedValue','cohort','changed','stateAuthority','requiresAcknowledgement',
 'singleTarget','safeVolumeMaximumPercent','postflightRequired',
 'rollbackOnUnverified','playbackStarted','captureStarted','profileChanged',
 'routingChanged','applied','bounded'}
assert v['schema']=='synapse.settings.audio-control-plan/v1'
assert v['status']=='Planned' and v['target']==sys.argv[2]
assert v['targetType']=='output' and v['control']=='volume'
assert v['originalValue']==50 and v['requestedValue']==35 and v['changed']
assert v['cohort'].startswith('control-') and len(v['cohort'])==24
assert v['stateAuthority']=='pipewire-pulse-model'
assert v['requiresAcknowledgement']=='synapse-settings/audio-control/v1'
assert v['singleTarget'] and v['safeVolumeMaximumPercent']==100
assert v['postflightRequired'] and v['rollbackOnUnverified'] and v['bounded']
assert not any(v[key] for key in ('playbackStarted','captureStarted',
                                   'profileChanged','routingChanged','applied'))
PY

# A confirmed endpoint-volume change executes one fixed pactl operation, then
# refreshes and verifies the exact same target identity and requested value.
: >"$control_log"
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-volume --target "$output_id" --from-percent 50 \
  --percent 35 --cohort "$volume_cohort" --ack "$ack" --format json \
  >"$work/control-volume-applied.json"
python - "$work/control-volume-applied.json" "$output_id" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert set(v)=={'schema','status','reason','target','targetType','control',
 'originalValue','requestedValue','changed','mutationAttempted','verified',
 'rollbackAttempted','rollbackVerified','stateAuthority',
 'requiresAcknowledgement','singleTarget','safeVolumeMaximumPercent',
 'playbackStarted','captureStarted','profileChanged','routingChanged','bounded'}
assert v['schema']=='synapse.settings.audio-control-receipt/v1'
assert v['status']=='Applied' and v['reason'] is None
assert v['target']==sys.argv[2] and v['targetType']=='output'
assert v['control']=='volume' and v['originalValue']==50
assert v['requestedValue']==35 and v['changed'] and v['mutationAttempted']
assert v['verified'] and not v['rollbackAttempted'] and not v['rollbackVerified']
assert v['stateAuthority']=='pipewire-pulse-model'
assert v['requiresAcknowledgement']=='synapse-settings/audio-control/v1'
assert v['singleTarget'] and v['safeVolumeMaximumPercent']==100 and v['bounded']
assert not any(v[key] for key in ('playbackStarted','captureStarted',
                                   'profileChanged','routingChanged'))
PY
[[ $(cat "$control_log") == $'volume\tsink.a\t35%' ]]
[[ $(fixture_value "$audio/sinks.json" name sink.a) == '35 false' ]]

# A same-value transaction is explicitly verified without invoking pactl.
"${AUDIO_ENV[@]}" "$binary" audio plan-volume --target "$output_id" \
  --percent 35 --format json >"$work/control-volume-same-plan.json"
same_cohort=$(cohort_from "$work/control-volume-same-plan.json")
: >"$control_log"
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-volume --target "$output_id" --from-percent 35 \
  --percent 35 --cohort "$same_cohort" --ack "$ack" --format json \
  >"$work/control-volume-same.json"
python - "$work/control-volume-same-plan.json" "$work/control-volume-same.json" <<'PY'
import json,sys
plan=json.load(open(sys.argv[1]));receipt=json.load(open(sys.argv[2]))
assert not plan['changed'] and not plan['applied']
assert receipt['status']=='AlreadySet' and receipt['reason'] is None
assert not receipt['changed'] and not receipt['mutationAttempted']
assert receipt['verified'] and not receipt['rollbackAttempted']
PY
[[ ! -s $control_log ]]

# Input mute and active-stream mute use their distinct fixed pactl operations.
restore_control_state
"${AUDIO_ENV[@]}" "$binary" audio plan-mute --target "$input_id" \
  --muted false --format json >"$work/control-input-mute-plan.json"
input_mute_cohort=$(cohort_from "$work/control-input-mute-plan.json")
: >"$control_log"
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-mute --target "$input_id" --from-muted true \
  --muted false --cohort "$input_mute_cohort" --ack "$ack" --format json \
  >"$work/control-input-mute.json"
python - "$work/control-input-mute-plan.json" "$work/control-input-mute.json" <<'PY'
import json,sys
plan=json.load(open(sys.argv[1]));receipt=json.load(open(sys.argv[2]))
assert plan['targetType']=='input' and plan['control']=='mute'
assert plan['originalValue'] is True and plan['requestedValue'] is False
assert receipt['status']=='Applied' and receipt['changed'] and receipt['verified']
assert receipt['originalValue'] is True and receipt['requestedValue'] is False
PY
[[ $(cat "$control_log") == $'mute\tsource.b\t0' ]]
[[ $(fixture_value "$audio/sources.json" name source.b) == '100 false' ]]

restore_control_state
"${AUDIO_ENV[@]}" "$binary" audio plan-mute --target playback-30 \
  --muted true --format json >"$work/control-stream-mute-plan.json"
stream_mute_cohort=$(cohort_from "$work/control-stream-mute-plan.json")
: >"$control_log"
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-mute --target playback-30 --from-muted false \
  --muted true --cohort "$stream_mute_cohort" --ack "$ack" --format json \
  >"$work/control-stream-mute.json"
python - "$work/control-stream-mute-plan.json" "$work/control-stream-mute.json" <<'PY'
import json,sys
plan=json.load(open(sys.argv[1]));receipt=json.load(open(sys.argv[2]))
assert plan['targetType']=='playback' and plan['control']=='mute'
assert receipt['status']=='Applied' and receipt['targetType']=='playback'
assert receipt['verified'] and receipt['changed']
PY
[[ $(cat "$control_log") == $'mute\t30\t1' ]]
[[ $(fixture_value "$audio/sink-inputs.json" index 30) == '75 true' ]]

restore_control_state
python - "$audio/sink-inputs.json" "$audio/source-outputs.json" <<'PY'
import json,sys
pid=json.load(open(sys.argv[1]))[0]['properties']['application.process.id']
value=[{'index':31,'source':20,'mute':False,
        'volume':{'mono':{'value':32768}},
        'properties':{'application.name':'Recorder',
                      'application.process.id':pid}}]
open(sys.argv[2],'w').write(json.dumps(value,separators=(',',':'))+'\n')
PY
"${AUDIO_ENV[@]}" "$binary" audio plan-volume --target recording-31 \
  --percent 25 --format json >"$work/control-recording-volume-plan.json"
recording_cohort=$(cohort_from "$work/control-recording-volume-plan.json")
: >"$control_log"
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-volume --target recording-31 --from-percent 50 \
  --percent 25 --cohort "$recording_cohort" --ack "$ack" --format json \
  >"$work/control-recording-volume.json"
python - "$work/control-recording-volume-plan.json" "$work/control-recording-volume.json" <<'PY'
import json,sys
plan=json.load(open(sys.argv[1]));receipt=json.load(open(sys.argv[2]))
assert plan['targetType']=='recording' and plan['control']=='volume'
assert receipt['status']=='Applied' and receipt['targetType']=='recording'
assert receipt['verified'] and receipt['changed']
PY
[[ $(cat "$control_log") == $'volume\t31\t25%' ]]
[[ $(fixture_value "$audio/source-outputs.json" index 31) == '25 false' ]]

# Invalid values, raw backend identities, absent transaction fields, and wrong
# acknowledgements fail at argv validation without a pactl mutation.
restore_control_state
: >"$control_log"
for args in \
  "audio plan-volume --target $output_id --percent 101 --format json" \
  'audio plan-volume --target sink.a --percent 40 --format json' \
  "audio set-volume --target $output_id --from-percent 50 --percent 40 --cohort control-0000000000000000 --ack wrong --format json" \
  "audio set-mute --target playback-30 --from-muted false --muted true --ack $ack --format json"; do
  set +e
  # The reviewed fixture vectors contain no glob metacharacters.
  # shellcheck disable=SC2086
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
    "$binary" $args >/dev/null 2>&1
  status=$?
  set -e
  [[ $status == 2 ]]
done
[[ ! -s $control_log ]]

# Original-value and cohort drift are distinct typed refusals and occur before
# any mutation. A syntactically valid but unrelated cohort never authorizes.
"${AUDIO_ENV[@]}" "$binary" audio plan-volume --target "$output_id" \
  --percent 40 --format json >"$work/control-stale-plan.json"
stale_cohort=$(cohort_from "$work/control-stale-plan.json")
python - "$audio/sinks.json" <<'PY'
import json,sys
p=sys.argv[1];v=json.load(open(p));raw=(49*65536+50)//100
for item in v:
    if item.get('name')=='sink.a':
        for channel in item['volume'].values(): channel['value']=raw
open(p,'w').write(json.dumps(v,separators=(',',':'))+'\n')
PY
: >"$control_log"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-volume --target "$output_id" --from-percent 50 \
  --percent 40 --cohort "$stale_cohort" --ack "$ack" --format json \
  >"$work/control-original-mismatch.json"
original_status=$?
set -e
[[ $original_status == 1 && ! -s $control_log ]]
python - "$work/control-original-mismatch.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Refused' and v['reason']=='original-value-mismatch'
assert not any(v[key] for key in ('changed','mutationAttempted','verified',
                                   'rollbackAttempted','rollbackVerified'))
PY

restore_control_state
bad_cohort=control-0000000000000000
[[ $bad_cohort != "$stale_cohort" ]] || bad_cohort=control-0000000000000001
: >"$control_log"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-volume --target "$output_id" --from-percent 50 \
  --percent 40 --cohort "$bad_cohort" --ack "$ack" --format json \
  >"$work/control-cohort-mismatch.json"
cohort_status=$?
set -e
[[ $cohort_status == 1 && ! -s $control_log ]]
python - "$work/control-cohort-mismatch.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Refused' and v['reason']=='control-cohort-changed'
assert not v['mutationAttempted'] and not v['rollbackAttempted']
PY

# A reported success without the requested postflight value fails verification.
# When the state did not move, rollback is neither needed nor claimed.
restore_control_state
: >"$control_log"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE=no-mutate-success \
  SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-volume --target "$output_id" --from-percent 50 \
  --percent 40 --cohort "$stale_cohort" --ack "$ack" --format json \
  >"$work/control-no-mutate.json"
no_mutate_status=$?
set -e
[[ $no_mutate_status == 1 ]]
python - "$work/control-no-mutate.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='verification-failed'
assert v['mutationAttempted'] and not v['changed'] and not v['verified']
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY
[[ $(fixture_value "$audio/sinks.json" name sink.a) == '50 false' ]]

# If pactl reports failure or timeout after the requested value becomes visible,
# the command restores and verifies the exact original value before reporting
# failure. It never retries the requested mutation.
for mode in fail-after-mutate timeout-after-mutate; do
  restore_control_state
  : >"$control_log"
  printf '0\n' >"$control_count"
  start=$(date +%s)
  set +e
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE="$mode" \
    SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
    SYNAPSE_AUDIO_CONTROL_COUNT="$control_count" \
    "$binary" audio set-volume --target "$output_id" --from-percent 50 \
    --percent 40 --cohort "$stale_cohort" --ack "$ack" --format json \
    >"$work/control-$mode.json"
  status=$?
  set -e
  elapsed=$(( $(date +%s) - start ))
  [[ $status == 1 ]]
  (( elapsed < 5 ))
  python - "$work/control-$mode.json" "$mode" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));mode=sys.argv[2]
assert v['status']=='Failed'
assert v['reason']==('mutation-timeout' if mode=='timeout-after-mutate' else 'mutation-failed')
assert v['mutationAttempted'] and not v['changed'] and not v['verified']
assert v['rollbackAttempted'] and v['rollbackVerified']
PY
  [[ $(cat "$control_log") == $'volume\tsink.a\t40%\nvolume\tsink.a\t50%' ]]
  [[ $(fixture_value "$audio/sinks.json" name sink.a) == '50 false' ]]
done

# A pre-existing software-amplified value may be captured only as rollback
# state. New requests remain capped at 100%, and failed apply restores 150%.
restore_control_state
python - "$audio/sinks.json" <<'PY'
import json,sys
p=sys.argv[1];v=json.load(open(p));raw=(150*65536+50)//100
for item in v:
    if item.get('name')=='sink.a':
        for channel in item['volume'].values(): channel['value']=raw
open(p,'w').write(json.dumps(v,separators=(',',':'))+'\n')
PY
"${AUDIO_ENV[@]}" "$binary" audio plan-volume --target "$output_id" \
  --percent 100 --format json >"$work/control-amplified-plan.json"
amplified_cohort=$(cohort_from "$work/control-amplified-plan.json")
: >"$control_log"
printf '0\n' >"$control_count"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE=fail-after-mutate \
  SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  SYNAPSE_AUDIO_CONTROL_COUNT="$control_count" \
  "$binary" audio set-volume --target "$output_id" --from-percent 150 \
  --percent 100 --cohort "$amplified_cohort" --ack "$ack" --format json \
  >"$work/control-amplified-rollback.json"
amplified_status=$?
set -e
[[ $amplified_status == 1 ]]
python - "$work/control-amplified-plan.json" "$work/control-amplified-rollback.json" <<'PY'
import json,sys
plan=json.load(open(sys.argv[1]));receipt=json.load(open(sys.argv[2]))
assert plan['originalValue']==150 and plan['requestedValue']==100
assert plan['safeVolumeMaximumPercent']==100
assert receipt['status']=='Failed' and receipt['reason']=='mutation-failed'
assert receipt['originalValue']==150 and receipt['requestedValue']==100
assert receipt['rollbackAttempted'] and receipt['rollbackVerified']
PY
[[ $(cat "$control_log") == $'volume\tsink.a\t100%\nvolume\tsink.a\t150%' ]]
[[ $(fixture_value "$audio/sinks.json" name sink.a) == '150 false' ]]

# Compensation gets one fresh identity/value preflight. If another actor has
# already restored the original, no redundant setter is issued.
restore_control_state
"${AUDIO_ENV[@]}" "$binary" audio plan-volume --target "$output_id" \
  --percent 40 --format json >"$work/control-external-restore-plan.json"
external_restore_cohort=$(cohort_from "$work/control-external-restore-plan.json")
: >"$control_log"
printf '0\n' >"$control_count"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE=external-restore-before-rollback \
  SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  SYNAPSE_AUDIO_CONTROL_COUNT="$control_count" \
  "$binary" audio set-volume --target "$output_id" --from-percent 50 \
  --percent 40 --cohort "$external_restore_cohort" --ack "$ack" --format json \
  >"$work/control-external-restore.json"
external_restore_status=$?
set -e
[[ $external_restore_status == 1 ]]
python - "$work/control-external-restore.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='mutation-failed'
assert v['mutationAttempted'] and not v['verified']
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY
[[ $(cat "$control_log") == $'volume\tsink.a\t40%' ]]
[[ $(fixture_value "$audio/sinks.json" name sink.a) == '50 false' ]]

# If the same identity carries an intervening third value at compensation
# preflight, fail closed rather than overwrite that concurrent change.
restore_control_state
"${AUDIO_ENV[@]}" "$binary" audio plan-volume --target "$output_id" \
  --percent 40 --format json >"$work/control-intervening-plan.json"
intervening_cohort=$(cohort_from "$work/control-intervening-plan.json")
: >"$control_log"
printf '0\n' >"$control_count"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE=intervene-before-rollback \
  SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  SYNAPSE_AUDIO_CONTROL_COUNT="$control_count" \
  "$binary" audio set-volume --target "$output_id" --from-percent 50 \
  --percent 40 --cohort "$intervening_cohort" --ack "$ack" --format json \
  >"$work/control-intervening-value.json"
intervening_status=$?
set -e
[[ $intervening_status == 1 ]]
python - "$work/control-intervening-value.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='verification-failed'
assert v['mutationAttempted'] and not v['verified']
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY
[[ $(cat "$control_log") == $'volume\tsink.a\t40%' ]]
[[ $(fixture_value "$audio/sinks.json" name sink.a) == '43 false' ]]

# A successful command that produces an unexpected value is also unverified;
# the original value is restored exactly and the receipt remains Failed.
restore_control_state
: >"$control_log"
printf '0\n' >"$control_count"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE=unexpected-success \
  SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  SYNAPSE_AUDIO_CONTROL_COUNT="$control_count" \
  "$binary" audio set-volume --target "$output_id" --from-percent 50 \
  --percent 40 --cohort "$stale_cohort" --ack "$ack" --format json \
  >"$work/control-unexpected.json"
unexpected_status=$?
set -e
[[ $unexpected_status == 1 ]]
python - "$work/control-unexpected.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='verification-failed'
assert v['rollbackAttempted'] and v['rollbackVerified']
assert not v['changed'] and not v['verified']
PY
[[ $(fixture_value "$audio/sinks.json" name sink.a) == '50 false' ]]

# Failed compensation is explicit and never represented as a successful or
# verified operation. The test restores fixture state out of band afterwards.
restore_control_state
: >"$control_log"
printf '0\n' >"$control_count"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE=rollback-fail \
  SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  SYNAPSE_AUDIO_CONTROL_COUNT="$control_count" \
  "$binary" audio set-volume --target "$output_id" --from-percent 50 \
  --percent 40 --cohort "$stale_cohort" --ack "$ack" --format json \
  >"$work/control-rollback-fail.json"
rollback_status=$?
set -e
[[ $rollback_status == 1 ]]
python - "$work/control-rollback-fail.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='rollback-failed'
assert v['rollbackAttempted'] and not v['rollbackVerified']
assert not v['changed'] and not v['verified']
PY
[[ $(fixture_value "$audio/sinks.json" name sink.a) == '40 false' ]]

# If the target vanishes, postflight is unavailable, or a stream's process
# identity changes after the single command, C authority fails closed and does
# not issue an unsafe rollback against an unproven identity.
restore_control_state
for mode in postflight-unavailable vanish-after-control; do
  : >"$control_log"
  set +e
  "${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE="$mode" \
    SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
    "$binary" audio set-volume --target "$output_id" --from-percent 50 \
    --percent 40 --cohort "$stale_cohort" --ack "$ack" --format json \
    >"$work/control-$mode.json"
  status=$?
  set -e
  [[ $status == 1 ]]
  python - "$work/control-$mode.json" "$mode" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));mode=sys.argv[2]
assert v['status']=='Failed'
assert v['reason']==('verification-unavailable' if mode=='postflight-unavailable' else 'target-vanished')
assert v['mutationAttempted'] and not v['verified']
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY
  restore_control_state
done

"${AUDIO_ENV[@]}" "$binary" audio plan-mute --target playback-30 \
  --muted true --format json >"$work/control-stream-identity-plan.json"
identity_cohort=$(cohort_from "$work/control-stream-identity-plan.json")
: >"$control_log"
set +e
"${AUDIO_ENV[@]}" SYNAPSE_AUDIO_CONTROL_MODE=identity-change \
  SYNAPSE_AUDIO_CONTROL_LOG="$control_log" \
  "$binary" audio set-mute --target playback-30 --from-muted false \
  --muted true --cohort "$identity_cohort" --ack "$ack" --format json \
  >"$work/control-identity-change.json"
identity_status=$?
set -e
[[ $identity_status == 1 ]]
python - "$work/control-identity-change.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['status']=='Failed' and v['reason']=='target-identity-changed'
assert v['mutationAttempted'] and not v['verified']
assert not v['rollbackAttempted'] and not v['rollbackVerified']
PY
[[ $(cat "$control_log") == $'mute\t30\t1' ]]

restore_control_state
printf '%s\n' 'synapse-settings guarded Audio controls: PASS'
