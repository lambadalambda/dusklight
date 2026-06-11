# Move matrix palette out of the per-draw uniform block

## Summary

Every draw carried 1,440 bytes of position/texture/normal matrices in its
uniform block. Store the palette in the storage buffer, uploaded only when
an XF load changes a slot, with the per-draw uniform carrying offsets.

## Acceptance Criteria

- Significant reduction in per-frame uniform staging traffic on real
  traces with no decode-CPU regression and no rendering differences.

## Notes

- Completed 2026-06-11 (aurora 7aa0a71). Gameplay trace: uniform staging
  2276 → 980 KB/frame (−57%), net −1.06 MB/frame upload traffic, decode
  CPU neutral. Verified in-game (windowed visual check + headless
  screenshots of the attract demo).
- Driven by gx_bench instrumentation showing matrices genuinely change for
  ~88% of draws, ruling out simple dirty-skip designs.
