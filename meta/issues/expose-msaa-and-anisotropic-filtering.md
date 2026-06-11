# Expose MSAA and anisotropic filtering settings

## Summary

Aurora supports MSAA (`AuroraConfig::msaa`) and anisotropic filtering, but
Dusklight exposes neither. Worse, the game's materials almost universally
request `GX_ANISO_1`, which aurora maps to 1× regardless of the anisotropy
cap — so the game effectively runs without anisotropic filtering, and with
no anti-aliasing (msaa=1).

## Requirements

- Aurora: `forceTextureAnisotropy` config + `aurora_set_force_anisotropy()`
  runtime setter that overrides `GX_ANISO_1` on mip-mapped textures
  (preserving the existing arbitrary-mips and no-mips guards, which exist
  because some effects abuse mip levels).
- Dusklight settings: `video.msaaSamples` (1 = off, 4 = 4× — WebGPU core
  guarantees only sample counts 1 and 4) applied at startup, and
  `video.anisotropicFiltering` (0 = game default, 2/4/8/16) applied live.
- Settings UI entries in the graphics section with help text; MSAA noted
  as taking effect after restart.

## Acceptance Criteria

- Headless A/B screenshots (via `--cvar` overrides) show reduced edge
  aliasing with MSAA 4× and sharper oblique ground textures with forced
  anisotropy, with no rendering artifacts.
- Defaults unchanged (off) so out-of-the-box output stays accurate to the
  original game.

## Notes

- Identified in the 2026-06-11 graphics review; first item of the visual
  improvements track.
