#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

settings=${1:?settings test binary required}
broker=${2:?broker test binary required}
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
pids=()
broker_pid=
trap 'if [[ -n "$broker_pid" ]]; then kill "$broker_pid" 2>/dev/null || true; wait "$broker_pid" 2>/dev/null || true; fi; for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; done; rm -rf "$work"' EXIT

audio="$work/audio"
mkdir -p "$audio" "$work/bin" "$work/config/synapse" "$work/runtime"
chmod 700 "$work/runtime"
cp "$(command -v sleep)" "$work/app-rule"
cp "$(command -v sleep)" "$work/app-other"
mkdir -p "$work/directory-apps"
cp "$(command -v sleep)" "$work/directory-apps/dir-app"
"$work/app-rule" 300 &
rule_pid=$!
pids+=("$rule_pid")
"$work/app-other" 300 &
other_pid=$!
pids+=("$other_pid")
"$work/directory-apps/dir-app" 300 &
directory_pid=$!
pids+=("$directory_pid")

cat >"$audio/sinks.json" <<'EOF'
[
 {"index":10,"name":"sink.a","description":"Integrated audio","mute":false,"volume":{"left":{"value":32768}}},
 {"index":11,"name":"sink.b","description":"USB headset","mute":false,"volume":{"left":{"value":65536}}}
]
EOF
cp "$audio/sinks.json" "$audio/sinks.all.json"
cat >"$audio/sources.json" <<'EOF'
[
 {"index":20,"name":"source.a","description":"Built-in microphone","monitor_of_sink":null,"monitor_source":"","mute":false,"volume":{"mono":{"value":32768}}},
 {"index":21,"name":"source.b","description":"USB microphone","monitor_of_sink":null,"monitor_source":"","mute":false,"volume":{"mono":{"value":65536}}},
 {"index":22,"name":"sink.a.monitor","description":"Monitor","monitor_of_sink":null,"monitor_source":"sink.a","mute":false,"volume":{"mono":{"value":65536}}}
]
EOF
cat >"$audio/cards.json" <<'EOF'
[{"index":40,"name":"card.a","description":"Primary card","active_profile":"HiFi"}]
EOF
printf 'sink.a\n' >"$audio/default-sink"
printf 'source.a\n' >"$audio/default-source"
printf 'success\n' >"$audio/move-mode"
: >"$audio/moves.log"

reset_streams() {
  cat >"$audio/sink-inputs.json" <<EOF
[{"index":30,"sink":10,"mute":false,"volume":{"left":{"value":65536}},"properties":{"application.name":"Existing","application.process.id":"$rule_pid"}}]
EOF
  printf '[]\n' >"$audio/source-outputs.json"
  cp "$audio/sinks.all.json" "$audio/sinks.json"
  printf 'success\n' >"$audio/move-mode"
  : >"$audio/moves.log"
}
reset_streams

cat >"$work/bin/pactl-fake" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
fixture=${SYNAPSE_AUDIO_FIXTURES:?}
case "${1-} ${2-} ${3-}" in
  '--format=json info ')
    printf '{"default_sink_name":"%s","default_source_name":"%s"}\n' "$(cat "$fixture/default-sink")" "$(cat "$fixture/default-source")" ;;
  '--format=json list sinks') cat "$fixture/sinks.json" ;;
  '--format=json list sources') cat "$fixture/sources.json" ;;
  '--format=json list sink-inputs') cat "$fixture/sink-inputs.json" ;;
  '--format=json list source-outputs') cat "$fixture/source-outputs.json" ;;
  '--format=json list cards') cat "$fixture/cards.json" ;;
  'subscribe  ')
    cat "${SYNAPSE_BROKER_EVENTS:?}" ;;
  move-sink-input*|move-source-output*)
    printf '%s\t%s\t%s\n' "$1" "$2" "$3" >>"$fixture/moves.log"
    mode=$(cat "$fixture/move-mode")
    if [[ $mode == timeout ]]; then
      exec 1>&-
      exec sleep 5
    fi
    if [[ $mode == fail ]]; then
      exit 1
    fi
    if [[ $mode != noverify ]]; then
      python3 - "$fixture" "$1" "$2" "$3" <<'PY'
import json,os,sys,tempfile
root,operation,index,target=sys.argv[1:]
stream_file='sink-inputs.json' if operation=='move-sink-input' else 'source-outputs.json'
endpoint_file='sinks.json' if operation=='move-sink-input' else 'sources.json'
field='sink' if operation=='move-sink-input' else 'source'
endpoints=json.load(open(os.path.join(root,endpoint_file)))
matched=[x['index'] for x in endpoints if x['name']==target]
if len(matched)!=1: raise SystemExit(66)
path=os.path.join(root,stream_file)
streams=json.load(open(path));found=False
for stream in streams:
    if stream['index']==int(index): stream[field]=matched[0];found=True
if not found: raise SystemExit(67)
fd,tmp=tempfile.mkstemp(dir=root,prefix='.streams-',text=True)
with os.fdopen(fd,'w') as out: json.dump(streams,out,separators=(',',':'));out.write('\n')
os.replace(tmp,path)
PY
      if [[ $mode == identity-change ]]; then
        python3 - "$fixture" "$1" "$2" <<'PY'
import json,os,sys,tempfile
root,operation,index=sys.argv[1:]
path=os.path.join(root,'sink-inputs.json' if operation=='move-sink-input' else 'source-outputs.json')
v=json.load(open(path));replacement=open(os.path.join(root,'replacement-pid')).read().strip()
for stream in v:
    if stream['index']==int(index): stream['properties']['application.process.id']=replacement
fd,tmp=tempfile.mkstemp(dir=root,prefix='.identity-',text=True)
with os.fdopen(fd,'w') as out: json.dump(v,out,separators=(',',':'));out.write('\n')
os.replace(tmp,path)
PY
      fi
    fi
    [[ $mode != partial-fail ]] ;;
  *) exit 64 ;;
esac
EOF
chmod 755 "$work/bin/pactl-fake"
policy="$work/config/synapse/audio-route-policy-v1.json"
BASE_ENV=(env HOME="$work" XDG_CONFIG_HOME="$work/config" \
  XDG_RUNTIME_DIR="$work/runtime" SYNAPSE_PACTL="$work/bin/pactl-fake" \
  SYNAPSE_AUDIO_FIXTURES="$audio" SYNAPSE_AUDIO_ROUTE_POLICY="$policy")

"${BASE_ENV[@]}" "$settings" audio inventory --format json >"$work/inventory.json"
read -r sink_a sink_b _ source_b < <(python3 - "$work/inventory.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));print(v['outputs'][0]['id'],v['outputs'][1]['id'],v['inputs'][0]['id'],v['inputs'][1]['id'])
PY
)
"${BASE_ENV[@]}" "$settings" audio policy set-rule --match executable \
  --path "$work/app-rule" --direction output --device "$sink_b" \
  --ack synapse-settings/audio-route-policy/v1 --format json >"$work/rule-output.json"
"${BASE_ENV[@]}" "$settings" audio policy set-rule --match executable \
  --path "$work/app-rule" --direction input --device "$source_b" \
  --ack synapse-settings/audio-route-policy/v1 --format json >"$work/rule-input.json"
"${BASE_ENV[@]}" "$settings" audio policy set-rule --match directory \
  --path "$work/directory-apps" --direction output --device "$sink_b" \
  --ack synapse-settings/audio-route-policy/v1 --format json >"$work/rule-directory.json"

"${BASE_ENV[@]}" "$broker" --probe --format json >"$work/probe.json"
[[ $("$broker" --version) == 'synapse-audio-route-broker 1.0.0-alpha.1' ]]
python3 - "$work/probe.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['schema']=='synapse.settings.audio-route-broker-status/v1'
assert v['status']=='Ready' and v['capable'] and not v['active']
assert not v['enforcementAvailable'] and v['reason']=='broker-not-running'
assert v['mode']=='new-streams-only' and v['baselineStreams']==1
assert not v['persistentPidRules'] and not v['existingStreamMigration'] and v['bounded']
PY

start_broker() {
  local name=$1 limit=$2
  events="$work/$name.events"
  rm -f "$events"
  mkfifo "$events"
  output="$work/$name.jsonl"
  : >"$output"
  "${BASE_ENV[@]}" SYNAPSE_BROKER_EVENTS="$events" "$broker" --foreground \
    --test-exit-after-events "$limit" >"$output" &
  broker_pid=$!
  for _ in $(seq 1 200); do
    [[ -s $output ]] && break
    sleep 0.01
  done
  [[ -s $output ]]
  status_output="$work/$name.status.json"
  "${BASE_ENV[@]}" "$settings" audio broker-status --format json >"$status_output"
  python3 - "$output" "$status_output" <<'PY'
import json,sys
started=json.loads(open(sys.argv[1]).readline());status=json.load(open(sys.argv[2]))
assert started['status']=='Ready' and started['active'] and started['enforcementAvailable']
assert status==started and status['reason'] is None
text=open(sys.argv[2]).read().lower()
assert all(term not in text for term in ('"pid"','executable','rawendpoint','subscriber'))
PY
  socket_path="$work/runtime/synapse/audio-route-broker-v1.sock"
  [[ -S $socket_path && $(stat -c %a "$socket_path") == 600 ]]
  [[ $(stat -c %a "$work/runtime/synapse") == 700 ]]
  [[ $(stat -c %a "$work/runtime/synapse/audio-route-broker-v1.lock") == 600 ]]
}

wait_broker() {
  wait "$broker_pid"
  broker_pid=
}

append_playback() {
  local index=$1 sink=$2 pid=$3 label=$4
  python3 - "$audio/sink-inputs.json" "$index" "$sink" "$pid" "$label" <<'PY'
import json,os,sys,tempfile
path,index,sink,pid,label=sys.argv[1:]
v=json.load(open(path));v.append({'index':int(index),'sink':int(sink),'mute':False,'volume':{'left':{'value':65536}},'properties':{'application.name':label,'application.process.id':pid}})
fd,tmp=tempfile.mkstemp(dir=os.path.dirname(path),prefix='.playback-',text=True)
with os.fdopen(fd,'w') as out: json.dump(v,out,separators=(',',':'));out.write('\n')
os.replace(tmp,path)
PY
}

append_recording() {
  local index=$1 source=$2 pid=$3
  python3 - "$audio/source-outputs.json" "$index" "$source" "$pid" <<'PY'
import json,os,sys,tempfile
path,index,source,pid=sys.argv[1:]
v=json.load(open(path));v.append({'index':int(index),'source':int(source),'mute':False,'volume':{'mono':{'value':65536}},'properties':{'application.name':'Recorder','application.process.id':pid}})
fd,tmp=tempfile.mkstemp(dir=os.path.dirname(path),prefix='.recording-',text=True)
with os.fdopen(fd,'w') as out: json.dump(v,out,separators=(',',':'));out.write('\n')
os.replace(tmp,path)
PY
}

# Baseline streams are never moved. A duplicate new event is idempotent, while
# the next unseen stream is independently resolved against the current policy.
reset_streams
start_broker applied-and-no-rule 2
python3 - "$socket_path" <<'PY'
import socket,sys
client=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);client.settimeout(1);client.connect(sys.argv[1]);client.sendall(b'invalid-request\n')
try: assert client.recv(1)==b''
except ConnectionResetError: pass
client.close()
PY
set +e
"${BASE_ENV[@]}" "$broker" --foreground >"$work/duplicate-broker.json"
duplicate_status=$?
set -e
[[ $duplicate_status != 0 ]]
python3 - "$work/duplicate-broker.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable' and v['reason']=='broker-already-running';assert not v['active'] and not v['enforcementAvailable']
PY
append_playback 31 10 "$rule_pid" Routed
append_playback 32 10 "$other_pid" Unmatched
printf "Event 'new' on sink-input #31\nEvent 'new' on sink-input #31\nEvent 'new' on sink-input #32\n" >"$events"
wait_broker
python3 - "$output" "$audio/sink-inputs.json" "$audio/moves.log" "$sink_b" <<'PY'
import json,sys
lines=[json.loads(x) for x in open(sys.argv[1])]
assert len(lines)==3
ready,applied,skipped=lines
assert ready['baselineStreams']==1
assert applied['status']=='Applied' and applied['stream']=='playback-31' and applied['direction']=='output'
assert applied['device']==sys.argv[4] and applied['source']=='exact-executable'
assert applied['changed'] and applied['routingApplied'] and applied['verified']
assert not applied['persistentPidRule'] and not applied['existingStreamMigration']
assert skipped['status']=='Skipped' and skipped['stream']=='playback-32' and skipped['reason']=='no-matching-rule'
streams={x['index']:x['sink'] for x in json.load(open(sys.argv[2]))}
assert streams=={30:10,31:11,32:10}
moves=open(sys.argv[3]).read().splitlines();assert len(moves)==1 and '\t31\t' in moves[0]
assert all('process' not in k.lower() and 'path' not in k.lower() for receipt in lines for k in receipt)
PY
"${BASE_ENV[@]}" "$settings" audio broker-status --format json >"$work/inactive-after-stop.json"
python3 - "$work/inactive-after-stop.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Ready' and not v['active'] and not v['enforcementAvailable'];assert v['reason']=='broker-not-running'
PY
[[ ! -e $socket_path ]]

# A policy update affects only later new events. It does not reconcile the
# baseline or the stream routed under the preceding generation.
reset_streams
start_broker policy-reload 2
exec 9>"$events"
append_playback 41 10 "$rule_pid" BeforePolicyUpdate
printf "Event 'new' on sink-input #41\n" >&9
for _ in $(seq 1 300); do
  [[ $(wc -l <"$output") -ge 2 ]] && break
  sleep 0.01
done
[[ $(wc -l <"$output") -ge 2 ]]
"${BASE_ENV[@]}" "$settings" audio policy set-rule --match executable \
  --path "$work/app-rule" --direction output --device "$sink_a" \
  --ack synapse-settings/audio-route-policy/v1 --format json >"$work/rule-output-updated.json"
append_playback 42 11 "$rule_pid" AfterPolicyUpdate
printf "Event 'new' on sink-input #42\n" >&9
exec 9>&-
wait_broker
python3 - "$output" "$audio/sink-inputs.json" <<'PY'
import json,sys
lines=[json.loads(x) for x in open(sys.argv[1])]
assert len(lines)==3 and lines[1]['status']==lines[2]['status']=='Applied'
assert lines[1]['policyGeneration']+1==lines[2]['policyGeneration']
streams={x['index']:x['sink'] for x in json.load(open(sys.argv[2]))}
assert streams[30]==10 and streams[41]==11 and streams[42]==10
PY
"${BASE_ENV[@]}" "$settings" audio policy set-rule --match executable \
  --path "$work/app-rule" --direction output --device "$sink_b" \
  --ack synapse-settings/audio-route-policy/v1 --format json >"$work/rule-output-restored.json"

# A malformed policy introduced after startup blocks the new event without
# moving it; the previously accepted baseline remains untouched.
reset_streams
cp "$policy" "$work/policy.valid"
start_broker policy-invalid 1
printf ' ' >>"$policy"
"${BASE_ENV[@]}" "$settings" audio broker-status --format json \
  >"$work/policy-invalid-runtime.status.json"
python3 - "$work/policy-invalid-runtime.status.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable' and v['reason']=='policy-unavailable';assert not v['active'] and not v['enforcementAvailable']
PY
append_playback 43 10 "$rule_pid" InvalidPolicy
printf "Event 'new' on sink-input #43\n" >"$events"
wait_broker
python3 - "$output" "$audio/moves.log" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Skipped' and v['reason']=='policy-unavailable' and not v['routingApplied']
assert open(sys.argv[2]).read()==''
PY
cp "$work/policy.valid" "$policy"
chmod 600 "$policy"

# Directory-prefix rules use the same broker transaction without persisting or
# exposing the process PID.
reset_streams
start_broker directory-rule 1
append_playback 44 10 "$directory_pid" DirectoryRule
printf "Event 'new' on sink-input #44\n" >"$events"
wait_broker
python3 - "$output" "$audio/sink-inputs.json" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Applied' and v['source']=='directory-prefix'
assert v['routingApplied'] and v['verified']
assert {x['index']:x['sink'] for x in json.load(open(sys.argv[2]))}[44]==11
PY

# A new stream already on the selected endpoint is verified without a move.
reset_streams
start_broker already 1
append_playback 33 11 "$rule_pid" Already
printf "Event 'new' on sink-input #33\n" >"$events"
wait_broker
python3 - "$output" "$audio/moves.log" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='AlreadyRouted' and v['verified'] and not v['changed'] and not v['routingApplied']
assert open(sys.argv[2]).read()==''
PY

# A remove event retires the tracked identity. Reuse of the same backend index
# is resolved from the replacement process and cannot inherit the old rule.
reset_streams
start_broker index-reuse 2
exec 9>"$events"
append_playback 45 10 "$rule_pid" FirstIndexOwner
printf "Event 'new' on sink-input #45\n" >&9
for _ in $(seq 1 300); do
  [[ $(wc -l <"$output") -ge 2 ]] && break
  sleep 0.01
done
python3 - "$audio/sink-inputs.json" <<'PY'
import json,os,sys,tempfile
p=sys.argv[1];v=[x for x in json.load(open(p)) if x['index']!=45]
fd,tmp=tempfile.mkstemp(dir=os.path.dirname(p),prefix='.remove-',text=True)
with os.fdopen(fd,'w') as out:json.dump(v,out,separators=(',',':'));out.write('\n')
os.replace(tmp,p)
PY
printf "Event 'remove' on sink-input #45\n" >&9
append_playback 45 10 "$other_pid" ReplacementIndexOwner
printf "Event 'new' on sink-input #45\n" >&9
exec 9>&-
wait_broker
python3 - "$output" "$audio/sink-inputs.json" <<'PY'
import json,sys
lines=[json.loads(x) for x in open(sys.argv[1])]
assert lines[1]['status']=='Applied' and lines[1]['stream']=='playback-45'
assert lines[2]['status']=='Skipped' and lines[2]['stream']=='playback-45'
assert lines[2]['reason']=='no-matching-rule'
stream={x['index']:x for x in json.load(open(sys.argv[2]))}[45]
assert stream['sink']==10
PY

# Endpoint disappearance skips safely. Restoration affects only later streams.
reset_streams
start_broker target-missing 1
python3 - "$audio/sinks.json" <<'PY'
import json,sys
p=sys.argv[1];v=[x for x in json.load(open(p)) if x['name']!='sink.b'];json.dump(v,open(p,'w'),separators=(',',':'))
PY
append_playback 34 10 "$rule_pid" MissingTarget
printf "Event 'new' on sink-input #34\n" >"$events"
wait_broker
python3 - "$output" "$audio/moves.log" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Skipped' and v['reason']=='target-unavailable' and not v['routingApplied']
assert open(sys.argv[2]).read()==''
PY
cp "$audio/sinks.all.json" "$audio/sinks.json"

# Vanished streams and untrusted process identities fail closed.
reset_streams
start_broker vanished 1
printf "Event 'new' on sink-input #35\n" >"$events"
wait_broker
python3 - "$output" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Skipped' and v['reason']=='stream-vanished' and v['device'] is None
PY
reset_streams
start_broker dead-process 1
append_playback 35 10 2147483647 Dead
printf "Event 'new' on sink-input #35\n" >"$events"
wait_broker
python3 - "$output" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Skipped' and v['reason']=='process-unavailable' and not v['routingApplied']
PY

# A command that partially changes state but fails is compensated to the exact
# original endpoint and never produces a routingApplied claim.
reset_streams
printf 'partial-fail\n' >"$audio/move-mode"
start_broker compensated-failure 1
append_playback 36 10 "$rule_pid" Partial
printf "Event 'new' on sink-input #36\n" >"$events"
wait_broker
python3 - "$output" "$audio/sink-inputs.json" "$audio/moves.log" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Failed' and v['reason']=='move-failed'
assert v['compensationAttempted'] and v['compensationVerified']
assert not v['changed'] and not v['routingApplied'] and not v['verified']
assert {x['index']:x['sink'] for x in json.load(open(sys.argv[2]))}[36]==10
assert len(open(sys.argv[3]).read().splitlines())==2
PY

# Stream-index reuse or PID reuse between plan and postflight cannot inherit the
# old process authority, even when the backend command reached the target.
reset_streams
printf '%s\n' "$other_pid" >"$audio/replacement-pid"
printf 'identity-change\n' >"$audio/move-mode"
start_broker identity-change 1
append_playback 39 10 "$rule_pid" OriginalIdentity
printf "Event 'new' on sink-input #39\n" >"$events"
wait_broker
python3 - "$output" "$audio/sink-inputs.json" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Failed' and v['reason']=='stream-identity-changed'
assert not v['routingApplied'] and not v['compensationAttempted']
stream={x['index']:x for x in json.load(open(sys.argv[2]))}[39]
assert stream['sink']==11
PY

# Successful command without matching post-state is a verification failure.
reset_streams
printf 'noverify\n' >"$audio/move-mode"
start_broker verification-failure 1
append_playback 37 10 "$rule_pid" NoVerify
printf "Event 'new' on sink-input #37\n" >"$events"
wait_broker
python3 - "$output" "$audio/sink-inputs.json" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Failed' and v['reason']=='verification-failed'
assert not v['routingApplied'] and not v['compensationAttempted']
assert {x['index']:x['sink'] for x in json.load(open(sys.argv[2]))}[37]==10
PY

# Move timeout is bounded and produces no success claim.
reset_streams
printf 'timeout\n' >"$audio/move-mode"
start_broker move-timeout 1
append_playback 38 10 "$rule_pid" Timeout
start_seconds=$(date +%s)
printf "Event 'new' on sink-input #38\n" >"$events"
wait_broker
elapsed=$(( $(date +%s) - start_seconds ))
(( elapsed < 4 ))
python3 - "$output" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Failed' and v['reason']=='move-timeout' and not v['routingApplied']
PY

# Input rules use the independent recording direction and source-output move.
reset_streams
start_broker input-applied 1
append_recording 40 20 "$rule_pid"
printf "Event 'new' on source-output #40\n" >"$events"
wait_broker
python3 - "$output" "$audio/source-outputs.json" "$audio/moves.log" "$source_b" <<'PY'
import json,sys
v=[json.loads(x) for x in open(sys.argv[1])][1]
assert v['status']=='Applied' and v['direction']=='input' and v['stream']=='recording-40'
assert v['device']==sys.argv[4] and v['routingApplied'] and v['verified']
assert {x['index']:x['source'] for x in json.load(open(sys.argv[2]))}[40]==21
assert open(sys.argv[3]).read().startswith('move-source-output\t40\t')
PY

# Relevant malformed subscriber events terminate the broker without mutation.
reset_streams
events="$work/malformed.events";rm -f "$events";mkfifo "$events"
output="$work/malformed.jsonl"
set +e
"${BASE_ENV[@]}" SYNAPSE_BROKER_EVENTS="$events" "$broker" --foreground >"$output" &
broker_pid=$!
set -e
for _ in $(seq 1 200); do [[ -s $output ]] && break;sleep 0.01;done
printf "Event 'new' on sink-input #01\n" >"$events"
set +e
wait "$broker_pid";status=$?;broker_pid=
set -e
[[ $status != 0 ]]
python3 - "$output" <<'PY'
import json,sys
lines=[json.loads(x) for x in open(sys.argv[1])]
assert lines[-1]['status']=='Unavailable' and lines[-1]['reason']=='invalid-subscriber-event'
PY
[[ ! -s "$audio/moves.log" ]]

# Oversized event lines fail before parsing or mutation.
reset_streams
events="$work/oversized.events";rm -f "$events";mkfifo "$events"
output="$work/oversized.jsonl"
"${BASE_ENV[@]}" SYNAPSE_BROKER_EVENTS="$events" "$broker" --foreground >"$output" &
broker_pid=$!
for _ in $(seq 1 200); do [[ -s $output ]] && break;sleep 0.01;done
python3 - <<'PY' >"$events"
print('A'*512)
PY
set +e
wait "$broker_pid";status=$?;broker_pid=
set -e
[[ $status != 0 ]]
python3 - "$output" <<'PY'
import json,sys
lines=[json.loads(x) for x in open(sys.argv[1])]
assert lines[-1]['status']=='Unavailable' and lines[-1]['reason']=='invalid-subscriber-event'
PY
[[ ! -s "$audio/moves.log" ]]

# A service stop exits cleanly and terminates its owned subscriber only.
reset_streams
events="$work/stop.events";rm -f "$events";mkfifo "$events"
output="$work/stop.jsonl"
"${BASE_ENV[@]}" SYNAPSE_BROKER_EVENTS="$events" "$broker" --foreground >"$output" &
broker_pid=$!
for _ in $(seq 1 200); do [[ -s $output ]] && break;sleep 0.01;done
kill -TERM "$broker_pid"
wait "$broker_pid"
broker_pid=
[[ ! -s "$audio/moves.log" ]]

# The private runtime status client rejects malformed, stalled and loose-mode
# peers while treating a stale unreachable socket as inactive.
start_fake_status() {
  local behavior=$1 mode=$2
  rm -f "$socket_path"
  python3 - "$socket_path" "$behavior" "$mode" <<'PY' &
import os,socket,sys,time
path,behavior,mode=sys.argv[1:]
server=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);server.bind(path);os.chmod(path,int(mode,8));server.listen(1)
if behavior!='no-accept':
    connection,_=server.accept();connection.recv(64)
    if behavior=='malformed': connection.sendall(b'{"schema":"invalid"}\n')
    elif behavior=='oversized':
        try: connection.sendall(b'x'*5000+b'\n')
        except BrokenPipeError: pass
    elif behavior=='timeout': time.sleep(1)
    connection.close()
else: time.sleep(1)
server.close()
PY
  fake_status_pid=$!
  pids+=("$fake_status_pid")
  for _ in $(seq 1 200); do [[ -S $socket_path ]] && break;sleep 0.01;done
  [[ -S $socket_path ]]
}

start_fake_status malformed 600
"${BASE_ENV[@]}" "$settings" audio broker-status --format json >"$work/status-malformed.json"
wait "$fake_status_pid"
python3 - "$work/status-malformed.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable' and v['reason']=='invalid-response' and not v['active']
PY

start_fake_status oversized 600
"${BASE_ENV[@]}" "$settings" audio broker-status --format json >"$work/status-oversized.json"
wait "$fake_status_pid"
python3 - "$work/status-oversized.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable' and v['reason']=='invalid-response' and not v['active']
PY

start_fake_status timeout 600
"${BASE_ENV[@]}" "$settings" audio broker-status --format json >"$work/status-timeout.json"
wait "$fake_status_pid"
python3 - "$work/status-timeout.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable' and v['reason']=='timeout' and not v['enforcementAvailable']
PY

start_fake_status no-accept 666
"${BASE_ENV[@]}" "$settings" audio broker-status --format json >"$work/status-loose-mode.json"
kill "$fake_status_pid" 2>/dev/null || true
wait "$fake_status_pid" 2>/dev/null || true
python3 - "$work/status-loose-mode.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable' and v['reason']=='runtime-state-invalid'
PY

rm -f "$socket_path"
python3 - "$socket_path" <<'PY'
import os,socket,sys
server=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);server.bind(sys.argv[1]);os.chmod(sys.argv[1],0o600);server.close()
PY
"${BASE_ENV[@]}" "$settings" audio broker-status --format json >"$work/status-stale.json"
python3 - "$work/status-stale.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Ready' and v['reason']=='broker-not-running' and not v['active']
PY
rm -f "$socket_path"
chmod 755 "$work/runtime/synapse"
"${BASE_ENV[@]}" "$settings" audio broker-status --format json >"$work/status-directory-mode.json"
chmod 700 "$work/runtime/synapse"
python3 - "$work/status-directory-mode.json" <<'PY'
import json,sys
v=json.load(open(sys.argv[1]));assert v['status']=='Unavailable' and v['reason']=='runtime-state-invalid'
PY

mkdir "$work/runtime-symlink";chmod 700 "$work/runtime-symlink"
ln -s "$work/runtime/synapse" "$work/runtime-symlink/synapse"
env XDG_RUNTIME_DIR="$work/runtime-symlink" "$settings" audio broker-status --format json >"$work/status-symlink.json"
env -u XDG_RUNTIME_DIR "$settings" audio broker-status --format json >"$work/status-no-runtime.json"
python3 - "$work/status-symlink.json" "$work/status-no-runtime.json" <<'PY'
import json,sys
symlink,no_runtime=(json.load(open(path)) for path in sys.argv[1:])
assert symlink['reason']=='runtime-state-invalid' and symlink['status']=='Unavailable'
assert no_runtime['reason']=='runtime-unavailable' and no_runtime['status']=='Unavailable'
PY

# Broker status and all event receipts validate against pinned strict schemas.
python3 - "$root" "$work" <<'PY'
import json,sys
from pathlib import Path
from jsonschema import Draft202012Validator
root,work=map(Path,sys.argv[1:])
status_schema=json.loads((root/'schemas/audio-route-broker-status-v1.schema.json').read_text())
receipt_schema=json.loads((root/'schemas/audio-route-broker-receipt-v1.schema.json').read_text())
Draft202012Validator.check_schema(status_schema);Draft202012Validator.check_schema(receipt_schema)
status_validator=Draft202012Validator(status_schema);receipt_validator=Draft202012Validator(receipt_schema)
status_paths=[work/'probe.json',work/'inactive-after-stop.json',work/'duplicate-broker.json',work/'status-malformed.json',work/'status-oversized.json',work/'status-timeout.json',work/'status-loose-mode.json',work/'status-stale.json',work/'status-directory-mode.json',work/'status-symlink.json',work/'status-no-runtime.json']
status_paths.extend(work.glob('*.status.json'))
for path in status_paths: status_validator.validate(json.loads(path.read_text()))
receipts=0
for path in work.glob('*.jsonl'):
    for line in path.read_text().splitlines():
        value=json.loads(line)
        if value['schema'].endswith('broker-status/v1'): status_validator.validate(value)
        elif value['schema'].endswith('broker-receipt/v1'): receipt_validator.validate(value);receipts+=1
        else: raise AssertionError(value['schema'])
assert receipts==17,receipts
print(f'broker schema validations: receipts={receipts}')
PY

bash -n "$root/tests/broker-run.sh"
printf 'synapse-settings new-stream Audio broker tests: PASS\n'
