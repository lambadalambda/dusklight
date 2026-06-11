# Extend ubershader envelope (projected lighting, emboss, indirect)

## Summary

Field testing with the shader cache disabled (2026-06-12, cold cache on
Linux/Vulkan): the ubershader covers everything except "advanced
lighting" materials, which still pop in while their specialized
pipelines compile. These map to the v1 envelope exclusions — most
likely post-transform texture matrices (projected light/shadow
textures) and emboss bump texgen; indirect texturing (heat shimmer)
is also excluded.

## Requirements

- Post-transform matrices: add the PT palette (or PT slots in the
  existing palette scheme) to the uber uniform and apply in the texgen
  interpreter (~960 B uniform + small WGSL).
- Emboss bump texgen (GX_TG_BUMP0-7): needs tangent/binormal attributes
  and the light-based offset; gate on attribute availability.
- Indirect texturing: largest piece (ind stage configs, scale, matrix,
  wrap); assess whether the affected materials are common enough to
  justify it.

## Acceptance Criteria

- With Disable Shader Cache on, the previously late-popping lighting
  materials render via the fallback from the first frame, visually
  matching their specialized output (headless screenshot comparison).

## Notes

- Follow-up to issues/ubershader-compile-fallback.md (v1 envelope
  documented there). Priority: PT matrices first — likely the bulk of
  what was observed.

Update (2026-06-12): tabled — the v1 envelope already eliminates the
distracting pop-in; remaining late materials are acceptable.
