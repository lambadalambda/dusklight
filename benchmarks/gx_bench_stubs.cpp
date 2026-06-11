// Renderer stubs for the GX command-processing benchmark.
//
// Derived from extern/aurora/tests/gx_test_stubs.cpp, with one key difference:
// the test stubs no-op everything, while these stubs faithfully reproduce the
// CPU-side cost of aurora's renderer frontend so the benchmark measures what
// the game actually pays per frame:
//
//   - push_verts/push_indices/push_uniform/push_storage memcpy into growing
//     ByteBuffer arenas with the same alignment behavior as the real
//     per-frame staging packets (lib/gfx/common.cpp:1455-1498).
//   - push_draw_command/get_last_draw_command record into a command list so
//     the command processor's draw-merge fast path works exactly as in
//     production (lib/gx/command_processor.cpp:1557).
//   - pipeline_ref reproduces the steady-state cost of the real pipeline
//     cache: xxh3 over the full PipelineConfig, last-ref short-circuit, and a
//     locked flat_hash_map lookup (lib/gfx/pipeline_cache.cpp:387-405).
//   - populate_pipeline_config / comp_type_size / comp_cnt_count are verbatim
//     copies of the real implementations in lib/gx/gx.cpp (which we cannot
//     link because the rest of that file requires a live WebGPU device).
//     If gx.cpp changes, re-sync these.
//
// build_shader_info and build_uniform are NOT stubbed — the real
// lib/gx/shader_info.cpp is compiled into the benchmark.
//
// Not modeled (all underestimate per-draw cost, so savings estimates derived
// from this benchmark are conservative): bind group descriptor hashing,
// sampler cache lookups, texture upload/conversion, render pass encoding.

#include "gx_bench_support.hpp"

#include "gx/gx.hpp"
#include "gfx/clear.hpp"
#include "gfx/common.hpp"
#include "gfx/depth_peek.hpp"
#include "gfx/tex_copy_conv.hpp"
#include "gfx/tex_palette_conv.hpp"
#include "gfx/texture.hpp"
#include "gx/gx_fmt.hpp"
#include "gx/pipeline.hpp"
#include "gx/shader_info.hpp"
#include "internal.hpp"
#include "webgpu/gpu.hpp"

#include <absl/container/flat_hash_map.h>

#include <bit>
#include <cstdio>
#include <limits>
#include <mutex>
#include <utility>

#include <fmt/format.h>

// --- aurora::g_config ---
namespace aurora {
AuroraConfig g_config{};
} // namespace aurora

// --- aurora::log_internal ---
namespace aurora {
void log_internal(AuroraLogLevel level, const char* module, const char* message, unsigned int len) noexcept {
  fprintf(stderr, "[%d] %s: %.*s\n", static_cast<int>(level), module, len, message);
}
} // namespace aurora

// --- fmt::formatter<AuroraLogLevel> ---
auto fmt::formatter<AuroraLogLevel>::format(AuroraLogLevel level, format_context& ctx) const
    -> format_context::iterator {
  return fmt::format_to(ctx.out(), "{}", static_cast<int>(level));
}

// --- GPU buffers (default-constructed, not used) ---
namespace aurora::gfx {
AuroraStats g_stats;
wgpu::Buffer g_vertexBuffer;
wgpu::Buffer g_uniformBuffer;
wgpu::Buffer g_indexBuffer;
wgpu::Buffer g_storageBuffer;
uint32_t g_drawCallCount = 0;
uint32_t g_mergedDrawCallCount = 0;
} // namespace aurora::gfx

namespace aurora::webgpu {
GraphicsConfig g_graphicsConfig{};
} // namespace aurora::webgpu

// --- GXState ---
namespace aurora::gx {
GXState g_gxState{};
} // namespace aurora::gx

namespace aurora::vi {
Vec2<uint32_t> configured_fb_size() noexcept { return {640, 480}; }
void configure(const GXRenderModeObj*) noexcept {}
} // namespace aurora::vi

namespace aurora::gx {
static Module Log("gx_bench");

const gfx::TextureBind& get_texture(GXTexMapID id) noexcept { return g_gxState.textures[id]; }
void evict_texture_object(u32 texObjId) noexcept {
  for (auto& obj : g_gxState.loadedTextures) {
    if (obj.texObjId == texObjId) {
      obj.set_no_cache(true);
    }
  }
}
void evict_tlut_object(u32 tlutObjId) noexcept {
  for (auto& obj : g_gxState.loadedTluts) {
    if (obj.tlutObjId == tlutObjId) {
      obj.set_no_cache(true);
    }
  }
}
void evict_copy_texture(const void* dest) noexcept {
  g_gxState.copyTextures.erase(dest);
  for (auto it = g_gxState.copyTextureCache.begin(); it != g_gxState.copyTextureCache.end();) {
    if (it->first.dest == dest) {
      g_gxState.copyTextureCache.erase(it++);
    } else {
      ++it;
    }
  }
}
void shutdown() noexcept {}
Vec2<uint32_t> logical_fb_size() noexcept { return {640, 480}; }
gfx::Viewport map_logical_viewport(const gfx::Viewport& logicalViewport) noexcept { return logicalViewport; }
gfx::ClipRect map_logical_scissor(const gfx::ClipRect& logicalScissor) noexcept { return logicalScissor; }
void set_logical_viewport(const gfx::Viewport& viewport) noexcept {
  g_gxState.logicalViewport = viewport;
  set_render_viewport(map_logical_viewport(viewport));
}
void set_render_viewport(const gfx::Viewport& viewport) noexcept { g_gxState.renderViewport = viewport; }
void set_logical_scissor(const gfx::ClipRect& scissor) noexcept {
  g_gxState.logicalScissor = scissor;
  set_render_scissor(map_logical_scissor(scissor));
}
void set_render_scissor(const gfx::ClipRect& scissor) noexcept { g_gxState.renderScissor = scissor; }

void resolve_sampled_textures(const ShaderInfo& info) noexcept {}
GXBindGroups build_bind_groups(const ShaderInfo& info) noexcept { return {}; }

// Copy of lib/gx/shader.cpp:91 (shader.cpp requires the WGSL generator's deps).
u8 color_channel(GXChannelID id) noexcept {
  switch (id) {
  case GX_COLOR0:
  case GX_ALPHA0:
  case GX_COLOR0A0:
    return 0;
  case GX_COLOR1:
  case GX_ALPHA1:
  case GX_COLOR1A1:
    return 1;
  default:
    Log.fatal("unimplemented color channel {}", static_cast<int>(id));
  }
}

// --- Verbatim copies from lib/gx/gx.cpp (see file header) ---

u8 comp_type_size(GXAttr attr, GXCompType type) noexcept {
  switch (attr) {
  case GX_VA_PNMTXIDX:
  case GX_VA_TEX0MTXIDX:
  case GX_VA_TEX1MTXIDX:
  case GX_VA_TEX2MTXIDX:
  case GX_VA_TEX3MTXIDX:
  case GX_VA_TEX4MTXIDX:
  case GX_VA_TEX5MTXIDX:
  case GX_VA_TEX6MTXIDX:
  case GX_VA_TEX7MTXIDX:
    return 1;
  case GX_VA_CLR0:
  case GX_VA_CLR1:
    switch (type) {
    case GX_RGB565:
    case GX_RGBA4:
      return 2;
    case GX_RGB8:
    case GX_RGBA6:
      return 3;
    case GX_RGBX8:
    case GX_RGBA8:
      return 4;
    }
  default:
    switch (type) {
    case GX_U8:
    case GX_S8:
      return 1;
    case GX_U16:
    case GX_S16:
      return 2;
    case GX_F32:
      return 4;
    default:
      Log.fatal("comp_type_size: Unsupported component type {}", type);
    }
  }
}

u8 comp_cnt_count(GXAttr attr, GXCompCnt cnt) noexcept {
  switch (attr) {
  case GX_VA_PNMTXIDX:
  case GX_VA_TEX0MTXIDX:
  case GX_VA_TEX1MTXIDX:
  case GX_VA_TEX2MTXIDX:
  case GX_VA_TEX3MTXIDX:
  case GX_VA_TEX4MTXIDX:
  case GX_VA_TEX5MTXIDX:
  case GX_VA_TEX6MTXIDX:
  case GX_VA_TEX7MTXIDX:
    return 1;
  case GX_VA_POS:
    switch (cnt) {
    case GX_POS_XY:
      return 2;
    case GX_POS_XYZ:
      return 3;
    default:
      break;
    }
    break;
  case GX_VA_NRM:
    switch (cnt) {
    case GX_NRM_XYZ:
      return 3;
    case GX_NRM_NBT:
    case GX_NRM_NBT3:
      return 9;
    default:
      break;
    }
    break;
  case GX_VA_CLR0:
  case GX_VA_CLR1:
    return 1;
  case GX_VA_TEX0:
  case GX_VA_TEX1:
  case GX_VA_TEX2:
  case GX_VA_TEX3:
  case GX_VA_TEX4:
  case GX_VA_TEX5:
  case GX_VA_TEX6:
  case GX_VA_TEX7:
    switch (cnt) {
    case GX_TEX_S:
      return 1;
    case GX_TEX_ST:
      return 2;
    default:
      break;
    }
    break;
  default:
    break;
  }
  Log.fatal("comp_cnt_count: Unsupported attr/cnt {} {}", attr, cnt);
}

static std::pair<f32, f32> polygon_offset_for_cull_mode(GXCullMode cullMode) noexcept {
  if (cullMode == GX_CULL_FRONT) {
    return {g_gxState.backOffset, g_gxState.backScale};
  }
  return {g_gxState.frontOffset, g_gxState.frontScale};
}

void populate_pipeline_config(PipelineConfig& config, GXPrimitive primitive, GXVtxFmt fmt) noexcept {
  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  config.shaderConfig.fogType = g_gxState.fog.type;
  u8 vtxOffset = 0;
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
    const auto attr = static_cast<GXAttr>(i);
    const auto type = g_gxState.vtxDesc[i];
    auto& mapping = config.shaderConfig.attrs[i];
    if (type == GX_NONE) {
      mapping = {};
      continue;
    }
    const auto& attrFmt = vtxFmt.attrs[i];
    const auto cnt = comp_cnt_count(attr, attrFmt.cnt);
    const bool nbt3 = attr == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3;
    mapping = AttrConfig{
        .attrType = static_cast<u8>(type),
        .cnt = cnt,
        .compType = static_cast<u8>(attrFmt.type),
        .offset = vtxOffset,
        .stride = 0,
        .frac = attrFmt.frac,
        .le = false,
        .nbt3 = nbt3,
    };
    switch (type) {
    case GX_DIRECT: {
      vtxOffset += comp_type_size(attr, attrFmt.type) * cnt;
      break;
    }
    case GX_INDEX8:
      mapping.stride = g_gxState.arrays[i].stride;
      mapping.le = g_gxState.arrays[i].le;
      vtxOffset += nbt3 ? 3 : 1;
      break;
    case GX_INDEX16:
      mapping.stride = g_gxState.arrays[i].stride;
      mapping.le = g_gxState.arrays[i].le;
      vtxOffset += nbt3 ? 6 : 2;
      break;
    default:
      Log.fatal("populate_pipeline_config: Invalid vertex type {}", type);
    }
  }
  config.shaderConfig.vtxStride = vtxOffset;
  if (primitive == GX_LINES) {
    config.shaderConfig.lineMode = 1;
  } else if (primitive == GX_LINESTRIP) {
    config.shaderConfig.lineMode = 2;
  } else if (primitive == GX_POINTS) {
    config.shaderConfig.lineMode = 3;
  } else {
    config.shaderConfig.lineMode = 0;
  }
  config.shaderConfig.tevSwapTable = g_gxState.tevSwapTable;
  for (u8 i = 0; i < g_gxState.numTevStages; ++i) {
    config.shaderConfig.tevStages[i] = g_gxState.tevStages[i];
  }
  config.shaderConfig.tevStageCount = g_gxState.numTevStages;
  for (u8 i = 0; i < g_gxState.numIndStages; ++i) {
    config.shaderConfig.indStages[i] = g_gxState.indStages[i];
  }
  config.shaderConfig.numIndStages = g_gxState.numIndStages;
  for (u8 i = 0; i < MaxColorChannels; ++i) {
    const auto& cc = g_gxState.colorChannelConfig[i];
    if (cc.lightingEnabled) {
      config.shaderConfig.colorChannels[i] = cc;
    } else {
      // Only matSrc matters when lighting disabled
      config.shaderConfig.colorChannels[i] = {
          .matSrc = cc.matSrc,
      };
    }
  }
  for (u8 i = 0; i < g_gxState.numTexGens; ++i) {
    config.shaderConfig.tcgs[i] = g_gxState.tcgs[i];
  }
  if (g_gxState.alphaCompare) {
    config.shaderConfig.alphaCompare = g_gxState.alphaCompare;
  }
  const auto cullMode = config.shaderConfig.lineMode == 0 ? g_gxState.cullMode : GX_CULL_NONE;
  const auto [polygonOffset, polygonOffsetScale] = polygon_offset_for_cull_mode(cullMode);
  config = {
      .msaaSamples = gfx::get_sample_count(),
      .shaderConfig = config.shaderConfig,
      .depthFunc = g_gxState.depthFunc,
      .cullMode = cullMode,
      .blendMode = g_gxState.blendMode,
      .blendFacSrc = g_gxState.blendFacSrc,
      .blendFacDst = g_gxState.blendFacDst,
      .blendOp = g_gxState.blendOp,
      .dstAlpha = g_gxState.dstAlpha,
      .polygonOffsetBits = std::bit_cast<uint32_t>(polygonOffset),
      .polygonOffsetScaleBits = std::bit_cast<uint32_t>(polygonOffsetScale),
      .polygonOffsetClampBits = std::bit_cast<uint32_t>(g_gxState.clamp),
      .depthCompare = g_gxState.depthCompare,
      .depthUpdate = g_gxState.depthUpdate,
      .alphaUpdate = g_gxState.alphaUpdate,
      .colorUpdate = g_gxState.colorUpdate,
  };
}
} // namespace aurora::gx

// --- Arena-backed buffer pushes (mirror lib/gfx/common.cpp:1455-1498) ---
namespace bench {
static FrameArenas s_frame;
static bool s_translateStorage = false;
static absl::flat_hash_map<const void*, std::vector<uint8_t>> s_storageScratch;

FrameArenas& frame() { return s_frame; }

void set_storage_translation(bool enabled) { s_translateStorage = enabled; }

static const uint8_t* translate_storage(const void* ptr, size_t length) {
  auto& buf = s_storageScratch[ptr];
  if (buf.size() < length) {
    buf.resize(length);
  }
  return buf.data();
}

static UniformStats s_uniformStats;
static std::vector<uint8_t> s_prevUniform;

UniformStats& uniform_stats() { return s_uniformStats; }

// Region boundaries per build_uniform's append order (see gx_bench_support.hpp).
// With the matrix palette in storage, the transform region is proj + 32
// palette offset words.
constexpr size_t kUniformHeaderEnd = 80;
constexpr size_t kUniformTransformEnd = 80 + 64 + 32 * 4;

void record_uniform_push(const uint8_t* data, size_t length) {
  auto& st = s_uniformStats;
  ++st.pushes;
  st.bytesPushed += length;
  const bool comparable = length == s_prevUniform.size() && length >= kUniformTransformEnd;
  if (comparable) {
    if (memcmp(data + kUniformHeaderEnd, s_prevUniform.data() + kUniformHeaderEnd,
               kUniformTransformEnd - kUniformHeaderEnd) == 0) {
      ++st.transformUnchanged;
      st.bytesSkippable += kUniformTransformEnd - kUniformHeaderEnd;
    }
    if (memcmp(data + kUniformTransformEnd, s_prevUniform.data() + kUniformTransformEnd,
               length - kUniformTransformEnd) == 0) {
      ++st.shadingUnchanged;
      st.bytesSkippable += length - kUniformTransformEnd;
    }
  } else {
    ++st.irregular;
  }
  s_prevUniform.assign(data, data + length);
}

void begin_frame() {
  s_frame.verts.clear();
  s_frame.indices.clear();
  s_frame.uniforms.clear();
  s_frame.storage.clear();
  s_frame.draws.clear();
  aurora::gfx::g_drawCallCount = 0;
  aurora::gfx::g_mergedDrawCallCount = 0;
  // Mirrors lib/gfx/common.cpp end-of-frame — storage ranges are invalidated
  // each frame.
  for (auto& array : aurora::gx::g_gxState.arrays) {
    array.cachedRange = {};
  }
  aurora::gx::g_gxState.mtxDirtyMask =
      (1u << (aurora::gx::MaxPnMtx + aurora::gx::MaxTexMtx + aurora::gx::MaxPnMtx)) - 1;
}
} // namespace bench

namespace aurora::gfx {
static Range bench_push(ByteBuffer& target, const uint8_t* data, size_t length, size_t alignment) {
  if (alignment != 0) {
    const size_t begin = target.size();
    const size_t alignedBegin = AURORA_ALIGN(begin, alignment);
    if (alignedBegin > begin) {
      target.append_zeroes(alignedBegin - begin);
    }
  }
  const auto begin = target.size();
  if (length > 0) {
    target.append(data, length);
  }
  return {static_cast<uint32_t>(begin), static_cast<uint32_t>(length)};
}

Range push_verts(const uint8_t* data, size_t length, size_t alignment) {
  return bench_push(bench::s_frame.verts, data, length, alignment);
}
Range push_indices(const uint8_t* data, size_t length, size_t alignment) {
  return bench_push(bench::s_frame.indices, data, length, alignment);
}
Range push_uniform(const uint8_t* data, size_t length) {
  bench::record_uniform_push(data, length);
  // 256 = typical minUniformBufferOffsetAlignment
  return bench_push(bench::s_frame.uniforms, data, length, 256);
}
Range push_storage(const uint8_t* data, size_t length) {
  if (bench::s_translateStorage)
    UNLIKELY { data = bench::translate_storage(data, length); }
  return bench_push(bench::s_frame.storage, data, length, 256);
}
Range push_storage_unaligned(const uint8_t* data, size_t length, size_t alignment) {
  return bench_push(bench::s_frame.storage, data, length, alignment);
}

Vec2<uint32_t> get_render_target_size() noexcept { return {640, 480}; }
void set_viewport(const Viewport& viewport) noexcept {}
void set_scissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) noexcept {}
uint32_t get_sample_count() noexcept { return 1; }
// 256 = typical minUniformBufferOffsetAlignment (lib/gfx/common.cpp:1561)
uint32_t align_uniform(uint32_t value) { return AURORA_ALIGN(value, 256); }
} // namespace aurora::gfx

// --- Pipeline/draw command recording ---
namespace aurora::gfx {
namespace {
// Mirrors the steady-state path of find_pipeline_impl (lib/gfx/pipeline_cache.cpp:387-405).
PipelineRef g_lastPipelineRef = std::numeric_limits<PipelineRef>::max();
std::mutex g_pipelineMutex;
absl::flat_hash_map<PipelineRef, uint32_t> g_pipelines;
} // namespace

template <>
PipelineRef pipeline_ref<clear::PipelineConfig>(const clear::PipelineConfig& config) {
  return 0;
}
template <>
void push_draw_command<clear::DrawData>(clear::DrawData data) {}

template <>
PipelineRef pipeline_ref<gx::PipelineConfig>(const gx::PipelineConfig& config) {
  const PipelineRef hash = xxh3_hash(config, 0);
  if (hash == g_lastPipelineRef) {
    return g_lastPipelineRef;
  }
  g_lastPipelineRef = hash;
  std::scoped_lock guard{g_pipelineMutex};
  g_pipelines.try_emplace(hash, 0u);
  return hash;
}

template <>
void push_draw_command<gx::DrawData>(gx::DrawData data) {
  bench::s_frame.draws.push_back(data);
  ++g_drawCallCount;
}

template <>
gx::DrawData* get_last_draw_command() {
  if (bench::s_frame.draws.empty()) {
    return nullptr;
  }
  return &bench::s_frame.draws.back();
}
} // namespace aurora::gfx

namespace bench {
size_t pipeline_count() {
  std::scoped_lock guard{aurora::gfx::g_pipelineMutex};
  return aurora::gfx::g_pipelines.size();
}
} // namespace bench

// --- TextureBind::get_descriptor ---
namespace aurora::gfx {
wgpu::SamplerDescriptor TextureBind::get_descriptor() const noexcept { return wgpu::SamplerDescriptor{}; }
} // namespace aurora::gfx

// --- Texture creation/write/replacement stubs ---
namespace aurora::gfx {
TextureHandle new_static_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 gxFormat,
                                    ArrayRef<uint8_t> data, bool tlut, const char* label) noexcept {
  return {};
}
TextureHandle new_dynamic_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 gxFormat,
                                     const char* label) noexcept {
  return {};
}
TextureHandle new_render_texture(uint32_t width, uint32_t height, u32 gxFormat, const char* label) noexcept {
  return {};
}
TextureHandle new_conv_texture(uint32_t width, uint32_t height, u32 gxFormat, const char* label) noexcept { return {}; }
void write_texture(TextureRef& ref, ArrayRef<uint8_t> data) noexcept {}
void queue_texture_upload(TextureUpload upload) {}
void queue_texture_upload_data(const uint8_t* data, size_t length, uint32_t bytesPerRow, uint32_t rowsPerImage,
                               wgpu::TexelCopyTextureInfo tex, wgpu::Extent3D size) {}
void resolve_pass(TextureHandle texture, ClipRect rect, bool clearColor, bool clearAlpha, bool clearDepth,
                  Vec4<float> clearColorValue, float clearDepthValue, GXTexFmt resolveFormat) {}
void queue_palette_conv(tex_palette_conv::ConvRequest req) {}
void begin_offscreen(uint32_t width, uint32_t height) {}
void end_offscreen() {}
bool is_offscreen() noexcept { return false; }
} // namespace aurora::gfx

namespace aurora::gfx::depth_peek {
void initialize() {}
void shutdown() {}
void request_snapshot() noexcept {}
void poll() noexcept {}
void encode_frame_snapshot(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView,
                           wgpu::Extent3D sourceSize, uint32_t msaaSamples) noexcept {}
void after_submit() noexcept {}
bool read_latest(uint16_t x, uint16_t y, uint32_t& z) noexcept { return false; }
namespace testing {
void reset() noexcept {}
bool snapshot_requested() noexcept { return false; }
void set_latest(uint32_t width, uint32_t height, const std::vector<uint32_t>& data) {}
} // namespace testing
} // namespace aurora::gfx::depth_peek

namespace aurora::gfx::tex_copy_conv {
bool needs_conversion(GXTexFmt fmt) { return false; }
} // namespace aurora::gfx::tex_copy_conv

namespace aurora::gfx::tex_palette_conv {
void queue(ConvRequest req) {}
} // namespace aurora::gfx::tex_palette_conv

namespace aurora::gfx::texture_replacement {
u32 compute_texture_upload_size(const GXTexObj_& obj) noexcept { return 0; }
void register_tlut(const GXTlutObj*, const void*, GXTlutFmt, u16) noexcept {}
void load_tlut(const GXTlutObj*, u32) noexcept {}
std::optional<TextureHandle> find_replacement(const GXTexObj_&) noexcept { return std::nullopt; }
} // namespace aurora::gfx::texture_replacement

// --- Window stub ---
#include "window.hpp"
namespace aurora::window {
AuroraWindowSize get_window_size() { return {640, 480, 640, 480, 640, 480, 1.0f}; }
void set_frame_buffer_aspect_fit(bool) {}
} // namespace aurora::window

// --- WebGPU C API stubs (prevent linker errors from wgpu:: destructors) ---
extern "C" {
void wgpuDeviceRelease(WGPUDevice) {}
void wgpuQueueRelease(WGPUQueue) {}
void wgpuSurfaceRelease(WGPUSurface) {}
void wgpuBufferRelease(WGPUBuffer) {}
void wgpuTextureRelease(WGPUTexture) {}
void wgpuTextureViewRelease(WGPUTextureView) {}
void wgpuSamplerRelease(WGPUSampler) {}
void wgpuShaderModuleRelease(WGPUShaderModule) {}
void wgpuRenderPipelineRelease(WGPURenderPipeline) {}
void wgpuBindGroupRelease(WGPUBindGroup) {}
void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout) {}
void wgpuPipelineLayoutRelease(WGPUPipelineLayout) {}
void wgpuInstanceRelease(WGPUInstance) {}
void wgpuDeviceAddRef(WGPUDevice) {}
void wgpuQueueAddRef(WGPUQueue) {}
void wgpuSurfaceAddRef(WGPUSurface) {}
void wgpuBufferAddRef(WGPUBuffer) {}
void wgpuTextureAddRef(WGPUTexture) {}
void wgpuTextureViewAddRef(WGPUTextureView) {}
void wgpuInstanceAddRef(WGPUInstance) {}
}

void aurora::gfx::push_debug_group(std::string) {}
void push_debug_group(const char*) {}
void pop_debug_group() {}
void aurora::gfx::insert_debug_marker(std::string) {}
