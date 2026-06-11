# Agent guidelines

## Contribution policy — read first

The upstream projects (`TwilitRealm/dusklight`, `TwilitRealm/aurora`, and
`encounter/aurora`) **do not accept AI-assisted contributions**. Never open
pull requests or issues against them, and do not contact their maintainers
about work done here.

All AI-assisted work stays on our personal forks:

- `lambadalambda/dusklight` — fork of TwilitRealm/dusklight
- `lambadalambda/aurora` — fork of the aurora renderer

Workflow:

- Branch from `main` using date-prefixed names (e.g. `26-06-11-gx-bench`).
- Push branches to the `fork` remote (already configured in this clone and
  in `extern/aurora`), never to `origin`.
- The `extern/aurora` submodule URL points at our fork so branches stay
  self-contained and buildable.
- Pulling upstream improvements *into* our forks is fine; sending our
  changes *to* upstream is not.

## Engineering practices

- **Every performance improvement must come with benchmark evidence.** Use
  `benchmarks/gx_bench` (synthetic workloads + real-game trace replay via
  `GX_BENCH_TRACE`) for renderer CPU work, and capture before/after numbers
  in the commit message. If no existing benchmark covers the change, add one
  first. The reference traces live in `benchmarks/traces/` (gitignored;
  capture new ones with `AURORA_GX_TRACE`, see `benchmarks/README.md`).

## Repo notes

- `benchmarks/` contains `gx_bench`, a CPU benchmark suite for aurora's GX
  command processing, including replay of real-game traces captured with
  `AURORA_GX_TRACE` (see `benchmarks/README.md`).
- `DUSK_DATA_DIR=<dir>` redirects user data/caches — useful for CI and for
  sandboxed environments where the platform data directory is not writable.
