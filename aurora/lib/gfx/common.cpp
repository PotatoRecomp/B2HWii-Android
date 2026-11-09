#include "common.hpp"
#include "staging_map.hpp"
#include "../gx/shader_info.hpp"

#include "clear.hpp"
#include "depth_peek.hpp"
#include "efb_ram_copy.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"
#include "../gx/pipeline.hpp"
#include "pipeline_cache.hpp"
#include "tex_copy_conv.hpp"
#include "tex_palette_conv.hpp"
#include "texture_replacement.hpp"
#include "texture.hpp"
#include "../window.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

#include <absl/container/flat_hash_map.h>
#include <magic_enum.hpp>

#include "tracy/Tracy.hpp"

namespace aurora::gfx {
static Module Log("aurora::gfx");

using webgpu::g_device;
using webgpu::g_instance;
using webgpu::g_queue;

struct DebugFrameData {
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> groups;
  std::vector<std::string> markers;
#endif
};
DebugFrameData g_debugFrame;

constexpr uint64_t StagingBufferSize = UniformBufferSize + VertexBufferSize + IndexBufferSize + StorageBufferSize +
                                       (UseTextureBuffer ? TextureUploadSize : 0);

struct ShaderDrawCommand {
  ShaderType type;
  union {
    clear::DrawData clear;
    gx::DrawData gx;
  };
};
enum class CommandType {
  SetViewport,
  SetScissor,
  Draw,
  DebugMarker,
};
struct Command {
  CommandType type;
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> debugGroupStack;
#endif
  union Data {
    Viewport setViewport;
    ClipRect setScissor;
    ShaderDrawCommand draw;
    size_t debugMarkerIndex;
  } data;
};
} // namespace aurora::gfx

void aurora_set_guest_write_hooks(AuroraGuestWriteGenerationCallback generation,
                                  AuroraGuestWriteNotifyCallback notify) {
  aurora::g_guestWriteGenerationHook = generation;
  aurora::g_guestWriteNotifyHook = notify;
}

namespace aurora {
// For types that we can't ensure are safe to hash with has_unique_object_representations,
// we create specialized methods to handle them. Note that these are highly dependent on
// the structure definition, which could easily change with Dawn updates.
template <>
inline HashType xxh3_hash(const WGPUBindGroupDescriptor& input, HashType seed) {
  constexpr auto offset = offsetof(WGPUBindGroupDescriptor, layout); // skip nextInChain, label
  const auto hash = xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset,
                                sizeof(WGPUBindGroupDescriptor) - offset - sizeof(void*) /* skip entries */, seed);
  const size_t entryBytes = sizeof(WGPUBindGroupEntry) * input.entryCount;
  // The entries are hashed unseeded so they stay off XXH3's seeded long path;
  // the descriptor head still carries the caller's seed.
  return hash_combine(hash, xxh3_hash_s(input.entries, entryBytes));
}
template <>
inline HashType xxh3_hash(const wgpu::SamplerDescriptor& input, HashType seed) {
  constexpr auto offset = offsetof(wgpu::SamplerDescriptor, addressModeU); // skip nextInChain, label
  return xxh3_hash_s(reinterpret_cast<const u8*>(&input) + offset,
                     sizeof(wgpu::SamplerDescriptor) - offset - 2 /* skip padding */, seed);
}
} // namespace aurora

namespace aurora::gfx {
namespace {
struct CachedBindGroup {
  wgpu::BindGroup bindGroup;
  uint32_t lastUsedFrame = 0;
};

constexpr uint32_t BindGroupCacheRetainFrames = 32;
constexpr uint32_t BindGroupCacheSweepPeriod = 16;
} // namespace

static absl::flat_hash_map<BindGroupRef, CachedBindGroup> g_cachedBindGroups;
// Bind groups dropped from the cache by clear_caches() while a sealed frame's recorded draws
// still reference them. Ownership moves here and is released at the next seal.
static std::vector<CachedBindGroup> g_retiredBindGroups;
static absl::flat_hash_map<SamplerRef, wgpu::Sampler> g_cachedSamplers;

static ByteBuffer g_verts;
static ByteBuffer g_uniforms;
static ByteBuffer g_indices;
static ByteBuffer g_storage;
static ByteBuffer g_textureUpload;
wgpu::Buffer g_vertexBuffer;
wgpu::Buffer g_uniformBuffer;
wgpu::Buffer g_indexBuffer;
wgpu::Buffer g_storageBuffer;
constexpr size_t FrameSlotCount = 3;
static std::array<wgpu::Buffer, FrameSlotCount> g_stagingBuffers;
static size_t currentStagingBuffer = 0;
static StagingMapState s_mappingState;
// Adapted from Dolphin VKStreamBuffer.cpp (GPL-2.0-or-later) and ARMSX2 VKStreamBuffer.cpp (GPL-3.0):
// on the direct Vulkan backend, vertex/uniform/index/storage buffers are allocated as persistently
// mapped host-visible ring buffers (3x capacity) and written directly by the CPU without staging copies.
static constexpr StagingSizes StreamRingCapacity{
    3 * VertexBufferSize,
    3 * UniformBufferSize,
    3 * IndexBufferSize,
    3 * StorageBufferSize,
};
struct StreamInFlightBatch {
  uint64_t gpuSerial = UINT64_MAX;
  StagingSizes begin{};
  StagingSizes end{};
  bool frameEnd = false;  // the batch that ended a frame (end_frame), not a mid-frame split
};
// Submitted frames the GPU may still be working on when a new one begins. The ring only waits when
// its byte window overlaps older data, and a light frame uses a small part of the window, so dozens
// of frames could queue on a GPU-bound phone: 1.2 showed 250-470 ms presents, the presenter dropping
// 8-48 frames the GPU had already drawn, and 2-5 fps on screen (Mali-G52, Adreno 610/618). Dawn's
// three staging slots (1.1) and ARMSX2's three command buffers bound it by count; this does too.
// AURORA_VK_MAX_FRAMES=n (1-8) overrides it for tests.
static size_t max_frames_in_flight() {
  static const size_t value = [] {
    const char* env = std::getenv("AURORA_VK_MAX_FRAMES");
    const int n = env != nullptr ? std::atoi(env) : 0;
    return n >= 1 && n <= 8 ? static_cast<size_t>(n) : size_t{2};
  }();
  return value;
}
static bool g_useStreamRing = false;
static std::array<uint8_t*, 4> g_streamMapped{};
static StagingSizes g_streamBase{};
static StagingSizes g_streamHead{};
static StagingSizes g_suspendedStreamBegin{};
static StagingSizes g_suspendedStreamEnd{};
static std::deque<StreamInFlightBatch> g_streamInFlight;
static std::mutex g_streamRingMutex;
static wgpu::Limits g_cachedLimits;
// Advanced once per logical frame in the seal prologue, under the renderer GPU mutex and with the
// producer blocked, so every later reader sees a value that no longer moves.
static uint32_t g_frameIndex = UINT32_MAX;
wgpu::BindGroupLayout g_staticBindGroupLayout;
wgpu::BindGroup g_staticBindGroup;
wgpu::BindGroupLayout g_uniformBindGroupLayout;
wgpu::BindGroup g_uniformBindGroup;

// for imgui debug
AuroraStats g_stats{};
uint32_t g_drawCallCount = 0;
uint32_t g_mergedDrawCallCount = 0;

using CommandList = std::vector<Command>;
struct RenderPass {
  wgpu::TextureView colorView;
  wgpu::TextureView resolveView; // MSAA resolve target; null if msaaSamples == 1
  wgpu::TextureView depthView;
  wgpu::Texture copySourceTexture;
  wgpu::TextureView copySourceView;
  wgpu::TextureView copySourceDepthView;
  wgpu::Extent3D targetSize;
  uint32_t msaaSamples = 1;

  TextureHandle resolveTarget;
  TextureHandle resolveSourceSnapshot;
  GXTexFmt resolveFormat = GX_TF_RGBA8;
  ClipRect resolveRect;
  ClipRect resolveSnapshotRect;
  Vec4<float> resolveSourceRect;
  Range resolveUniformRange;
  std::array<u32, 3> resolveCopyFilterCoefficients{0, 64, 0};
  Vec4<float> clearColorValue{0.f, 0.f, 0.f, 0.f};
  // 1.f is the forward-Z "farthest" clear value; under UseReversedZ farthest is 0.f instead (see
  // gx::clear_depth_value(), which the main render pass explicitly overrides this default with -
  // any OTHER pass that keeps this default, e.g. an offscreen render-to-texture pass composited
  // later, needs the same reversed-Z-aware value or its depth buffer starts "already nearest",
  // failing every subsequent depth test and making whatever's drawn into it vanish).
  float clearDepthValue = gx::UseReversedZ ? 0.f : 1.f;
  CommandList commands;
  bool clearColor = true;
  bool clearDepth = true;
  // The resolve destination outlives the frame with no re-issue path (one-shot
  // bake), so this pass may not drop draws even in skip-unready-pipelines mode.
  bool requireReadyPipelines = false;
  bool resolveHalfScale = false;
  bool resolveCopyFilterActive = false;
  bool resolveForceOpaqueAlpha = false;
  bool resolveNeedsConversion = false;
  bool resolveNeedsShaderSampling = false;
  bool resolveLinearSampling = false;
  bool snapshotColorResolveSource = false;
  std::vector<tex_palette_conv::ConvRequest> paletteConvs;
  // NSMBW: copies of this pass's resolve target into a region of another texture (add_resolve_patch).
  struct Patch {
    TextureHandle dst;
    TextureHandle native;  // the resolve target scaled to the patch's own size
    Range uniformRange;
    uint32_t x = 0, y = 0, width = 0, height = 0;
  };
  std::vector<Patch> patches;
};
static std::vector<RenderPass> g_renderPasses;
static u32 g_currentRenderPass = UINT32_MAX;

// Recycle command storage: discarding passes used to free their command lists too, so each frame
// rebuilt hundreds of KB from zero capacity. The passes themselves are cheap to recreate.
using CommandListPool = std::vector<CommandList>;
static CommandListPool g_commandListPool;
static constexpr size_t MaxPooledCommandLists = 32;
// The pool is the only recording storage both sides touch, a few pointer moves per frame, so a
// plain mutex is cheaper than the alternatives and keeps the vector's invariants.
static std::mutex g_commandListPoolMutex;

static CommandList acquire_command_list() noexcept {
  std::lock_guard lock{g_commandListPoolMutex};
  if (g_commandListPool.empty()) {
    return {};
  }
  CommandList list = std::move(g_commandListPool.back());
  g_commandListPool.pop_back();
  list.clear();
  return list;
}

static void release_command_list(CommandList&& list) noexcept {
  if (list.capacity() == 0) {
    return;
  }
  std::lock_guard lock{g_commandListPoolMutex};
  if (g_commandListPool.size() >= MaxPooledCommandLists) {
    return;
  }
  list.clear();
  g_commandListPool.emplace_back(std::move(list));
}

static void release_render_pass(RenderPass& pass) noexcept { release_command_list(std::move(pass.commands)); }

static void recycle_render_passes(std::vector<RenderPass>& passes) noexcept {
  for (auto& pass : passes) {
    release_render_pass(pass);
  }
  passes.clear();
}

struct SealedFrameData {
  depth_peek::FrameMapping depthMapping;
  DebugFrameData debug;
  std::vector<RenderPass> passes;
};

SealedFrame::SealedFrame() : m_data(std::make_unique<SealedFrameData>()) {}
SealedFrame::~SealedFrame() = default;
SealedFrame::SealedFrame(SealedFrame&&) noexcept = default;
SealedFrame& SealedFrame::operator=(SealedFrame&&) noexcept = default;

static RenderPass& push_render_pass(RenderPass&& pass) {
  auto& out = g_renderPasses.emplace_back(std::move(pass));
  if (out.commands.capacity() == 0) {
    out.commands = acquire_command_list();
  }
  return out;
}
// The producer may build the next frame's GX stream while the worker prepares the target, and
// this is the only renderer-owned property that path queries, so publish it explicitly.
static std::atomic_bool g_inOffscreen{false};
static std::optional<RenderPass> g_suspendedEfbPass;
static Viewport g_suspendedEfbViewport;
static ClipRect g_suspendedEfbScissor;
// Prefix referenced by a suspended EFB pass. Preserve its offsets across an
// offscreen split, without rendering it before the bake it may sample finishes.
static StagingSizes g_suspendedEfbBytes{};
static constexpr StagingSizes PhysicalStagingCapacity{
    VertexBufferSize, UniformBufferSize, IndexBufferSize, StorageBufferSize};
static StagingSizes g_stagingCapacity = PhysicalStagingCapacity;
static uint64_t g_stagingEpoch = 0;
static uint64_t g_stagingSplitCount = 0;
static StagingSizes g_stagingHighWater{};

StagingSizes staging_usage() noexcept {
  return {
      g_verts.size() >= g_streamBase[0] ? g_verts.size() - g_streamBase[0] : 0,
      g_uniforms.size() >= g_streamBase[1] ? g_uniforms.size() - g_streamBase[1] : 0,
      g_indices.size() >= g_streamBase[2] ? g_indices.size() - g_streamBase[2] : 0,
      g_storage.size() >= g_streamBase[3] ? g_storage.size() - g_streamBase[3] : 0,
  };
}
StagingSizes staging_high_water() noexcept { return g_stagingHighWater; }
uint64_t staging_epoch() noexcept { return g_stagingEpoch; }
uint64_t staging_split_count() noexcept { return g_stagingSplitCount; }
uint64_t staging_uniform_bytes(uint64_t bytes) {
  return staging_padded(bytes, g_cachedLimits.minUniformBufferOffsetAlignment);
}
uint64_t staging_storage_bytes(uint64_t bytes) {
  return staging_padded(bytes, g_cachedLimits.minStorageBufferOffsetAlignment);
}
void set_staging_capacity_limits_for_testing(const StagingSizes& limits) {
  for (unsigned i = 0; i < limits.size(); ++i) {
    if (limits[i] > PhysicalStagingCapacity[i])
      throw StagingCapacityError("Test staging capacity exceeds physical buffer");
  }
  g_stagingCapacity = limits;
  g_stagingHighWater = {};
}
bool staging_has_space(const StagingSizes& demand) {
  // Async readback preparation runs in the worker's noexcept seal prologue.
  // Reserve all 32 slots plus the uniform binding's 3840-byte trailing window.
  const StagingSizes tail{0, gx::MaxUniformSize + efb_ram::MaxAsyncReadbackSlots * staging_uniform_bytes(48), 0, 0};
  const StagingSizes retained = g_suspendedEfbPass ? g_suspendedEfbBytes : StagingSizes{};
  if (!staging_fits(retained, demand, tail, g_stagingCapacity))
    throw StagingCapacityError("GPU operation exceeds staging capacity including retained EFB data");
  return staging_fits(staging_usage(), demand, tail, g_stagingCapacity);
}
void ensure_staging_space(const StagingSizes& demand) {
  if (staging_has_space(demand)) return;
  split_staging_batch();
  if (!staging_has_space(demand))
    throw StagingCapacityError("GPU operation still exceeds staging capacity after submission");
}

static void discard_suspended_efb_pass() noexcept {
  if (g_suspendedEfbPass) {
    release_render_pass(*g_suspendedEfbPass);
    g_suspendedEfbPass.reset();
  }
}

static bool has_current_render_pass() noexcept { return g_currentRenderPass < g_renderPasses.size(); }
static webgpu::TextureWithSampler g_offscreenColor;
static webgpu::TextureWithSampler g_offscreenDepth;

struct ResolveSourceSnapshotPool {
  TextureHandle entry;
};

// Snapshot resources live in the same safe frame slots as the mapped staging buffers, and copies
// within a frame are ordered, so one grow-only snapshot serves every resolve in that slot.
static std::array<ResolveSourceSnapshotPool, FrameSlotCount> g_resolveSourceSnapshotPools;
static size_t g_recordingSnapshotSlot = 0;

static TextureHandle new_resolve_source_snapshot(wgpu::Extent3D size, wgpu::TextureFormat format) noexcept {
  const wgpu::TextureDescriptor textureDescriptor{
      .label = "GX Copy Source Snapshot",
      .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc |
               wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);
  constexpr wgpu::TextureViewDescriptor viewDescriptor{
      .label = "GX Copy Source Snapshot view",
      .dimension = wgpu::TextureViewDimension::e2D,
  };
  auto textureView = texture.CreateView(&viewDescriptor);
  wgpu::TextureView sampleTextureView = textureView;
  return std::make_shared<TextureRef>(std::move(texture), std::move(sampleTextureView), std::move(textureView), size,
                                      format, 1, GX_TF_RGBA8);
}

static TextureHandle acquire_resolve_source_snapshot(uint32_t width, uint32_t height) noexcept {
  auto& pool = g_resolveSourceSnapshotPools[g_recordingSnapshotSlot];
  const auto format = webgpu::g_graphicsConfig.surfaceConfiguration.format;

  if (pool.entry && pool.entry->size.width >= width && pool.entry->size.height >= height &&
      pool.entry->format == format) {
    return pool.entry;
  }

  const wgpu::Extent3D size{
      std::max(width, pool.entry ? pool.entry->size.width : 0u),
      std::max(height, pool.entry ? pool.entry->size.height : 0u),
      1,
  };
  pool.entry = new_resolve_source_snapshot(size, format);
  return pool.entry;
}

struct ResolveSamplingPlan {
  bool needsConversion = false;
  bool needsShaderSampling = false;
  bool usesLinearSampling = false;
};

static ResolveSamplingPlan make_resolve_sampling_plan(const TextureHandle& target, GXTexFmt format,
                                                       const ClipRect& resolveRect, const Vec4<float>& sourceRect,
                                                       bool halfScale, bool copyFilterActive,
                                                       bool forceOpaqueAlpha) noexcept {
  const auto differs = [](float lhs, float rhs) { return std::abs(lhs - rhs) > 0.01f; };
  const auto differsFromDst = [&](uint32_t dst, float src) {
    return differs(static_cast<float>(dst), src);
  };
  const bool sourceMatchesIntegerRect =
      !differs(sourceRect.x(), static_cast<float>(resolveRect.x)) &&
      !differs(sourceRect.y(), static_cast<float>(resolveRect.y)) &&
      !differs(sourceRect.z(), static_cast<float>(resolveRect.width)) &&
      !differs(sourceRect.w(), static_cast<float>(resolveRect.height));
  const bool dstMatchesSource = target && !differsFromDst(target->size.width, sourceRect.z()) &&
                                !differsFromDst(target->size.height, sourceRect.w());
  const bool needsShaderSampling =
      halfScale || copyFilterActive || forceOpaqueAlpha || !sourceMatchesIntegerRect || !dstMatchesSource;
  return {
      .needsConversion = tex_copy_conv::needs_conversion(format),
      .needsShaderSampling = needsShaderSampling,
      .usesLinearSampling = halfScale || (needsShaderSampling && !copyFilterActive),
  };
}

static ClipRect calculate_resolve_snapshot_rect(const wgpu::Extent3D& targetSize,
                                                const Vec4<float>& sourceRect,
                                                const ResolveSamplingPlan& samplingPlan,
                                                bool copyFilterActive) noexcept {
  const ClipRect fullTarget{
      .x = 0,
      .y = 0,
      .width = static_cast<int32_t>(targetSize.width),
      .height = static_cast<int32_t>(targetSize.height),
  };
  // Shader-sampled and converted copies can feed effects that read outside the copy rectangle
  // (MKW's DOF chain), so keep those full-target. Exact copies have a closed, crop-safe footprint.
  if (samplingPlan.needsConversion ||
      samplingPlan.needsShaderSampling ||
      targetSize.width == 0 || targetSize.height == 0 ||
      !std::isfinite(sourceRect.x()) || !std::isfinite(sourceRect.y()) ||
      !std::isfinite(sourceRect.z()) || !std::isfinite(sourceRect.w()) ||
      sourceRect.z() <= 0.0f || sourceRect.w() <= 0.0f) {
    return fullTarget;
  }

  // Linear sampling can touch one neighbor on every edge. The vertical GX
  // copy filter explicitly samples the previous and next rows.
  const int32_t haloX = samplingPlan.usesLinearSampling ? 1 : 0;
  const int32_t haloY = (samplingPlan.usesLinearSampling || copyFilterActive) ? 1 : 0;
  const int32_t targetWidth = static_cast<int32_t>(targetSize.width);
  const int32_t targetHeight = static_cast<int32_t>(targetSize.height);
  const int32_t left = std::clamp(static_cast<int32_t>(std::floor(sourceRect.x())) - haloX, 0, targetWidth - 1);
  const int32_t top = std::clamp(static_cast<int32_t>(std::floor(sourceRect.y())) - haloY, 0, targetHeight - 1);
  const int32_t right = std::clamp(static_cast<int32_t>(std::ceil(sourceRect.x() + sourceRect.z())) + haloX,
                                   left + 1, targetWidth);
  const int32_t bottom = std::clamp(static_cast<int32_t>(std::ceil(sourceRect.y() + sourceRect.w())) + haloY,
                                    top + 1, targetHeight);
  return {
      .x = left,
      .y = top,
      .width = right - left,
      .height = bottom - top,
  };
}

static void set_efb_targets(RenderPass& pass) {
  pass.colorView = webgpu::g_frameBuffer.view;
  pass.resolveView = webgpu::g_graphicsConfig.msaaSamples > 1 ? webgpu::g_frameBufferResolved.view : nullptr;
  pass.depthView = webgpu::g_depthBuffer.view;
  pass.copySourceTexture =
      webgpu::g_graphicsConfig.msaaSamples > 1 ? webgpu::g_frameBufferResolved.texture : webgpu::g_frameBuffer.texture;
  pass.copySourceView =
      webgpu::g_graphicsConfig.msaaSamples > 1 ? webgpu::g_frameBufferResolved.view : webgpu::g_frameBuffer.view;
  pass.copySourceDepthView = webgpu::g_depthBuffer.view;
  pass.targetSize = webgpu::g_frameBuffer.size;
  pass.msaaSamples = webgpu::g_graphicsConfig.msaaSamples;
}

struct OffscreenCacheKey {
  uint32_t width;
  uint32_t height;

  bool operator==(const OffscreenCacheKey& rhs) const { return width == rhs.width && height == rhs.height; }
  template <typename H>
  friend H AbslHashValue(H h, const OffscreenCacheKey& key) {
    return H::combine(std::move(h), key.width, key.height);
  }
};
struct OffscreenCacheEntry {
  webgpu::TextureWithSampler color;
  webgpu::TextureWithSampler depth;
};
static absl::flat_hash_map<OffscreenCacheKey, OffscreenCacheEntry> g_offscreenCache;
std::vector<TextureUpload> g_textureUploads;

static inline void push_command(CommandType type, const Command::Data& data) {
  if (!has_current_render_pass())
    UNLIKELY {
      Log.warn("Dropping command {}", magic_enum::enum_name(type));
      return;
    }
  g_renderPasses[g_currentRenderPass].commands.push_back({
      .type = type,
#ifdef AURORA_GFX_DEBUG_GROUPS
      .debugGroupStack = g_debugFrame.groups,
#endif
      .data = data,
  });
}

template <>
gx::DrawData* get_last_draw_command() {
  if (g_currentRenderPass >= g_renderPasses.size()) {
    return nullptr;
  }
  auto& last = g_renderPasses[g_currentRenderPass].commands.back();
  if (last.type != CommandType::Draw || last.data.draw.type != ShaderType::GX) {
    return nullptr;
  }
  return &last.data.draw.gx;
}

static void push_draw_command(ShaderDrawCommand data) {
  push_command(CommandType::Draw, Command::Data{.draw = data});
  ++g_drawCallCount;
}

Vec2<uint32_t> get_frame_buffer_size() noexcept {
  if (webgpu::g_frameBuffer.size.width != 0 && webgpu::g_frameBuffer.size.height != 0) {
    return {webgpu::g_frameBuffer.size.width, webgpu::g_frameBuffer.size.height};
  }
  const auto windowSize = window::get_window_size();
  return {windowSize.fb_width, windowSize.fb_height};
}

// Render-thread only: the frame worker recycles g_renderPasses in end_frame, so an off-thread
// read is a use-after-free rather than a stale value. Use get_frame_buffer_size() instead.
Vec2<uint32_t> get_render_target_size() noexcept {
  if (g_currentRenderPass < g_renderPasses.size()) {
    const auto& size = g_renderPasses[g_currentRenderPass].targetSize;
    return {size.width, size.height};
  }
  return get_frame_buffer_size();
}

static Viewport g_cachedViewport;
void set_viewport(const Viewport& cmd) noexcept {
  if (cmd != g_cachedViewport) {
    push_command(CommandType::SetViewport, Command::Data{.setViewport = cmd});
    g_cachedViewport = cmd;
  }
}

static ClipRect g_cachedScissor;
void set_scissor(const ClipRect& cmd) noexcept {
  if (cmd != g_cachedScissor) {
    push_command(CommandType::SetScissor, Command::Data{.setScissor = cmd});
    g_cachedScissor = cmd;
  }
}

template <>
void push_draw_command(clear::DrawData data) {
  const bool vkDirectClear = webgpu::vulkan_backend_active() && data.clearColor == data.clearAlpha;
  if (!vkDirectClear && data.uniformRange.size == 0) {
    ensure_staging_space({0, staging_uniform_bytes(16), 0, 0});
    const std::array clearUniform{
        std::clamp(data.depth, 0.f, 1.f),
        0.f,
        0.f,
        0.f,
    };
    data.uniformRange = push_uniform(clearUniform);
  }
  push_draw_command(ShaderDrawCommand{.type = ShaderType::Clear, .clear = data});
}

template <>
PipelineRef pipeline_ref(const clear::PipelineConfig& config) {
  return find_pipeline(ShaderType::Clear, config, [=] { return create_pipeline(config); });
}

bool add_resolve_patch(const TextureHandle& source, TextureHandle dst, uint32_t x, uint32_t y, uint32_t width,
                       uint32_t height) {
  if (!source || !dst || width == 0 || height == 0 || dst->format != wgpu::TextureFormat::RGBA8Unorm ||
      x + width > dst->size.width || y + height > dst->size.height || g_currentRenderPass >= g_renderPasses.size()) {
    return false;
  }
  // The pass that resolves `source`: the most recent one with it as its target.
  for (size_t i = g_currentRenderPass + 1; i-- > 0;) {
    auto& pass = g_renderPasses[i];
    if (pass.resolveTarget != source) {
      continue;
    }
    // One scratch texture per patch size: the blits and copies are encoded in order.
    static absl::flat_hash_map<uint64_t, TextureHandle> natives;
    auto& native = natives[(static_cast<uint64_t>(width) << 32) | height];
    if (!native) {
      native = new_conv_texture(width, height, GX_TF_RGBA8, "GX resolve patch");
    }
    const std::array blitUniform{
        0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 64.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
    };
    pass.patches.push_back({std::move(dst), native, push_uniform(blitUniform), x, y, width, height});
    return true;
  }
  return false;
}

void resolve_pass(TextureHandle texture, ClipRect rect, bool clearColor, bool clearAlpha, bool clearDepth,
                  Vec4<float> clearColorValue, float clearDepthValue, GXTexFmt resolveFormat,
                  const Vec4<float>* sourceRectPixels, bool halfScale,
                  const std::array<u32, 3>* copyFilterCoefficients, bool forceOpaqueAlpha,
                  float copyFilterRowStride, bool clampTop, bool clampBottom, bool persistentCopy) {
  // Resolve current render pass
  if (!has_current_render_pass()) {
    Log.warn("Dropping resolve pass without an active render pass");
    return;
  }
  ensure_staging_space({0, 2 * staging_uniform_bytes(48), 0, 0});
  auto& prevPass = g_renderPasses[g_currentRenderPass];
  const auto targetWidth = static_cast<int32_t>(prevPass.targetSize.width);
  const auto targetHeight = static_cast<int32_t>(prevPass.targetSize.height);
  if (targetWidth <= 0 || targetHeight <= 0) {
    Log.warn("Dropping resolve pass with invalid target size {}x{}", targetWidth, targetHeight);
    return;
  }
  Vec4<float> sourceRect = sourceRectPixels != nullptr
                               ? *sourceRectPixels
                               : Vec4<float>{static_cast<float>(rect.x), static_cast<float>(rect.y),
                                             static_cast<float>(rect.width), static_cast<float>(rect.height)};
  if (targetWidth > 0 && targetHeight > 0) {
    const int32_t left = std::clamp(rect.x, 0, targetWidth - 1);
    const int32_t top = std::clamp(rect.y, 0, targetHeight - 1);
    const int32_t right = std::clamp(rect.x + rect.width, left + 1, targetWidth);
    const int32_t bottom = std::clamp(rect.y + rect.height, top + 1, targetHeight);
    rect = {
        .x = left,
        .y = top,
        .width = right - left,
        .height = bottom - top,
    };

    const float srcW = static_cast<float>(targetWidth);
    const float srcH = static_cast<float>(targetHeight);
    const float srcLeft = std::clamp(sourceRect.x(), 0.0f, srcW);
    const float srcTop = std::clamp(sourceRect.y(), 0.0f, srcH);
    const float srcRight = std::clamp(sourceRect.x() + sourceRect.z(), srcLeft, srcW);
    const float srcBottom = std::clamp(sourceRect.y() + sourceRect.w(), srcTop, srcH);
    sourceRect = {srcLeft, srcTop, std::max(srcRight - srcLeft, 1.0f), std::max(srcBottom - srcTop, 1.0f)};
  }
  prevPass.resolveTarget = std::move(texture);
  prevPass.requireReadyPipelines |= persistentCopy;
  prevPass.resolveRect = rect;
  prevPass.resolveSourceRect = sourceRect;
  prevPass.resolveFormat = resolveFormat;
  prevPass.resolveHalfScale = halfScale;
  prevPass.resolveForceOpaqueAlpha = forceOpaqueAlpha;
  prevPass.resolveCopyFilterCoefficients =
      copyFilterCoefficients != nullptr ? *copyFilterCoefficients : std::array<u32, 3>{0, 64, 0};
  prevPass.resolveCopyFilterActive = prevPass.resolveCopyFilterCoefficients[0] != 0 ||
                                     prevPass.resolveCopyFilterCoefficients[1] != 64 ||
                                     prevPass.resolveCopyFilterCoefficients[2] != 0;
  const auto samplingPlan = make_resolve_sampling_plan(
      prevPass.resolveTarget, resolveFormat, rect, sourceRect, halfScale,
      prevPass.resolveCopyFilterActive, forceOpaqueAlpha);
  prevPass.resolveNeedsConversion = samplingPlan.needsConversion;
  prevPass.resolveNeedsShaderSampling = samplingPlan.needsShaderSampling;
  prevPass.resolveLinearSampling = samplingPlan.usesLinearSampling;
  prevPass.snapshotColorResolveSource =
      !gx::is_depth_format(resolveFormat) && (clearColor || clearAlpha || clearDepth);
  Vec4<float> uniformSourceRect = sourceRect;
  float srcW = static_cast<float>(prevPass.targetSize.width);
  float srcH = static_cast<float>(prevPass.targetSize.height);
  if (prevPass.snapshotColorResolveSource) {
    prevPass.resolveSnapshotRect = calculate_resolve_snapshot_rect(
        prevPass.targetSize, sourceRect, samplingPlan, prevPass.resolveCopyFilterActive);
    prevPass.resolveSourceSnapshot = acquire_resolve_source_snapshot(
        static_cast<uint32_t>(prevPass.resolveSnapshotRect.width),
        static_cast<uint32_t>(prevPass.resolveSnapshotRect.height));
    uniformSourceRect = {
        sourceRect.x() - static_cast<float>(prevPass.resolveSnapshotRect.x),
        sourceRect.y() - static_cast<float>(prevPass.resolveSnapshotRect.y),
        sourceRect.z(), sourceRect.w()};
    srcW = static_cast<float>(prevPass.resolveSourceSnapshot->size.width);
    srcH = static_cast<float>(prevPass.resolveSourceSnapshot->size.height);
  }
  // GX's copy-clamp bits pin every vertical filter tap to the first or last source texel; without
  // it a filtered copy at internal resolution samples an unrelated EFB row as a visible border.
  const float clampTopPixels = clampTop ? uniformSourceRect.y() : 0.0f;
  const float clampBottomPixels =
      clampBottom ? uniformSourceRect.y() + uniformSourceRect.w() : srcH;
  const float clampTopUv = (clampTopPixels + 0.5f) / srcH;
  const float clampBottomUv = (clampBottomPixels - 0.5f) / srcH;
  // Push UV transform uniform for tex_copy_conv (crop region in UV space)
  const std::array resolveUniform{
      uniformSourceRect.x() / srcW,
      uniformSourceRect.y() / srcH,
      uniformSourceRect.z() / srcW,
      uniformSourceRect.w() / srcH,
      static_cast<float>(prevPass.resolveCopyFilterCoefficients[0]),
      static_cast<float>(prevPass.resolveCopyFilterCoefficients[1]),
      static_cast<float>(prevPass.resolveCopyFilterCoefficients[2]),
      prevPass.resolveCopyFilterActive ? 1.0f : 0.0f,
      prevPass.resolveForceOpaqueAlpha ? 1.0f : 0.0f,
      std::max(copyFilterRowStride, 1.0f),
      clampTopUv,
      clampBottomUv,
  };
  prevPass.resolveUniformRange = push_uniform(resolveUniform);
  const bool clearFullTarget = rect.x <= 0 && rect.y <= 0 &&
                               rect.width >= static_cast<int32_t>(prevPass.targetSize.width) &&
                               rect.height >= static_cast<int32_t>(prevPass.targetSize.height);
  const bool useAttachmentColorClear = clearFullTarget && clearColor && clearAlpha;
  const bool useAttachmentDepthClear = clearFullTarget && clearDepth;

  // Populate new render pass from previous
  const auto msaaSamples = prevPass.msaaSamples;
  RenderPass newPass{
      .colorView = prevPass.colorView,
      .resolveView = prevPass.resolveView,
      .depthView = prevPass.depthView,
      .copySourceTexture = prevPass.copySourceTexture,
      .copySourceView = prevPass.copySourceView,
      .copySourceDepthView = prevPass.copySourceDepthView,
      .targetSize = prevPass.targetSize,
      .msaaSamples = msaaSamples,
      .clearColorValue = clearColorValue,
      .clearDepthValue = clearDepthValue,
      .clearColor = useAttachmentColorClear,
      .clearDepth = useAttachmentDepthClear,
  };
  push_render_pass(std::move(newPass));
  ++g_currentRenderPass;

  if ((!useAttachmentColorClear && (clearColor || clearAlpha)) || (!useAttachmentDepthClear && clearDepth)) {
    // GX copy clears cover the copied EFB rectangle, not always the whole target, so use a scissored
    // clear draw unless the load op can clear all of it.
    push_draw_command(clear::DrawData{
        .pipeline = pipeline_ref(clear::PipelineConfig{
            .msaaSamples = msaaSamples,
            .clearColor = clearColor,
            .clearAlpha = clearAlpha,
            .clearDepth = clearDepth,
        }),
        .color =
            wgpu::Color{
                .r = clearColorValue.x(),
                .g = clearColorValue.y(),
                .b = clearColorValue.z(),
                .a = clearColorValue.w(),
            },
        .depth = clearDepthValue,
        .useScissor = !clearFullTarget,
        .scissor = rect,
        .clearColor = clearColor,
        .clearAlpha = clearAlpha,
        .clearDepth = clearDepth,
    });
  }
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
}

void queue_palette_conv(tex_palette_conv::ConvRequest req) {
  if (!has_current_render_pass()) {
    Log.warn("Dropping palette conversion without an active render pass");
    return;
  }
  g_renderPasses[g_currentRenderPass].paletteConvs.push_back(std::move(req));
}

bool is_offscreen() noexcept { return g_inOffscreen; }

uint32_t get_sample_count() noexcept {
  if (!has_current_render_pass()) {
    return webgpu::g_graphicsConfig.msaaSamples;
  }
  return g_renderPasses[g_currentRenderPass].msaaSamples;
}

void clear_caches() noexcept {
  g_offscreenCache.clear();
}

static OffscreenCacheEntry get_offscreen_textures(uint32_t width, uint32_t height) {
  OffscreenCacheKey key{width, height};
  if (const auto it = g_offscreenCache.find(key); it != g_offscreenCache.end()) {
    return it->second;
  }
  const auto colorFormat = webgpu::g_graphicsConfig.surfaceConfiguration.format;
  const wgpu::Extent3D size{width, height, 1};
  const wgpu::TextureDescriptor colorDesc{
      .label = "Offscreen Color",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc |
               wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = colorFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  auto colorTexture = g_device.CreateTexture(&colorDesc);
  auto colorView = colorTexture.CreateView();
  webgpu::TextureWithSampler color{
      .texture = std::move(colorTexture),
      .view = std::move(colorView),
      .size = size,
      .format = colorFormat,
  };
  const auto depthFormat = webgpu::g_graphicsConfig.depthFormat;
  const wgpu::TextureDescriptor depthDesc{
      .label = "Offscreen Depth",
      .usage = wgpu::TextureUsage::RenderAttachment,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = depthFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  auto depthTexture = g_device.CreateTexture(&depthDesc);
  auto depthView = depthTexture.CreateView();
  webgpu::TextureWithSampler depth{
      .texture = std::move(depthTexture),
      .view = std::move(depthView),
      .size = size,
      .format = depthFormat,
  };
  OffscreenCacheEntry entry{
      .color = std::move(color),
      .depth = std::move(depth),
  };
  auto [insertIt, _] = g_offscreenCache.emplace(key, std::move(entry));
  return insertIt->second;
}

void begin_offscreen(uint32_t width, uint32_t height) {
  ZoneScoped;
  CHECK(g_currentRenderPass != UINT32_MAX, "begin_offscreen called outside of a frame");

  // If the current EFB pass has no resolve target, its output is unobservable.
  // Suspend it so that we can resume it after the offscreen pass.
  if (!g_inOffscreen) {
    auto& currentPass = g_renderPasses[g_currentRenderPass];
    if (!currentPass.resolveTarget) {
      g_suspendedEfbBytes = staging_usage();
      g_suspendedStreamBegin = g_streamBase;
      for (size_t i = 0; i < 4; ++i) {
        g_suspendedStreamEnd[i] = AURORA_ALIGN(g_streamBase[i] + g_suspendedEfbBytes[i], 256);
      }
      g_suspendedEfbPass = std::move(currentPass);
      g_renderPasses.pop_back();
      --g_currentRenderPass;
    }
    g_suspendedEfbViewport = g_cachedViewport;
    g_suspendedEfbScissor = g_cachedScissor;
  }

  // Create offscreen textures
  auto offscreenEntry = get_offscreen_textures(width, height);
  g_offscreenColor = std::move(offscreenEntry.color);
  g_offscreenDepth = std::move(offscreenEntry.depth);

  // Start a new pass with offscreen targets
  RenderPass newPass{
      .colorView = g_offscreenColor.view,
      .depthView = g_offscreenDepth.view,
      .copySourceTexture = g_offscreenColor.texture,
      .copySourceView = g_offscreenColor.view,
      .copySourceDepthView = g_offscreenDepth.view,
      .targetSize = {width, height, 1},
      .msaaSamples = 1,
      .clearColorValue = {0.f, 0.f, 0.f, 0.f},
      // See the RenderPass::clearDepthValue default's comment: this offscreen pass gets its own
      // depth buffer, and the farthest clear value is 0.f, not 1.f, under UseReversedZ.
      .clearDepthValue = gx::UseReversedZ ? 0.f : 1.f,
      .clearColor = true,
      .clearDepth = true,
  };
  push_render_pass(std::move(newPass));
  ++g_currentRenderPass;

  g_inOffscreen = true;

  g_cachedViewport = {0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
  g_cachedScissor = {0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)};
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
}

void end_offscreen() {
  ZoneScoped;
  CHECK(g_inOffscreen, "end_offscreen called without begin_offscreen");

  g_inOffscreen = false;
  g_offscreenColor = {};
  g_offscreenDepth = {};

  // Resume suspended EFB pass, or start a new one (load existing content)
  if (g_suspendedEfbPass) {
    // Keeps its own recorded commands.
    g_renderPasses.emplace_back(std::move(*g_suspendedEfbPass));
    g_suspendedEfbPass.reset();
  } else {
    auto& pass = push_render_pass(RenderPass{});
    pass.clearColor = false;
    pass.clearDepth = false;
  }
  ++g_currentRenderPass;
  set_efb_targets(g_renderPasses[g_currentRenderPass]);

  g_cachedViewport = g_suspendedEfbViewport;
  g_cachedScissor = g_suspendedEfbScissor;
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
}

template <>
void push_draw_command(gx::DrawData data) {
  push_draw_command(ShaderDrawCommand{.type = ShaderType::GX, .gx = data});
}

template <>
PipelineRef pipeline_ref(const gx::PipelineConfig& config) {
  return find_pipeline(ShaderType::GX, config, [=] { return create_pipeline(config); });
}

void initialize() {
  g_frameIndex = 0;
  depth_peek::initialize();
  tex_copy_conv::initialize();
  tex_palette_conv::initialize();
  texture_replacement::initialize();

  // For uniform & storage buffer offset alignments
  g_device.GetLimits(&g_cachedLimits);

  const auto createBuffer = [](wgpu::Buffer& out, wgpu::BufferUsage usage, uint64_t size, const char* label) {
    if (size <= 0) {
      return;
    }
    const wgpu::BufferDescriptor descriptor{
        .label = label,
        .usage = usage,
        .size = size + webgpu::g_bufferTailSlack,
    };
    out = g_device.CreateBuffer(&descriptor);
  };
  static const bool allowStreamRing = [] {
    const char* v = std::getenv("AURORA_VK_STREAM_RING");
    return v == nullptr || v[0] != '0';
  }();
  g_useStreamRing = webgpu::vulkan_backend_active() && allowStreamRing && !UseTextureBuffer;
  const auto extraUsage = g_useStreamRing ? wgpu::BufferUsage::MapWrite : wgpu::BufferUsage::None;
  const auto bufSize = [&](size_t idx, uint64_t baseSize) {
    return g_useStreamRing ? StreamRingCapacity[idx] : baseSize;
  };
  createBuffer(g_uniformBuffer, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst | extraUsage,
               bufSize(1, UniformBufferSize), "Shared Uniform Buffer");
  createBuffer(g_vertexBuffer,
               wgpu::BufferUsage::Storage | wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst | extraUsage,
               bufSize(0, VertexBufferSize), "Shared Vertex Buffer");
  createBuffer(g_indexBuffer,
               wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst | extraUsage |
                   (webgpu::g_indexPulling ? wgpu::BufferUsage::Storage : wgpu::BufferUsage::None),
               bufSize(2, IndexBufferSize), "Shared Index Buffer");
  createBuffer(g_storageBuffer, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | extraUsage,
               bufSize(3, StorageBufferSize), "Shared Storage Buffer");
  if (g_useStreamRing) {
    g_streamMapped[0] = static_cast<uint8_t*>(g_vertexBuffer.GetMappedRange(0, StreamRingCapacity[0]));
    g_streamMapped[1] = static_cast<uint8_t*>(g_uniformBuffer.GetMappedRange(0, StreamRingCapacity[1]));
    g_streamMapped[2] = static_cast<uint8_t*>(g_indexBuffer.GetMappedRange(0, StreamRingCapacity[2]));
    g_streamMapped[3] = static_cast<uint8_t*>(g_storageBuffer.GetMappedRange(0, StreamRingCapacity[3]));
    if (g_streamMapped[0] == nullptr || g_streamMapped[1] == nullptr || g_streamMapped[2] == nullptr ||
        g_streamMapped[3] == nullptr) {
      Log.warn("Vulkan stream ring mapping unavailable; falling back to staging buffers");
      g_useStreamRing = false;
      g_streamMapped = {};
    } else {
      Log.info("Vulkan stream ring buffers active (vertex {} MiB, uniform {} MiB, index {} MiB, storage {} MiB)",
               StreamRingCapacity[0] >> 20, StreamRingCapacity[1] >> 20, StreamRingCapacity[2] >> 20,
               StreamRingCapacity[3] >> 20);
    }
  }
  g_streamBase = {};
  g_streamHead = {};
  g_suspendedStreamBegin = {};
  g_suspendedStreamEnd = {};
  {
    std::lock_guard lock(g_streamRingMutex);
    g_streamInFlight.clear();
  }
  if (!g_useStreamRing) {
    for (int i = 0; i < g_stagingBuffers.size(); ++i) {
      const auto label = fmt::format("Staging Buffer {}", i);
      createBuffer(g_stagingBuffers[i], wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc, StagingBufferSize,
                   label.c_str());
    }
    currentStagingBuffer = 0;
    s_mappingState.reset();
    map_staging_buffer();
  }

  {
    std::vector<wgpu::BindGroupLayoutEntry> layoutEntries{
        // Vertex data buffer
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                },
        },
        // Storage data buffer
        wgpu::BindGroupLayoutEntry{
            .binding = 1,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                },
        },
    };
    if (webgpu::g_indexPulling) {
      // Index data buffer, read by the GX vertex shader (webgpu::g_indexPulling)
      layoutEntries.push_back(wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Vertex,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::ReadOnlyStorage,
              },
      });
    }
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "Static bind group layout",
        .entryCount = layoutEntries.size(),
        .entries = layoutEntries.data(),
    };
    g_staticBindGroupLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    std::vector<wgpu::BindGroupEntry> entries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .buffer = g_vertexBuffer,
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .buffer = g_storageBuffer,
        },
    };
    if (webgpu::g_indexPulling) {
      entries.push_back(wgpu::BindGroupEntry{
          .binding = 2,
          .buffer = g_indexBuffer,
      });
    }
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .label = "Static bind group",
        .layout = g_staticBindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    g_staticBindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  }

  {
    constexpr std::array layoutEntries{
        // Uniform buffer (dynamic offset)
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = true,
                },
        },
    };
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "Uniform bind group layout",
        .entryCount = layoutEntries.size(),
        .entries = layoutEntries.data(),
    };
    g_uniformBindGroupLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    const std::array entries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .buffer = g_uniformBuffer,
            .size = gx::MaxUniformSize,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .label = "Uniform bind group",
        .layout = g_uniformBindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    g_uniformBindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  }

  gx::initialize();
  initialize_pipeline_cache();
}

void shutdown() {
  shutdown_pipeline_cache();
  gx::clear_shader_module_cache();
  efb_ram::shutdown();
  depth_peek::shutdown();
  tex_copy_conv::shutdown();
  tex_palette_conv::shutdown();
  texture_replacement::shutdown();
  gx::shutdown();

  g_textureUploads.clear();
  g_cachedBindGroups.clear();
  g_retiredBindGroups.clear();
  g_cachedSamplers.clear();
  g_verts.release();
  g_uniforms.release();
  g_indices.release();
  g_storage.release();
  g_streamMapped = {};
  g_streamBase = {};
  g_streamHead = {};
  g_suspendedStreamBegin = {};
  g_suspendedStreamEnd = {};
  {
    std::lock_guard lock(g_streamRingMutex);
    g_streamInFlight.clear();
  }
  g_useStreamRing = false;
  g_vertexBuffer = {};
  g_uniformBuffer = {};
  g_indexBuffer = {};
  g_storageBuffer = {};
  // Invalidate outstanding callbacks before releasing their buffers.
  s_mappingState.reset();
  g_stagingBuffers.fill({});
  for (auto& pool : g_resolveSourceSnapshotPools) {
    pool.entry.reset();
  }
  discard_suspended_efb_pass();
  g_renderPasses.clear();
  g_commandListPool.clear();
  g_currentRenderPass = UINT32_MAX;
  g_offscreenCache.clear();
  g_offscreenColor = {};
  g_offscreenDepth = {};
  g_staticBindGroup = {};
  g_staticBindGroupLayout = {};
  g_uniformBindGroup = {};
  g_uniformBindGroupLayout = {};
  g_inOffscreen = false;
  g_frameIndex = UINT32_MAX;
  currentStagingBuffer = 0;
}

void map_staging_buffer() {
  if (g_useStreamRing) {
    return;
  }
  const auto generation = s_mappingState.request();
  if (generation == 0) {
    return;
  }

  g_stagingBuffers[currentStagingBuffer].MapAsync(
      wgpu::MapMode::Write, 0, StagingBufferSize, wgpu::CallbackMode::AllowSpontaneous,
      [generation](wgpu::MapAsyncStatus status, wgpu::StringView message) {
        const auto result = status == wgpu::MapAsyncStatus::Success
            ? BufferMapState::Mapped : BufferMapState::Unmapped;
        if (!s_mappingState.complete(generation, result)) return;
        if (status == wgpu::MapAsyncStatus::CallbackCancelled || status == wgpu::MapAsyncStatus::Aborted) {
          Log.warn("Buffer mapping {}: {}", magic_enum::enum_name(status), message);
          return;
        }
        ASSERT(status == wgpu::MapAsyncStatus::Success, "Buffer mapping failed: {} {}", magic_enum::enum_name(status),
               message);
      });
}

static bool begin_frame_impl(bool clearEfb, bool capacityResume = false) {
  ZoneScoped;
  if (g_useStreamRing) {
    uint64_t waitSerial = 0;
    {
      std::lock_guard lock(g_streamRingMutex);
      const uint64_t completed = webgpu::vulkan_poll_completed();
      while (!g_streamInFlight.empty() && g_streamInFlight.front().gpuSerial != UINT64_MAX &&
             g_streamInFlight.front().gpuSerial <= completed) {
        g_streamInFlight.pop_front();
      }
      for (size_t i = 0; i < g_streamHead.size(); ++i) {
        if (g_streamHead[i] + g_stagingCapacity[i] > StreamRingCapacity[i]) {
          g_streamHead[i] = 0;
        }
        if (capacityResume && g_suspendedEfbPass && g_suspendedEfbBytes[i] > 0) {
          const uint64_t suspBegin = g_suspendedStreamBegin[i];
          const uint64_t suspEnd = g_suspendedStreamEnd[i];
          if (g_streamHead[i] < suspEnd && suspBegin < g_streamHead[i] + g_stagingCapacity[i]) {
            g_streamHead[i] = suspEnd;
            if (g_streamHead[i] + g_stagingCapacity[i] > StreamRingCapacity[i]) {
              g_streamHead[i] = 0;
            }
          }
        }
        const uint64_t winBegin = g_streamHead[i];
        const uint64_t winEnd = winBegin + g_stagingCapacity[i];
        for (const auto& entry : g_streamInFlight) {
          if (entry.gpuSerial != UINT64_MAX && winBegin < entry.end[i] && entry.begin[i] < winEnd) {
            waitSerial = std::max(waitSerial, entry.gpuSerial);
          }
        }
        g_streamBase[i] = winBegin;
      }
      // Count bound: at most max_frames_in_flight() submitted frames left running while this one
      // records, so wait for the one before them.
      size_t frames = 0;
      for (auto it = g_streamInFlight.rbegin(); it != g_streamInFlight.rend(); ++it) {
        if (!it->frameEnd || it->gpuSerial == UINT64_MAX) {
          continue;
        }
        if (++frames > max_frames_in_flight()) {
          waitSerial = std::max(waitSerial, it->gpuSerial);
          break;
        }
      }
    }
    if (waitSerial != 0) {
      ZoneScopedN("Wait for stream ring serial");
      webgpu::vulkan_wait_serial(waitSerial);
      std::lock_guard lock(g_streamRingMutex);
      const uint64_t completed = webgpu::vulkan_poll_completed();
      while (!g_streamInFlight.empty() && g_streamInFlight.front().gpuSerial != UINT64_MAX &&
             g_streamInFlight.front().gpuSerial <= completed) {
        g_streamInFlight.pop_front();
      }
    }
    ++g_stagingEpoch;
    g_recordingSnapshotSlot = (g_recordingSnapshotSlot + 1) % FrameSlotCount;
    g_verts.attach_ring(g_streamMapped[0], static_cast<size_t>(g_streamBase[0]),
                        static_cast<size_t>(g_streamBase[0] + g_stagingCapacity[0]));
    g_uniforms.attach_ring(g_streamMapped[1], static_cast<size_t>(g_streamBase[1]),
                           static_cast<size_t>(g_streamBase[1] + g_stagingCapacity[1]));
    g_indices.attach_ring(g_streamMapped[2], static_cast<size_t>(g_streamBase[2]),
                          static_cast<size_t>(g_streamBase[2] + g_stagingCapacity[2]));
    g_storage.attach_ring(g_streamMapped[3], static_cast<size_t>(g_streamBase[3]),
                          static_cast<size_t>(g_streamBase[3] + g_stagingCapacity[3]));
  } else {
    {
      ZoneScopedN("Wait for buffer map");
      map_staging_buffer();
      while (true) {
        const auto mappingState = s_mappingState.state();
        if (mappingState == BufferMapState::Mapped) {
          break;
        }
        if (mappingState == BufferMapState::Unmapped) {
          // Frame begin failed because the staging map was aborted; the caller's retry loop drops the frame
          // and any one-shot bakes recorded into it. Rate-limited.
          static uint32_t s_beginFrameMapFailCount = 0;
          if (s_beginFrameMapFailCount < 64 || (s_beginFrameMapFailCount & 255) == 0) {
            Log.warn("begin_frame aborted: staging buffer unmapped (frame={} occurrences={})", g_frameIndex,
                     s_beginFrameMapFailCount + 1);
          }
          ++s_beginFrameMapFailCount;
          return false;
        }
        g_instance.ProcessEvents();
        webgpu::fail_if_device_lost();
        s_mappingState.wait_for_progress();
      }
    }
    ++g_stagingEpoch;
    g_recordingSnapshotSlot = currentStagingBuffer;
    size_t bufferOffset = 0;
    const auto& stagingBuf = g_stagingBuffers[currentStagingBuffer];
    const auto mapBuffer = [&](ByteBuffer& buf, uint64_t size) {
      if (size <= 0) {
        return;
      }
      buf = ByteBuffer{static_cast<u8*>(stagingBuf.GetMappedRange(bufferOffset, size)), static_cast<size_t>(size)};
      bufferOffset += size;
    };
    mapBuffer(g_verts, VertexBufferSize);
    mapBuffer(g_uniforms, UniformBufferSize);
    mapBuffer(g_indices, IndexBufferSize);
    mapBuffer(g_storage, StorageBufferSize);
    if constexpr (UseTextureBuffer) {
      mapBuffer(g_textureUpload, TextureUploadSize);
    }
  }

  g_drawCallCount = 0;
  g_mergedDrawCallCount = 0;
  if (clearEfb) {
    gx::begin_frame_interpolation();
  }
  discard_suspended_efb_pass();
  if (!capacityResume) webgpu::clear_present_source_override();

  push_render_pass(RenderPass{});
  set_efb_targets(g_renderPasses[0]);
  g_renderPasses[0].clearColorValue = gx::g_gxState.clearColor;
  g_renderPasses[0].clearDepthValue = gx::clear_depth_value();
  g_renderPasses[0].clearColor = clearEfb;
  g_renderPasses[0].clearDepth = clearEfb;
  g_currentRenderPass = 0;
  // Refresh paired render viewport/scissor from logical state in case the FB size changed.
  const auto mappedRenderState = gx::map_logical_render_state();
  const bool renderStateChanged = gx::g_gxState.renderViewport != mappedRenderState.viewport ||
                                  gx::g_gxState.renderScissor != mappedRenderState.scissor;
  gx::g_gxState.renderViewport = mappedRenderState.viewport;
  gx::g_gxState.renderScissor = mappedRenderState.scissor;
  gx::g_gxState.stateDirty = gx::g_gxState.stateDirty || renderStateChanged;
  g_cachedViewport = mappedRenderState.viewport;
  g_cachedScissor = mappedRenderState.scissor;
  push_command(CommandType::SetViewport, Command::Data{.setViewport = g_cachedViewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = g_cachedScissor});
  begin_pipeline_frame();
  return true;
}

bool begin_frame() { return begin_frame_impl(true); }

bool resume_frame() { return begin_frame_impl(false); }

void abort_frame() noexcept {
  efb_ram::cancel();
  efb_ram::abort_async();
  g_verts.release();
  g_uniforms.release();
  g_indices.release();
  g_storage.release();
  if constexpr (UseTextureBuffer) {
    g_textureUploads.clear();
    g_textureUpload.release();
  }
  if (g_useStreamRing) {
    gx::drop_pending_frame_interpolation_uniforms();
    g_streamHead = g_streamBase;
  } else if (s_mappingState.state() == BufferMapState::Mapped) {
    // Pending interpolation tasks hold raw pointers into the mapped staging
    // range; they must be dropped before the buffer is unmapped and rotated.
    gx::drop_pending_frame_interpolation_uniforms();
    g_stagingBuffers[currentStagingBuffer].Unmap();
    s_mappingState.reset();
    currentStagingBuffer = (currentStagingBuffer + 1) % g_stagingBuffers.size();
    map_staging_buffer();
  }
  recycle_render_passes(g_renderPasses);
  g_currentRenderPass = UINT32_MAX;
  discard_suspended_efb_pass();
  g_inOffscreen = false;
  for (auto& array : gx::g_gxState.arrays) {
    array.cachedRange = {};
  }
  webgpu::clear_present_source_override();
  end_pipeline_frame();
}

// Duplicate frame probe (AURORA_DUPE_PROBE=1, a test): ARMSX2 skips presenting a frame identical to the
// last one. Before skipping anything here, count how often Basara really draws the same frame twice:
// a frame's vertex, uniform, index and storage bytes, its draw count and texture uploads, hashed and
// compared with the last frame's. A frame split into several batches never counts as a duplicate.
static uint64_t hash_bytes(uint64_t h, const ByteBuffer& buf, size_t baseOffset = 0) {
  if (buf.size() <= baseOffset) return h;
  const uint8_t* p = buf.data() + baseOffset;
  size_t n = buf.size() - baseOffset;
  for (; n >= 8; p += 8, n -= 8) {
    uint64_t v;
    memcpy(&v, p, 8);
    h = (h ^ v) * 0x100000001b3ull;
  }
  for (; n > 0; ++p, --n) h = (h ^ *p) * 0x100000001b3ull;
  return h;
}

static void probe_duplicate_frame(bool advanceFrame) {
  static const bool enabled = [] {
    const char* v = std::getenv("AURORA_DUPE_PROBE");
    return v != nullptr && *v == '1';
  }();
  if (!enabled) return;
  static bool s_split = false;
  static uint64_t s_last = 0, s_frames = 0, s_dupes = 0, s_run = 0, s_longestRun = 0;
  if (!advanceFrame) {
    s_split = true;
    return;
  }
  uint64_t h = 0xcbf29ce484222325ull;
  h = hash_bytes(h, g_verts, static_cast<size_t>(g_streamBase[0]));
  h = hash_bytes(h, g_uniforms, static_cast<size_t>(g_streamBase[1]));
  h = hash_bytes(h, g_indices, static_cast<size_t>(g_streamBase[2]));
  h = hash_bytes(h, g_storage, static_cast<size_t>(g_streamBase[3]));
  h = (h ^ g_drawCallCount) * 0x100000001b3ull;
  size_t uploads = 0;
  if constexpr (UseTextureBuffer) uploads = g_textureUploads.size();
  const bool dupe = !s_split && uploads == 0 && g_drawCallCount > 0 && h == s_last;
  s_split = false;
  s_last = h;
  ++s_frames;
  if (dupe) {
    ++s_dupes;
    s_longestRun = std::max(s_longestRun, ++s_run);
  } else {
    s_run = 0;
  }
  if (s_frames % 300 == 0) {
    Log.info("duplicate frames: {} of the last 300 (longest run {}, {} draws now)", s_dupes, s_longestRun,
             g_drawCallCount);
    s_dupes = 0;
    s_longestRun = 0;
  }
}

static void end_batch_impl(const wgpu::CommandEncoder& cmd, bool advanceFrame) {
  ZoneScoped;
  ASSERT(!advanceFrame || !g_inOffscreen, "end_frame called while offscreen rendering is active");
  if (advanceFrame) {
    gx::finalize_frame_interpolation();
  } else {
    // Mid-frame batch split: the staging buffer is about to be unmapped and rotated, so pending
    // interpolation tasks pointing into it would dangle. Tie the clear to the rotation itself.
    gx::drop_pending_frame_interpolation_uniforms();
  }
  probe_duplicate_frame(advanceFrame);
  g_uniforms.append_zeroes(gx::MaxUniformSize); // Pad the end of the buffer
  const auto used = staging_usage();
  for (unsigned i = 0; i < used.size(); ++i) g_stagingHighWater[i] = std::max(g_stagingHighWater[i], used[i]);
  {
    // Peak use of the shared buffers, logged each time one grows by 1 MiB or a batch split happens,
    // so tester logs show how much of the fixed sizes (vertex 3, uniform 24, index 2, storage 8 MiB)
    // a battle needs before anyone shrinks them for low-RAM phones.
    static StagingSizes s_logged{};
    static uint64_t s_loggedSplits = 0;
    bool grew = g_stagingSplitCount != s_loggedSplits;
    for (unsigned i = 0; i < used.size(); ++i) grew |= g_stagingHighWater[i] >= s_logged[i] + (1u << 20);
    if (grew) {
      s_logged = g_stagingHighWater;
      s_loggedSplits = g_stagingSplitCount;
      Log.info("staging peak: vertex {} KiB, uniform {} KiB, index {} KiB, storage {} KiB, splits {}",
               s_logged[0] >> 10, s_logged[1] >> 10, s_logged[2] >> 10, s_logged[3] >> 10, s_loggedSplits);
    }
  }
  g_stats.drawCallCount = g_drawCallCount;
  g_stats.mergedDrawCallCount = g_mergedDrawCallCount;
  if (g_useStreamRing) {
    const auto flushStream = [&](size_t idx, ByteBuffer& buf, const wgpu::Buffer& out) -> uint64_t {
      const uint64_t begin = g_streamBase[idx];
      const uint64_t end = buf.size();
      const uint64_t writeSize = end >= begin ? end - begin : 0;
      if (writeSize > 0) {
        webgpu::vulkan_flush_buffer_range(out, begin, AURORA_ALIGN(writeSize, 4));
      }
      g_streamHead[idx] = AURORA_ALIGN(std::max(begin, end), 256);
      buf.release();
      return writeSize;
    };
    g_stats.lastVertSize = flushStream(0, g_verts, g_vertexBuffer);
    g_stats.lastUniformSize = flushStream(1, g_uniforms, g_uniformBuffer);
    g_stats.lastIndexSize = flushStream(2, g_indices, g_indexBuffer);
    g_stats.lastStorageSize = flushStream(3, g_storage, g_storageBuffer);
    {
      std::lock_guard lock(g_streamRingMutex);
      g_streamInFlight.push_back(StreamInFlightBatch{
          .gpuSerial = UINT64_MAX,
          .begin = g_streamBase,
          .end = g_streamHead,
          .frameEnd = advanceFrame,
      });
    }
  } else {
    uint64_t bufferOffset = 0;
    const auto writeBuffer = [&](ByteBuffer& buf, wgpu::Buffer& out, uint64_t size, std::string_view label) {
      const auto writeSize = buf.size(); // Only need to copy this many bytes
      if (writeSize > 0) {
        cmd.CopyBufferToBuffer(g_stagingBuffers[currentStagingBuffer], bufferOffset, out, 0, AURORA_ALIGN(writeSize, 4));
        buf.release();
      }
      bufferOffset += size;
      return writeSize;
    };
    g_stagingBuffers[currentStagingBuffer].Unmap();
    s_mappingState.reset();
    g_stats.lastVertSize = writeBuffer(g_verts, g_vertexBuffer, VertexBufferSize, "Vertex");
    g_stats.lastUniformSize = writeBuffer(g_uniforms, g_uniformBuffer, UniformBufferSize, "Uniform");
    g_stats.lastIndexSize = writeBuffer(g_indices, g_indexBuffer, IndexBufferSize, "Index");
    g_stats.lastStorageSize = writeBuffer(g_storage, g_storageBuffer, StorageBufferSize, "Storage");
    if constexpr (UseTextureBuffer) {
      g_stats.lastTextureUploadSize = g_textureUpload.size();
      {
        // Perform texture copies
        for (const auto& item : g_textureUploads) {
          const wgpu::TexelCopyBufferInfo buf{
              .layout =
                  wgpu::TexelCopyBufferLayout{
                      .offset = item.layout.offset + bufferOffset,
                      .bytesPerRow = AURORA_ALIGN(item.layout.bytesPerRow, 256),
                      .rowsPerImage = item.layout.rowsPerImage,
                  },
              .buffer = g_stagingBuffers[currentStagingBuffer],
          };
          cmd.CopyBufferToTexture(&buf, &item.tex, &item.size);
        }
        g_textureUploads.clear();
        g_textureUpload.release();
      }
    }
    currentStagingBuffer = (currentStagingBuffer + 1) % g_stagingBuffers.size();
    map_staging_buffer();
  }
  g_currentRenderPass = UINT32_MAX;
  for (auto& array : gx::g_gxState.arrays) {
    array.cachedRange = {};
  }
  end_pipeline_frame();
  if (advanceFrame) {
    ++g_frameIndex;
  }
}

void end_frame(const wgpu::CommandEncoder& cmd) { end_batch_impl(cmd, true); }

void end_batch(const wgpu::CommandEncoder& cmd) { end_batch_impl(cmd, false); }

void split_staging_batch() {
  // Never called under the decoder's renderer lock: the worker needs that lock
  // to reach DONE. FIFO admission yields its unconsumed command first.
  aurora::wait_for_frame_worker();
  std::lock_guard gpuLock(aurora::renderer_gpu_mutex());
  if (!has_current_render_pass())
    throw StagingCapacityError("Cannot split staging outside an active render pass");
  gx::mark_frame_interpolation_replay_unsafe();
  const bool offscreen = g_inOffscreen;
  const auto viewport = g_cachedViewport;
  const auto scissor = g_cachedScissor;
  const auto renderViewport = gx::g_gxState.renderViewport;
  const auto renderScissor = gx::g_gxState.renderScissor;
  const auto& active = g_renderPasses[g_currentRenderPass];
  RenderPass continuation{
      .colorView = active.colorView, .resolveView = active.resolveView,
      .depthView = active.depthView, .copySourceTexture = active.copySourceTexture,
      .copySourceView = active.copySourceView, .copySourceDepthView = active.copySourceDepthView,
      .targetSize = active.targetSize, .msaaSamples = active.msaaSamples,
      .clearColor = false, .clearDepth = false,
      .requireReadyPipelines = active.requireReadyPipelines || offscreen,
  };
  auto suspended = std::move(g_suspendedEfbPass);
  g_suspendedEfbPass.reset();
  std::array<std::vector<uint8_t>, 4> retained;
  std::array<ByteBuffer*, 4> buffers{&g_verts, &g_uniforms, &g_indices, &g_storage};
  if (suspended && !g_useStreamRing) {
    for (unsigned i = 0; i < buffers.size(); ++i) {
      if (g_suspendedEfbBytes[i])
        retained[i].assign(buffers[i]->data(), buffers[i]->data() + g_suspendedEfbBytes[i]);
    }
  }
  auto encoder = g_device.CreateCommandEncoder();
  end_batch(encoder);
  render(encoder);
  aurora::submit_staging_commands(encoder.Finish());
  after_submit();
  if (!begin_frame_impl(false, true))
    throw StagingCapacityError("Staging remap failed after capacity submission");
  recycle_render_passes(g_renderPasses);
  push_render_pass(std::move(continuation));
  g_currentRenderPass = 0;
  g_suspendedEfbPass = std::move(suspended);
  for (unsigned i = 0; i < buffers.size(); ++i) {
    if (!retained[i].empty()) buffers[i]->append(retained[i].data(), retained[i].size());
  }
  g_inOffscreen = offscreen;
  g_cachedViewport = viewport;
  g_cachedScissor = scissor;
  gx::g_gxState.renderViewport = renderViewport;
  gx::g_gxState.renderScissor = renderScissor;
  gx::g_gxState.stateDirty = true;
  push_command(CommandType::SetViewport, Command::Data{.setViewport = viewport});
  push_command(CommandType::SetScissor, Command::Data{.setScissor = scissor});
  ++g_stagingSplitCount;
}

uint32_t current_frame() noexcept { return g_frameIndex; }

// The only place that erases from g_cachedBindGroups, whose handles the frame being encoded still
// holds, so it runs in the seal prologue with the renderer mutex held and the producer excluded.
void expire_bind_group_cache() noexcept {
  if (g_cachedBindGroups.empty() || g_frameIndex == UINT32_MAX || g_frameIndex % BindGroupCacheSweepPeriod != 0) {
    return;
  }

  ZoneScoped;
  for (auto it = g_cachedBindGroups.begin(); it != g_cachedBindGroups.end();) {
    if (g_frameIndex - it->second.lastUsedFrame > BindGroupCacheRetainFrames) {
      g_cachedBindGroups.erase(it++);
    } else {
      ++it;
    }
  }
}

// Debug labels for the render passes. Formatting them per pass per slot costs a heap-allocating
// fmt::format for a name nothing reads outside a capture.
static const char* render_pass_label(u32 index) noexcept {
  static constexpr std::array<const char*, 16> kRenderPassLabels{
      "Render pass 0",  "Render pass 1",  "Render pass 2",  "Render pass 3",
      "Render pass 4",  "Render pass 5",  "Render pass 6",  "Render pass 7",
      "Render pass 8",  "Render pass 9",  "Render pass 10", "Render pass 11",
      "Render pass 12", "Render pass 13", "Render pass 14", "Render pass 15",
  };
  return index < kRenderPassLabels.size() ? kRenderPassLabels[index] : "Render pass";
}

static void render_pass_impl(const wgpu::RenderPassEncoder& pass, const std::vector<RenderPass>& passes, u32 idx,
                             int32_t interpolatedFrame, DebugFrameData& debugFrame);

static void render_impl(std::vector<RenderPass>& renderPasses, wgpu::CommandEncoder& cmd, int32_t interpolatedFrame,
                        bool finalize, DebugFrameData& debugFrame, const depth_peek::FrameMapping& depthMapping) {
  ZoneScoped;
  // Palette conversions, MSAA resolves and EFB copies depend on sealed frame state, not on the
  // interpolation weight, so encode them on the native render and let replay slots sample them.
  const bool encodeTextureBakes = interpolatedFrame < 0;
  const auto passRuns = [&](u32 j) {
    const auto& p = renderPasses[j];
    return j == renderPasses.size() - 1 || (p.resolveTarget && encodeTextureBakes) || p.clearColor ||
           p.clearDepth || !p.commands.empty();
  };
  // An attachment that the next pass to run clears, and nothing reads before that, need not be written
  // back: on a tile-based GPU that saves storing the whole tile memory (EFB copy + clear does this every
  // copy). AURORA_EFB_DISCARD=0 stores everything.
  static const bool discardEnabled = [] {
    const char* v = std::getenv("AURORA_EFB_DISCARD");
    return v == nullptr || v[0] != '0';
  }();
  for (u32 i = 0; i < renderPasses.size(); ++i) {
    const auto& passInfo = renderPasses[i];
    if (encodeTextureBakes) {
      for (const auto& conv : passInfo.paletteConvs) {
        tex_palette_conv::run(cmd, conv);
      }
    }
    const bool hasRenderWork = passInfo.clearColor || passInfo.clearDepth || !passInfo.commands.empty();
    if (i == renderPasses.size() - 1) {
      ASSERT(!passInfo.resolveTarget, "Final render pass must not have resolve target");
    } else if (!(passInfo.resolveTarget && encodeTextureBakes) && !hasRenderWork) {
      // Skip only empty intermediate passes: offscreen and scratch passes with resolves still have to
      // run for later samplers, and on a replay slot a resolve-only pass has nothing to encode.
      continue;
    }

    wgpu::StoreOp colorStore = wgpu::StoreOp::Store;
    wgpu::StoreOp depthStore = wgpu::StoreOp::Store;
    if (discardEnabled) {
      for (u32 j = i + 1; j < renderPasses.size(); ++j) {
        if (!passRuns(j)) {
          continue;
        }
        const auto& next = renderPasses[j];
        // An EFB copy after this pass reads color or depth; keep whichever it reads.
        const bool copyReadsDepth = passInfo.resolveTarget && gx::is_depth_format(passInfo.resolveFormat);
        const bool copyReadsColor = passInfo.resolveTarget && !copyReadsDepth;
        if (next.clearDepth && next.depthView.Get() == passInfo.depthView.Get() && !copyReadsDepth) {
          depthStore = wgpu::StoreOp::Discard;
        }
        if (next.clearColor && next.colorView.Get() == passInfo.colorView.Get() && !copyReadsColor) {
          colorStore = wgpu::StoreOp::Discard;
        }
        break;
      }
    }
    const std::array attachments{
        wgpu::RenderPassColorAttachment{
            .view = passInfo.colorView,
            .resolveTarget = passInfo.resolveView,
            .loadOp = passInfo.clearColor ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
            .storeOp = colorStore,
            .clearValue =
                {
                    .r = passInfo.clearColorValue.x(),
                    .g = passInfo.clearColorValue.y(),
                    .b = passInfo.clearColorValue.z(),
                    .a = passInfo.clearColorValue.w(),
                },
        },
    };
    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment{
        .view = passInfo.depthView,
        .depthLoadOp = passInfo.clearDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load,
        .depthStoreOp = depthStore,
        .depthClearValue = passInfo.clearDepthValue,
    };
    const wgpu::RenderPassDescriptor renderPassDescriptor{
        .label = render_pass_label(i),
        .colorAttachmentCount = attachments.size(),
        .colorAttachments = attachments.data(),
        .depthStencilAttachment = &depthStencilAttachment,
    };

    // A pass that only carries an EFB copy (no clear, no commands) changes nothing in its target:
    // the pass before it stored the picture. Opening it anyway loads and stores the whole EFB on a
    // tile-based GPU just to close it again, once per back-to-back copy (ultimates and cutscenes copy
    // 20-30 times a frame). The copy reads the stored texture directly. Single-sample only (an MSAA
    // pass resolves on End). AURORA_EFB_COPY_PASS=1 opens them as before.
    static const bool skipCopyOnlyPasses = [] {
      const char* v = std::getenv("AURORA_EFB_COPY_PASS");
      return v == nullptr || v[0] != '1';
    }();
    const bool copyOnly = skipCopyOnlyPasses && !hasRenderWork && passInfo.resolveTarget &&
                          i != renderPasses.size() - 1 && passInfo.msaaSamples <= 1 &&
                          !passInfo.snapshotColorResolveSource;
    if (!copyOnly) {
      auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
      render_pass_impl(pass, renderPasses, i, interpolatedFrame, debugFrame);
      pass.End();
    }

    if (finalize && i == renderPasses.size() - 1) {
      depth_peek::encode_frame_snapshot(cmd, passInfo.copySourceDepthView, passInfo.targetSize, passInfo.msaaSamples, depthMapping);
    }

    if (passInfo.resolveTarget) {
      const bool isDepth = gx::is_depth_format(passInfo.resolveFormat);
      wgpu::Texture resolveSourceTexture = passInfo.copySourceTexture;
      wgpu::TextureView resolveSourceView = isDepth ? passInfo.copySourceDepthView : passInfo.copySourceView;
      if (passInfo.snapshotColorResolveSource && passInfo.resolveSourceSnapshot) {
        const wgpu::TexelCopyTextureInfo src{
            .texture = passInfo.copySourceTexture,
            .origin = wgpu::Origin3D{
                .x = static_cast<uint32_t>(passInfo.resolveSnapshotRect.x),
                .y = static_cast<uint32_t>(passInfo.resolveSnapshotRect.y),
            },
        };
        const wgpu::TexelCopyTextureInfo dst{
            .texture = passInfo.resolveSourceSnapshot->texture,
        };
        const wgpu::Extent3D size{
            .width = static_cast<uint32_t>(passInfo.resolveSnapshotRect.width),
            .height = static_cast<uint32_t>(passInfo.resolveSnapshotRect.height),
            .depthOrArrayLayers = 1,
        };
        cmd.CopyTextureToTexture(&src, &dst, &size);
        resolveSourceTexture = passInfo.resolveSourceSnapshot->texture;
        resolveSourceView = passInfo.resolveSourceSnapshot->sampleTextureView;
      }
      if (isDepth && passInfo.msaaSamples > 1) {
        Log.fatal("Depth tex copies from multisampled EFB targets are not supported");
      }
      const tex_copy_conv::ConvRequest convReq{
          .fmt = passInfo.resolveFormat,
          .srcView = resolveSourceView,
          .uniformRange = passInfo.resolveUniformRange,
          .dst = passInfo.resolveTarget,
          .sampleFilter = passInfo.resolveLinearSampling
                              ? tex_copy_conv::SampleFilter::Linear
                              : tex_copy_conv::SampleFilter::Nearest,
          .forceOpaqueAlpha = passInfo.resolveForceOpaqueAlpha,
      };
      if (passInfo.resolveNeedsConversion) {
        tex_copy_conv::run(cmd, convReq);
      } else if (passInfo.resolveNeedsShaderSampling) {
        tex_copy_conv::blit(cmd, convReq);
      } else {
        const wgpu::TexelCopyTextureInfo src{
            .texture = resolveSourceTexture,
            .origin =
                wgpu::Origin3D{
                    .x = static_cast<uint32_t>(passInfo.resolveRect.x -
                                               (passInfo.snapshotColorResolveSource
                                                    ? passInfo.resolveSnapshotRect.x : 0)),
                    .y = static_cast<uint32_t>(passInfo.resolveRect.y -
                                               (passInfo.snapshotColorResolveSource
                                                    ? passInfo.resolveSnapshotRect.y : 0)),
                },
        };
        const wgpu::TexelCopyTextureInfo dst{
            .texture = passInfo.resolveTarget->texture,
        };
        const wgpu::Extent3D size{
            .width = static_cast<uint32_t>(passInfo.resolveRect.width),
            .height = static_cast<uint32_t>(passInfo.resolveRect.height),
            .depthOrArrayLayers = 1,
        };
        cmd.CopyTextureToTexture(&src, &dst, &size);
      }
      for (const auto& patch : passInfo.patches) {
        if (!patch.dst || !patch.native) {
          continue;
        }
        tex_copy_conv::blit(cmd, tex_copy_conv::ConvRequest{
                                     .fmt = GX_TF_RGBA8,
                                     .srcView = passInfo.resolveTarget->sampleTextureView,
                                     .uniformRange = patch.uniformRange,
                                     .dst = patch.native,
                                     .sampleFilter = tex_copy_conv::SampleFilter::Linear,
                                 });
        const wgpu::TexelCopyTextureInfo src{.texture = patch.native->texture};
        const wgpu::TexelCopyTextureInfo dst{
            .texture = patch.dst->texture,
            .origin = wgpu::Origin3D{.x = patch.x, .y = patch.y},
        };
        const wgpu::Extent3D size{.width = patch.width, .height = patch.height, .depthOrArrayLayers = 1};
        cmd.CopyTextureToTexture(&src, &dst, &size);
      }
    }
  }
  if (finalize) {
    recycle_render_passes(renderPasses);
  }

#if defined(AURORA_GFX_DEBUG_GROUPS)
  if (finalize && !debugFrame.groups.empty()) {
    for (auto it = debugFrame.groups.rbegin(); it != debugFrame.groups.rend(); ++it) {
      Log.warn("Debug group was not popped at end of frame: {}", *it);
    }
    debugFrame.groups.clear();
  }

  if (finalize && debugFrame.markers.size() > 0) {
    debugFrame.markers.clear();
  }
#endif
}

void seal_frame(SealedFrame& out) noexcept {
  out.data().depthMapping = depth_peek::capture_frame_mapping();
  ZoneScoped;
  // The encode that could still have been holding these has completed: the
  // producer joins the worker's DONE phase before it seals another frame.
  g_retiredBindGroups.clear();
  auto& passes = out.data().passes;
  // The previous cycle already recycled these, so this normally just hands the empty vector, its
  // capacity included, back to the producer.
  recycle_render_passes(passes);
  passes.swap(g_renderPasses);
#ifdef AURORA_GFX_DEBUG_GROUPS
  // Marker indices and unmatched-group warnings belong to these detached passes.
  // The next producer frame must not modify strings still read by this encoder.
  auto& debug = out.data().debug;
  debug.groups.clear();
  debug.markers.clear();
  debug.groups.swap(g_debugFrame.groups);
  debug.markers.swap(g_debugFrame.markers);
#endif
  g_currentRenderPass = UINT32_MAX;
}

void render(SealedFrame& frame, wgpu::CommandEncoder& cmd, int32_t interpolatedFrame, bool finalize) {
  render_impl(frame.data().passes, cmd, interpolatedFrame, finalize, frame.data().debug, frame.data().depthMapping);
}

void render(wgpu::CommandEncoder& cmd, int32_t interpolatedFrame, bool finalize) {
  render_impl(g_renderPasses, cmd, interpolatedFrame, finalize, g_debugFrame, depth_peek::capture_frame_mapping());
  if (finalize) {
    g_currentRenderPass = UINT32_MAX;
    expire_bind_group_cache();
  }
}

void after_submit() noexcept {
  if (g_useStreamRing) {
    const uint64_t serial = webgpu::vulkan_submitted_serial();
    if (serial != 0) {
      std::lock_guard lock(g_streamRingMutex);
      for (auto& entry : g_streamInFlight) {
        if (entry.gpuSerial == UINT64_MAX) {
          entry.gpuSerial = serial;
        }
      }
    }
  }
  depth_peek::after_submit();
  efb_ram::after_submit();
  // Retire this frame's completed GPU work. Dawn only reclaims destroyed resources inside a device
  // tick, and a frame that never ticks keeps every released image and its memory for the run.
  if (g_instance) {
    g_instance.ProcessEvents();
  }
}

static void render_pass_impl(const wgpu::RenderPassEncoder& pass, const std::vector<RenderPass>& renderPasses, u32 idx,
                             int32_t interpolatedFrame, DebugFrameData& debugFrame) {
  // Per-invocation, not per-process: two encoders can be recording at once.
  gx::DrawEncodeState encodeState{};
  encodeState.boundTextureBindGroup = gx::g_emptyTextureBindGroup.Get();
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> lastDebugGroupStack;
#endif

  // Bind static bind group for the whole pass
  pass.SetBindGroup(0, g_staticBindGroup);
  pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);

  for (const auto& cmd : renderPasses[idx].commands) {
#ifdef AURORA_GFX_DEBUG_GROUPS
    {
      size_t firstDiff = lastDebugGroupStack.size();
      for (size_t i = 0; i < lastDebugGroupStack.size(); ++i) {
        if (i >= cmd.debugGroupStack.size() || cmd.debugGroupStack[i] != lastDebugGroupStack[i]) {
          firstDiff = i;
          break;
        }
      }
      for (size_t i = firstDiff; i < lastDebugGroupStack.size(); ++i) {
        pass.PopDebugGroup();
      }
      for (size_t i = firstDiff; i < cmd.debugGroupStack.size(); ++i) {
        pass.PushDebugGroup(cmd.debugGroupStack[i].c_str());
      }
      lastDebugGroupStack = cmd.debugGroupStack;
    }
#endif
    switch (cmd.type) {
    case CommandType::SetViewport: {
      const auto& vp = cmd.data.setViewport;
      // WebGPU requires 0 <= minDepth <= maxDepth <= 1. vp.znear/vp.zfar are in GX's own distance
      // terms (0 = near); under UseReversedZ the host depth-buffer storage direction is flipped
      // (near = 1, far = 0), so this range has to be remapped through 1-x the same way the
      // projection matrix, depth compare function, and clear value all are - a plain min/max clamp
      // (the previous code here) maps a *restricted* range (e.g. a viewport deliberately narrowed
      // to force something to draw "in front of everything") to the wrong end of the buffer: what
      // should land near the near-storage-extreme (1.0) instead lands near the far-storage-extreme
      // (0.0), so anything else drawn afterward at its true depth wins the compare test and the
      // "in front" geometry silently vanishes. A full [0,1] viewport is unaffected either way,
      // which is why this only broke specific elements, not the whole scene. Matches upstream
      // aurora's apply_viewport (lib/gfx/encoding.cpp) exactly.
      const float minDepth = gx::UseReversedZ ? 1.0f - vp.zfar : vp.znear;
      const float maxDepth = gx::UseReversedZ ? 1.0f - vp.znear : vp.zfar;
      pass.SetViewport(vp.left, vp.top, vp.width, vp.height, minDepth, maxDepth);
    } break;
    case CommandType::SetScissor: {
      const auto& sc = cmd.data.setScissor;
      const auto& size = renderPasses[idx].targetSize;
      const auto left = std::clamp(sc.x, 0, static_cast<int32_t>(size.width));
      const auto top = std::clamp(sc.y, 0, static_cast<int32_t>(size.height));
      const auto right =
          std::clamp(sc.x + sc.width, left, static_cast<int32_t>(size.width));
      const auto bottom =
          std::clamp(sc.y + sc.height, top, static_cast<int32_t>(size.height));
      pass.SetScissorRect(static_cast<uint32_t>(left), static_cast<uint32_t>(top),
                          static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top));
    } break;
    case CommandType::Draw: {
      const auto& draw = cmd.data.draw;
      switch (draw.type) {
      case ShaderType::GX: {
        const gfx::Range* uniformOverride = nullptr;
        if (interpolatedFrame >= 0 &&
            static_cast<size_t>(interpolatedFrame) < draw.gx.interpolatedUniformRanges.size() &&
            draw.gx.interpolatedUniformRanges[interpolatedFrame].size != 0) {
          uniformOverride = &draw.gx.interpolatedUniformRanges[interpolatedFrame];
        }
        gx::render(draw.gx, pass, encodeState, renderPasses[idx].requireReadyPipelines, uniformOverride);
      } break;
      case ShaderType::Clear:
        clear::render(draw.clear, pass, renderPasses[idx].targetSize, encodeState.currentPipeline);
        if (!webgpu::vulkan_backend_active() || draw.clear.clearColor != draw.clear.clearAlpha) {
          encodeState.uniformOffset = UINT32_MAX; // clear binds its own uniform range in slot 1
          encodeState.boundDstAlpha = UINT32_MAX;
        }
        break;
      }
    } break;
    case CommandType::DebugMarker: {
#if defined(AURORA_GFX_DEBUG_GROUPS)
      pass.InsertDebugMarker(wgpu::StringView(debugFrame.markers[cmd.data.debugMarkerIndex]));
#endif
    } break;
    }
  }

#ifdef AURORA_GFX_DEBUG_GROUPS
  for (size_t i = 0; i < lastDebugGroupStack.size(); ++i) {
    pass.PopDebugGroup();
  }
#endif
}

bool bind_pipeline(PipelineRef ref, const wgpu::RenderPassEncoder& pass, PipelineRef& currentPipeline,
                   bool requireReady) {
  if (ref == currentPipeline) {
    return true;
  }
  wgpu::RenderPipeline pipeline;
  bool pipelineReady;
  if (!skip_unready_pipelines()) {
    pipelineReady = wait_pipeline(ref, pipeline);
  } else if (requireReady) {
    // The pass resolves into a persistent texture (a one-shot bake such as MKW's minimap), so a
    // skipped draw would never be re-issued. These run behind loads, not mid-race.
    pipelineReady = wait_pipeline_for_persistent_pass(ref, pipeline);
  } else {
    pipelineReady = try_pipeline(ref, pipeline);
  }
  if (!pipelineReady) {
    return false;
  }
  pass.SetPipeline(pipeline);
  currentPipeline = ref;
  return true;
}

static inline Range push(ByteBuffer& target, const uint8_t* data, size_t length, size_t alignment) {
  size_t padding = 0;
  if (alignment != 0) {
    const size_t remainder = length % alignment;
    if (remainder != 0) {
      padding = alignment - remainder;
    }
  }
  auto begin = target.size();
  if (length == 0) {
    length = alignment;
    target.append_zeroes(alignment);
  } else {
    target.append(data, length);
    if (padding > 0) {
      target.append_zeroes(padding);
    }
  }
  return {static_cast<uint32_t>(begin), static_cast<uint32_t>(length + padding)};
}
static inline Range map(ByteBuffer& target, size_t length, size_t alignment) {
  size_t padding = 0;
  if (alignment != 0) {
    const size_t remainder = length % alignment;
    if (remainder != 0) {
      padding = alignment - remainder;
    }
  }
  auto begin = target.size();
  if (length == 0) {
    length = alignment;
    target.append_zeroes(length);
  } else {
    // The caller fills [0, length); only the alignment padding needs clearing,
    // otherwise it hands the next frame's readers whatever was there before.
    target.append_uninitialized(length);
    target.append_zeroes(padding);
  }
  return {static_cast<uint32_t>(begin), static_cast<uint32_t>(length + padding)};
}
Range push_verts(const uint8_t* data, size_t length) { return push(g_verts, data, length, 0); }
Range push_indices(const uint8_t* data, size_t length) { return push(g_indices, data, length, 0); }
Range push_uniform(const uint8_t* data, size_t length) {
  return push(g_uniforms, data, length, g_cachedLimits.minUniformBufferOffsetAlignment);
}
Range push_storage(const uint8_t* data, size_t length) {
  return push(g_storage, data, length, g_cachedLimits.minStorageBufferOffsetAlignment);
}
Range push_texture_data(const uint8_t* data, size_t length, u32 bytesPerRow, u32 rowsPerImage) {
  // For CopyBufferToTexture, we need an alignment of 256 per row (see Dawn kTextureBytesPerRowAlignment)
  const auto copyBytesPerRow = AURORA_ALIGN(bytesPerRow, 256);
  const auto range = map(g_textureUpload, copyBytesPerRow * rowsPerImage, 0);
  u8* dst = g_textureUpload.data() + range.offset;
  for (u32 i = 0; i < rowsPerImage; ++i) {
    memcpy(dst, data, bytesPerRow);
    data += bytesPerRow;
    dst += copyBytesPerRow;
  }
  return range;
}
std::pair<ByteBuffer, Range> map_verts(size_t length) {
  const auto range = map(g_verts, length, 4);
  return {ByteBuffer{g_verts.data() + range.offset, range.size}, range};
}
u8* map_verts_strided(size_t length, size_t stride, Range& range) {
  // The range starts on a multiple of stride, so draws address it with baseVertex = offset / stride.
  const size_t begin = g_verts.size();
  const size_t padding = (stride - begin % stride) % stride;
  if (padding != 0) {
    g_verts.append_zeroes(padding);
  }
  range = {static_cast<uint32_t>(begin + padding), static_cast<uint32_t>(length)};
  g_verts.append_uninitialized(length);
  return g_verts.data() + range.offset;
}
std::pair<ByteBuffer, Range> map_indices(size_t length) {
  const auto range = map(g_indices, length, 4);
  return {ByteBuffer{g_indices.data() + range.offset, range.size}, range};
}
std::pair<ByteBuffer, Range> map_uniform(size_t length) {
  const auto range = map(g_uniforms, length, g_cachedLimits.minUniformBufferOffsetAlignment);
  return {ByteBuffer{g_uniforms.data() + range.offset, range.size}, range};
}
std::pair<ByteBuffer, Range> copy_uniform(Range source) {
  const auto destination = map(g_uniforms, source.size, g_cachedLimits.minUniformBufferOffsetAlignment);
  std::memcpy(g_uniforms.data() + destination.offset, g_uniforms.data() + source.offset, source.size);
  return {ByteBuffer{g_uniforms.data() + destination.offset, destination.size}, destination};
}
std::pair<ByteBuffer, Range> map_storage(size_t length) {
  const auto range = map(g_storage, length, g_cachedLimits.minStorageBufferOffsetAlignment);
  return {ByteBuffer{g_storage.data() + range.offset, range.size}, range};
}

BindGroupRef bind_group_ref(const WGPUBindGroupDescriptor& descriptor) {
  const auto id = xxh3_hash(descriptor);
  const auto it = g_cachedBindGroups.find(id);
  if (it == g_cachedBindGroups.end()) {
    auto bg = wgpu::BindGroup::Acquire(wgpuDeviceCreateBindGroup(g_device.Get(), &descriptor));
    g_cachedBindGroups.emplace(id, CachedBindGroup{
                                       .bindGroup = std::move(bg),
                                       .lastUsedFrame = g_frameIndex,
                                   });
  } else {
    it->second.lastUsedFrame = g_frameIndex;
  }
  return id;
}

wgpu::BindGroup& find_bind_group(BindGroupRef id) {
  const auto it = g_cachedBindGroups.find(id);
  CHECK(it != g_cachedBindGroups.end(), "get_bind_group: failed to locate {:x}", id);
  return it->second.bindGroup;
}

wgpu::Sampler& sampler_ref(const wgpu::SamplerDescriptor& descriptor) {
  const auto id = xxh3_hash(descriptor);
  auto it = g_cachedSamplers.find(id);
  if (it == g_cachedSamplers.end()) {
    it = g_cachedSamplers.try_emplace(id, g_device.CreateSampler(&descriptor)).first;
  }
  return it->second;
}

uint32_t align_uniform(uint32_t value) { return AURORA_ALIGN(value, g_cachedLimits.minUniformBufferOffsetAlignment); }

void insert_debug_marker(std::string label) {
#if defined(AURORA_GFX_DEBUG_GROUPS)
  auto idx = g_debugFrame.markers.size();
  g_debugFrame.markers.emplace_back(std::move(label));
  push_command(CommandType::DebugMarker, {.debugMarkerIndex = idx});
#endif
}

} // namespace aurora::gfx

void aurora::gfx::push_debug_group(std::string label) {
#if defined(AURORA_GFX_DEBUG_GROUPS)
  g_debugFrame.groups.push_back(std::move(label));
#endif
}
void aurora_push_debug_group(const char* label) {
#ifdef AURORA_GFX_DEBUG_GROUPS
  aurora::gfx::g_debugFrame.groups.emplace_back(label);
#endif
}
void aurora_pop_debug_group() {
#ifdef AURORA_GFX_DEBUG_GROUPS
  if (aurora::gfx::g_debugFrame.groups.empty()) {
    aurora::gfx::Log.error("Debug group stack underflowed!");
    return;
  }

  aurora::gfx::g_debugFrame.groups.pop_back();
#endif
}

const AuroraStats* aurora_get_stats() { return &aurora::gfx::g_stats; }
