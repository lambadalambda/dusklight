# Beta content viewer (web gallery for unused actors)

## Summary

The archaeology pass found ~90 actors that exist in code but are never
placed in any map nor spawned dynamically — including a cut boss
prototype (B_go statue boss + B_gos stone-turning Gorons), unused NPCs
(Ganondorf, Zant, two robed Zeldas, Light Spirit NPCs, sewer soldiers)
and unused enemies (E_is moving statue, E_yd shadow Baba). All of their
model archives ship on the retail disc. Build a web-based gallery to
view these models in the browser.

## Requirements

- Offline pipeline (tools/beta_viewer/): extract target .arc files from
  the game dump (libnod-based extractor), parse Yaz0/RARC, convert BMD
  models to glTF 2.0 (geometry, rest-pose skinning baked, GX texture
  formats decoded to PNG).
- Static site: gallery index of the unused actors with notes (actor
  source file, identity, why it's unused), three.js model viewer with
  orbit controls per model.
- No game assets committed to the repo: the site's models/ directory is
  generated locally from the user's own dump and gitignored.

## Acceptance Criteria

- `python3 tools/beta_viewer/build.py <dump.rvz>` produces a servable
  site directory with the headline finds viewable (B_go, B_gos, E_is,
  E_yd, Zant, zelRf, zelRo, Seirei, seiB-D, gnd, DrainSol1/2, Shop0,
  K_cube00/01, Tbox2).
- Models render with correct geometry and textures in a browser
  (verified by serving locally).

## Notes

- Animation (BCK) playback is a stretch goal; first iteration is the
  bind/rest pose.
- Update (2026-06-11): BCK playback implemented — the converter now
  emits a full glTF skeleton (JNT1 hierarchy, DRW1/EVP1 skin weights,
  inverse bind matrices) and bakes each BCK to per-frame TRS animation
  clips (constant channels collapsed). The viewer plays clips via
  AnimationMixer with a per-model clip selector. 63 clips across the
  23 models; clips whose joint count mismatches a model are skipped.
- Origin: archaeology session 2026-06-11 (unused-actor scan of all 384
  stage archives vs the OBJNAME table in d_stage.cpp).
