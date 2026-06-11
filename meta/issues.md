# Open issues

Graphics track:

- [ ] [Expose MSAA and anisotropic filtering settings](issues/expose-msaa-and-anisotropic-filtering.md)

Performance track (tabled 2026-06-11 — fast enough on current desktop
hardware; revisit for Android/high-refresh; baselines and methodology in
benchmarks/README.md, reference traces in benchmarks/traces/):

- [ ] [Static geometry cache for aurora GX decode](issues/static-geometry-cache.md)
- [ ] [Split shading state out of the per-draw uniform block](issues/uniform-shading-region-split.md)
- [ ] [Process GXCallDisplayList without the FIFO copy](issues/gx-calldisplaylist-direct-process.md)
- [ ] [GPU skinning for J3D CPU-deformed models](issues/gpu-skinning.md)
- [ ] [Uber-shader fallback for pipeline compile stutter](issues/ubershader-compile-fallback.md)
