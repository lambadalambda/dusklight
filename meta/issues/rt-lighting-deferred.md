# Ray-traced lighting (deferred)

## Summary

Hardware ray tracing is currently unreachable from aurora: WebGPU/Dawn
exposes no acceleration structures or ray queries on any backend.
Deferred until the ecosystem provides a path. This issue records the
2026-06-12 findings so the research isn't repeated.

## Findings

- Dawn (current backend): no RT API, none announced.
- wgpu-native: `EXPERIMENTAL_RAY_QUERY` exists (Vulkan + Metal) but is
  documented as unstable with expected breaking changes. A Dawn→wgpu swap
  would also hit Tint/Naga WGSL differences (e.g. packed u32 arrays in
  uniforms used by the matrix palette), lose dawn_cache.db, and trade a
  Chrome-hardened implementation for experimental features.
- Dolphin VideoCommon (most complete GX implementation): no RT either;
  GPL-2.0+; not a library — adopting it is major surgery for no RT gain.
- Bespoke GX layers (e.g. ACGC-PC-Port's GX→OpenGL 3.3): lower capability
  ceilings than aurora; not a path forward.
- Content concern independent of API: TP's look is baked vertex-color
  ambience + TEV materials + d_kankyo grading; path-traced GI re-lights
  the world and fights the art direction without re-authored materials.

## Revisit when

- Dawn ships a ray-tracing extension, or wgpu's ray-query API stabilizes
  enough for a desktop-only experiment branch.

## Notes

- Interim lighting improvements that don't need RT are tracked in
  per-pixel-lighting.md, ssao.md, screen-space-contact-shadows.md.
