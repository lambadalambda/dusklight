# Note anisotropic filtering subtlety in help text

## Summary

Frame-exact A/B testing (2026-06-12) confirmed forced anisotropic
filtering works (sampler-level verification, byte-differing output) but
is very subtle at high internal resolutions: low-res GC textures are
magnified nearly everywhere on screen, so mip-based minification —
where anisotropy acts — barely engages. Update the setting's help text
so users know what to expect.

## Acceptance Criteria

- Help text states the effect is very subtle at high internal
  resolutions and most visible at low internal resolutions / with HD
  texture packs.

## Notes

- Completed 2026-06-12 alongside the investigation. Comparison
  methodology: AURORA_SCREENSHOT_FRAMES frame-exact captures + flicker
  comparison page.
