#if defined(__ANDROID__)
#include <android/api-level.h>
#endif
#include "gpu.hpp"
#include "fsr.hpp"
#include "sgsr.hpp"
#include "custom_driver.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <aurora/render_size_limits.hpp>
#include <magic_enum.hpp>
#include <webgpu/webgpu_cpp.h>

#include "../gfx/common.hpp"
#include "../internal.hpp"
#include "../window.hpp"
#include "../dolphin/vi/vi_internal.hpp"

#if defined(WEBGPU_DAWN) && !defined(__MINGW32__)
#include "../dawn/BackendBinding.hpp"
#include <dawn/native/DawnNative.h>
#elif defined(WEBGPU_DAWN)
#include "../dawn/BackendBinding.hpp"
#endif

#if defined(WEBGPU_DAWN) && defined(_WIN32)
#include <windows.h>
#endif

namespace aurora::gx {
void clear_display_copy_cache() noexcept;
} // namespace aurora::gx
namespace aurora::gfx {
void clear_offscreen_cache();
} // namespace aurora::gfx

namespace aurora::webgpu {
static Module Log("aurora::gpu");

#if defined(__ANDROID__) && defined(AURORA_VULKAN_BACKEND)
extern "C" bool aurora_wgpu_vk;  // lib/vulkan/wgpu_dispatch.cpp
extern "C" void aurora_vk_save_pipeline_cache();
extern "C" bool aurora_vk_has_bc();
static bool vulkan_backend_has_bc() { return aurora_vk_has_bc(); }
extern "C" int aurora_vk_surface_rotation();
extern "C" void aurora_vk_flush_buffer_range(WGPUBuffer buffer, uint64_t offset, uint64_t size);
extern "C" uint64_t aurora_vk_submitted_serial();
extern "C" uint64_t aurora_vk_poll_completed();
extern "C" void aurora_vk_wait_serial(uint64_t serial);
extern "C" void aurora_vk_wait_submit_idle();
extern "C" void aurora_vk_clear_attachments(WGPURenderPassEncoder pass, bool clearColor, WGPUColor color,
                                            bool clearDepth, float depth, uint32_t x, uint32_t y, uint32_t width,
                                            uint32_t height);
bool vulkan_backend_active() noexcept { return aurora_wgpu_vk; }
static void set_vulkan_backend(bool on) { aurora_wgpu_vk = on; }
void vulkan_flush_buffer_range(const wgpu::Buffer& buffer, uint64_t offset, uint64_t size) noexcept {
  if (aurora_wgpu_vk) {
    aurora_vk_flush_buffer_range(buffer.Get(), offset, size);
  }
}
uint64_t vulkan_submitted_serial() noexcept { return aurora_wgpu_vk ? aurora_vk_submitted_serial() : 0; }
uint64_t vulkan_poll_completed() noexcept { return aurora_wgpu_vk ? aurora_vk_poll_completed() : 0; }
void vulkan_wait_serial(uint64_t serial) noexcept {
  if (aurora_wgpu_vk) {
    aurora_vk_wait_serial(serial);
  }
}
void vulkan_wait_submit_idle() noexcept {
  if (aurora_wgpu_vk) {
    aurora_vk_wait_submit_idle();
  }
}
void vulkan_clear_attachments(const wgpu::RenderPassEncoder& pass, bool clearColor, const wgpu::Color& color,
                              bool clearDepth, float depth, uint32_t x, uint32_t y, uint32_t width,
                              uint32_t height) noexcept {
  if (aurora_wgpu_vk) {
    aurora_vk_clear_attachments(pass.Get(), clearColor, WGPUColor{color.r, color.g, color.b, color.a}, clearDepth,
                                depth, x, y, width, height);
  }
}
#else
bool vulkan_backend_active() noexcept { return false; }
static void set_vulkan_backend(bool) {}
static bool vulkan_backend_has_bc() { return true; }
void vulkan_flush_buffer_range(const wgpu::Buffer&, uint64_t, uint64_t) noexcept {}
uint64_t vulkan_submitted_serial() noexcept { return 0; }
uint64_t vulkan_poll_completed() noexcept { return 0; }
void vulkan_wait_serial(uint64_t) noexcept {}
void vulkan_wait_submit_idle() noexcept {}
void vulkan_clear_attachments(const wgpu::RenderPassEncoder&, bool, const wgpu::Color&, bool, float, uint32_t,
                              uint32_t, uint32_t, uint32_t) noexcept {}
#endif

wgpu::Device g_device;
wgpu::Queue g_queue;
wgpu::Surface g_surface;
wgpu::BackendType g_backendType;
GraphicsConfig g_graphicsConfig;
TextureWithSampler g_frameBuffer;
TextureWithSampler g_frameBufferResolved;
TextureWithSampler g_depthBuffer;

// EFB -> XFB copy pipeline
wgpu::BindGroupLayout g_CopyBindGroupLayout;
wgpu::RenderPipeline g_CopyPipeline;
std::array<wgpu::RenderPipeline, 3> g_CopyPipelineRotated;
wgpu::BindGroup g_CopyBindGroup;
static bool g_presentSourceOverrideActive = false;
static wgpu::BindGroup g_presentSourceOverrideBindGroup;
static wgpu::Texture g_presentSourceOverrideTexture;
static wgpu::Extent3D g_presentSourceOverrideSize{};
static wgpu::TextureFormat g_presentSourceOverrideFormat = wgpu::TextureFormat::Undefined;

static wgpu::Adapter g_adapter;
wgpu::Instance g_instance;
static wgpu::AdapterInfo g_adapterInfo;
static wgpu::SurfaceCapabilities g_surfaceCapabilities;
bool g_bcTexturesSupported;
bool g_safeVertexFetch = false;
uint64_t g_bufferTailSlack = 0;
bool g_indexPulling = false;
bool g_vertexLoader = false;
// PowerVR: swapchain width rounded down to a multiple of 32 (see choose_gpu_compat).
static bool g_alignSwapchainWidth32 = false;
uint32_t g_shaderDebug = 0;
static std::string g_adapterName;
// Written by Dawn's device-loss callback and consumed at ordered frame boundaries. Keep the
// callback free of logging, allocation, teardown and renderer state mutation.
static std::atomic_bool g_deviceLost{false};
static std::atomic<wgpu::DeviceLostReason> g_deviceLostReason{wgpu::DeviceLostReason::Unknown};
// The reason enum is almost always `Unknown`, while Dawn's message carries the real cause, so
// keep a truncated copy. Written with a plain memcpy, published by the g_deviceLost store.
static std::array<char, 256> g_deviceLostMessage{};
// Errors raised before initialize() completes must not be fatal: the backend fallback loop retries
// the next backend, and a broken ICD can raise uncaptured errors mid-probe.
static std::atomic_bool g_initialized{false};

// AURORA_GFX_FLAGS: comma-separated graphics debugging switches, read at device creation. On Android
// the runtime fills it from `adb shell setprop debug.basara.gfx ...`.
//   validate   keep Dawn's validation and log its errors instead of aborting on them
//   robust     keep Dawn's bounds checks on buffer and texture access
//   nobc       do not enable BC texture compression
//   nomono     do not use one Vulkan pipeline cache for all pipelines
//   nocompat   do not retry Vulkan at the compatibility feature level when core is refused
//   forcecompat  skip core and ask Vulkan for the compatibility feature level (testing)
//   safefetch / nosafefetch   force the GPU compatibility vertex fetch on or off
//   nobufslack / bufslack   leave out or add the spare tail on the shared GPU buffers (auto: Adreno)
//   notex      every texture samples as grey (is it geometry or texturing?)
//   uvvis      texture samples show their coordinates instead
//   dumpwgsl   write every generated shader to <cache>/wgsl
//   +name      enable the Dawn toggle `name`; -name disables it
static std::vector<std::string> g_gfxFlags;
static std::atomic_bool g_logValidationErrors{false};
// True when Dawn only offered the adapter at the compatibility feature level.
static bool g_compatFeatureLevel = false;

static bool gfx_flag(std::string_view flag) {
  return std::find(g_gfxFlags.begin(), g_gfxFlags.end(), flag) != g_gfxFlags.end();
}

static void load_gfx_flags() {
  g_gfxFlags.clear();
  const char* env = std::getenv("AURORA_GFX_FLAGS");
  if (env == nullptr) {
    return;
  }
  std::string_view rest{env};
  while (!rest.empty()) {
    const auto comma = rest.find(',');
    auto item = rest.substr(0, comma);
    while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
    while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
    if (!item.empty()) {
      g_gfxFlags.emplace_back(item);
    }
    rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
  }
}

static bool contains_ignore_case(std::string_view haystack, std::string_view needle) {
  if (needle.size() > haystack.size()) {
    return false;
  }
  for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    size_t j = 0;
    while (j < needle.size() && std::tolower(static_cast<unsigned char>(haystack[i + j])) ==
                                    std::tolower(static_cast<unsigned char>(needle[j]))) {
      ++j;
    }
    if (j == needle.size()) {
      return true;
    }
  }
  return false;
}

// GPU driver workarounds for the adapter in use. Qualcomm's Adreno drivers (phones, Quest headsets and
// Snapdragon laptops alike) miscompile aurora's vertex pulling on some driver versions - Adreno 840
// always, some 6xx/7xx too - and every Android GPU runs a vendor driver that games rarely exercise
// this way, so phones get the safe path whatever their GPU. Its cost is a bounds check per load.
static void choose_gpu_compat() {
  const auto text = [](const wgpu::StringView& value) {
    return value.IsUndefined() ? std::string_view{} : std::string_view{value};
  };
  const bool qualcomm = contains_ignore_case(text(g_adapterInfo.vendor), "qualcomm") ||
                        contains_ignore_case(text(g_adapterInfo.device), "adreno") ||
                        contains_ignore_case(text(g_adapterInfo.description), "qualcomm") ||
                        g_adapterInfo.vendorID == 0x5143;
  // Adreno needs scalar vertex loads on some driver revisions. Mali has different
  // shader compilers, so do not pay this cost there unless a user forces Safe.
  // Unknown mobile vendors retain the conservative path.
  const bool mali = contains_ignore_case(text(g_adapterInfo.device), "mali") ||
                    contains_ignore_case(text(g_adapterInfo.description), "mali") ||
                    contains_ignore_case(text(g_adapterInfo.vendor), "arm") ||
                    g_adapterInfo.vendorID == 0x13B5;
#if defined(__ANDROID__)
  constexpr bool mobile = true;
#else
  constexpr bool mobile = false;
#endif
  const char* reason = nullptr;
  if (gfx_flag("nosafefetch")) {
    g_safeVertexFetch = false;
    reason = "off (debug flag)";
  } else if (gfx_flag("safefetch")) {
    g_safeVertexFetch = true;
    reason = "on (debug flag)";
  } else if (g_config.gpuCompat == AURORA_GPU_COMPAT_ON) {
    g_safeVertexFetch = true;
    reason = "on (setting)";
  } else if (g_config.gpuCompat == AURORA_GPU_COMPAT_OFF) {
    g_safeVertexFetch = false;
    reason = "off (setting)";
  } else {
    g_safeVertexFetch = qualcomm || (mobile && !mali);
    reason = qualcomm ? "on (auto: Qualcomm Adreno)" :
             mali ? "off (auto: ARM Mali; enable compatibility if broken)" :
             mobile ? "on (auto: unknown mobile GPU)" : "off (auto)";
  }
  // Old Adreno drivers (Adreno 512, Android 10) read past the bound range of a buffer on the CPU while
  // recording a render pass, and fault when a draw's data sits at the very end of a shared buffer.
  // A spare tail behind each shared buffer keeps those reads inside mapped memory.
  // Android 10 Adreno and every Adreno 5xx only: other phones keep the buffers exactly as before.
#if defined(__ANDROID__)
  const bool oldAndroid = android_get_device_api_level() < 30;
#else
  constexpr bool oldAndroid = false;
#endif
  // Adreno 5xx gets it on every Android version: a Redmi Note 7 on Android 14 (2023 driver) faults the
  // same way.
  const bool adreno5 = contains_ignore_case(text(g_adapterInfo.device), "adreno (tm) 5");
  g_bufferTailSlack = gfx_flag("nobufslack")                                          ? 0
                      : ((qualcomm && oldAndroid) || adreno5 || gfx_flag("bufslack")) ? (256u << 10)
                                                                                       : 0;
  if (g_bufferTailSlack != 0) {
    Log.info("Shared GPU buffers get a {} KiB spare tail (driver over-read guard)", g_bufferTailSlack >> 10);
  }
  // Adreno 5xx Vulkan drivers read the index buffer on the CPU in vkCmdDrawIndexed, before the copy
  // that fills it has run, and fault or lose the device; GX draws pull their indices instead there.
  g_indexPulling = !gfx_flag("noindexpull") &&
                   ((adreno5 && g_backendType == wgpu::BackendType::Vulkan) || gfx_flag("indexpull"));
  if (g_indexPulling) {
    Log.info("GX draws pull their indices in the vertex shader (no indexed draws)");
  }
  // GX vertices decoded on the CPU and fed through vertex inputs (gx/vertex_loader.hpp): off by
  // default. Battles on a Redmi (Adreno 618, same APK, 2026-11-07) ran 48.9 fps with vertex pulling
  // and 46-47.5 with the loader, the GX thread busier; the A610 phones are CPU-bound as well. Only
  // for tests: AURORA_VERTEX_LOADER=1 or the gfx flag vtxload. Pulled indices keep vertex pulling.
  // PowerVR Rogue on Vulkan gets it by default: an OPPO CPH2209 (GM9446, Vulkan 1.1.131, Android 12,
  // 2026-11-08) drew a black game picture with vertex pulling (the ImGui overlay, which uses vertex
  // inputs, still showed) and the whole picture with AURORA_VERTEX_LOADER=1. B-Series (BXM) is fine
  // with vertex pulling and keeps it.
  const bool powervrRogue = contains_ignore_case(text(g_adapterInfo.device), "rogue") &&
                            g_backendType == wgpu::BackendType::Vulkan;
  {
    const char* env = std::getenv("AURORA_VERTEX_LOADER");
    bool want = gfx_flag("vtxload") || powervrRogue;
    if (env != nullptr && env[0] != '\0') want = env[0] == '1';
    g_vertexLoader = !g_indexPulling && !gfx_flag("novtxload") && want;
  }
  Log.info("GX vertex loader: {}", g_vertexLoader ? "CPU decode, vertex inputs" : "vertex pulling");
  // Older PowerVR Rogue drivers corrupt the output, or show nothing, unless the swapchain width is a
  // multiple of 32 (PPSSPP #11743/#15773, ARMSX2 vk-powervr-old-swapchain-width); seen as a black
  // screen with sound on a GE8320 (driver 1.11@5425693) with a 1092-wide surface. Losing up to 31
  // columns at the edge costs nothing elsewhere, so every PowerVR gets it.
  const bool powervr = contains_ignore_case(text(g_adapterInfo.device), "powervr") ||
                       contains_ignore_case(text(g_adapterInfo.vendor), "imagination") ||
                       g_adapterInfo.vendorID == 0x1010;
  g_alignSwapchainWidth32 = !gfx_flag("nopvralign") && (powervr || gfx_flag("pvralign"));
  if (g_alignSwapchainWidth32) {
    Log.info("Swapchain width rounded down to a multiple of 32 (PowerVR)");
  }
  if (oldAndroid && adreno5) {
    Log.warn("Unsupported GPU: Adreno 5xx on Android 10 ({}); its Vulkan driver loses the device on the "
             "first screens, OpenGL ES is the fallback", magic_enum::enum_name(g_adapterInfo.backendType));
  }
  g_shaderDebug = (gfx_flag("notex") ? ShaderDebugNoTextures : 0u) |
                  (gfx_flag("uvvis") ? ShaderDebugTexCoords : 0u) |
                  (gfx_flag("dumpwgsl") ? ShaderDebugDumpSource : 0u);
  Log.info("GPU compatibility vertex fetch {} on {}", reason, g_adapterName);
}

const char* adapter_name() noexcept { return g_adapterName.c_str(); }

namespace {

struct RenderTargetSize {
  uint32_t width;
  uint32_t height;
};

RenderTargetSize clamp_render_target_size(uint32_t width, uint32_t height) noexcept {
  const uint32_t maxDimension = g_graphicsConfig.maxTextureDimension2D;
  if (width == 0 || height == 0 || maxDimension == 0 || maxDimension == WGPU_LIMIT_U32_UNDEFINED ||
      (width <= maxDimension && height <= maxDimension)) {
    return {width, height};
  }

  // Keep the requested aspect while fitting both axes inside the adapter's maximum 2D texture size.
  // 64-bit arithmetic so a large window cannot wrap during the scale.
  if (width > maxDimension) {
    height = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(height) * maxDimension) / width));
    width = maxDimension;
  }
  if (height > maxDimension) {
    width = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(width) * maxDimension) / height));
    height = maxDimension;
  }
  return {width, height};
}

RenderTargetSize clamp_frame_buffer_size(uint32_t width, uint32_t height) noexcept {
  const auto adapterClamped = clamp_render_target_size(width, height);
  const auto budgeted =
      render_size_limits::fit_framebuffer_to_budget(adapterClamped.width, adapterClamped.height,
                                                    g_graphicsConfig.maxTextureDimension2D);
  return {budgeted.width, budgeted.height};
}

// NSMBW: V-Sync (Fifo) is a setting, off by default. Off, the guest paces itself and presents never
// wait (Immediate, else Mailbox). On, the runtime runs the game's VI clock at a whole fraction of the
// display's refresh (runtime/src/hle/vi.cpp), so presents land one per k vblanks and never queue up.
std::atomic<bool> g_vsyncEnabled{false};
std::atomic<int> g_vsyncRequest{-1};

wgpu::PresentMode best_present_mode() {
  const auto supports = [](const wgpu::PresentMode candidate) {
    for (size_t i = 0; i < g_surfaceCapabilities.presentModeCount; ++i) {
      if (g_surfaceCapabilities.presentModes[i] == candidate) {
        return true;
      }
    }
    return false;
  };
  if (g_vsyncEnabled.load(std::memory_order_acquire)) {
    // Relaxed Fifo where offered (AURORA_FIFO_RELAXED=0 turns it off): a frame that misses its vblank
    // by a little shows at once (a short tear) instead of waiting a whole refresh, so a heavy scene
    // dips to 55 instead of 30. The game's clock follows the Choreographer, not the present.
    static const bool relaxed = [] {
      const char* v = std::getenv("AURORA_FIFO_RELAXED");
      return v == nullptr || *v != '0';
    }();
    if (relaxed && supports(wgpu::PresentMode::FifoRelaxed)) {
      return wgpu::PresentMode::FifoRelaxed;
    }
    if (supports(wgpu::PresentMode::Fifo)) {
      return wgpu::PresentMode::Fifo;
    }
  }
  // Vulkan prefers Mailbox, every other backend Immediate. Under window capture the Vulkan driver
  // cannot flip and Immediate leaks about a megabyte per present until the device is lost.
  const bool preferMailbox = g_backendType == wgpu::BackendType::Vulkan;
  if (preferMailbox && supports(wgpu::PresentMode::Mailbox)) {
    return wgpu::PresentMode::Mailbox;
  }
  if (supports(wgpu::PresentMode::Immediate)) {
    return wgpu::PresentMode::Immediate;
  }
  if (g_backendType != wgpu::BackendType::Metal && supports(wgpu::PresentMode::Mailbox)) {
    return wgpu::PresentMode::Mailbox;
  }
  // Mailbox is preferred over Fifo because Fifo caps presentation at the refresh rate and every slot
  // deadline after the first is missed. Reaching this means neither is offered, so say so loudly.
  Log.warn("Surface supports neither Immediate nor Mailbox; falling back to Fifo. Presentation "
           "is capped at the display refresh rate, so the game may run slower than its own "
           "pace and frame interpolation cannot exceed the refresh rate.");
  return wgpu::PresentMode::Fifo;
}

wgpu::TextureFormat to_linear(wgpu::TextureFormat format) {
  if (format == wgpu::TextureFormat::RGBA8UnormSrgb) {
    return wgpu::TextureFormat::RGBA8Unorm;
  }
  if (format == wgpu::TextureFormat::BGRA8UnormSrgb) {
    return wgpu::TextureFormat::BGRA8Unorm;
  }
  return format;
}

wgpu::TextureFormat best_surface_format() {
  if (g_surfaceCapabilities.formatCount == 0) {
    return wgpu::TextureFormat::Undefined;
  }
  for (size_t i = 0; i < g_surfaceCapabilities.formatCount; ++i) {
    const auto format = to_linear(g_surfaceCapabilities.formats[i]);
    if (format == wgpu::TextureFormat::RGBA8Unorm || format == wgpu::TextureFormat::BGRA8Unorm) {
      return format;
    }
  }
  return g_surfaceCapabilities.formats[0];
}

} // namespace

TextureWithSampler create_render_texture(uint32_t width, uint32_t height, bool multisampled) {
  const auto renderTargetSize = clamp_render_target_size(width, height);
  const wgpu::Extent3D size{
      .width = renderTargetSize.width,
      .height = renderTargetSize.height,
      .depthOrArrayLayers = 1,
  };
  const auto format = g_graphicsConfig.surfaceConfiguration.format;
  uint32_t sampleCount = 1;
  if (multisampled) {
    sampleCount = g_graphicsConfig.msaaSamples;
  }
  if (width == 0 || height == 0) {
    Log.fatal("Invalid render texture size! {}x{}, multisampled {}, format {}", width, height, static_cast<uint32_t>(format), multisampled);
  }
  const wgpu::TextureDescriptor textureDescriptor{
      .label = "Render texture",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc |
               wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = sampleCount,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);

  constexpr wgpu::TextureViewDescriptor viewDescriptor{
      .label = "Render texture view",
      .dimension = wgpu::TextureViewDimension::e2D,
  };
  auto view = texture.CreateView(&viewDescriptor);

  constexpr wgpu::SamplerDescriptor samplerDescriptor{
      .label = "Render sampler",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .addressModeW = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .mipmapFilter = wgpu::MipmapFilterMode::Linear,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 1000.f,
      .maxAnisotropy = 1,
  };
  auto sampler = g_device.CreateSampler(&samplerDescriptor);

  return {
      .texture = std::move(texture),
      .view = std::move(view),
      .size = size,
      .format = format,
      .sampler = std::move(sampler),
  };
}

const TextureWithSampler& present_source() noexcept {
  return g_graphicsConfig.msaaSamples > 1 ? g_frameBufferResolved : g_frameBuffer;
}

PresentSource current_present_source() noexcept {
  if (g_presentSourceOverrideActive && g_presentSourceOverrideBindGroup != nullptr) {
    return {
        .bindGroup = g_presentSourceOverrideBindGroup,
        .texture = g_presentSourceOverrideTexture,
        .size = g_presentSourceOverrideSize,
        .format = g_presentSourceOverrideFormat,
    };
  }

  return {
      .bindGroup = g_CopyBindGroup,
      .texture = present_source().texture,
      .size = present_source().size,
      .format = present_source().format,
  };
}

void set_present_source_override(wgpu::BindGroup bindGroup, wgpu::Texture texture, wgpu::Extent3D size,
                                 wgpu::TextureFormat format) noexcept {
  g_presentSourceOverrideBindGroup = std::move(bindGroup);
  g_presentSourceOverrideTexture = std::move(texture);
  g_presentSourceOverrideSize = size;
  g_presentSourceOverrideFormat = format;
  g_presentSourceOverrideActive = true;
}

void clear_present_source_override() noexcept {
  g_presentSourceOverrideActive = false;
  g_presentSourceOverrideBindGroup = {};
  g_presentSourceOverrideTexture = {};
  g_presentSourceOverrideSize = {};
  g_presentSourceOverrideFormat = wgpu::TextureFormat::Undefined;
}

Viewport calculate_present_viewport_for_aspect(uint32_t surface_width, uint32_t surface_height,
                                               float content_aspect) noexcept {
  if (surface_width == 0 || surface_height == 0 || !(content_aspect > 0.f)) {
    return {};
  }

  uint32_t viewport_width = surface_width;
  uint32_t viewport_height = std::min<uint32_t>(
      surface_height, std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(static_cast<double>(viewport_width) *
                                                                               static_cast<double>(1.f / content_aspect)))));
  if (viewport_height == surface_height) {
    viewport_width = std::min<uint32_t>(
        surface_width, std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(static_cast<double>(viewport_height) *
                                                                                static_cast<double>(content_aspect)))));
  }

  return {
      .left = static_cast<float>((surface_width - viewport_width) / 2),
      .top = static_cast<float>((surface_height - viewport_height) / 2),
      .width = static_cast<float>(viewport_width),
      .height = static_cast<float>(viewport_height),
      .znear = 0.f,
      .zfar = 1.f,
  };
}

Viewport calculate_present_viewport(uint32_t surface_width, uint32_t surface_height, uint32_t content_width,
                                    uint32_t content_height) noexcept {
  if (content_width == 0 || content_height == 0) {
    return {};
  }
  return calculate_present_viewport_for_aspect(
      surface_width, surface_height, static_cast<float>(content_width) / static_cast<float>(content_height));
}

static TextureWithSampler create_depth_texture(uint32_t width, uint32_t height) {
  const auto renderTargetSize = clamp_render_target_size(width, height);
  const wgpu::Extent3D size{
      .width = renderTargetSize.width,
      .height = renderTargetSize.height,
      .depthOrArrayLayers = 1,
  };
  const auto format = g_graphicsConfig.depthFormat;
  const wgpu::TextureDescriptor textureDescriptor{
      .label = "Depth texture",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = g_graphicsConfig.msaaSamples,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);

  const wgpu::TextureViewDescriptor viewDescriptor{
      .label = "Depth texture view",
      .dimension = wgpu::TextureViewDimension::e2D,
  };
  auto view = texture.CreateView(&viewDescriptor);

  const wgpu::SamplerDescriptor samplerDescriptor{
      .label = "Depth sampler",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .addressModeW = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .mipmapFilter = wgpu::MipmapFilterMode::Linear,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 1000.f,
      .maxAnisotropy = 1,
  };
  auto sampler = g_device.CreateSampler(&samplerDescriptor);

  return {
      .texture = std::move(texture),
      .view = std::move(view),
      .size = size,
      .format = format,
      .sampler = std::move(sampler),
  };
}

void create_copy_pipeline() {
  wgpu::ShaderSourceWGSL sourceDescriptor{};
  sourceDescriptor.code = R"""(
@group(0) @binding(0)
var efb_sampler: sampler;
@group(0) @binding(1)
var efb_texture: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -3.0),
    vec2(3.0, 1.0),
);
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(0.0, 0.0),
    vec2(0.0, 2.0),
    vec2(2.0, 0.0),
);

fn fullscreen(vtxIdx: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[vtxIdx], 0.0, 1.0);
    out.uv = uvs[vtxIdx];
    return out;
}

@vertex
fn vs_main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    return fullscreen(vtxIdx);
}

// Pre-rotated copies into a swapchain in the panel's orientation: uvs are the target's own
// coordinates, turned back into the picture's.
@vertex
fn vs_rot90(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    var out = fullscreen(vtxIdx);
    out.uv = vec2(out.uv.y, 1.0 - out.uv.x);
    return out;
}

@vertex
fn vs_rot180(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    var out = fullscreen(vtxIdx);
    out.uv = vec2(1.0 - out.uv.x, 1.0 - out.uv.y);
    return out;
}

@vertex
fn vs_rot270(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    var out = fullscreen(vtxIdx);
    out.uv = vec2(1.0 - out.uv.y, out.uv.x);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let color = textureSample(efb_texture, efb_sampler, in.uv);
    return vec4(color.rgb, 1.0);
}
)""";
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &sourceDescriptor,
      .label = "XFB Copy Module",
  };
  auto module = g_device.CreateShaderModule(&moduleDescriptor);
  const std::array colorTargets{wgpu::ColorTargetState{
      .format = g_graphicsConfig.surfaceConfiguration.format,
      .writeMask = wgpu::ColorWriteMask::All,
  }};
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  const std::array bindGroupLayoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler =
              wgpu::SamplerBindingLayout{
                  .type = wgpu::SamplerBindingType::Filtering,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
      .entryCount = bindGroupLayoutEntries.size(),
      .entries = bindGroupLayoutEntries.data(),
  };
  g_CopyBindGroupLayout = g_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);
  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_CopyBindGroupLayout,
  };
  auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);
  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .layout = pipelineLayout,
      .vertex =
          wgpu::VertexState{
              .module = module,
              .entryPoint = "vs_main",
          },
      .primitive =
          wgpu::PrimitiveState{
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .multisample =
          wgpu::MultisampleState{
              .count = 1,
              .mask = UINT32_MAX,
          },
      .fragment = &fragmentState,
  };
  g_CopyPipeline = g_device.CreateRenderPipeline(&pipelineDescriptor);
  if (vulkan_backend_active()) {
    constexpr std::array<const char*, 3> rotatedEntries{"vs_rot90", "vs_rot180", "vs_rot270"};
    for (size_t i = 0; i < rotatedEntries.size(); ++i) {
      auto rotated = pipelineDescriptor;
      rotated.vertex.entryPoint = rotatedEntries[i];
      g_CopyPipelineRotated[i] = g_device.CreateRenderPipeline(&rotated);
    }
  }
}

int surface_rotation() {
#if defined(__ANDROID__) && defined(AURORA_VULKAN_BACKEND)
  return vulkan_backend_active() ? aurora_vk_surface_rotation() : 0;
#else
  return 0;
#endif
}

wgpu::RenderPipeline surface_copy_pipeline(int rotation) {
  const size_t index = static_cast<size_t>(rotation / 90);
  if (index >= 1 && index <= g_CopyPipelineRotated.size() && g_CopyPipelineRotated[index - 1]) {
    return g_CopyPipelineRotated[index - 1];
  }
  return g_CopyPipeline;
}

wgpu::BindGroup create_copy_bind_group(wgpu::TextureView sourceView, wgpu::Sampler sampler) {
  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .sampler = sampler,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .textureView = sourceView,
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = g_CopyBindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  return g_device.CreateBindGroup(&bindGroupDescriptor);
}

wgpu::BindGroup create_copy_bind_group(const TextureWithSampler& source) {
  return create_copy_bind_group(source.view, source.sampler);
}

static wgpu::BackendType to_wgpu_backend(AuroraBackend backend) {
  switch (backend) {
  case BACKEND_WEBGPU:
    return wgpu::BackendType::WebGPU;
  case BACKEND_D3D11:
    return wgpu::BackendType::D3D11;
  case BACKEND_D3D12:
    return wgpu::BackendType::D3D12;
  case BACKEND_METAL:
    return wgpu::BackendType::Metal;
  case BACKEND_VULKAN:
    return wgpu::BackendType::Vulkan;
  case BACKEND_OPENGL:
    return wgpu::BackendType::OpenGL;
  case BACKEND_OPENGLES:
    return wgpu::BackendType::OpenGLES;
  default:
    return wgpu::BackendType::Null;
  }
}

static bool create_surface() {
  SDL_Window* window = window::get_sdl_window();
  if (window == nullptr) {
    Log.error("Failed to create surface: no window");
    return false;
  }
  const auto chainedDescriptor = utils::SetupWindowAndGetSurfaceDescriptor(window);
  if (!chainedDescriptor) {
    Log.error("Failed to create surface descriptor for current window");
    return false;
  }
  const wgpu::SurfaceDescriptor surfaceDescriptor{
      .nextInChain = chainedDescriptor.get(),
      .label = "Surface",
  };
  release_surface();
  g_surface = g_instance.CreateSurface(&surfaceDescriptor);
  if (!g_surface) {
    Log.error("Failed to create surface");
    return false;
  }
  return true;
}

static bool initialize_once(AuroraBackend auroraBackend) {
  if (!g_instance) {
    Log.info("Creating WebGPU instance");
    const std::array requiredInstanceFeatures{
        wgpu::InstanceFeatureName::TimedWaitAny,
    };
    wgpu::InstanceDescriptor instanceDescriptor{
        .requiredFeatureCount = requiredInstanceFeatures.size(),
        .requiredFeatures = requiredInstanceFeatures.data(),
    };
#if defined(WEBGPU_DAWN) && !defined(__MINGW32__)
    // DawnNative.h's C++ constructor has an MSVC ABI that cannot cross into llvm-mingw, and the
    // descriptor only restates Dawn's defaults, so use the public WebGPU descriptor here.
    dawn::native::DawnInstanceDescriptor dawnInstanceDescriptor;
    dawnInstanceDescriptor.backendValidationLevel = dawn::native::BackendValidationLevel::Disabled;
    instanceDescriptor.nextInChain = &dawnInstanceDescriptor;
#endif
    g_instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!g_instance) {
      Log.error("Failed to create WebGPU instance");
      return false;
    }
  }
  const wgpu::BackendType backend = to_wgpu_backend(auroraBackend);
  Log.info("Attempting to initialize {}", magic_enum::enum_name(backend));
  // One call is one backend attempt. aurora::initialize() retries without calling shutdown(), so a
  // leftover adapter would pass the `if (!g_adapter)` guard and mismatch adapter with device.
  g_queue = {};
  g_device = {};
  g_deviceLostReason.store(wgpu::DeviceLostReason::Unknown, std::memory_order_relaxed);
  g_deviceLost.store(false, std::memory_order_release);
  g_adapter = {};
  g_backendType = wgpu::BackendType::Undefined;
  {
    window::SurfaceLock surfaceLock;
    if (!create_surface()) {
      return false;
    }
  }
  load_gfx_flags();
  g_compatFeatureLevel = false;
  // Dawn marks some Vulkan drivers (for example Imagination PowerVR proprietary ones) as unable to
  // run core WebGPU and only exposes them at the compatibility feature level, so retry with it.
  for (const auto featureLevel : {wgpu::FeatureLevel::Core, wgpu::FeatureLevel::Compatibility}) {
    // OpenGL ES is a compatibility-level backend only.
    if (featureLevel == wgpu::FeatureLevel::Core && backend == wgpu::BackendType::OpenGLES) {
      continue;
    }
    if (featureLevel == wgpu::FeatureLevel::Compatibility && backend != wgpu::BackendType::OpenGLES &&
        (backend != wgpu::BackendType::Vulkan || gfx_flag("nocompat"))) {
      break;
    }
    if (featureLevel == wgpu::FeatureLevel::Core && backend == wgpu::BackendType::Vulkan &&
        gfx_flag("forcecompat")) {
      continue;
    }
    const wgpu::RequestAdapterOptions options{
        .featureLevel = featureLevel,
        .powerPreference = wgpu::PowerPreference::HighPerformance,
        .backendType = backend,
        .compatibleSurface = g_surface,
    };
    const auto future = g_instance.RequestAdapter(
        &options, wgpu::CallbackMode::WaitAnyOnly,
        [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
          if (status == wgpu::RequestAdapterStatus::Success) {
            g_adapter = std::move(adapter);
          } else {
            Log.warn("Adapter request failed: {}", message);
            const std::string_view reason{message};
            SDL_SetError("Graphics adapter unavailable: %.*s",
                         static_cast<int>(std::min<size_t>(reason.size(), 512)), reason.data());
          }
        });
    const auto status = g_instance.WaitAny(future, 5000000000);
    if (status != wgpu::WaitStatus::Success) {
      Log.error("Failed to create {} adapter: {}", magic_enum::enum_name(backend),
                magic_enum::enum_name(status));
      SDL_SetError("Graphics adapter request did not complete within its startup deadline");
      return false;
    }
    if (g_adapter) {
      if (featureLevel == wgpu::FeatureLevel::Compatibility) {
        g_compatFeatureLevel = true;
        Log.warn("{} adapter is only available at the compatibility feature level",
                 magic_enum::enum_name(backend));
      }
      break;
    }
  }
  if (!g_adapter) {
    Log.error("No {} adapter is available on this system", magic_enum::enum_name(backend));
    return false;
  }
  g_adapter.GetInfo(&g_adapterInfo);
  g_backendType = g_adapterInfo.backendType;
  const auto backendName = magic_enum::enum_name(g_backendType);
  auto adapterName = g_adapterInfo.device;
  if (adapterName.IsUndefined()) {
    adapterName = wgpu::StringView("Unknown");
  }
  g_adapterName = std::string{std::string_view{adapterName}};
  load_gfx_flags();
  choose_gpu_compat();
  auto description = g_adapterInfo.description;
  if (description.IsUndefined()) {
    description = wgpu::StringView("Unknown");
  }
  Log.info("Graphics adapter information\n  API: {}\n  Device: {} ({})\n  Driver: {}", backendName, adapterName,
           magic_enum::enum_name(g_adapterInfo.adapterType), description);

  uint32_t maxTextureDimension2D = 0;
  {
    wgpu::Limits supportedLimits{};
    wgpu::CompatibilityModeLimits supportedCompatLimits{};
    if (g_compatFeatureLevel) {
      supportedLimits.nextInChain = &supportedCompatLimits;
    }
    g_adapter.GetLimits(&supportedLimits);
    supportedLimits.nextInChain = nullptr;
    maxTextureDimension2D = supportedLimits.maxTextureDimension2D;
    wgpu::Limits requiredLimits{
        // Use "best" supported limits
        .maxTextureDimension1D = supportedLimits.maxTextureDimension1D == 0 ? WGPU_LIMIT_U32_UNDEFINED
                                                                            : supportedLimits.maxTextureDimension1D,
        .maxTextureDimension2D = supportedLimits.maxTextureDimension2D == 0 ? WGPU_LIMIT_U32_UNDEFINED
                                                                            : supportedLimits.maxTextureDimension2D,
        .maxTextureDimension3D = supportedLimits.maxTextureDimension3D == 0 ? WGPU_LIMIT_U32_UNDEFINED
                                                                            : supportedLimits.maxTextureDimension3D,
        .maxTextureArrayLayers = supportedLimits.maxTextureArrayLayers == 0 ? WGPU_LIMIT_U32_UNDEFINED
                                                                            : supportedLimits.maxTextureArrayLayers,
        .maxDynamicStorageBuffersPerPipelineLayout = supportedLimits.maxDynamicStorageBuffersPerPipelineLayout == 0
                                                         ? WGPU_LIMIT_U32_UNDEFINED
                                                         : supportedLimits.maxDynamicStorageBuffersPerPipelineLayout,
        .maxStorageBuffersPerShaderStage = supportedLimits.maxStorageBuffersPerShaderStage == 0
                                               ? WGPU_LIMIT_U32_UNDEFINED
                                               : supportedLimits.maxStorageBuffersPerShaderStage,
        .minUniformBufferOffsetAlignment =
            supportedLimits.minUniformBufferOffsetAlignment < 64 ? 64 : supportedLimits.minUniformBufferOffsetAlignment,
        .minStorageBufferOffsetAlignment =
            supportedLimits.minStorageBufferOffsetAlignment < 16 ? 16 : supportedLimits.minStorageBufferOffsetAlignment,
    };
    Log.info(
        "Using limits:"
        "\n  maxTextureDimension1D: {}"
        "\n  maxTextureDimension2D: {}"
        "\n  maxTextureDimension3D: {}"
        "\n  maxTextureArrayLayers: {}"
        "\n  maxDynamicStorageBuffersPerPipelineLayout: {}"
        "\n  maxStorageBuffersPerShaderStage: {}"
        "\n  minUniformBufferOffsetAlignment: {}"
        "\n  minStorageBufferOffsetAlignment: {}",
        requiredLimits.maxTextureDimension1D, requiredLimits.maxTextureDimension2D,
        requiredLimits.maxTextureDimension3D, requiredLimits.maxTextureArrayLayers,
        requiredLimits.maxDynamicStorageBuffersPerPipelineLayout, requiredLimits.maxStorageBuffersPerShaderStage,
        requiredLimits.minUniformBufferOffsetAlignment, requiredLimits.minStorageBufferOffsetAlignment);
    // Compatibility mode allows no storage buffers or textures outside compute unless asked for;
    // the GX vertex fetch reads its vertex data from storage buffers in the vertex stage.
    wgpu::CompatibilityModeLimits requiredCompatLimits{};
    if (g_compatFeatureLevel) {
      requiredCompatLimits.maxStorageBuffersInVertexStage = supportedCompatLimits.maxStorageBuffersInVertexStage;
      requiredCompatLimits.maxStorageTexturesInVertexStage = supportedCompatLimits.maxStorageTexturesInVertexStage;
      requiredCompatLimits.maxStorageBuffersInFragmentStage = supportedCompatLimits.maxStorageBuffersInFragmentStage;
      requiredCompatLimits.maxStorageTexturesInFragmentStage =
          supportedCompatLimits.maxStorageTexturesInFragmentStage;
      requiredLimits.nextInChain = &requiredCompatLimits;
      Log.info("Compatibility limits: storage buffers vertex {} fragment {}, storage textures vertex {} fragment {}",
               requiredCompatLimits.maxStorageBuffersInVertexStage,
               requiredCompatLimits.maxStorageBuffersInFragmentStage,
               requiredCompatLimits.maxStorageTexturesInVertexStage,
               requiredCompatLimits.maxStorageTexturesInFragmentStage);
      // The compatibility vertex fetch reads vertex data from storage buffers in the vertex shader; an
      // adapter with none there (Mali-G52 on OpenGL ES) fails to link every GX pipeline. Off instead.
      if (g_safeVertexFetch && requiredCompatLimits.maxStorageBuffersInVertexStage == 0) {
        g_safeVertexFetch = false;
        Log.warn("GPU compatibility vertex fetch off: {} has no vertex storage buffers", g_adapterName);
      }
      // Vertex pulling also reads storage buffers in the vertex stage, so every GX pipeline fails to
      // link there ("vertex shader storage blocks (1) > maximum (0)", Mali-G57 OpenGL ES r54p1,
      // 2026-11-08). Decode GX vertices on the CPU and use vertex inputs instead.
      if (requiredCompatLimits.maxStorageBuffersInVertexStage == 0 && !g_vertexLoader) {
        g_vertexLoader = true;
        g_indexPulling = false;
        Log.warn("GX vertex loader: CPU decode, vertex inputs ({} has no vertex storage buffers)", g_adapterName);
      }
    }
    std::vector<wgpu::FeatureName> requiredFeatures;
    // Optional native sharing for MetalFX. Devices without either feature keep
    // the normal renderer; the upscaler checks the enabled pair at runtime.
    if (backend == wgpu::BackendType::Metal &&
        g_adapter.HasFeature(wgpu::FeatureName::SharedTextureMemoryIOSurface) &&
        g_adapter.HasFeature(wgpu::FeatureName::SharedFenceMTLSharedEvent)) {
      requiredFeatures.push_back(wgpu::FeatureName::SharedTextureMemoryIOSurface);
      requiredFeatures.push_back(wgpu::FeatureName::SharedFenceMTLSharedEvent);
    }
    bool implicitDeviceSynchronizationSupported = false;
    wgpu::SupportedFeatures supportedFeatures;
    g_adapter.GetFeatures(&supportedFeatures);
    for (size_t i = 0; i < supportedFeatures.featureCount; ++i) {
      const auto feature = supportedFeatures.features[i];
      if (feature == wgpu::FeatureName::TextureCompressionBC && !gfx_flag("nobc")) {
        g_bcTexturesSupported = true;
        requiredFeatures.push_back(feature);
      }
      // The presenter calls device and queue methods while the frame worker encodes, which Dawn only
      // supports with this feature; without it the two race inside the device's dynamic uploader.
      if (feature == wgpu::FeatureName::ImplicitDeviceSynchronization) {
        implicitDeviceSynchronizationSupported = true;
        requiredFeatures.push_back(feature);
      }
    }
    if (!implicitDeviceSynchronizationSupported) {
      Log.warn(
          "Adapter does not support ImplicitDeviceSynchronization; multi-threaded presentation is "
          "not safe on this device.");
    }
#ifdef WEBGPU_DAWN
    wgpu::DawnCacheDeviceDescriptor cacheDescriptor({
        .isolationKey = nullptr,
        .loadDataFunction = load_from_cache,
        .storeDataFunction = store_to_cache,
        .functionUserdata = nullptr,
    });

    std::vector<const char*> enableToggles{
    /* clang-format off */
#if _WIN32
      "use_dxc",
#ifndef NDEBUG
      "emit_hlsl_debug_symbols",
#endif
#endif
#ifndef ANDROID
      "use_user_defined_labels_in_backend",
#endif
      "disable_symbol_renaming",
      "enable_immediate_error_handling",
        /* clang-format on */
    };
#ifdef NDEBUG
    if (!gfx_flag("validate")) {
      enableToggles.push_back("skip_validation");
    }
    if (!gfx_flag("robust")) {
      enableToggles.push_back("disable_robustness");
    }
#endif
    if (g_backendType == wgpu::BackendType::Vulkan && !gfx_flag("nomono")) {
      enableToggles.push_back("vulkan_monolithic_pipeline_cache");
    }
    // aurora drives the device from several threads (frame worker, presenter, pipeline workers, buffer
    // maps); a GL context left current on one makes eglMakeCurrent fail on the next (EGL_BAD_ACCESS).
    if (g_backendType == wgpu::BackendType::OpenGLES || g_backendType == wgpu::BackendType::OpenGL) {
      enableToggles.push_back("gl_allow_context_on_multi_threads");
      enableToggles.push_back("shadow_copy_map_write");
    }
    std::vector<const char*> disableToggles;
    for (const auto& flag : g_gfxFlags) {
      if (flag.size() > 1 && flag.front() == '+') {
        enableToggles.push_back(flag.c_str() + 1);
      } else if (flag.size() > 1 && flag.front() == '-') {
        disableToggles.push_back(flag.c_str() + 1);
      }
    }
    g_logValidationErrors.store(gfx_flag("validate"), std::memory_order_relaxed);
    if (!g_gfxFlags.empty()) {
      std::string joined;
      for (const auto& flag : g_gfxFlags) {
        joined += joined.empty() ? "" : ",";
        joined += flag;
      }
      Log.info("Graphics debug flags: {}", joined);
    }
    const wgpu::DawnTogglesDescriptor togglesDescriptor({
        .nextInChain = &cacheDescriptor,
        .enabledToggleCount = enableToggles.size(),
        .enabledToggles = enableToggles.data(),
        .disabledToggleCount = disableToggles.size(),
        .disabledToggles = disableToggles.data(),
    });
#endif
    wgpu::DeviceDescriptor deviceDescriptor;
#ifdef WEBGPU_DAWN
    deviceDescriptor.nextInChain = &togglesDescriptor;
#endif
    deviceDescriptor.requiredFeatureCount = requiredFeatures.size();
    deviceDescriptor.requiredFeatures = requiredFeatures.data();
    deviceDescriptor.requiredLimits = &requiredLimits;
    deviceDescriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device& device, wgpu::ErrorType type, wgpu::StringView message) {
          if (g_logValidationErrors.load(std::memory_order_relaxed)) {
            static std::atomic<uint32_t> s_logged{0};
            if (s_logged.fetch_add(1, std::memory_order_relaxed) < 200) {
              Log.error("WebGPU error {}: {}", underlying(type), message);
            }
          } else if (g_initialized.load(std::memory_order_acquire)) {
            FATAL("WebGPU error {}: {}", underlying(type), message);
          } else {
            Log.warn("WebGPU error {}: {}", underlying(type), message);
          }
        });
    deviceDescriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
                                           [](const wgpu::Device& device, wgpu::DeviceLostReason reason,
                                              wgpu::StringView message) {
                                             (void)device;
                                             // Shutdown and backend retry release the final
                                             // device reference here too, not a real failure.
                                             if (reason == wgpu::DeviceLostReason::Destroyed) {
                                               return;
                                             }
                                             // Via string_view, so Dawn resolves a
                                             // WGPU_STRLEN length instead of SIZE_MAX.
                                             const std::string_view text{message};
                                             const size_t copied =
                                                 std::min(text.size(), g_deviceLostMessage.size() - 1);
                                             if (copied > 0) {
                                               std::memcpy(g_deviceLostMessage.data(), text.data(), copied);
                                             }
                                             g_deviceLostMessage[copied] = '\0';
                                             g_deviceLostReason.store(reason, std::memory_order_relaxed);
                                             g_deviceLost.store(true, std::memory_order_release);
                                           });
    const auto future =
        g_adapter.RequestDevice(&deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
                                [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
                                  if (status == wgpu::RequestDeviceStatus::Success) {
                                    g_device = std::move(device);
                                  } else {
                                    Log.warn("Device request failed: {}", message);
                                    const std::string_view reason{message};
                                    SDL_SetError("Graphics device unavailable: %.*s",
                                        static_cast<int>(std::min<size_t>(reason.size(), 512)), reason.data());
                                  }
                                });
    const auto status = g_instance.WaitAny(future, 5000000000);
    if (status != wgpu::WaitStatus::Success) {
      Log.error("Failed to create device: {}", magic_enum::enum_name(status));
      SDL_SetError("Graphics device request did not complete within its startup deadline");
      return false;
    }
    if (!g_device) {
      return false;
    }
    if (vulkan_backend_active() && g_bcTexturesSupported && !vulkan_backend_has_bc()) {
      g_bcTexturesSupported = false;
    }
    Log.info("BC textures: {}", g_bcTexturesSupported ? "supported" : "not supported");
#if defined(WEBGPU_DAWN) && !defined(__MINGW32__)
    {
      // Dawn picks its own workarounds from the adapter (vendor, driver, extensions); record them.
      std::string toggles;
      for (const char* toggle : vulkan_backend_active() ? std::vector<const char*>{}
                                                        : dawn::native::GetTogglesUsed(g_device.Get())) {
        toggles += toggles.empty() ? "" : " ";
        toggles += toggle;
      }
      Log.info("Dawn toggles in use: {}", toggles);
    }
#endif
    g_device.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message) {
      AuroraLogLevel level = LOG_FATAL;
      switch (type) {
      case wgpu::LoggingType::Verbose:
        level = LOG_DEBUG;
        break;
      case wgpu::LoggingType::Info:
        level = LOG_INFO;
        break;
      case wgpu::LoggingType::Warning:
        level = LOG_WARNING;
        break;
      case wgpu::LoggingType::Error:
        level = LOG_ERROR;
        break;
      default:
        break;
      }
      Log.report(level, "WebGPU message: {}", message);
    });
  }
  g_queue = g_device.GetQueue();

  const wgpu::Status status = g_surface.GetCapabilities(g_adapter, &g_surfaceCapabilities);
  if (status != wgpu::Status::Success) {
    Log.error("Failed to get surface capabilities: {}", magic_enum::enum_name(status));
    return false;
  }
  if (g_surfaceCapabilities.formatCount == 0) {
    Log.error("Surface has no formats");
    return false;
  }
  if (g_surfaceCapabilities.presentModeCount == 0) {
    Log.error("Surface has no present modes");
    return false;
  }
  auto surfaceFormat = best_surface_format();
  auto presentMode = best_present_mode();
  Log.info("Using surface format {}, present mode {}", magic_enum::enum_name(surfaceFormat),
           magic_enum::enum_name(presentMode));
  const auto size = window::get_window_size();
  g_graphicsConfig = GraphicsConfig{
      .surfaceConfiguration =
          wgpu::SurfaceConfiguration{
              .format = surfaceFormat,
              .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc,
              .width = size.native_fb_width,
              .height = size.native_fb_height,
              .presentMode = presentMode,
          },
      .depthFormat = wgpu::TextureFormat::Depth32Float,
      .msaaSamples = g_config.msaa,
      .textureAnisotropy = g_config.maxTextureAnisotropy,
      .maxTextureDimension2D = maxTextureDimension2D,
  };
  create_copy_pipeline();
  {
    window::SurfaceLock surfaceLock;
    resize_swapchain(size.fb_width, size.fb_height, size.native_fb_width, size.native_fb_height, true);
  }
  g_initialized.store(true, std::memory_order_release);
  return true;
}

static void reset_instance() {
  g_queue = {};
  g_device = {};
  g_adapter = {};
  {
    window::SurfaceLock surfaceLock;
    release_surface();
  }
  g_instance = {};
}

// Two WebGPU implementations share libmain on Android (lib/vulkan/wgpu_dispatch.cpp): aurora's own
// Vulkan backend (lib/vulkan/vk_webgpu.cpp, including tier 0 VkRenderPass + fences for Vulkan 1.1
// devices without dynamic rendering) and Dawn. The order:
//   default (Auto / Vulkan): aurora's Vulkan backend first, then Dawn as fallback;
//   BASARA_RENDERER=dawn (Renderer Dawn, or debug flag `dawn`): Dawn first, then aurora's Vulkan backend;
//   BASARA_RENDERER=gles (Adreno 5xx on Android 10): Dawn's OpenGL ES backend first, then Vulkan;
//   a player driver that neither starts is dropped, and the order runs again on the system driver.
bool initialize(AuroraBackend auroraBackend) {
  const bool custom = custom_driver::begin();
  load_gfx_flags();
#if defined(__ANDROID__) && defined(AURORA_VULKAN_BACKEND)
  const char* renderer = std::getenv("BASARA_RENDERER");
  const bool dawnFirst =
      gfx_flag("dawn") || (renderer != nullptr && std::strcmp(renderer, "dawn") == 0);
  const bool vulkanTarget = auroraBackend == BACKEND_VULKAN || auroraBackend == BACKEND_AUTO;
  std::vector<bool> order;
  if (vulkanTarget) {
    order = dawnFirst ? std::vector<bool>{false, true} : std::vector<bool>{true, false};
  } else {
    order = {false};
  }
#else
  const std::vector<bool> order{false};
#endif
#if defined(__ANDROID__)
  // BASARA_RENDERER=gles (the launcher sets it for Adreno 5xx on Android 10 only, whose Vulkan driver
  // loses the device): Dawn's OpenGL ES backend first, then the usual Vulkan order if it cannot start.
  // Debug flag `gles` forces it on any phone, `nogles` turns it off.
  const char* rendererEnv = std::getenv("BASARA_RENDERER");
  const bool glesFirst = !custom && !gfx_flag("nogles") &&
                         (gfx_flag("gles") || (rendererEnv != nullptr && std::strcmp(rendererEnv, "gles") == 0));
  if (glesFirst && (auroraBackend == BACKEND_VULKAN || auroraBackend == BACKEND_AUTO)) {
    set_vulkan_backend(false);
    Log.info("Renderer: Dawn OpenGLES");
    if (initialize_once(BACKEND_OPENGLES)) {
      return true;
    }
    Log.warn("OpenGLES renderer could not start; trying Vulkan");
    reset_instance();
  }
#endif
  for (const bool vk : order) {
    set_vulkan_backend(vk);
    Log.info("Renderer: {}{}", vk ? "aurora Vulkan" : "Dawn", custom_driver::active() ? " (custom driver)" : "");
    if (initialize_once(auroraBackend)) {
      if (custom_driver::active()) {
        custom_driver::outcome(vk, true);
        custom_driver::running([] { return g_deviceLost.load(std::memory_order_acquire); });
      }
      return true;
    }
    custom_driver::outcome(vk, false);
    reset_instance();
  }
  set_vulkan_backend(false);
  if (!custom_driver::active()) {
    return false;
  }
  custom_driver::fail(std::string{"no renderer could start on it ("} + SDL_GetError() + ")");
  return initialize_once(auroraBackend);
}

void fail_if_device_lost() noexcept {
  if (!g_deviceLost.load(std::memory_order_acquire)) {
    return;
  }

  // Several frame-owning threads can observe loss, so serialize escalation: one thread logs and the
  // rest wait for termination instead of submitting more work to a lost device.
  static std::mutex fatalMutex;
  const std::lock_guard lock(fatalMutex);
  const auto reason = g_deviceLostReason.load(std::memory_order_relaxed);
  const char* const detail = g_deviceLostMessage.data();
  if (detail[0] != '\0') {
    Log.fatal("WebGPU device was lost ({}: {}). Rendering cannot continue safely; restart the application.",
              magic_enum::enum_name(reason), detail);
  } else {
    Log.fatal("WebGPU device was lost ({}). Rendering cannot continue safely; restart the application.",
              magic_enum::enum_name(reason));
  }
}

void serialize_pipeline_caches() noexcept {
#if defined(WEBGPU_DAWN) && defined(_WIN32)
  if (!g_device || g_backendType != wgpu::BackendType::Vulkan) {
    return;
  }
  using PerformIdleTasksFn = void(*)(const wgpu::Device*);
  static const auto performIdleTasks = []() -> PerformIdleTasksFn {
    const HMODULE dawnModule = GetModuleHandleW(L"webgpu_dawn.dll");
    if (dawnModule == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<PerformIdleTasksFn>(reinterpret_cast<void*>(
        GetProcAddress(dawnModule, "?PerformIdleTasks@native@dawn@@YAXAEBVDevice@wgpu@@@Z")));
  }();
  if (performIdleTasks != nullptr) {
    performIdleTasks(&g_device);
  }
#elif defined(WEBGPU_DAWN)
  // NSMBW: Android links Dawn statically, so call it directly. Without this the monolithic
  // VkPipelineCache was never written there and every launch recompiled every pipeline in the
  // driver (773 pipelines, ~16 s on an Adreno 710) despite the blob cache hitting.
  if (!g_device || g_backendType != wgpu::BackendType::Vulkan) {
    return;
  }
#if defined(__ANDROID__) && defined(AURORA_VULKAN_BACKEND)
  if (vulkan_backend_active()) {
    aurora_vk_save_pipeline_cache();
    return;
  }
#endif
  const auto start = std::chrono::steady_clock::now();
  dawn::native::PerformIdleTasks(g_device);
  Log.info("Saved the Vulkan pipeline cache in {} ms",
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
#endif
}

struct CachedEfbTargets {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t msaaSamples = 0;
  wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined;
  wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined;
  TextureWithSampler frameBuffer;
  TextureWithSampler frameBufferResolved;
  TextureWithSampler depthBuffer;
  wgpu::BindGroup copyBindGroup;
};
static std::vector<CachedEfbTargets> g_efbTargetCache;

void shutdown() {
  serialize_pipeline_caches();
  g_initialized.store(false, std::memory_order_release);
  fsr::shutdown();
  sgsr::shutdown();
  g_efbTargetCache.clear();
  g_CopyBindGroupLayout = {};
  g_CopyPipeline = {};
  g_CopyPipelineRotated = {};
  g_CopyBindGroup = {};
  g_frameBuffer = {};
  g_frameBufferResolved = {};
  g_depthBuffer = {};
  g_queue = {};
  g_surface = {};
  g_device = {};
  g_adapter = {};
  g_instance = {};

  cache_shutdown();
}

void release_surface() noexcept {
  const bool hadSurface = static_cast<bool>(g_surface);
  if (g_surface) {
    g_surface.Unconfigure();
  }
  g_surface = {};
  if (hadSurface && g_instance && g_device && g_queue) {
    const auto future = g_queue.OnSubmittedWorkDone(
        wgpu::CallbackMode::WaitAnyOnly, [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
    g_instance.WaitAny(future, 1000000000);
  }
}

bool refresh_surface(bool recreate) {
  if (!g_instance || !g_device) {
    return false;
  }
  if (!window::is_presentable()) {
    release_surface();
    return false;
  }
  if ((!g_surface || recreate) && !create_surface()) {
    return false;
  }
  uint32_t width = g_graphicsConfig.surfaceConfiguration.width;
  uint32_t height = g_graphicsConfig.surfaceConfiguration.height;
  uint32_t native_width = width;
  uint32_t native_height = height;
  if (window::get_sdl_window() != nullptr) {
    const auto size = window::get_window_size();
    width = size.fb_width;
    height = size.fb_height;
    native_width = size.native_fb_width;
    native_height = size.native_fb_height;
  }
  if (width != 0 && height != 0) {
    resize_swapchain(width, height, native_width, native_height, true);
  }
  return true;
}

void request_vsync(bool enabled) noexcept {
  if (!g_initialized.load(std::memory_order_acquire)) {
    g_vsyncEnabled.store(enabled, std::memory_order_release);  // the first configuration picks it up
    return;
  }
  g_vsyncRequest.store(enabled ? 1 : 0, std::memory_order_release);
}

bool vsync_enabled() noexcept { return g_vsyncEnabled.load(std::memory_order_acquire); }

bool vsync_change_pending() noexcept { return g_vsyncRequest.load(std::memory_order_acquire) >= 0; }

bool take_vsync_change() noexcept {
  const int request = g_vsyncRequest.exchange(-1, std::memory_order_acq_rel);
  if (request < 0) {
    return false;
  }
  g_vsyncEnabled.store(request == 1, std::memory_order_release);
  if (!g_surface || !g_device) {
    return false;
  }
  const auto mode = best_present_mode();
  if (mode == g_graphicsConfig.surfaceConfiguration.presentMode) {
    return false;
  }
  Log.info("Present mode {} (V-Sync {})", magic_enum::enum_name(mode), request == 1 ? "on" : "off");
  g_graphicsConfig.surfaceConfiguration.presentMode = mode;
  return true;
}

void resize_swapchain(uint32_t width, uint32_t height, uint32_t native_width, uint32_t native_height, bool force) {
  if (!g_surface || !g_device || width == 0 || height == 0 || native_height == 0 || native_width == 0) {
    return;
  }

  uint32_t render_width = width;
  uint32_t render_height = height;
  const auto [efbWidth, efbHeight] = vi::configured_fb_size();
  // A line target sizes the render target exactly, including below 1x; otherwise the EFB
  // workspace is the floor.
  if (efbWidth != 0 && efbHeight != 0 && !window::frame_buffer_line_target()) {
    render_width = std::max(render_width, efbWidth);
    render_height = std::max(render_height, efbHeight);
  }

  const auto requestedRenderSize = RenderTargetSize{render_width, render_height};
  const auto clampedRenderSize = clamp_frame_buffer_size(render_width, render_height);
  render_width = clampedRenderSize.width;
  render_height = clampedRenderSize.height;
  if (requestedRenderSize.width != render_width || requestedRenderSize.height != render_height) {
    Log.warn(
        "Render target {}x{} exceeds the safe framebuffer budget (adapter max {}, practical max {} / {} "
        "pixels); clamping to {}x{}",
        requestedRenderSize.width, requestedRenderSize.height, g_graphicsConfig.maxTextureDimension2D,
        render_size_limits::kMaxFramebufferDimension, render_size_limits::kMaxFramebufferPixels, render_width,
        render_height);
  }

  const bool sizeChanged = g_graphicsConfig.surfaceConfiguration.width != native_width ||
                           g_graphicsConfig.surfaceConfiguration.height != native_height ||
                           g_frameBuffer.size.width != render_width || g_frameBuffer.size.height != render_height;
  if (!force && !sizeChanged) {
    return;
  }
  if (sizeChanged) {
    gx::clear_display_copy_cache();
    gfx::clear_caches();
    clear_present_source_override();
    Log.info("Render target {}x{} for a {}x{} surface", render_width, render_height, native_width,
             native_height);
  }
  // Only a new surface size (or a forced reconfigure) recreates the swapchain. A render-size change
  // alone (dynamic resolution) used to reconfigure it too; on Adreno 610 (2021 driver) one of those
  // dozens of swapchain rebuilds ended in VK_ERROR_DEVICE_LOST.
  const bool surfaceChanged = g_graphicsConfig.surfaceConfiguration.width != native_width ||
                              g_graphicsConfig.surfaceConfiguration.height != native_height;
  if (force || surfaceChanged) {
    g_graphicsConfig.surfaceConfiguration.width = native_width;
    g_graphicsConfig.surfaceConfiguration.height = native_height;
    auto surfaceConfiguration = g_graphicsConfig.surfaceConfiguration;
    surfaceConfiguration.device = g_device;
    // Only the swapchain is narrower; the present pass keeps the native size and the right edge clips.
    if (g_alignSwapchainWidth32 && surfaceConfiguration.width >= 64) {
      surfaceConfiguration.width &= ~31u;
    }
    g_surface.Configure(&surfaceConfiguration);
  }
  if (!sizeChanged) {
    // Forced reconfigure at an unchanged size (present-mode change or recreated surface). The
    // offscreen targets are not swapchain images, so reallocating them would only stall the frame.
    return;
  }
  const auto colorFormat = g_graphicsConfig.surfaceConfiguration.format;
  const auto depthFormat = g_graphicsConfig.depthFormat;
  const uint32_t msaaSamples = g_graphicsConfig.msaaSamples;
  for (const auto& entry : g_efbTargetCache) {
    if (entry.width == render_width && entry.height == render_height && entry.msaaSamples == msaaSamples &&
        entry.colorFormat == colorFormat && entry.depthFormat == depthFormat) {
      g_frameBuffer = entry.frameBuffer;
      g_frameBufferResolved = entry.frameBufferResolved;
      g_depthBuffer = entry.depthBuffer;
      g_CopyBindGroup = entry.copyBindGroup;
      return;
    }
  }
  g_frameBuffer = create_render_texture(render_width, render_height, true);
  g_frameBufferResolved = msaaSamples > 1 ? create_render_texture(render_width, render_height, false) : TextureWithSampler{};
  g_depthBuffer = create_depth_texture(render_width, render_height);
  g_CopyBindGroup = create_copy_bind_group(present_source());
  if (g_efbTargetCache.size() >= 4) {
    g_efbTargetCache.erase(g_efbTargetCache.begin());
  }
  g_efbTargetCache.push_back(CachedEfbTargets{
      .width = render_width,
      .height = render_height,
      .msaaSamples = msaaSamples,
      .colorFormat = colorFormat,
      .depthFormat = depthFormat,
      .frameBuffer = g_frameBuffer,
      .frameBufferResolved = g_frameBufferResolved,
      .depthBuffer = g_depthBuffer,
      .copyBindGroup = g_CopyBindGroup,
  });
}
} // namespace aurora::webgpu

extern "C" const char* AuroraAdapterSummary() {
  static std::string summary;
  if (summary.empty() && !aurora::webgpu::g_adapterName.empty()) {
    summary = aurora::webgpu::g_adapterName + " (" +
              std::string{magic_enum::enum_name(aurora::webgpu::g_backendType)} + ")";
  }
  return summary.c_str();
}
