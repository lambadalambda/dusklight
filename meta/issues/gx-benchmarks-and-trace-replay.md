# GX decode benchmarks and real-game trace replay

## Summary

Build automated benchmarks for aurora's GX command processing so renderer
optimizations are justified and validated by data, including replay of
real captured game frames.

## Requirements

- `benchmarks/gx_bench` compiling the real command processor + shader_info
  against arena-backed stubs; synthetic J3D-shaped workloads plus per-draw
  micro-benchmarks.
- GX command stream trace capture in aurora (`AURORA_GX_TRACE`, frame-skip
  and trigger-file modes) and trace replay in the bench (`GX_BENCH_TRACE`).

## Acceptance Criteria

- Benchmarks run via a normal CMake target and produce stable numbers
  (CV < ~5%) on synthetic and real traces.
- Real intro and gameplay traces captured and measured.

## Notes

- Completed 2026-06-11. Baselines: intro 0.25 ms/frame (182 draws),
  gameplay 0.95 ms/frame (~1k draws, ~5 MB/frame staging). Reference
  traces in benchmarks/traces/ (gitignored); methodology in
  benchmarks/README.md.
