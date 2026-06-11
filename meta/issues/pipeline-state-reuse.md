# Reuse pipeline state across uniform-only dirty draws

## Summary

88% of real gameplay draws were state-dirty only because J3D loaded
matrices for the next shape, yet each rebuilt pipeline config, shader
info, texture bindings, bind groups, and pipeline hash. Split stateDirty
into uniform-dirty vs pipelineDirty and cache the previous draw's pipeline
state.

## Acceptance Criteria

- Decode-time reduction on real traces with identical draw/pipeline
  counts; no rendering differences across material and blend switches.

## Notes

- Completed 2026-06-11 (aurora 7e9b6e7). Gameplay trace 231 → 222 ms per
  240 frames in gx_bench (bench understates: bind-group build and texture
  resolution are stubbed there but skipped in the real renderer too).
  Verified headless on the Hyrule Field attract scene.
