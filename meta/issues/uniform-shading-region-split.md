# Split shading state out of the per-draw uniform block

## Summary

After the matrix palette move, the per-draw uniform block still carries
TEV registers, lights, channel/konst colors, fog, and texcoord scales on
every state-dirty draw. Content instrumentation in gx_bench shows this
"shading region" is byte-identical to the previous draw for ~52% of real
gameplay draws (~335 KB/frame skippable; counter `uni_shade_same_pct`).

## Requirements

- Move the shading members into a second uniform binding with its own
  dynamic offset; re-push only when changed, reuse the previous offset
  otherwise (per-frame staging means the first draw of each frame pushes).
- Requires bind group layout + generated-WGSL changes (split `Uniform`
  struct) and a second entry in the dynamic offsets array in
  `lib/gx/pipeline.cpp` render(). Bump `GXPipelineConfigVersion`.
- Decide skip condition: content-compare the built region (provably
  correct) or extend the dirty-flag classification (cheaper per draw).

## Acceptance Criteria

- gx_bench gameplay trace: uniform_KB drops by roughly the measured
  skippable amount with no decode-time regression.
- Headless screenshot verification of title + gameplay scenes.

## Notes

- Wins are upload bandwidth, not desktop CPU (same as the matrix palette
  change); most valuable on Android targets.
- The conditional members per ShaderConfig make a shared layout tricky:
  reuse across draws is only valid while the shader config is unchanged —
  pipelineDirty already tracks exactly that.
