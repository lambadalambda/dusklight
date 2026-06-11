# Per-pixel GX lighting

## Summary

Aurora evaluates GX lighting in the vertex shader (faithful to the
GameCube, which lights per-vertex). On low-poly geometry this produces
visible banding/tessellation artifacts in dynamic light falloff (lantern,
torches, Midna effects, point lights in caves). Evaluating the identical
lighting equations per fragment removes the artifacts without changing
the art: same lights, same attenuation math, smoother result. Dolphin has
shipped this exact enhancement ("Per-Pixel Lighting") for years.

## Requirements

- Opt-in via a ShaderConfig flag (must be part of the pipeline key; bump
  GXPipelineConfigVersion) driven by an AuroraConfig field + runtime
  setter, exposed as a Dusklight graphics setting (default Off).
- Vertex shader passes view-space position and normal as varyings; the
  fragment shader evaluates the channel lighting (diffuse/attenuation
  functions) that currently runs per vertex.
- Channels consumed by vertex-stage texgen (GX_TG_SRTG) or bump texgens
  must keep their vertex-stage evaluation so texcoord generation is
  unchanged.
- Material/ambient colors sourced from vertex colors (GX_SRC_VTX) are
  passed through as varyings.

## Acceptance Criteria

- Headless screenshots with the setting Off are pixel-identical (or
  near-identical) to before the change.
- With the setting On: no rendering artifacts across the attract demo
  scenes; dynamic light falloff is visibly smoother where applicable.
- gx_bench trace replay shows no decode-CPU regression (shader generation
  changes only affect pipeline build).

## Notes

- Part of the lighting track (2026-06-12). True ray tracing is deferred —
  see issues/rt-lighting-deferred.md.
