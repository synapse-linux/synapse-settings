# Synapse Settings

CLI-first C11 settings inventory for Synapse. It exposes available settings
sections and semantic layer/package ownership as text or versioned JSON,
including an explicit generated `unlayered` view of live package drift.

```bash
make CORE_ROOT=/path/to/staged-core clean all test
build/synapse-settings sections --format json
build/synapse-settings layers --format text
build/synapse-settings --version
```

Docker process inventory is optional and bounded; the tool performs no Docker
or package mutations. Runtime dependency: `libsynapse-core.so.0`.
