# Synapse Settings

CLI-first C11 settings core for Synapse. It exposes lazy Settings sections,
semantic layers, a typed Audio inventory/default boundary, and an application
GPU policy as deterministic text or versioned JSON.

```bash
make CORE_ROOT=/path/to/staged-core clean all test
build/synapse-settings sections --format json
build/synapse-settings audio inventory --format json
build/synapse-settings audio plan-default --direction output --device OUTPUT_ID --format json
build/synapse-settings graphics inventory --format json
build/synapse-settings graphics policy show --format json
build/synapse-settings graphics resolve --path /absolute/executable --format json
```

Audio mutation accepts only an opaque device token from the current inventory
and the exact `synapse-settings/audio-default/v1` acknowledgement. It uses fixed,
bounded `pactl` argv and verifies the new default.

Graphics policy supports:

- a default GPU for future Synapse-brokered applications;
- exact executable rules;
- canonical directory-prefix rules, such as a Steam library assigned to a
  gaming GPU;
- deterministic resolution: exact executable, then longest directory prefix,
  then default.

The policy is private, bounded and atomically replaced. Alpha 2 deliberately
reports `enforcementAvailable=false`: the launch broker that translates a
semantic GPU choice into capability-specific Mesa or NVIDIA process environment
is not integrated yet. Existing processes cannot be migrated, and this policy
does not change the compositor/display GPU.

Docker process inventory remains optional and bounded; the tool performs no
Docker or package mutations. Runtime dependencies: `libsynapse-core.so.0` and
`json-c`.
