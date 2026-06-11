# CPU benchmarks

## gx_bench

Measures the per-frame CPU cost of aurora's GX command processing — the work
the game pays every frame to re-decode display lists, re-upload vertex/array
data into staging buffers, and rebuild per-draw state (pipeline config, shader
info, uniform blocks).

Motivation: almost all Twilight Princess geometry is static, but the current
architecture re-parses every display list and re-pushes every vertex array
each frame (`extern/aurora/lib/gfx/common.cpp:1125` invalidates all array
storage ranges per frame; `GXCallDisplayList` re-feeds DL bytes through the
command processor on every call). These benchmarks quantify what a persistent
display-list / geometry cache would actually save before we build one.

### Building & running

```sh
cmake --preset macos-default-relwithdebinfo   # or your platform's preset
cmake --build build/macos-default-relwithdebinfo --target gx_bench
./build/macos-default-relwithdebinfo/benchmarks/gx_bench
```

Run with `--benchmark_repetitions=5 --benchmark_report_aggregates_only=true`
for stable numbers.

### What it builds

The real `lib/gx/fifo.cpp`, `lib/gx/command_processor.cpp`,
`lib/gx/shader_info.cpp`, and the GX encoder API are compiled directly (same
approach as aurora's `gx_fifo_tests`). Renderer calls are backed by stubs in
`gx_bench_stubs.cpp` that reproduce CPU-side costs: staging-buffer pushes do
real memcpys with real alignment, the draw-merge fast path works, and pipeline
lookup hashes the full `PipelineConfig` like the real cache.
`populate_pipeline_config`/`comp_type_size`/`comp_cnt_count` are verbatim
copies from `lib/gx/gx.cpp` (which can't be linked without a WebGPU device) —
re-sync them if gx.cpp changes.

### Workload

A synthetic frame shaped like J3D's GX usage in Twilight Princess: indexed
strips (`GX_INDEX16` pos/nrm/uv + direct `PNMTXIDX`) in pre-recorded display
lists, per-shape position/normal matrix loads, `GXSetArray` per model, material
state changes every few shapes, lighting + fog enabled, 2 TEV stages.

### Interpreting results

- `BM_FrameReplay/<shapes>/<strips>` — total decode cost for a frame at light
  (500 shapes), typical (1500), and heavy (3000) scene scales.
- `BM_FrameReplay_TinyDL/<shapes>` — identical draw/material/matrix structure
  but 3-vert display lists. **`FrameReplay − TinyDL` ≈ the vertex parse +
  staging copy cost a static DL cache would eliminate.** The estimate is
  conservative: a real cache would also skip the per-draw pipeline-config /
  shader-info / pipeline-hash work for state-clean draws.
- `BM_StateOnly` — matrix/material decode with no draws at all (the floor that
  no geometry cache can remove).
- `BM_PopulatePipelineConfig` / `BM_BuildShaderInfo` / `BM_BuildUniform` —
  per-draw fixed costs (multiply by `draws` from the frame benchmarks).

Not modeled (all make savings estimates conservative): bind-group descriptor
hashing, sampler cache lookups, texture upload, mutex contention from the
texture-load thread, render-pass encoding on the worker thread.

### Real-game trace replay

To benchmark real frames instead of the synthetic workload, capture a GX
command trace from the running game (requires the trace hooks in
`extern/aurora/lib/gx/fifo.cpp`):

```sh
AURORA_GX_TRACE=/tmp/tp_intro.gxtrace \
AURORA_GX_TRACE_SKIP=900 \
AURORA_GX_TRACE_FRAMES=240 \
./build/<preset>/dusklight "<path to game>.rvz"
# wait for "gx-trace: capture complete" in the log, then quit
```

For interactive captures (e.g. gameplay), use a trigger file instead of a
frame skip — the capture is armed at launch but only starts once the trigger
file exists:

```sh
AURORA_GX_TRACE=/tmp/tp_gameplay.gxtrace \
AURORA_GX_TRACE_TRIGGER=/tmp/start_trace \
AURORA_GX_TRACE_FRAMES=240 \
./build/<preset>/dusklight "<path to game>.rvz"
# play to the scene you want, then from another terminal:
#   touch /tmp/start_trace
# wait for "gx-trace: capture complete", quit, and rm /tmp/start_trace
```

Then replay it through the benchmark:

```sh
GX_BENCH_TRACE=/tmp/tp_intro.gxtrace ./build/<preset>/benchmarks/gx_bench \
  --benchmark_filter=BM_TraceReplay --benchmark_repetitions=5
```

The replay decodes the exact per-frame command stream the game produced
(including real display lists and draw structure). Vertex array base pointers
from the game process are redirected to same-sized scratch buffers, so upload
memcpy costs are preserved without dereferencing game memory. The Time column
is for the whole trace; divide by the `frames` counter for per-frame cost.

### Headless runs & screenshots

For automated verification without opening a window, aurora supports:

```sh
AURORA_HEADLESS=1                       # window never shown, no present,
                                        # frames still render offscreen
AURORA_SCREENSHOT_TRIGGER=/tmp/snap     # `touch /tmp/snap` captures the next
                                        # frame (trigger file is consumed)
AURORA_SCREENSHOT_DIR=/tmp/screens      # PNG output dir (screenshot_<n>.png)
```

Combine with `DUSK_DATA_DIR=<dir>` and `--cvar audio.masterVolume=0` for a
fully self-contained verification run:

```sh
env DUSK_DATA_DIR=/tmp/duskdata AURORA_HEADLESS=1 \
    AURORA_SCREENSHOT_TRIGGER=/tmp/snap AURORA_SCREENSHOT_DIR=/tmp/screens \
    ./Dusklight.app/Contents/MacOS/Dusklight --cvar audio.masterVolume=0 "<game>.rvz" &
# ...wait for the scene you want, then:
touch /tmp/snap   # capture; "screenshot: wrote ..." appears in the log
```

Screenshots also work in windowed (non-headless) runs.
