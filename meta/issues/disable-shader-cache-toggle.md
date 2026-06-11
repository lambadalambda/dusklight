# Debug toggle to disable the shader cache

## Summary

A settings-menu debug option that disables the persistent shader/pipeline
cache entirely (nothing loaded, nothing written), so every shader compiles
fresh each run — for exercising the ubershader fallback and debugging
shader compilation behavior.

## Acceptance Criteria

- With the toggle on: the uber shader module compiles every run (pipelines
  cold), the pipeline_cache.db file is not modified (mtime unchanged), and
  scenes render completely via the fallback.
- Off (default): behavior unchanged.

## Notes

- Completed 2026-06-12 (aurora b1ba899 + dusklight settings/UI).
  "Disable Shader Cache (Debug)" in Settings -> Graphics -> Rendering;
  applies at startup. Verified headless with mtime comparison and cold-
  pipeline screenshots.
