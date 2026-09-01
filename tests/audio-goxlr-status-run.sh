#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

binary=${1:?binary required}
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/synapse-goxlr-fake" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >>"$SYNAPSE_GOXLR_FAKE_LOG"
[[ $# == 3 && $1 == provider-status && $2 == --format && $3 == json ]] || exit 64
case "${SYNAPSE_GOXLR_FAKE_MODE:-ready}" in
  ready)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":2,"truncated":false,"devices":[{"id":"goxlr-2","model":"GoXLR","systemOutputSupported":false,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":false,"systemVolume":1,"lineOutVolume":2,"systemFader":null,"systemMuteState":null,"lineOutMix":"B","submixEnabled":true}},{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":254,"lineOutVolume":255,"systemFader":"D","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}}]}'
    ;;
  empty)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":0,"truncated":false,"devices":[]}'
    ;;
  truncated|too-many)
    python3 - "$SYNAPSE_GOXLR_FAKE_MODE" <<'PY'
import json,sys
count=8 if sys.argv[1]=='truncated' else 9
value={'schema':'synapse.goxlr.provider-status/v2','deviceCount':count,
       'truncated':sys.argv[1]=='truncated','devices':[]}
for number in range(count,0,-1):
 value['devices'].append({
  'id':f'goxlr-{number}','model':'Unknown',
  'systemOutputSupported':False,
  'stateAuthority':'provider-profile-model',
  'systemOutput':{'routeToLineOut':False,'systemVolume':0,
                  'lineOutVolume':0,'systemFader':None,
                  'systemMuteState':None,'lineOutMix':'A',
                  'submixEnabled':False}})
print(json.dumps(value,separators=(',',':')))
PY
    ;;
  inactive)
    exit 69
    ;;
  inactive-126)
    exit 126
    ;;
  inactive-127)
    exit 127
    ;;
  signaled)
    kill -TERM "$$"
    ;;
  malformed)
    printf '%s\n' '{broken'
    ;;
  wrong-schema)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v1","deviceCount":0,"truncated":false,"devices":[]}'
    ;;
  extra-root)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":0,"truncated":false,"devices":[],"extra":true}'
    ;;
  duplicate-root)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","schema":"synapse.goxlr.provider-status/v2","deviceCount":0,"truncated":false,"devices":[]}'
    ;;
  count-mismatch)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[]}'
    ;;
  invalid-truncation)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":0,"truncated":true,"devices":[]}'
    ;;
  wrong-authority)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"hardware","systemOutput":{"routeToLineOut":true,"systemVolume":1,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}}]}'
    ;;
  duplicate-device)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":2,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":1,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}},{"id":"goxlr-1","model":"GoXLR","systemOutputSupported":false,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":false,"systemVolume":1,"lineOutVolume":2,"systemFader":null,"systemMuteState":null,"lineOutMix":"B","submixEnabled":true}}]}'
    ;;
  unknown-model)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"Other","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":1,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}}]}'
    ;;
  extra-system-field)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":1,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false,"rawAddress":"forbidden"}}]}'
    ;;
  null-mix)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":1,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":null,"submixEnabled":false}}]}'
    ;;
  leading-zero)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":01,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}}]}'
    ;;
  non-ascii)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Miní","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":1,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}}]}'
    ;;
  missing-system-field)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":1,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":"A"}}]}'
    ;;
  volume-out-of-range)
    printf '%s\n' '{"schema":"synapse.goxlr.provider-status/v2","deviceCount":1,"truncated":false,"devices":[{"id":"goxlr-1","model":"GoXLR Mini","systemOutputSupported":true,"stateAuthority":"provider-profile-model","systemOutput":{"routeToLineOut":true,"systemVolume":256,"lineOutVolume":2,"systemFader":"A","systemMuteState":"Unmuted","lineOutMix":"A","submixEnabled":false}}]}'
    ;;
  limit)
    python3 - <<'PY'
import sys
value='{"schema":"synapse.goxlr.provider-status/v2","deviceCount":0,"truncated":false,"devices":[]}'
sys.stdout.write(value + ' ' * (65536 - len(value)))
PY
    ;;
  oversized)
    python3 - <<'PY'
import sys
sys.stdout.write('x' * 65537)
PY
    ;;
  timeout)
    exec 1>&-
    exec sleep 10
    ;;
  *) exit 64 ;;
esac
SH
chmod 755 "$work/synapse-goxlr-fake"

run_status() {
  local mode=$1 output=$2
  env SYNAPSE_GOXLR="$work/synapse-goxlr-fake" \
    SYNAPSE_GOXLR_FAKE_MODE="$mode" SYNAPSE_GOXLR_FAKE_LOG="$work/argv.log" \
    "$binary" audio goxlr-status --format json >"$output"
}

: >"$work/argv.log"
run_status ready "$work/ready.json"
python3 - "$work/ready.json" <<'PY'
import json,sys
value=json.load(open(sys.argv[1]))
assert value == {
 'schema':'synapse.settings.audio-goxlr-status/v1',
 'status':'Ready','reason':None,'providerActive':True,
 'deviceCount':2,'truncated':False,
 'devices':[
  {'id':'goxlr-1','model':'GoXLR Mini','systemOutputSupported':True,
   'controlAvailable':False},
  {'id':'goxlr-2','model':'GoXLR','systemOutputSupported':False,
   'controlAvailable':False}],
 'stateAuthority':'provider-profile-model','hardwareReadback':False,
 'hardwareExactRollback':False,'mutationAvailable':False,
 'readOnly':True,'bounded':True}
raw=open(sys.argv[1]).read()
for forbidden in ('systemVolume','lineOutVolume','systemFader','systemMuteState',
                  'lineOutMix','submixEnabled','rawAddress'):
 assert forbidden not in raw
PY

env SYNAPSE_GOXLR="$work/synapse-goxlr-fake" SYNAPSE_GOXLR_FAKE_MODE=ready \
  SYNAPSE_GOXLR_FAKE_LOG="$work/argv.log" \
  "$binary" audio goxlr-status --format text >"$work/ready.txt"
grep -Fq 'GoXLR provider ready: 2 device(s)' "$work/ready.txt"
grep -Fq 'goxlr-1  GoXLR Mini  System output supported  control unavailable' \
  "$work/ready.txt"

run_status empty "$work/empty.json"
python3 - "$work/empty.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Ready' and v['providerActive']
assert v['deviceCount']==0 and v['devices']==[] and v['reason'] is None
PY

run_status truncated "$work/truncated.json"
python3 - "$work/truncated.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Ready' and v['providerActive']
assert v['deviceCount']==8 and len(v['devices'])==8 and v['truncated']
assert [item['id'] for item in v['devices']]==[f'goxlr-{n}' for n in range(1,9)]
PY

SYNAPSE_GOXLR="$work/absent" "$binary" audio goxlr-status --format json \
  >"$work/unavailable.json"
python3 - "$work/unavailable.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable'
assert v['reason']=='adapter-unavailable' and not v['providerActive']
assert v['deviceCount']==0 and v['devices']==[] and not v['truncated']
PY

cp "$work/synapse-goxlr-fake" "$work/non-executable"
chmod 644 "$work/non-executable"
SYNAPSE_GOXLR="$work/non-executable" \
  "$binary" audio goxlr-status --format json >"$work/non-executable.json"
python3 - "$work/non-executable.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Failed'
assert v['reason']=='status-unavailable' and not v['providerActive']
PY

for mode in inactive inactive-126 inactive-127; do
  run_status "$mode" "$work/$mode.json"
  python3 - "$work/$mode.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Inactive'
assert v['reason']=='provider-inactive' and not v['providerActive']
PY
done

run_status signaled "$work/signaled.json"
python3 - "$work/signaled.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Failed'
assert v['reason']=='status-unavailable' and not v['providerActive']
PY

mkdir "$work/exec-directory"
SYNAPSE_GOXLR="$work/exec-directory" \
  "$binary" audio goxlr-status --format json >"$work/exec-failed.json"
python3 - "$work/exec-failed.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Failed'
assert v['reason']=='status-unavailable' and not v['providerActive']
PY

for mode in malformed wrong-schema extra-root duplicate-root count-mismatch \
  invalid-truncation too-many wrong-authority duplicate-device unknown-model \
  extra-system-field null-mix leading-zero non-ascii missing-system-field \
  volume-out-of-range; do
  run_status "$mode" "$work/$mode.json"
  python3 - "$work/$mode.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Failed'
assert v['reason']=='invalid-response' and not v['providerActive']
assert v['deviceCount']==0 and v['devices']==[]
PY
done

run_status limit "$work/limit.json"
python3 - "$work/limit.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Ready'
assert v['deviceCount']==0 and v['devices']==[] and v['reason'] is None
PY

run_status oversized "$work/oversized.json"
python3 - "$work/oversized.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Failed'
assert v['reason']=='response-too-large' and v['bounded']
PY

start=$(date +%s)
run_status timeout "$work/timeout.json"
elapsed=$(( $(date +%s) - start ))
(( elapsed < 5 ))
python3 - "$work/timeout.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Failed'
assert v['reason']=='timeout' and not v['providerActive']
PY

before=$(wc -l <"$work/argv.log")
set +e
SYNAPSE_GOXLR="$work/synapse-goxlr-fake" \
  SYNAPSE_GOXLR_FAKE_LOG="$work/argv.log" \
  "$binary" audio goxlr-status --unknown >"$work/invalid.stdout" \
  2>"$work/invalid.stderr"
status=$?
set -e
[[ $status == 2 && $(wc -l <"$work/argv.log") == "$before" ]]
[[ $(sort -u "$work/argv.log") == 'provider-status --format json' ]]

python3 - "$root" "$work" <<'PY'
import json,sys
from pathlib import Path
from jsonschema import Draft202012Validator
root=Path(sys.argv[1]);work=Path(sys.argv[2])
schema=json.loads((root/'schemas/audio-goxlr-status-v1.schema.json').read_text())
Draft202012Validator.check_schema(schema)
validator=Draft202012Validator(schema)
files=list(work.glob('*.json'))
for path in files:
 validator.validate(json.loads(path.read_text()))
ready=json.loads((work/'ready.json').read_text())
invalid=[]
value=dict(ready);value['hardwareReadback']=True;invalid.append(value)
value=dict(ready);value['mutationAvailable']=True;invalid.append(value)
value=dict(ready);value['reason']='provider-inactive';invalid.append(value)
value=dict(ready);value['deviceCount']=9;invalid.append(value)
value=dict(ready);value['deviceCount']=1;invalid.append(value)
value=dict(ready);value['truncated']=True;invalid.append(value)
value=dict(ready);value['devices']=[ready['devices'][0],ready['devices'][0]];invalid.append(value)
for value in invalid:
 assert list(validator.iter_errors(value)), value
print(f'GoXLR status schema validations: {len(files)}')
print(f'GoXLR status schema rejections: {len(invalid)}')
PY

bash -n "$root/tests/audio-goxlr-status-run.sh"
printf 'synapse-settings read-only GoXLR status tests: PASS\n'
