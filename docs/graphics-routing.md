# Application GPU routing

## User model

Settings exposes **Grafica** separately from **Audio**.

- **Predefinita** chooses the rendering GPU for newly launched mediated apps.
- **Applicazione** matches one exact executable.
- **Cartella** matches executable paths below a canonical directory; this is the
  intended Steam-library rule.

Exact executable rules beat directory rules. Among directory rules, the
longest component-safe prefix wins. The default is used otherwise.

## Important limitations

A GPU is selected at process creation. An already running process cannot be
moved safely. A directory is not continuously watched and a process name alone
is not accepted because names collide; Synapse resolves the executable's
canonical path at launch.

A Steam rule may be applied either to the Steam executable (inherited by its
children) or to one or more canonical library roots. The launch-broker phase
must account for Steam Runtime/container paths before claiming coverage.

The display GPU and compositor ownership are a separate reboot/session gate.
They are never changed by this application policy.
