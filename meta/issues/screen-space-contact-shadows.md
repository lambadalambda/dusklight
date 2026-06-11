# Screen-space contact shadows (opt-in)

## Summary

Short-range ray march against the depth buffer along the dominant light
direction, darkening pixels whose path to the light is occluded near the
surface. Complements TP's blob/projected shadows with grounded contact
shadows for characters and props — the closest "ray-traced shadows"
analogue available without a ray-tracing API.

## Requirements

- Compute or fragment pass over the depth buffer; light direction sourced
  from the scene's dominant GX light (d_kankyo sun/moon state).
- Short fixed-step march (e.g. 8-16 steps, screen-space), depth-threshold
  occlusion test, soft falloff.
- Opt-in Dusklight setting, Off by default; intensity conservative.

## Acceptance Criteria

- Headless A/B screenshots: characters/props gain contact grounding, no
  shimmer or streak artifacts in the attract demo; Off is identical.
- Frame-time cost measured and documented.

## Notes

- Lighting track (2026-06-12). Depends on the same depth-read
  infrastructure as SSAO; do after it.
