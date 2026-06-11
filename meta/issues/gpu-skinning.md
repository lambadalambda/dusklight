# GPU skinning for J3D CPU-deformed models

## Summary

Models flagged `J3DMdlFlag_SkinPosCpu` / `J3DMdlFlag_SkinNrmCpu` deform
every vertex on the CPU each frame (`libs/JSystem/src/J3DGraphAnimator/
J3DSkinDeform.cpp:647`) and re-upload the arrays. A compute pre-pass in
aurora fed with envelope weights would remove the largest remaining
per-frame CPU cost in character-heavy scenes and let skinned geometry stay
GPU-resident (compounds with the static geometry cache).

## Requirements

- Profile first (Tracy is integrated): measure actual J3DSkinDeform time
  in a character-heavy scene to size the win before designing.
- Envelope/multi-weight skinning in a WGSL compute pass writing into a
  storage buffer consumed as the vertex array.
- Preserve bit-exactness expectations where gameplay reads back positions
  (verify nothing reads the deformed arrays on the CPU side).

## Acceptance Criteria

- Measured frame-time improvement in a character-heavy scene (in-game
  numbers, not just microbench).
- Headless screenshot verification of skinned characters (attract demo NPC,
  gameplay capture).

## Notes

- High effort; tabled 2026-06-11 along with the rest of the performance
  track. Do after the static geometry cache.
