# Building

Cross-compilation currently uses a Linux x86-64 host, Python 3.12+, LLVM/Clang
19.1.7, Ninja, CMake 3.31.10 and the matched native Orbis SDK/Mesa bundle.
`evidence/sdk-inputs.json` records the required input hashes. SDK binaries are
supplied separately; an arbitrary OpenOrbis bundle is not interchangeable.

The RADV command-buffer overlay also requires the pinned Clang 18.1.8 build.
`renderer/orbis/radv-command-generated/manifest.json` records that compiler,
SDK headers, generated headers and clean Mesa Git blobs. Before applying the
PS4 CPU instruction flags, `tools/build_radv_command_overlay.py` must reproduce
the original SDK object's code, relocations and symbols. It writes a private
object and leaves the SDK archive unchanged. `tools/build_game.py` invokes it.

```sh
python3 -m venv .tools
.tools/bin/pip install cmake==3.31.10
export PRIME_SDK_BUNDLE=/path/to/matching/orbis-sdk-v1
export PATH=/usr/lib/llvm-19/bin:$PATH
python3 tools/bootstrap.py
python3 tools/bootstrap.py --verify
python3 tools/build_runtime.py
python3 tools/build_dawn.py
python3 tools/build_game.py
```

The target is native Orbis. The FreeBSD compiler triple does not add a desktop
userspace. Keep the pinned runtime, ABI overlays, linker script and platform
patches together. The shared SDK remains read-only.

Use `python3 tools/package.py --help` for the versioned packaging inputs. The
packager verifies native ELF mappings, constructs the PKG and independently
extracts its executable and metadata. Packaging never installs or launches it.

## Tests and PC tools

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 tools/test_reader_sanitized.py
python3 tools/prepare_data.py /path/to/MetroidPrime.rvz
```

Tests requiring retail data or the native SDK need those inputs. Host passes do
not establish console gameplay or performance.

The standalone Windows helper is built on Linux with Wine and xvfb-run:

```sh
python3 tools/build_game_prep.py --output artifacts/game-prep
```

Its compiler inputs and downloads are pinned in
`tools/game_prep_build_inputs.json`. Player downloads contain the resulting EXE;
players do not install Python. [INSTALLING.md](INSTALLING.md) covers setup.

Detailed Vulkan call timing is off by default. For a profiling session,
`/data/project-prime/release/benchmark-v2.cfg` can contain
`dawn_call_profile=1`; use `0` or remove the file to disable it. The normal
five-second frame summaries remain available without this extra sampling.

The optional developer room tour uses `benchmark-tour.cfg` in
`/data/project-prime/release/`, containing exactly
`PROJECT_PRIME_BENCHMARK_TOUR_V1` followed by a newline. It copies existing
settings into `benchmarks/tour-v1/`, with separate user data, caches and logs.
The tour warps through six rooms with assisted inventory and four stationary
camera directions; it is a rendering workload, not a gameplay completion test.
For the movement workload, use `benchmark-play.cfg` containing exactly
`PROJECT_PRIME_BENCHMARK_PLAY_V2` followed by a newline. Its separate data and
logs are under `benchmarks/play-v2/`; it also enables sampled Vulkan diagnostics.
For assisted 24-room/96-phase coverage, use `benchmark-coverage.cfg` with
`PROJECT_PRIME_BENCHMARK_COVERAGE_V3` followed by a newline. It writes to
`benchmarks/coverage-v3/`. It uses validated multiwaypoint routes, replenishes
assisted inventory per room and stops incomplete if a room or action fails.
V3 owns its scripted controller input and acknowledges settled reset poses
before timing begins.
V2 retains its original six-room comparison workload. This is assisted room
coverage, not a complete playthrough. Remove active request files and relaunch
for normal play.

Collect `benchmark-tour.jsonl` and `frame-profile.jsonl` from that folder's
`logs/` directory, then analyse the matching launch on the PC:

```sh
python3 tools/analyse_benchmark_tour.py benchmark-tour.jsonl frame-profile.jsonl --details
```
