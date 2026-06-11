#pragma once

// Shared state between the benchmark workloads (gx_bench.cpp) and the
// arena-backed renderer stubs (gx_bench_stubs.cpp).
//
// The stubs mirror the *CPU-side* behavior of aurora's real renderer frontend
// (lib/gfx/common.cpp): buffer pushes memcpy into growing ByteBuffers exactly
// like the per-frame staging packets, draw commands are recorded into a list
// that supports aurora's draw-merge fast path, and pipeline_ref reproduces the
// steady-state hash + lookup cost. GPU submission, texture upload, and bind
// group creation are not modeled — see README.md for what that means when
// interpreting results.

#include "gfx/common.hpp"
#include "gx/pipeline.hpp"

#include <cstdint>
#include <vector>

namespace bench {

struct FrameArenas {
  aurora::ByteBuffer verts;
  aurora::ByteBuffer indices;
  aurora::ByteBuffer uniforms;
  aurora::ByteBuffer storage;
  std::vector<aurora::gx::DrawData> draws;
};

FrameArenas& frame();

// Mirrors aurora::gfx end-of-frame behavior: clears the staging arenas and the
// recorded command list, resets draw counters, and invalidates the per-frame
// vertex array storage ranges (lib/gfx/common.cpp:1125).
void begin_frame();

// Number of distinct pipeline configs seen since process start.
size_t pipeline_count();

// Trace replay support: GX traces captured from the running game contain
// vertex array base pointers from the game process. When translation is
// enabled, push_storage redirects unknown pointers to per-pointer scratch
// buffers of the same size, so the memcpy cost is identical without
// dereferencing dangling memory.
void set_storage_translation(bool enabled);

// Uniform region-change statistics, measured by content-comparing each
// pushed uniform blob against the previous draw's. Regions follow
// build_uniform's append order (lib/gx/shader_info.cpp:371-486):
//   header    [0, 80)      vtx_start, pnmtx idx, viewports, array offsets
//   transform [80, 1584)   proj + 10 pos + 10 tex + 10 normal matrices
//   shading   [1584, end)  TEV regs, lights, chan/kcolors, fog, tex scales
// (Line/point draws insert 16 bytes after the header and shift the regions;
// they are not detectable from the blob alone, so attribution for them is
// approximate. Blobs whose length differs from the previous draw's are
// counted under `irregular` and excluded from region comparison.)
struct UniformStats {
  uint64_t pushes = 0;
  uint64_t irregular = 0;           // length changed vs previous blob
  uint64_t transformUnchanged = 0;  // transform region identical to previous
  uint64_t shadingUnchanged = 0;    // shading region identical to previous
  uint64_t bytesPushed = 0;
  uint64_t bytesSkippable = 0;  // bytes in unchanged transform/shading regions
};
UniformStats& uniform_stats();

} // namespace bench
