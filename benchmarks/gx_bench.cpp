// Benchmarks for aurora's GX command processing — the CPU cost the game pays
// every frame to re-decode display lists, re-upload vertex data, and rebuild
// per-draw state. Built to answer: "how much would a static display-list
// cache actually save?"
//
// The workload mimics how Twilight Princess drives GX through J3D:
//   - vertex arrays are indexed (GX_INDEX16 pos/nrm/uv) with a direct
//     PNMTXIDX byte, set per-model via GXSetArray
//   - geometry is pre-recorded display lists of triangle strips, replayed
//     with GXCallDisplayList every frame
//   - position/normal matrices are loaded per shape (J3DModel draw matrices)
//   - material state (TEV colors, channel colors, TEV order) changes every
//     few shapes (J3DMatPacket boundaries)
//
// Scenarios:
//   BM_FrameReplay/<shapes>/<strips>  - full frame at various scene scales
//   BM_FrameReplay_TinyDL/<shapes>    - same draw structure, 3-vert DLs;
//                                       (FrameReplay - TinyDL) isolates the
//                                       vertex parse + staging upload cost a
//                                       static DL cache would eliminate
//   BM_StateOnly/<shapes>             - matrix/material decode only, no draws
//   BM_PopulatePipelineConfig         - per-draw ShaderConfig build
//   BM_BuildShaderInfo                - per-draw shader info derivation
//   BM_BuildUniform                   - per-draw uniform block build+push

#include <benchmark/benchmark.h>

#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include "gx/gx.hpp"
#include "gx/fifo.hpp"
#include "gx/command_processor.hpp"
#include "gx/shader_info.hpp"
#include "gx/pipeline.hpp"

#include "gx_bench_support.hpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr int kVertsPerStrip = 24;
constexpr int kNumUniqueDLs = 64;
constexpr int kNumModels = 32;
constexpr int kVertsPerModel = 2048;
constexpr int kShapesPerModel = 10;  // GXSetArray frequency
constexpr int kShapesPerMaterial = 3;

struct ModelArrays {
  std::vector<s16> pos;  // xyz, stride 6
  std::vector<s16> nrm;  // xyz, stride 6
  std::vector<s16> tex;  // st, stride 4
};

struct MtxHolder {
  Mtx m;
};

class Workload {
public:
  Workload(int shapes, int stripsPerDL) : m_shapes(shapes) {
    GXInit(nullptr, 0);
    aurora::gx::fifo::clear_buffer();
    aurora::gx::g_gxState = aurora::gx::GXState{};

    std::mt19937 rng(0x7261636b);
    m_models.resize(kNumModels);
    for (auto& m : m_models) {
      m.pos.resize(kVertsPerModel * 3);
      m.nrm.resize(kVertsPerModel * 3);
      m.tex.resize(kVertsPerModel * 2);
      for (auto& v : m.pos) v = static_cast<s16>(rng());
      for (auto& v : m.nrm) v = static_cast<s16>(rng());
      for (auto& v : m.tex) v = static_cast<s16>(rng());
    }

    setup_static_state();
    record_dls(stripsPerDL);

    // Decode the setup commands once; per-frame state re-issue happens in
    // replay_frame.
    aurora::gx::fifo::drain();
  }

  void replay_frame() {
    set_frame_state();
    int model = -1;
    for (int shape = 0; shape < m_shapes; ++shape) {
      if (shape % kShapesPerModel == 0) {
        model = (model + 1) % kNumModels;
        set_arrays(m_models[model]);
      }
      if (shape % kShapesPerMaterial == 0) {
        set_material(shape);
      }
      load_shape_matrices(shape);
      if (!m_dls.empty()) {
        const auto& dl = m_dls[shape % m_dls.size()];
        GXCallDisplayList(dl.data(), static_cast<u32>(dl.size()));
      }
    }
    m_fifoBytes = aurora::gx::fifo::get_buffer_size();
  }

  void drop_dls() { m_dls.clear(); }
  int shapes() const { return m_shapes; }
  u32 fifo_bytes() const { return m_fifoBytes; }

private:
  void setup_static_state() {
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_PNMTXIDX, GX_DIRECT);
    GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
    GXSetVtxDesc(GX_VA_NRM, GX_INDEX16);
    GXSetVtxDesc(GX_VA_TEX0, GX_INDEX16);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_S16, 14);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 8);

    GXSetCullMode(GX_CULL_BACK);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetFog(GX_FOG_LIN, 100.0f, 5000.0f, 0.1f, 10000.0f, GXColor{160, 180, 200, 255});
  }

  void set_frame_state() {
    GXSetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    GXSetScissor(0, 0, 640, 480);
    Mtx44 proj;
    std::memset(proj, 0, sizeof(proj));
    proj[0][0] = 1.2f;
    proj[1][1] = 1.6f;
    proj[2][2] = -1.0f;
    proj[2][3] = -0.2f;
    proj[3][2] = -1.0f;
    GXSetProjection(proj, GX_PERSPECTIVE);
  }

  void set_arrays(const ModelArrays& m) {
    GXSETARRAY(GX_VA_POS, m.pos.data(), static_cast<u32>(m.pos.size() * sizeof(s16)), 6, false);
    GXSETARRAY(GX_VA_NRM, m.nrm.data(), static_cast<u32>(m.nrm.size() * sizeof(s16)), 6, false);
    GXSETARRAY(GX_VA_TEX0, m.tex.data(), static_cast<u32>(m.tex.size() * sizeof(s16)), 4, false);
  }

  void set_material(int shape) {
    const u8 c = static_cast<u8>(shape);
    // Two TEV stages: raster lighting modulated by a constant register —
    // texture sampling is intentionally absent (texture binding/upload is
    // outside this benchmark's scope).
    GXSetNumTevStages(2);
    GXSetNumChans(1);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);
    GXSetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT0, GX_DF_CLAMP, GX_AF_SPOT);
    GXSetChanMatColor(GX_COLOR0A0, GXColor{c, 200, 255, 255});
    GXSetChanAmbColor(GX_COLOR0A0, GXColor{32, 32, 48, 255});
    GXSetTevColor(GX_TEVREG0, GXColor{255, c, 128, 255});

    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_C0, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_A0, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  }

  void load_shape_matrices(int shape) {
    Mtx& mtx = m_mtxPool[shape % m_mtxPool.size()].m;
    const u32 id = GX_PNMTX0 + (shape % 10) * 3;
    GXLoadPosMtxImm(mtx, id);
    GXLoadNrmMtxImm(mtx, id);
  }

  void record_dls(int stripsPerDL) {
    for (size_t i = 0; i < m_mtxPool.size(); ++i) {
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
          m_mtxPool[i].m[r][c] = (r == c) ? 1.0f : 0.01f * static_cast<float>(i);
        }
      }
    }
    if (stripsPerDL <= 0) {
      return;
    }

    m_dls.resize(kNumUniqueDLs);
    for (int d = 0; d < kNumUniqueDLs; ++d) {
      auto& dl = m_dls[d];
      dl.resize(stripsPerDL * (8 + kVertsPerStrip * 7) + 64);
      GXBeginDisplayList(dl.data(), static_cast<u32>(dl.size()));
      for (int s = 0; s < stripsPerDL; ++s) {
        GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, kVertsPerStrip);
        for (int v = 0; v < kVertsPerStrip; ++v) {
          const u16 idx = static_cast<u16>((d * 131 + s * 31 + v * 7) % kVertsPerModel);
          // VCD order: PNMTXIDX (one raw byte), then pos/nrm/tex indices.
          GXPosition1x8(static_cast<u8>((v % 3) * 3));
          GXPosition1x16(idx);
          GXNormal1x16(idx);
          GXTexCoord1x16(idx);
        }
        GXEnd();
      }
      const u32 size = GXEndDisplayList();
      dl.resize(size);
    }
  }

  int m_shapes;
  u32 m_fifoBytes = 0;
  std::vector<ModelArrays> m_models;
  std::vector<std::vector<u8>> m_dls;
  std::vector<MtxHolder> m_mtxPool{32};
};

void report_frame_counters(benchmark::State& state, const Workload& w) {
  const auto& f = bench::frame();
  const double frameBytes = static_cast<double>(w.fifo_bytes()) + f.verts.size() + f.indices.size() +
                            f.uniforms.size() + f.storage.size();
  state.counters["draws"] = aurora::gfx::g_drawCallCount;
  state.counters["merged"] = aurora::gfx::g_mergedDrawCallCount;
  state.counters["fifo_KB"] = w.fifo_bytes() / 1024.0;
  state.counters["verts_KB"] = f.verts.size() / 1024.0;
  state.counters["index_KB"] = f.indices.size() / 1024.0;
  state.counters["uniform_KB"] = f.uniforms.size() / 1024.0;
  state.counters["storage_KB"] = f.storage.size() / 1024.0;
  state.counters["pipelines"] = static_cast<double>(bench::pipeline_count());
  state.SetBytesProcessed(static_cast<int64_t>(frameBytes) * state.iterations());
}

void BM_FrameReplay(benchmark::State& state) {
  Workload w(static_cast<int>(state.range(0)), static_cast<int>(state.range(1)));
  for (auto _ : state) {
    bench::begin_frame();
    w.replay_frame();
    aurora::gx::fifo::drain();
  }
  report_frame_counters(state, w);
}
BENCHMARK(BM_FrameReplay)
    ->Args({500, 3})    // light scene   (~36k verts)
    ->Args({1500, 5})   // typical scene (~180k verts)
    ->Args({3000, 8})   // heavy scene   (~576k verts)
    ->Unit(benchmark::kMillisecond);

void BM_FrameReplay_TinyDL(benchmark::State& state) {
  // Same per-shape structure but near-empty display lists. The difference
  // against BM_FrameReplay at the same shape count is the vertex
  // parse + staging copy cost — the share a static DL cache would remove.
  Workload w(static_cast<int>(state.range(0)), 1);
  for (auto _ : state) {
    bench::begin_frame();
    w.replay_frame();
    aurora::gx::fifo::drain();
  }
  report_frame_counters(state, w);
}
BENCHMARK(BM_FrameReplay_TinyDL)->Arg(500)->Arg(1500)->Arg(3000)->Unit(benchmark::kMillisecond);

void BM_StateOnly(benchmark::State& state) {
  Workload w(static_cast<int>(state.range(0)), 0);
  for (auto _ : state) {
    bench::begin_frame();
    w.replay_frame();
    aurora::gx::fifo::drain();
  }
  report_frame_counters(state, w);
}
BENCHMARK(BM_StateOnly)->Arg(1500)->Unit(benchmark::kMillisecond);

// --- Per-draw micro-benchmarks (real implementations) ---

// Replays one frame so the decode side of g_gxState holds a realistic
// material/vertex configuration.
struct DecodedStateFixture {
  DecodedStateFixture() : w(16, 2) {
    bench::begin_frame();
    w.replay_frame();
    aurora::gx::fifo::drain();
  }
  Workload w;
};

void BM_PopulatePipelineConfig(benchmark::State& state) {
  DecodedStateFixture fx;
  aurora::gx::PipelineConfig config{};
  for (auto _ : state) {
    aurora::gx::populate_pipeline_config(config, GX_TRIANGLESTRIP, GX_VTXFMT0);
    benchmark::DoNotOptimize(config);
  }
}
BENCHMARK(BM_PopulatePipelineConfig);

void BM_BuildShaderInfo(benchmark::State& state) {
  DecodedStateFixture fx;
  aurora::gx::PipelineConfig config{};
  aurora::gx::populate_pipeline_config(config, GX_TRIANGLESTRIP, GX_VTXFMT0);
  for (auto _ : state) {
    auto info = aurora::gx::build_shader_info(config.shaderConfig);
    benchmark::DoNotOptimize(info);
  }
}
BENCHMARK(BM_BuildShaderInfo);

void BM_BuildUniform(benchmark::State& state) {
  DecodedStateFixture fx;
  aurora::gx::PipelineConfig config{};
  aurora::gx::populate_pipeline_config(config, GX_TRIANGLESTRIP, GX_VTXFMT0);
  const auto info = aurora::gx::build_shader_info(config.shaderConfig);
  aurora::gx::BindGroupRanges ranges{};
  for (auto _ : state) {
    bench::frame().uniforms.clear();
    auto range = aurora::gx::build_uniform(info, 0, ranges);
    benchmark::DoNotOptimize(range);
  }
  state.counters["uniform_bytes"] = static_cast<double>(info.uniformSize);
}
BENCHMARK(BM_BuildUniform);

// --- Real-game trace replay (GX_BENCH_TRACE=<file from AURORA_GX_TRACE>) ---

struct TraceChunk {
  uint32_t offset;
  uint32_t size;
  bool bigEndian;
};
struct Trace {
  std::vector<uint8_t> blob;
  std::vector<std::vector<TraceChunk>> frames;
};
Trace g_trace;

bool load_trace(const char* path) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) {
    fprintf(stderr, "trace: cannot open '%s'\n", path);
    return false;
  }
  fseek(f, 0, SEEK_END);
  const long fileSize = ftell(f);
  fseek(f, 0, SEEK_SET);
  g_trace.blob.resize(static_cast<size_t>(fileSize));
  if (fread(g_trace.blob.data(), 1, g_trace.blob.size(), f) != g_trace.blob.size()) {
    fprintf(stderr, "trace: short read\n");
    fclose(f);
    return false;
  }
  fclose(f);
  if (g_trace.blob.size() < 8 || memcmp(g_trace.blob.data(), "AURGXTR1", 8) != 0) {
    fprintf(stderr, "trace: bad magic\n");
    return false;
  }
  size_t pos = 8;
  std::vector<TraceChunk> frame;
  while (pos < g_trace.blob.size()) {
    const uint8_t type = g_trace.blob[pos++];
    if (type == 1) {
      g_trace.frames.push_back(std::move(frame));
      frame.clear();
      continue;
    }
    if (pos + 5 > g_trace.blob.size()) {
      break;
    }
    const bool be = g_trace.blob[pos++] != 0;
    uint32_t size;
    memcpy(&size, g_trace.blob.data() + pos, sizeof(size));
    pos += sizeof(size);
    if (pos + size > g_trace.blob.size()) {
      break; // truncated chunk; drop the partial frame
    }
    frame.push_back({static_cast<uint32_t>(pos), size, be});
    pos += size;
  }
  fprintf(stderr, "trace: loaded %zu frames (%.1f MiB)\n", g_trace.frames.size(),
          static_cast<double>(g_trace.blob.size()) / (1024.0 * 1024.0));
  return !g_trace.frames.empty();
}

void BM_TraceReplay(benchmark::State& state) {
  aurora::gx::g_gxState = aurora::gx::GXState{};
  bench::set_storage_translation(true);
  bench::uniform_stats() = {};

  const double numFrames = static_cast<double>(g_trace.frames.size());
  double draws = 0, merged = 0, fifoB = 0, vertsB = 0, idxB = 0, uniB = 0, storB = 0;
  for (auto _ : state) {
    for (const auto& chunks : g_trace.frames) {
      bench::begin_frame();
      for (const auto& c : chunks) {
        aurora::gx::fifo::process(g_trace.blob.data() + c.offset, c.size, c.bigEndian);
        fifoB += c.size;
      }
      const auto& f = bench::frame();
      draws += aurora::gfx::g_drawCallCount;
      merged += aurora::gfx::g_mergedDrawCallCount;
      vertsB += static_cast<double>(f.verts.size());
      idxB += static_cast<double>(f.indices.size());
      uniB += static_cast<double>(f.uniforms.size());
      storB += static_cast<double>(f.storage.size());
    }
  }
  bench::set_storage_translation(false);

  const double samples = numFrames * static_cast<double>(state.iterations());
  state.counters["frames"] = numFrames;
  // frames/sec over the whole run; per-frame time = Time column / frames
  state.SetItemsProcessed(static_cast<int64_t>(samples));
  state.counters["draws"] = draws / samples;
  state.counters["merged"] = merged / samples;
  state.counters["fifo_KB"] = fifoB / samples / 1024.0;
  state.counters["verts_KB"] = vertsB / samples / 1024.0;
  state.counters["index_KB"] = idxB / samples / 1024.0;
  state.counters["uniform_KB"] = uniB / samples / 1024.0;
  state.counters["storage_KB"] = storB / samples / 1024.0;
  state.counters["pipelines"] = static_cast<double>(bench::pipeline_count());
  const auto& us = bench::uniform_stats();
  if (us.pushes > 0) {
    state.counters["uni_xform_same_pct"] = 100.0 * static_cast<double>(us.transformUnchanged) / us.pushes;
    state.counters["uni_shade_same_pct"] = 100.0 * static_cast<double>(us.shadingUnchanged) / us.pushes;
    state.counters["uni_irregular_pct"] = 100.0 * static_cast<double>(us.irregular) / us.pushes;
    state.counters["uni_skippable_KB"] = static_cast<double>(us.bytesSkippable) / samples / 1024.0;
  }
  state.SetBytesProcessed(static_cast<int64_t>(fifoB + vertsB + idxB + uniB + storB));
}

} // namespace

int main(int argc, char** argv) {
  if (const char* tracePath = getenv("GX_BENCH_TRACE")) {
    if (!load_trace(tracePath)) {
      return 1;
    }
    benchmark::RegisterBenchmark("BM_TraceReplay", &BM_TraceReplay)->Unit(benchmark::kMillisecond);
  }
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
