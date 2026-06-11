# internalResolutionScale=1 renders white in headless captures

## Summary

While A/B-testing anisotropic filtering: with
`--cvar game.internalResolutionScale=1`, frame-exact screenshots of the
present source are solid white (RGB white, noisy alpha) at 608x448 for
every captured frame across the attract demo, while the identical run at
auto resolution captures normal scenes. Either 1x rendering is broken
(would affect the Classic preset) or the screenshot path reads the wrong
texture when the EFB is at native GC size.

## Requirements

- Reproduce windowed (is the presented image also white, or only the
  captured present_source?).
- If capture-only: identify where the final 1x image lives relative to
  webgpu::present_source() and fix the screenshot source.

## Acceptance Criteria

- Frame-exact captures at internalResolutionScale=1 show the scene; the
  windowed Classic preset is confirmed visually correct.

## Notes

- Found 2026-06-12 during the anisotropic filtering investigation. The
  sampler-level aniso verification was unaffected (done at auto res).
