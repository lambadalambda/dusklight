# DUSK_DATA_DIR environment override for data paths

## Summary

Allow placing user data and caches in a fixed directory via environment
variable, bypassing platform preference-path resolution — needed for CI
and sandboxed environments where the platform data directory is not
writable.

## Acceptance Criteria

- `DUSK_DATA_DIR=<dir>` makes the game place settings, saves, logs, and
  caches under that directory and boot successfully in an environment
  where `~/Library/Application Support` is not writable.

## Notes

- Completed 2026-06-11 (dusklight f39b00fcfa, src/dusk/data.cpp).
