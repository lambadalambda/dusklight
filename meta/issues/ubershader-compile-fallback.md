# Uber-shader fallback for pipeline compile stutter

## Summary

A pipeline config never seen before (fresh install, new scene, new
effects) either stalls or draws late while the specialized WGSL pipeline
compiles, despite the async dual-queue compiler and the seeded
dawn_cache.db. A generic TEV-interpreter uber-shader (Dolphin's approach)
could render such draws immediately while the real pipeline compiles in
the background.

## Requirements

- One (or few) uber-pipelines that read the full TEV/TCG configuration
  from a uniform/storage block and interpret it per fragment.
- Draw path: on pipeline cache miss, render with the uber-pipeline and
  queue the specialized compile; swap when ready.
- Most valuable on Android/iOS where per-driver caches can't be shipped.

## Acceptance Criteria

- First-encounter scene transitions show no hitch attributable to pipeline
  compilation (measure with the frame stats / Tracy).
- Rendering via the uber-path is visually correct for the configs it
  covers (screenshot comparison against specialized output).

## Notes

- Large effort; tabled 2026-06-11 with the performance track.

## Resolution (2026-06-12)

Implemented in aurora 244d585 as a static WGSL interpreter driven by a
fixed-layout uniform (re-encoded ShaderConfig words + full GX state).
Uber pipelines are keyed by render state only and persist in the
pipeline cache. Verified cold-cache: attract scenes render complete
during the compile storm with no validation errors.

v1 envelope exclusions (draws keep skip-until-compiled): indirect
texturing, post-transform texture matrices, emboss/SRTG texgen,
alpha-bump channels, lines/points.
