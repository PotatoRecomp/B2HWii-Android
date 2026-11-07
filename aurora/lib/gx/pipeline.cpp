#include "pipeline.hpp"

#include "../webgpu/gpu.hpp"
#include "gx_fmt.hpp"
#include "shader_info.hpp"
#include "vertex_loader.hpp"
#include "tracy/Tracy.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>

#include <absl/container/flat_hash_map.h>

namespace aurora::gx {
static Module Log("aurora::gx");

namespace {
struct ShaderConfigHash {
  size_t operator()(const ShaderConfig& config) const noexcept { return static_cast<size_t>(xxh3_hash(config)); }
};

struct CachedShaderModule {
  std::condition_variable ready;
  wgpu::ShaderModule module;
  bool compiling = true;
};

std::mutex sShaderModuleCacheMutex;
absl::flat_hash_map<ShaderConfig, std::shared_ptr<CachedShaderModule>, ShaderConfigHash> sShaderModuleCache;

wgpu::ShaderModule cached_shader_module(const ShaderConfig& config) {
  std::shared_ptr<CachedShaderModule> entry;
  {
    std::unique_lock lock{sShaderModuleCacheMutex};
    const auto it = sShaderModuleCache.find(config);
    if (it == sShaderModuleCache.end()) {
      entry = std::make_shared<CachedShaderModule>();
      sShaderModuleCache.emplace(config, entry);
    } else {
      entry = it->second;
      entry->ready.wait(lock, [&] { return !entry->compiling; });
      return entry->module;
    }
  }

  auto module = build_shader(config);
  {
    std::lock_guard lock{sShaderModuleCacheMutex};
    entry->module = module;
    entry->compiling = false;
  }
  entry->ready.notify_all();
  return module;
}
} // namespace

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config) {
  ZoneScoped;
  const auto shader = cached_shader_module(config.shaderConfig);
  if (!config.shaderConfig.vtxInput) {
    return build_pipeline(config, {}, shader, "GX Pipeline");
  }
  const auto layout = native_vertex_layout(config.shaderConfig);
  std::array<wgpu::VertexAttribute, NativeVertexLayout::MaxLocations> attributes{};
  for (u32 i = 0; i < layout.locationCount; ++i) {
    wgpu::VertexFormat format = wgpu::VertexFormat::Uint8x4;
    switch (layout.locations[i].format) {
    case NativeVertexLayout::Uint8x4:
      format = wgpu::VertexFormat::Uint8x4;
      break;
    case NativeVertexLayout::Unorm8x4:
      format = wgpu::VertexFormat::Unorm8x4;
      break;
    case NativeVertexLayout::Float32x2:
      format = wgpu::VertexFormat::Float32x2;
      break;
    case NativeVertexLayout::Float32x3:
      format = wgpu::VertexFormat::Float32x3;
      break;
    }
    attributes[i] = wgpu::VertexAttribute{
        .format = format,
        .offset = layout.locations[i].offset,
        .shaderLocation = i,
    };
  }
  const std::array buffers{wgpu::VertexBufferLayout{
      .stepMode = wgpu::VertexStepMode::Vertex,
      .arrayStride = layout.stride,
      .attributeCount = layout.locationCount,
      .attributes = attributes.data(),
  }};
  return build_pipeline(config, buffers, shader, "GX Pipeline");
}

void clear_shader_module_cache() {
  std::lock_guard lock{sShaderModuleCacheMutex};
  sShaderModuleCache.clear();
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass, DrawEncodeState& state,
            bool requireReadyPipeline, const gfx::Range* uniformRangeOverride) {
  if (!gfx::bind_pipeline(data.pipeline, pass, state.currentPipeline, requireReadyPipeline)) {
    return;
  }

  // An interpolated presentation slot re-encodes the identical draw with only this range replaced; overriding here avoids copying the whole DrawData per draw per slot.
  const gfx::Range& uniformRange = uniformRangeOverride != nullptr ? *uniformRangeOverride : data.uniformRange;
  // Draws that reuse the previous draw's uniform block (gx/shader_info.cpp build_uniform) keep the binding.
  if (uniformRange.offset != state.uniformOffset) {
    const std::array offsets{uniformRange.offset};
    pass.SetBindGroup(1, gfx::g_uniformBindGroup, offsets.size(), offsets.data());
    state.uniformOffset = uniformRange.offset;
  }
  // Resolved when the draw was recorded; see GXBindGroups.
  if (data.bindGroups.resolvedTextureBindGroup != nullptr &&
      data.bindGroups.resolvedTextureBindGroup != state.boundTextureBindGroup) {
    wgpuRenderPassEncoderSetBindGroup(pass.Get(), 2, data.bindGroups.resolvedTextureBindGroup, 0, nullptr);
    state.boundTextureBindGroup = data.bindGroups.resolvedTextureBindGroup;
  }
  if (data.dstAlpha != UINT32_MAX) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
  }
  if (webgpu::g_indexPulling) {
    // The vertex shader reads index firstIndex + i itself (gx/shader.cpp, ibuf); base vertex is 0.
    pass.Draw(data.indexCount, data.instanceCount, static_cast<uint32_t>(data.idxRange.offset / sizeof(uint16_t)));
    return;
  }
  if (data.vtxStride != 0) {
    // Decoded vertices (gx/vertex_loader.hpp): the shared vertex buffer is bound once per pass and
    // each draw starts at its own vertex through baseVertex.
    if (!state.vertexBufferBound) {
      pass.SetVertexBuffer(0, gfx::g_vertexBuffer, 0, wgpu::kWholeSize);
      state.vertexBufferBound = true;
    }
    if (!state.indexBufferBound) {
      pass.SetIndexBuffer(gfx::g_indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
      state.indexBufferBound = true;
    }
    pass.DrawIndexed(data.indexCount, data.instanceCount,
                     static_cast<uint32_t>(data.idxRange.offset / sizeof(uint16_t)),
                     static_cast<int32_t>(data.vertRange.offset / data.vtxStride));
    return;
  }
  if (!state.indexBufferBound) {
    // Bound once for the pass; draws select their range with firstIndex below.
    pass.SetIndexBuffer(gfx::g_indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
    state.indexBufferBound = true;
  }
  pass.DrawIndexed(data.indexCount, data.instanceCount,
                   static_cast<uint32_t>(data.idxRange.offset / sizeof(uint16_t)));
}
} // namespace aurora::gx
