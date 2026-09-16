#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

binary=${1:?binary required}
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/synapse-goxlr-fake" <<'PY'
#!/usr/bin/env python3
import json, os, signal, sys, time

args=sys.argv[1:]
with open(os.environ['SYNAPSE_GOXLR_FAKE_LOG'],'a',encoding='ascii') as stream:
    stream.write(' '.join(args)+'\n')
mode=os.environ.get('SYNAPSE_GOXLR_FAKE_MODE','ready')

def compact(value):
    print(json.dumps(value,separators=(',',':')))

def inventory(count):
    devices=[]
    if count:
        devices=[{'id':'goxlr-1','model':'GoXLR Mini','usbId':'1220:8fe4','connected':True}]
    compact({'schema':'synapse.goxlr.inventory/v1','deviceCount':len(devices),
             'truncated':False,'devices':devices})

def provider_status():
    if mode in ('inactive-present','inactive-absent'):
        raise SystemExit(3)
    if mode == 'signaled':
        os.kill(os.getpid(),signal.SIGTERM)
    if mode == 'timeout':
        os.close(1);time.sleep(10);raise SystemExit(1)
    if mode == 'oversized':
        sys.stdout.write('x'*65537);raise SystemExit(0)
    if mode == 'malformed':
        print('{broken');raise SystemExit(0)
    value={
      'schema':'synapse.goxlr.provider-status/v3','providerActive':True,
      'deviceCount':1,'truncated':False,'devices':[{
        'id':'goxlr-1','model':'GoXLR Mini','profileModelReady':True,
        'generation':7,'stateAuthority':'provider-profile-model',
        'hardwareReadback':False,'hardwareExactRollback':False,
        'popupCapabilities':{
          'faderAVolume':True,'faderBVolume':True,'faderCVolume':True,
          'faderDVolume':True,'faderAMute':True,'faderBMute':True,
          'faderCMute':True,'faderDMute':True,'coughMute':True,
          'headphonesVolume':True,'lineOutVolume':True},
        'faders':[
          {'fader':'A','channel':'Mic','volume':110,'muteState':'Unmuted'},
          {'fader':'B','channel':'Chat','volume':120,'muteState':'Unmuted'},
          {'fader':'C','channel':'Music','volume':130,'muteState':'MutedToAll'},
          {'fader':'D','channel':'System','volume':127,'muteState':'Unmuted'}],
        'cough':{'mode':'Toggle','muteState':'Unmuted'},
        'outputs':{'headphonesVolume':180,'lineOutVolume':200,
                   'monitoredOutput':'Headphones'},
        'systemOutputSupported':True,
        'systemOutput':{'routeToLineOut':False,'systemVolume':127,
          'lineOutVolume':200,'systemFader':'D','systemMuteState':'Unmuted',
          'lineOutMix':'A','submixEnabled':False}}]}
    if mode == 'partial':
        caps=value['devices'][0]['popupCapabilities']
        for key in caps: caps[key]=False
        caps['faderAVolume']=True
    if mode == 'hold':
        value['devices'][0]['cough']['mode']='Hold'
        value['devices'][0]['popupCapabilities']['coughMute']=False
    if mode == 'bad-capability':
        value['devices'][0]['popupCapabilities']['coughMute']=False
        value['devices'][0]['extra']=True
    compact(value)

def parsed_option(name):
    try: return args[args.index(name)+1]
    except (ValueError,IndexError): raise SystemExit(64)

def control_parts():
    control=parsed_option('--control');requested=int(parsed_option('--value'))
    mute=control.endswith('-mute')
    if mute:
        original=0;kind='Mute';channel='Mic'
    else:
        kind='Volume';original=110
        channel='Headphones' if control=='headphones-volume' else ('LineOut' if control=='line-out-volume' else 'Mic')
    return control,requested,original,kind,channel

def plan():
    control,requested,original,kind,channel=control_parts()
    compact({'schema':'synapse.goxlr.popup-control-plan/v1','device':'goxlr-1',
      'model':'GoXLR Mini','control':control,'channel':channel,'generation':7,
      'original':{'kind':kind,'value':original},
      'requested':{'kind':kind,'value':requested},'cohort':'0123456789abcdef',
      'requiresAcknowledgement':'synapse-goxlr/popup-control/v1',
      'stateAuthority':'provider-profile-model','hardwareReadback':False,
      'hardwareExactRollback':False,'bounded':True})

def apply():
    control,requested,original,kind,channel=control_parts()
    if parsed_option('--cohort')!='0123456789abcdef' or parsed_option('--ack')!='synapse-goxlr/popup-control/v1':
        raise SystemExit(64)
    status=os.environ.get('SYNAPSE_GOXLR_APPLY_RESULT','Applied')
    attempted=status in ('RolledBack','RollbackFailed')
    succeeded=True if status=='RolledBack' else (False if status=='RollbackFailed' else None)
    observed=original if status in ('Refused','RolledBack') else requested
    changed=status in ('Applied','RollbackFailed','Drifted')
    compact({'schema':'synapse.goxlr.popup-control-receipt/v1','device':'goxlr-1',
      'model':'GoXLR Mini','status':status,'control':control,'channel':channel,
      'original':{'kind':kind,'value':original},
      'requested':{'kind':kind,'value':requested},
      'observed':{'kind':kind,'value':observed},'changed':changed,
      'stateAuthority':'provider-profile-model','hardwareReadback':False,
      'rollback':{'available':True,'authority':'provider-profile-model-only',
                  'hardwareExact':False,'attempted':attempted,'succeeded':succeeded},
      'playbackStarted':False,'captureStarted':False,'bounded':True})
    raise SystemExit(0 if status in ('Applied','AlreadyApplied') else 1)

if args==['provider-status','--format','json']:
    provider_status()
elif args==['inventory','--format','json']:
    inventory(1 if mode=='inactive-present' else 0)
elif len(args)==7 and args[0]=='plan-popup-control' and args[-2:]==['--format','json']:
    plan()
elif len(args)==11 and args[0]=='apply-popup-control' and args[-2:]==['--format','json']:
    apply()
else:
    raise SystemExit(64)
PY
chmod 755 "$work/synapse-goxlr-fake"
: >"$work/argv.log"

run() {
  local mode=$1; shift
  env SYNAPSE_GOXLR="$work/synapse-goxlr-fake" \
    SYNAPSE_GOXLR_FAKE_MODE="$mode" SYNAPSE_GOXLR_FAKE_LOG="$work/argv.log" \
    "$binary" "$@"
}

run ready audio goxlr-status --format json >"$work/ready.json"
python3 - "$work/ready.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]))
assert v['schema']=='synapse.settings.audio-goxlr-status/v2'
assert v['status']=='Ready' and v['reason'] is None and v['providerActive']
assert v['presenceKnown'] and v['devicePresent'] and v['deviceCount']==1
assert v['stateAuthority']=='provider-profile-model'
assert not v['hardwareReadback'] and not v['hardwareExactRollback']
assert v['mutationAvailable'] and v['inspectionReadOnly'] and v['bounded']
d=v['devices'][0]
assert d['model']=='GoXLR Mini' and d['profileModelReady']
assert [f['fader'] for f in d['faders']]==list('ABCD')
assert [f['channel'] for f in d['faders']]==['Mic','Chat','Music','System']
assert d['faders'][2]['muted'] and d['cough']=={'mode':'Toggle','muted':False,'available':True}
assert d['outputs']['headphonesVolume']==180 and d['outputs']['lineOutVolume']==200
raw=open(sys.argv[1]).read()
for forbidden in ('goxlr-1','generation','cohort','requiresAcknowledgement','usbId','serial','bus','address','profileName'):
 assert forbidden not in raw, forbidden
PY

run partial audio goxlr-status --format json >"$work/partial.json"
python3 - "$work/partial.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));d=v['devices'][0]
assert v['mutationAvailable'] and d['controlAvailable']
assert d['faders'][0]['volumeAvailable']
assert not d['faders'][0]['muteAvailable'] and not d['cough']['available']
assert not d['outputs']['headphonesAvailable'] and not d['outputs']['lineOutAvailable']
PY

run hold audio goxlr-status --format json >"$work/hold.json"
python3 - "$work/hold.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['devices'][0]['cough']['mode']=='Hold'
assert not v['devices'][0]['cough']['available']
PY

for mode in inactive-present inactive-absent; do
  run "$mode" audio goxlr-status --format json >"$work/$mode.json"
done
python3 - "$work/inactive-present.json" "$work/inactive-absent.json" <<'PY'
import json,sys
present,absent=(json.load(open(p)) for p in sys.argv[1:])
for value in (present,absent):
 assert value['status']=='Inactive' and value['reason']=='provider-inactive'
 assert not value['providerActive'] and value['presenceKnown']
 assert value['deviceCount']==0 and value['devices']==[] and not value['mutationAvailable']
assert present['devicePresent'] and not absent['devicePresent']
PY

SYNAPSE_GOXLR="$work/missing" "$binary" audio goxlr-status --format json >"$work/unavailable.json"
python3 - "$work/unavailable.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable'
assert v['reason']=='adapter-unavailable' and not v['providerActive']
assert not v['presenceKnown'] and not v['devicePresent'] and not v['mutationAvailable']
PY

for mode in malformed bad-capability signaled oversized; do
  run "$mode" audio goxlr-status --format json >"$work/$mode.json"
  python3 - "$work/$mode.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Failed'
assert v['reason'] in ('invalid-response','status-unavailable','response-too-large')
assert not v['providerActive'] and not v['mutationAvailable']
PY
done

start=$(date +%s)
run timeout audio goxlr-status --format json >"$work/timeout.json"
(( $(date +%s) - start < 5 ))
python3 - "$work/timeout.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Failed' and v['reason']=='timeout'
PY

run ready audio plan-goxlr-control --control fader-a-volume --value 123 --format json >"$work/plan.json"
python3 - "$work/plan.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v=={
 'schema':'synapse.settings.audio-goxlr-control-plan/v1','status':'Planned',
 'control':'fader-a-volume','originalValue':110,'requestedValue':123,
 'cohort':'0123456789abcdef',
 'requiresAcknowledgement':'synapse-settings/audio-goxlr-popup/v1',
 'stateAuthority':'provider-profile-model','hardwareReadback':False,
 'hardwareExactRollback':False,'applied':False,'bounded':True}
PY
run ready audio set-goxlr-control --control fader-a-volume --value 123 \
  --original 110 --cohort 0123456789abcdef \
  --ack synapse-settings/audio-goxlr-popup/v1 --format json >"$work/receipt.json"
python3 - "$work/receipt.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Applied'
assert v['originalValue']==110 and v['requestedValue']==123 and v['observedValue']==123
assert v['changed'] and not v['rollbackAttempted'] and v['rollbackSucceeded'] is None
assert not v['hardwareReadback'] and not v['hardwareExactRollback']
assert not v['playbackStarted'] and not v['captureStarted'] and v['bounded']
PY

set +e
SYNAPSE_GOXLR_APPLY_RESULT=Refused run ready audio set-goxlr-control \
  --control fader-a-volume --value 123 --original 110 \
  --cohort 0123456789abcdef --ack synapse-settings/audio-goxlr-popup/v1 \
  --format json >"$work/refused.json"
refused_status=$?
set -e
[[ $refused_status == 1 ]]
python3 - "$work/refused.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Refused'
assert not v['changed'] and v['observedValue']==110 and not v['rollbackAttempted']
PY

before=$(wc -l <"$work/argv.log")
for value in 2 19 4294967296; do
  set +e
  run ready audio plan-goxlr-control --control cough-mute --value "$value" --format json >/dev/null 2>&1
  code=$?
  set -e
  [[ $code == 2 ]]
done
[[ $(wc -l <"$work/argv.log") == "$before" ]]

set +e
run ready audio set-goxlr-control --control fader-a-volume --value 123 \
  --original 110 --cohort 0123456789abcdef --ack wrong --format json >/dev/null 2>&1
code=$?
set -e
[[ $code == 2 ]]

! grep -Eq 'provider-serve|provider-start|configure-profile|initialize|system-output' "$work/argv.log"
grep -Fq 'apply-popup-control --control fader-a-volume --value 123 --cohort 0123456789abcdef --ack synapse-goxlr/popup-control/v1 --format json' "$work/argv.log"
! grep -Fq -- '--original' "$work/argv.log"

python3 - "$root" "$work" <<'PY'
import json,sys
from pathlib import Path
from jsonschema import Draft202012Validator
root=Path(sys.argv[1]);work=Path(sys.argv[2])
for schema_name,files in (
 ('audio-goxlr-status-v2.schema.json',['ready.json','partial.json','hold.json','inactive-present.json','inactive-absent.json','unavailable.json','malformed.json','bad-capability.json','signaled.json','oversized.json','timeout.json']),
 ('audio-goxlr-control-plan-v1.schema.json',['plan.json']),
 ('audio-goxlr-control-receipt-v1.schema.json',['receipt.json','refused.json'])):
 schema=json.loads((root/'schemas'/schema_name).read_text())
 Draft202012Validator.check_schema(schema);validator=Draft202012Validator(schema)
 for name in files: validator.validate(json.loads((work/name).read_text()))
print('GoXLR v2 status and popup mediation schema validations: PASS')
PY

bash -n "$root/tests/audio-goxlr-status-run.sh"
printf 'synapse-settings GoXLR popup mediation tests: PASS\n'
