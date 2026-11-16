# Source

This repository contains the PS4 backend, build tools, tests and integration
patches. The engine and dependencies are fetched by `tools/bootstrap.py` at the
commits in `upstream.lock.json`; their complete source trees are not duplicated.

| Path | Contents |
|---|---|
| `platform/ps4/` | Native startup, controller, audio, storage, diagnostics and C++ runtime integration |
| `renderer/orbis/` | GPU ownership, allocation and presentation support |
| `game/CMakeLists.txt` | Upstream game build integration |
| `patches/` | Changes to the pinned game, Aurora, Dawn and Mesa sources |
| `tools/` | Build, data preparation, packaging and profiling tools |
| `tests/` | Renderer, runtime, storage and ownership regressions |

Aurora translates the game's GX rendering into Dawn/WebGPU. Dawn uses native
Orbis RADV and PS4 video output. SDL connects native input/audio services to the
upstream engine, with MusyX providing the original audio mixer.

The RC1 source tag is `v0.1.0-rc1` and matches package version `00.44`, with
the portable build-path adaptations recorded separately. The game source is pinned to `c872da704adde9a7838492aaa56dac73a32053e4`.
[SOURCE-RECEIPT.json](../SOURCE-RECEIPT.json) identifies the frozen game-package
inputs and separately records PC helper, documentation and portable build-path
adaptations. Those tooling changes do not alter the packaged executable.

See [BUILDING.md](BUILDING.md) for build requirements and commands. Game images,
keys, saves, SDK binaries and generated console packages are excluded from Git.
Per-file licences and notices are retained in [NOTICE.md](../NOTICE.md) and
`LICENSES/`; there is no blanket licence over all upstream components.
