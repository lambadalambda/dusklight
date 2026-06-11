# Static geometry cache for aurora GX decode

## Summary

Roughly half of aurora's per-frame GX decode cost is re-parsing static
display lists and re-uploading static vertex data that never changes.
Cache decoded display lists and vertex arrays in persistent GPU buffers,
keyed by content hash.

Measured on real traces (gx_bench `BM_TraceReplay`, M-series, 240 frames,
see benchmarks/README.md):

- Gameplay: 0.95 ms/frame decode; ~0.4–0.55 ms is vertex parse + staging
  copy + array re-push of static data (1.56 MB verts + 1.04 MB arrays per
  frame). Intro/title: 0.25 ms/frame, ~70% eliminable.
- Cost is paid per *rendered* frame: at 144 fps interpolated this is ~14%
  of a core; on Android-class CPUs estimated 3–5 ms/frame.

## Requirements

- Content-addressed caching (xxh3 of DL bytes / array contents + size):
  correctness must not depend on game-side invalidation calls. CPU-skinned
  arrays and dynamic DLs change content per frame and must miss naturally,
  falling back to today's streaming path.
- Only pure-geometry DLs (draw commands only, no state commands) are
  cacheable; detect during first decode. Cache key must include the active
  VCD/VAT state that affects vertex layout.
- Persistent GPU arenas for cached vertex payloads, index buffers, and
  arrays. First use streams through the per-frame staging path as today and
  schedules a staging→arena copy in the end-of-frame encoder (avoids
  ordering hazards); subsequent frames draw from the arena.
- Static bind group needs cached/streamed variants for vbuf × abuf
  (4 combinations) selected per draw, since a skinned shape has static DL
  indices but dynamic array contents.
- Cached entry stores vert/idx ranges + draw parameters; uniforms are still
  rebuilt per draw (matrices change per shape). Combine with pipelineDirty
  reuse (already landed) for the per-draw fixed costs.
- LRU eviction by last-used frame; on arena exhaustion flush and rebuild.

## Acceptance Criteria

- gx_bench `BM_TraceReplay` on `benchmarks/traces/tp_gameplay.gxtrace`
  shows a significant decode-time reduction (target ≥30%) with identical
  draw counts.
- Headless screenshot verification: title attract scenes AND a gameplay
  capture with skinned characters and JPA particles render identically.
- Memory bounded: arena size capped, eviction verified by scene changes.

## Notes

- Hashing the full DL stream costs ~0.1–0.2 ms/frame at 2.3 MB/frame —
  cheap insurance vs pointer-keying, which breaks on JKRHeap reuse after
  scene unloads.
- The PC-only DL merge optimizer in `libs/JSystem/src/J3DGraphBase/
  J3DShapeDraw.cpp` calls itself a stop-gap pending exactly this.
