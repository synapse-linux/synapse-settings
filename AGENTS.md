# Synapse Settings agent contract

- The authoritative settings inventory core is C11 and works headlessly.
- Composition lock data and pacman state are read-only inputs. Bound all files,
  records, strings and optional subprocess output before expanding schemas.
- Keep semantic layers distinct from generated live drift (`unlayered`). Never
  invent ownership for transitive or machine-local packages.
- Docker inventory is optional, time/output bounded and direct argv only. This
  project must never gain Docker-socket mutation or isolation responsibilities.
- Keep text/JSON stdout clean and never expose credentials, SSIDs or unbounded
  container/application labels.
- Build against released `libsynapse-core`; packaging lives only in
  `synapse-pkgbuilds/synapse-settings`.
- Pass strict GCC/Clang, ASan/UBSan, analyzer, malformed composition and
  subprocess timeout tests.
