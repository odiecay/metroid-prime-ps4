# Performance Benchmarks

Metroid Prime's 60 FPS target gives each new frame about **16.67 milliseconds**.
The interesting part of this port has been finding out where that time goes on
PS4: running the game, preparing its many draws, executing them on the GPU, and
getting the finished image onto the display.

Testing on a PS4 Slim shows why one FPS number cannot describe the whole game.
Tallon Canyon and Research Lab Hydra sit close to the target in the recorded
workload. Main Plaza asks much more of the CPU: its first-person actions run
around 41–43 FPS, while the Morph Ball view is considerably lighter. Phendrana
Shorelines and Main Quarry fall between those extremes.

These are measured results from particular builds and routes, not a prediction
for every room. **This is not a locked 60 FPS port.** The current prepared source
is 00.55 RC1; the latest complete, validated six-room comparison below is
**00.54 RC11**. Its numbers are kept under that identity rather than relabelled
as results for a newer build.

## What was tested

The main dataset was captured on **17 November 2026**, using the V2 movement
workload. It completed six rooms and 24 actions. Each action has roughly
12 seconds of measurement after placement, rehearsal and shader settling.
The whole route took 681.876 seconds, including setup and excluded intervals;
35 intact profiler windows contribute **175.098 seconds and 9,923 game frames**.

| Setting | Recorded configuration |
|---|---|
| Console | PS4 Slim; model recorded by the tester. Exact CUH revision, firmware and clock telemetry were not recorded with this dataset. |
| Build | 00.54 RC11, application `PRME00013`; package identity below |
| Graphics | Original HD; gameplay EFB and output logged at 1920×1080, framebuffer scale 1, dynamic resolution off |
| Input | Scripted virtual GameCube-pad input through the native game path |
| Actions in every room | Run/aim, beam/jump, missile/aim, Morph Ball rolling/bombs |
| Assistance | Inventory assistance and invulnerability enabled |
| Cache | Separate persistent benchmark cache; each measurement waits for shader work to settle |
| Profiling | Frame tracing and sampled Dawn/Vulkan call diagnostics enabled |

The shader gate records no queued, active or pending shader work, no failures,
and at least eight stable frames at measurement start. Rehearsal and this gate
make these **settled-cache workload tests**. They do not measure a fresh empty
cache, and “pass 1” does not mean “cold cache.”

The room names come from matching session IDs, world/area IDs and
`measure_begin`/`measure_end` markers. They were not guessed from the time a
log was downloaded. Loading, warm-up, failed routes and windows crossing action
boundaries are excluded.

## How the rooms compare

These averages use all accepted windows in each room. They describe the sampled
mix of all four actions; they are not a fixed first-person viewpoint. In
particular, Main Plaza's lighter Morph Ball action raises its room average.

| Scene | Average FPS | Game frames | Selected window time (s) | Worst frame (ms) |
|---|---:|---:|---:|---:|
| Tallon Canyon | 59.97 | 1,799 | 30.010 | 18.623 |
| Main Plaza | 46.77 | 1,402 | 30.019 | 26.002 |
| Fiery Shores | 59.98 | 1,500 | 25.008 | 20.738 |
| Phendrana Shorelines | 57.37 | 1,720 | 30.016 | 20.830 |
| Research Lab Hydra | 59.97 | 1,801 | 30.027 | 18.287 |
| Main Quarry | 56.64 | 1,701 | 30.018 | 21.831 |

No combined P50/P95/P99 is supplied for these room totals. The files preserve
each window's frame-time distribution summary and a bounded set of slow-frame
details, rather than every individual frame. A median of window medians or an
average of percentiles would not be a room percentile.

### Every measured window

The table below keeps the real **P50, P95, P99 and worst frame** for each
individual window. All four timing columns are milliseconds; lower is better.
P95 means 95% of the recorded frame times in that window were at or below that
value. Average FPS is completed game frames divided by their summed wall time.

Action **0** is run/aim, **1** beam/jump, **2** missile/aim and **3** Morph Ball
rolling/bombs. The interval is seconds on the capture's monotonic clock, making
each row identifiable in the source JSONL. Windows last about five seconds.
Small readings above 60 reflect finite measurement windows and pacing variation.

<details>
<summary>Open all 35 hardware windows</summary>

| Scene · action | Window (s) | Frames | Average FPS | P50 | P95 | P99 | Worst frame |
|---|---:|---:|---:|---:|---:|---:|---:|
| Tallon Canyon · 0 | 51.079–56.080 | 300 | 59.96 | 16.812 | 17.279 | 17.369 | 17.408 |
| Tallon Canyon · 1 | 76.086–81.088 | 300 | 59.96 | 16.769 | 17.204 | 17.432 | 17.604 |
| Tallon Canyon · 1 | 81.088–86.090 | 300 | 59.96 | 16.724 | 17.300 | 18.567 | 18.623 |
| Tallon Canyon · 2 | 101.094–106.096 | 300 | 59.96 | 16.746 | 17.219 | 17.361 | 17.450 |
| Tallon Canyon · 2 | 106.096–111.097 | 299 | 59.96 | 16.742 | 17.258 | 17.378 | 17.582 |
| Tallon Canyon · 3 | 131.104–136.106 | 300 | 60.02 | 16.973 | 17.023 | 17.055 | 17.582 |
| Main Plaza · 0 | 161.140–166.143 | 214 | 42.91 | 22.881 | 24.489 | 24.636 | 24.645 |
| Main Plaza · 0 | 166.143–171.146 | 215 | 42.96 | 22.805 | 24.476 | 24.638 | 24.709 |
| Main Plaza · 1 | 191.159–196.163 | 205 | 41.16 | 23.619 | 25.682 | 25.862 | 26.002 |
| Main Plaza · 2 | 216.176–221.179 | 206 | 41.17 | 23.704 | 25.703 | 25.868 | 25.986 |
| Main Plaza · 3 | 241.192–246.196 | 282 | 56.33 | 17.663 | 19.351 | 20.145 | 20.467 |
| Main Plaza · 3 | 246.196–251.198 | 280 | 56.08 | 17.715 | 19.259 | 20.396 | 20.627 |
| Fiery Shores · 0 | 276.210–281.211 | 300 | 60.02 | 16.983 | 18.909 | 20.344 | 20.512 |
| Fiery Shores · 1 | 301.218–306.220 | 300 | 60.02 | 16.964 | 17.024 | 17.163 | 20.738 |
| Fiery Shores · 2 | 326.225–331.226 | 300 | 59.96 | 16.821 | 17.251 | 17.516 | 17.559 |
| Fiery Shores · 2 | 331.226–336.227 | 300 | 59.96 | 16.830 | 17.253 | 17.404 | 17.568 |
| Fiery Shores · 3 | 356.238–361.241 | 300 | 59.94 | 16.917 | 19.303 | 20.133 | 20.266 |
| Phendrana Shorelines · 0 | 386.270–391.272 | 292 | 58.47 | 17.131 | 17.950 | 19.258 | 19.904 |
| Phendrana Shorelines · 0 | 391.272–396.273 | 293 | 58.65 | 17.016 | 17.997 | 19.396 | 19.678 |
| Phendrana Shorelines · 1 | 411.280–416.284 | 282 | 56.31 | 17.660 | 19.594 | 20.224 | 20.288 |
| Phendrana Shorelines · 1 | 416.284–421.288 | 277 | 55.46 | 17.955 | 19.960 | 20.563 | 20.830 |
| Phendrana Shorelines · 2 | 441.303–446.307 | 276 | 55.29 | 18.003 | 20.121 | 20.489 | 20.523 |
| Phendrana Shorelines · 3 | 466.315–471.316 | 300 | 60.02 | 16.981 | 17.023 | 17.044 | 17.097 |
| Research Lab Hydra · 0 | 501.374–506.375 | 300 | 60.02 | 16.980 | 17.024 | 17.039 | 17.047 |
| Research Lab Hydra · 1 | 526.381–531.383 | 300 | 59.98 | 16.750 | 17.319 | 17.506 | 18.118 |
| Research Lab Hydra · 1 | 531.383–536.403 | 301 | 59.96 | 16.748 | 17.367 | 17.586 | 18.109 |
| Research Lab Hydra · 2 | 551.443–556.444 | 300 | 59.96 | 16.797 | 17.274 | 17.445 | 18.220 |
| Research Lab Hydra · 2 | 556.444–561.445 | 300 | 59.98 | 16.815 | 17.275 | 17.423 | 18.287 |
| Research Lab Hydra · 3 | 581.449–586.451 | 300 | 59.94 | 16.886 | 17.135 | 17.730 | 17.850 |
| Main Quarry · 0 | 621.510–626.514 | 282 | 56.40 | 17.252 | 19.544 | 20.621 | 20.664 |
| Main Quarry · 0 | 626.514–631.518 | 282 | 56.19 | 17.363 | 19.512 | 20.698 | 20.882 |
| Main Quarry · 1 | 651.534–656.538 | 270 | 53.95 | 18.049 | 21.242 | 21.616 | 21.715 |
| Main Quarry · 2 | 676.554–681.558 | 267 | 53.28 | 18.905 | 21.374 | 21.599 | 21.831 |
| Main Quarry · 3 | 701.569–706.570 | 300 | 60.02 | 16.978 | 17.022 | 17.032 | 17.071 |
| Main Quarry · 3 | 706.570–711.571 | 300 | 59.99 | 16.880 | 17.167 | 17.538 | 17.738 |

</details>

## Why a busy room can miss 60 FPS

A frame can spend more time being prepared than being drawn by the GPU.
The game submits a large amount of original GX rendering work. Aurora decodes
and prepares it, Dawn encodes and translates it, and RADV builds the native GPU
commands. That is real CPU work even though it ultimately produces graphics.

Main Plaza makes the distinction visible. Across its accepted v54 windows, the
GPU's measured pass span averages about **9.1 ms**, while the game frame takes
about **21.4 ms**. The FIFO decoder uses about **17.3 ms of CPU per main frame**,
and the render thread about **18.2 ms per render frame**. Those workers overlap;
their times are not added together to make the frame time.

| Scene | Main CPU | FIFO worker CPU¹ | Render CPU | Dawn encoding wall² | Vulkan translation wall² | GPU pass span³ |
|---|---:|---:|---:|---:|---:|---:|
| Tallon Canyon | 9.316 | 10.634 | 13.284 | 3.923 | 7.775 | 7.403 |
| Main Plaza | 9.909 | 17.340 | 18.207 | 6.463 | 10.036 | 9.115 |
| Fiery Shores | 7.798 | 7.408 | 11.106 | 2.922 | 6.625 | 6.805 |
| Phendrana Shorelines | 10.304 | 11.926 | 14.676 | 4.294 | 8.770 | 7.572 |
| Research Lab Hydra | 7.571 | 7.885 | 11.130 | 3.397 | 6.205 | 6.644 |
| Main Quarry | 9.085 | 11.896 | 14.649 | 4.740 | 8.278 | 7.565 |

All values are milliseconds. Main and render CPU figures use native per-thread
CPU counters, averaged over their own completed frames.

1. FIFO CPU is the worker's accumulated batch CPU time divided by completed
   main frames in the same selected windows. Every recorded batch in these
   windows has a CPU sample. Batch execution can cross a window boundary;
   this is an approximate window-aligned cost, not a per-frame percentile.
2. Encoding and Vulkan translation are inclusive CPU-side wall intervals.
   They include scheduling and any waits inside the scope, and overlap the
   render thread's work. They are **not GPU execution times**.
3. GPU values are arithmetic means of the fresh GPU-query window averages
   sampled by the profiler: five samples for Fiery Shores and six for each
   other room. They measure timestamped render-pass spans, not every GPU
   operation, display latency or a distribution of individual GPU frames.

This points to CPU preparation as a major limit in the demanding recorded
views. It explains the focus on batching, state reuse, fewer geometry copies
and cheaper thread hand-offs. It does not imply that every scene is CPU-bound,
or that a 9 ms GPU span guarantees a 9 ms game frame.

## What changed across builds

The same intended Main Plaza actions give a more useful comparison than mixing
whole-room averages with different window alignments.

| Main Plaza action | v49 FPS | v53 FPS | v54 FPS |
|---|---:|---:|---:|
| Run / aim | 41.58 | 43.03 | 42.97 |
| Beam / jump | 40.05 | 41.38 | 41.25 |
| Missile / aim | 39.94 | 41.32 | 41.21 |
| Morph Ball / bombs | 54.98 | 56.33 | 56.36 |

Here FPS uses the **whole successful action interval**, about 12 seconds:
the marker's observed frame count divided by elapsed time. This differs
slightly from the five-second profiler-window FPS above. Both measure game
throughput; neither counts display refreshes.

The corresponding FIFO worker measurements help explain the change:

| Main Plaza action | v49 CPU ms/main frame | v53 CPU ms/main frame | v54 CPU ms/main frame |
|---|---:|---:|---:|
| Run / aim | 20.186 | 19.390 | 19.488 |
| Beam / jump | 20.611 | 19.785 | 19.838 |
| Missile / aim | 20.671 | 19.835 | 19.863 |
| Morph Ball / bombs | 14.455 | 13.870 | 13.864 |

The v49-to-v53 comparison supports a modest improvement in FIFO preparation and
game throughput after mixed-primitive preparation batching. It does **not**
show a render-thread CPU improvement: for example, the run/aim render CPU
measurement went from 19.413 to 19.958 ms. Additional profiling and other changes
between builds limit how much can be attributed to one optimisation.

The instruction-tuned RADV command work in v54 was exercised successfully,
but the v53-to-v54 comparison is essentially flat. Smaller or more specialised
code is useful only when the workload benefits; these captures do not establish
a sustained FPS gain from that change.

There were also changes that did not earn their place. The v50 Dawn tracker
experiment completed its six-room workload but showed no supported overall
FPS, CPU or memory benefit over v49. Its Main Plaza run/aim interval measured
41.68 versus 41.58 FPS, with higher measured render-thread CPU time. That
experiment was rejected. The result does not establish that the cache caused
every observed difference.

The v55 source then reduced small geometry-copy work, changed the FIFO wake
channel to the native signed-64-bit path, and reduced detailed profiling overhead
during normal play. Those changes have local build and host-test evidence.
There is no equivalent completed hardware comparison here from which to quote
a v55 FPS gain.

These are sequential runs, not a randomised, repeated A/B study. Routes and
intended settings match, but camera motion, scheduling, instrumentation and
cache history can still vary. The v53 recording ends during Fiery Shores
rehearsal after a deliberate quit; only its completed Tallon Canyon and Main
Plaza visits are accepted. It is not counted as a completed six-room test.

### Consistency matters as well as average FPS

A same-package v49 test changed only the presentation prewait setting.
In Fiery Shores, the worst accepted window's P95 fell from **28.792 to
18.636 ms**, and its P99 from **30.348 to 19.926 ms**, with prewait enabled.
Main Plaza's sustained frame rate did not show an established gain.

Those are the worst *individual-window* percentiles in each run, not pooled
room percentiles. Both routes completed, but this was one sequential pair with
different accepted window counts. It supports keeping the pacing change;
it does not establish an input-latency improvement or a universal FPS increase.

## Game frames and the television

A display can refresh while the previously rendered image remains on screen.
Likewise, queueing a present does not prove a new game frame has reached the
panel. The FPS in this document comes from game-loop completion or observed
game frames inside marked actions. It does not use the television's refresh
rate, count repeated scan-outs as new frames, or infer 60 FPS from a 1080p
output mode.

GPU completion, presentation ownership and display retirement are separate
parts of the native backend. [How the port works](TECHNICAL.md) explains that
path. These captures do not measure controller-to-display latency.

## Reading the limits

Main Plaza remains the clearest demanding scene in this controlled dataset.
Its beam and missile windows reach roughly 26 ms at the slow end. Main Quarry
and Phendrana Shorelines also miss the 16.67 ms budget in several actions.
Even the near-60 scenes have individual frames above that budget.

The settled-cache tour is deliberately narrower than playing through the game.
It exercises movement, aiming, firing, jumps where the route allows them,
Morph Ball and bombs, with inventory assistance. It does not establish
unassisted progression, every boss, every room, physical-controller latency,
audio synchronisation or save durability. Normal play and those functional
checks are separate from a renderer benchmark.

A successful compile, a passing host test, a verified PKG, a hardware workload,
a playable session and an accepted release are different results. These tables
establish the recorded hardware workloads. They do not turn a newer unmeasured
build into a benchmarked one or a six-room route into a complete playthrough.

## Method and evidence

The existing [tour analyser](../tools/analyse_benchmark_tour.py) validates paired
markers, successful actions, shader gates and wholly contained profiler windows.
The v53 and v54 source files were hash-checked and reanalysed for this document;
their run results reproduce the retained reports exactly. The v54 selection
has zero dropped trace events, overwritten buckets or unknown stages.

The [frame accumulator](../platform/ps4/frame_metrics.hpp) uses nearest-rank
percentiles from up to 1,024 frame times per window. Every main-frame row used
above is below that limit. FPS is `frames × 1,000,000,000 / sum_ns`; combined
means use sums and frame counts, never an unweighted average of FPS. The
bounded slow-frame list is not a complete raw sample set and is never used to
manufacture pooled percentiles. Frames and asynchronous stages can straddle
poll boundaries, so window wall duration and summed completed-frame duration
need not be identical.

### Scene placement

These are the recorded player/route start positions for the v54 actions, in
engine coordinates. Each action resets to this position. The camera moves
during aiming and follows the player in Morph Ball, so these are **not fixed
camera coordinates**. The marker file contains the action observations and
camera-yaw fields; a continuous camera trajectory is not retained.

| Scene | World / area ID | Start position (x, y, z) | Route yaw |
|---|---|---|---:|
| Tallon Canyon | `39F2DE28` / `2043C96E` | -528.74072, 402.54202, -24.32370 | -180° |
| Main Plaza | `83F6FF6F` / `D5CDB809` | 95.27023, -48.10115, 5.18860 | 0° |
| Fiery Shores | `3EF8237C` / `F5EF1862` | 318.04785, -330.04929, 25.65938 | -45° |
| Phendrana Shorelines | `A8BE6291` / `F7285979` | -37.00319, -96.79867, -4.10391 | -180° |
| Research Lab Hydra | `A8BE6291` / `43E4CC25` | -27.01937, -763.81158, 70.98235 | -90° |
| Main Quarry | `B1AC4D65` / `643D038F` | 191.50763, 83.15202, 14.49074 | 90° |

### Build identities

| Build | Package SHA-256 | Evidence role |
|---|---|---|
| 00.49 | `e07e0b097b3c044f5a38b6ba157324d3998058467c6155f78a86295fd7499ddd` | Same-package prewait comparison and historical Main Plaza baseline |
| 00.53 | `1072b1c05c0ee4f2a79a3095bc3e487af9a4401446adf8e1108f6b5b9addf555` | Completed Main Plaza comparison; partial overall tour |
| 00.54 RC11 | `73b131f5af4c435e6375a81f61a6fb47e59fa09db150769fb9525efb477306f6` | Primary six-room results |
| 00.55 RC1 | `158e571103e8e65d9296aff75815dfc5e7a6f3ab090e4175de73c83ddb2cf013` | Prepared source identity; no comparable completed benchmark quoted |

The v54 package is 83,492,864 bytes. Its frozen integration commit is
`cdb10cf9b7f52abd2fe031bb83b5a2b903bd3eac`; the native-port base is
`c872da704adde9a7838492aaa56dac73a32053e4`. Installed metadata recorded
`APP_VER=00.54` and `TITLE_ID=PRME00013`. Packaging receipts describe host
verification; the separate hardware receipt records the completed workload.

### Capture index

These are relative paths in the original evidence workspace, **not files
bundled in this source checkout**. Raw console logs remain outside Git. The
paths and hashes allow a holder of the captures to reproduce the selection;
they are not public download links.

- Primary report and selection: `evidence/performance/v54-gameplay/report.json`,
  `data-integrity.json`, `main-plaza-actions.json` and `hardware-receipt.json`
  in the same directory.
- v54 captures: `private/hardware/v54-play-comparator-20261117-live/`.
  Session `1794900918200785000-837202630`; run
  `1794900918200785000-837202630-play-837206309`.
- Resolution/settings log in that directory: `0823-boot.log`,
  SHA-256 `ccaaa8ccb0d451249e2ed0bb9bfc4b977f191c725f79c4557d57c00a530bcf41`.
- v53 selection and comparison: `evidence/performance/v53-gameplay/report.json`,
  `data-integrity.json`, `main-plaza-actions.json` and `comparison.md`.
  Captures: `private/hardware/v53-play-comparator-20261117-live/`;
  session `1794897292169535000-851402909`.
- v49 prewait reports: `evidence/performance/v49-prewait-ab/on.json`,
  `off.json`, `comparison.json`. Sessions
  `1794880987194167000-700407867` (on) and
  `1794882222849785000-809068615` (off).
- Rejected tracker experiment: `evidence/performance/v50-gameplay/report.json`,
  `data-integrity.json`, `main-plaza-actions.json` and `comparison.md`.
- Package receipts: `artifacts/original-hd-v0053/receipt.json`,
  `artifacts/release-v0054/RC11-RECEIPT.json`,
  `artifacts/release-v0054/build-receipt.json`,
  `artifacts/original-hd-v0055/receipt.json`.
  Installed-version record: `private/transfer-original-hd-v54/installed-metadata.json`.

The four hashed v54 frame sources include older launches as well. Select only
the session above before analysing them:

| v54 frame source | SHA-256 |
|---|---|
| `0008-frame-profile.jsonl.previous` | `41f44086e9c794f7d87c95a882511cf4e0c187ebe4258cb6e4de3249f0693c6c` |
| `0163-frame-profile.jsonl.previous` | `450b0ebd0c24987fb7a02afc08f8183709f6c53c8a0c8e930fe391524f242c0d` |
| `0454-frame-profile.jsonl.previous` | `e7613e28d41537a0259e8aac8da6d03f038d66dd84c4d961b9e9f4fe921bde93` |
| `0746-frame-profile.jsonl` | `97def1e61550fde7778befa692d785f706c00c535df5e625cd45755254298a90` |
| `0216-benchmark-tour.jsonl` (markers) | `e07961cba310cb6139f930ff819887b5f6ba973c84f639cc6a1ea13f89e2dc70` |

For a single-launch pair of log files, run the existing analyser locally:

```sh
python3 tools/analyse_benchmark_tour.py benchmark-tour.jsonl frame-profile.jsonl --details
```

For the rotated v54 capture, use its existing Python API to avoid mixing
launches or counting repeated snapshots. This reads files only:

```python
from pathlib import Path
import hashlib
import json
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, "tools")
import analyse_benchmark_tour as tour

evidence = Path("/path/to/original-evidence-workspace")
capture = evidence / "private/hardware/v54-play-comparator-20261117-live"
integrity = json.loads(
    (evidence / "evidence/performance/v54-gameplay/data-integrity.json").read_text()
)
frames = []
for source in integrity["sources"]:
    path = capture / source["file"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == source["sha256"]
    rows, errors = tour.load(path, "cpu_frame_window")
    assert not errors
    frames.extend(rows)
path = capture / integrity["marker_source"]
assert hashlib.sha256(path.read_bytes()).hexdigest() == integrity["marker_sha256"]
markers, errors = tour.load(path, "benchmark_tour")
assert not errors
session = integrity["session"]
frames = [row for row in frames if row["session_id"] == session]
markers = [row for row in markers if row["session_id"] == session]
result = tour.analyse(frames, markers)
print(tour.report(result))
```

The analyser used for this documentation has SHA-256
`75faab02480c8e75d283f3b30baec14c3de2d02a356fca885321e51b8486dafd`. Its room report
labels window summaries explicitly; the 35-row table above reads each selected
window's own main-lane `complete frame wall` statistics.


## Final 1.0 configuration

The final package keeps Original HD gameplay, 1.5× menu movies (960×720) and
sharper distance-field lettering. Linear R8 menu-video uploads use 75% fewer
bytes than the previous four-byte plane format; no new native FPS gain is
claimed from that change. The complete hardware benchmark above remains v54.

A later normal New Game replay still recorded an approximately 1.22-second
opening-area script-construction loading pause, mostly waiting/descheduling.
That pause remains a known limitation. The public package starts normally;
the optional one-time screenshot tour is confined to a separate private test
package. A complete playthrough and locked 60 FPS remain unverified.
