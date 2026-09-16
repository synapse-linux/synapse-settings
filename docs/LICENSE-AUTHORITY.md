# Synapse Settings MIT license authority

The project owner selected the MIT License for all first-party Synapse Settings source on 2026-09-02. This branch aligns the project `LICENSE` and first-party SPDX identifiers with that decision.

Provenance review at alignment time:

- all 11 commits then reachable in this repository were authored by Mario Giustiniani `<mariogiustiniani@gmail.com>`;
- the C11 core and broker, C++/Qt host and plugin, QML presentation, schemas, translations, tests and build definitions were prepared as first-party Synapse work;
- no third-party source, assets or generated implementation are being relicensed by this change;
- Qt, json-c, PulseAudio/PipeWire utilities and other system components remain independent dependencies under their own licenses;
- invoking a separately installed executable through fixed argv does not relabel that executable or its license;
- `libsynapse-core`, Synapse GoXLR and the Synapse OS shell remain separate projects and retain their own license and provenance records.

Every redistributed dependency and generated artifact still requires its own license inventory and applicable notices. This record does not grant trademark rights or override any third-party license obligation.
