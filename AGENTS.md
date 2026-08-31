# Synapse Settings agent contract

- The authoritative settings inventory core is C11 and works headlessly.
- Composition lock data and pacman state are read-only inputs. Bound all files,
  records, strings and optional subprocess output before expanding schemas.
- Keep semantic layers distinct from generated live drift (`unlayered`). Never
  invent ownership for transitive or machine-local packages.
- Docker inventory is optional, time/output bounded and direct argv only. This
  project must never gain Docker-socket mutation or isolation responsibilities.
- Audio observation and default-device changes use only bounded fixed `pactl`
  argv, stable opaque tokens, exact acknowledgements and post-write verification.
  Raw PipeWire/Pulse names must not leave the core contract.
- Graphics inventory is read directly from bounded sysfs records. Persist only a
  private atomic application-rendering policy with stable opaque GPU IDs, exact
  executable paths or canonical directory-prefix rules. Exact paths outrank the
  longest matching directory. Never claim that a running process can migrate
  GPUs, that policy changes the display GPU, or that a rule is enforced before
  the launch broker is integrated.
- QML is presentation-only: it cannot parse raw JSON, inject environment
  variables, construct arbitrary commands or mutate PipeWire/GPU policy itself.
- Keep text/JSON stdout clean and never expose credentials, SSIDs, raw device
  identities or unbounded container/application labels.
- Build against released `libsynapse-core`; packaging lives only in
  `synapse-pkgbuilds/synapse-settings`.
- Pass strict GCC/Clang, ASan/UBSan, analyzer, malformed composition/policy,
  symlink, fixed-argv mutation, prefix-boundary and subprocess timeout tests.
