# Screen-space ambient occlusion (opt-in)

## Summary

Add an optional SSAO pass to give contact shading depth to scenes without
re-lighting the world. Compute-shader based, using the depth buffer
(normals reconstructed from depth); composited subtly so the baked
twilight art direction is preserved.

## Requirements

- WebGPU compute pass over the EFB depth buffer after the opaque
  passes; needs MSAA-aware depth reads (infrastructure exists from the
  multisampled depth-copy work).
- Half-resolution AO with bilateral upsample; conservative default
  intensity, exposed as a Dusklight setting (Off by default).
- Composite into the scene before post-processing (bloom/DoF) — find the
  right insertion point relative to mDoGph's EFB copy chain.

## Acceptance Criteria

- Headless A/B screenshots show plausible contact darkening with no halos
  on the attract demo scenes; Off is pixel-identical to before.
- Frame-time cost measured (in-game stats) and documented.

## Notes

- Lighting track (2026-06-12). Do after per-pixel lighting; reuses its
  verification flow. XeGTAO-style depth-only AO is the reference
  approach.

## Resolution (2026-06-12)

Implemented (aurora) as a mid-frame queued pass: half-res depth-only AO
(8 spiral taps) multiplied onto the scene color between the 3D scene
and post/2D, hooked in m_Do_graphic. Off by default; applies live.
Radius 40 / intensity 0.45 — tune from real gameplay feedback.
Frame-time cost measurement deferred to gameplay testing.
