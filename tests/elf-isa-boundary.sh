#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
export LC_ALL=C

if [[ $# -lt 1 ]]; then
  printf 'production ELF path required\n' >&2
  exit 2
fi

for elf in "$@"; do
  [[ -f "$elf" ]] || {
    printf 'production ELF missing: %s\n' "$elf" >&2
    exit 1
  }

  mapfile -t properties < <(readelf -nW "$elf" | grep 'x86 ISA needed:')
  [[ ${#properties[@]} -eq 1 ]] || {
    printf 'exactly one x86 ISA property note required: %s\n' "$elf" >&2
    exit 1
  }

  property=${properties[0]}
  needed=${property#*x86 ISA needed: }
  needed=${needed%%, x86 feature used:*}
  needed=${needed%%, x86 ISA used:*}
  used=${property##*x86 ISA used: }
  [[ "$needed" == 'x86-64-baseline' && "$used" == 'x86-64-baseline' ]] || {
    printf 'exact x86-64-baseline needed/used note required: %s\n' "$elf" >&2
    exit 1
  }
done

printf 'synapse-settings production ELF ISA boundary: PASS\n'
