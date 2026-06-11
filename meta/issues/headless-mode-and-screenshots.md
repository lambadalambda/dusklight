# Headless mode and trigger-file screenshots

## Summary

Enable fully automated visual verification: run the game without opening
a window and capture PNG screenshots of the rendered frame on demand.

## Acceptance Criteria

- `AURORA_HEADLESS=1` boots and runs the game with no visible window,
  rendering normally offscreen.
- `AURORA_SCREENSHOT_TRIGGER`/`AURORA_SCREENSHOT_DIR` produce correct PNGs
  of the present source on demand, logged on completion.

## Notes

- Completed 2026-06-11 (aurora 93b8b43). Verified by booting headless to
  the title attract demo and reading back captures. Documented in
  benchmarks/README.md. Combine with DUSK_DATA_DIR and
  `--cvar audio.masterVolume=0` for self-contained runs.
