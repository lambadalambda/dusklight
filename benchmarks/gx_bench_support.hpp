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

} // namespace bench
