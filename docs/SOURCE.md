# Finding your way around the source

Project Prime brings an existing native Metroid Prime port to PS4. This
repository holds the platform backend, build tools, tests and integration
patches that make that adaptation reproducible. The original game and Aurora
engine remain part of the build.

For an explanation of the running game, start with [How it works](TECHNICAL.md).
For commands, see [Building](BUILDING.md). The engine and dependencies are
fetched by `tools/bootstrap.py` at the revisions in
[`upstream.lock.json`](../upstream.lock.json); their complete trees are not
duplicated here.

## Upstream foundation

**PrimeDecomp** supplied the original game decompilation. **Odrannnn and the
MetroidPrimePort contributors** built the native-port foundation used here.
The upstream README separately credits Aurora, MusyX, nod and the many other
projects behind that work. Those acknowledgements and licence boundaries are
preserved in [the upstream notices](../LICENSES/port-notice.txt).

The canonical project identities are
`PrimeDecomp/prime` and `Odrannnn/MetroidPrimePort`.
Those repository URLs were unavailable when checked for this documentation.
The pinned native-port revision
`c872da704adde9a7838492aaa56dac73a32053e4` is available through the
[preserved public fork](https://github.com/timehikari/metroidprimeport).
Using that source location does not change the original authorship.

Aurora provides the source-level GX and platform layer. Dawn implements WebGPU,
including shader translation and the Vulkan backend. Mesa RADV and the
orbis-ports adaptation supply the native Vulkan driver. Project Prime's patches
connect and adapt these components to the PS4 game; they are not claims of
authorship over the upstream projects.

## Repository map

| Path | Contents |
|---|---|
| `platform/ps4/` | Native startup, controller, audio, storage, diagnostics and C++ runtime integration |
| `renderer/orbis/` | GPU ownership, allocation and presentation support |
| `game/CMakeLists.txt` | Upstream game build integration |
| `patches/` | Changes to the pinned game, Aurora, Dawn and Mesa sources |
| `tools/` | Build, data preparation, packaging and profiling tools |
| `tests/` | Renderer, runtime, storage and ownership regressions |
| `docs/` | Player setup, controls, architecture, measurements and build guidance |
| `LICENSES/` | Preserved upstream licences and acknowledgements |

Platform changes belong in `platform/ps4/` and `renderer/orbis/` where possible.
Changes that must reach the game, Aurora or another upstream component are
recorded as patches against the pinned revision. Bootstrap verification checks
that the reconstructed source still matches those inputs.

## Which build this source describes

Current source matches the frozen native 1.0 package (`MPRM00001`, Native99) inputs, with portable
build-path adaptations recorded separately. Development commits remain in the repository history. The game source is pinned to `c872da704adde9a7838492aaa56dac73a32053e4`.
[SOURCE-RECEIPT.json](../SOURCE-RECEIPT.json) identifies the frozen game-package
inputs and separately records PC helper and portable build-path adaptations.
The documentation may explain earlier hardware captures or be revised after
the package was frozen. The receipt identifies the executable's frozen inputs;
later prose revisions do not change that runtime identity.

The latest complete six-room hardware comparison documented here is v54 RC11.
The final 1.0 package has a different identity and retains that historical
comparison without claiming a new measured FPS gain. See [Performance Benchmarks](PERFORMANCE.md)
for the exact relationship between source, package and measured results.

## Licences and private inputs

Game images, keys, saves, SDK binaries and generated console packages are
excluded from Git.
Per-file licences and notices are retained in [NOTICE.md](../NOTICE.md) and
`LICENSES/`; there is no blanket licence over all upstream components.
