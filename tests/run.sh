#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

binary=${1:?binary required}
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
assert value['docker']['active'][0]=={
    'name':'builder','image':'image:test','status':'Up 5 minutes'}
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
assert value['schema']=='synapse.settings.sections/v1'
assert [x['id'] for x in value['sections']]==['layers','input','themes']
assert all(x['available'] and x['icon'] for x in value['sections'])
PY
"$binary" sections --format text | grep -Fq $'layers\tLayers\tlayers\tavailable'
[[ $($binary --version) == 'synapse-settings 0.1.0-alpha.1' ]]
"$binary" --help | grep -Fq 'synapse-settings layers'

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

for args in 'unknown' 'sections --format yaml' 'layers --unknown'; do
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
