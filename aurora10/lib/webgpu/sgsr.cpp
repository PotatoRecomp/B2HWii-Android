// Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// SPDX-License-Identifier: BSD-3-Clause

#include "sgsr.hpp"

#include "../internal.hpp"

#include <cmath>
#include <cstring>

// A WGSL port of Qualcomm Snapdragon Game Super Resolution 1 (sgsr/v1/include/hlsl/sgsr1_mobile.h and
// sgsr/v1/include/glsl/sgsr1_shader_mobile.frag, BSD-3-Clause): single-pass spatial edge-aware upscale + sharpen
// with UseEdgeDirection enabled and OperationMode = 1 (RGBA, green channel as luma).

namespace aurora::webgpu::sgsr {
namespace {
Module Log("aurora::sgsr");

constexpr const char* kShaderSource = R"""(
struct Params {
    viewport_info: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv_img: vec4<f32>,
};

@group(0) @binding(0) var src_sampler: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(1) @binding(0) var<uniform> params: Params;

var<private> positions: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -3.0),
    vec2(3.0, 1.0),
);
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(0.0, 0.0),
    vec2(0.0, 2.0),
    vec2(2.0, 0.0),
);

@vertex
fn vs_main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    let uv = uvs[vtxIdx];
    var out: VertexOutput;
    out.pos = vec4<f32>(positions[vtxIdx], 0.0, 1.0);
    out.uv_img = vec4<f32>(uv, (uv * params.viewport_info.zw) + vec2<f32>(-0.5, 0.5));
    return out;
}

fn fast_lanczos4(x: vec4<f32>) -> vec4<f32> {
    var wA = x - vec4<f32>(4.0);
    let wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let edge_threshold = 8.0 / 255.0;
    let edge_sharpness = 2.0;

    let uv = in.uv_img.xy;
    let img_coord = in.uv_img.zw;
    let vp = params.viewport_info;

    var color = vec4<f32>(textureSampleLevel(src, src_sampler, uv, 0.0).xyz, 1.0);

    let img_coord_pixel = floor(img_coord);
    var coord = img_coord_pixel * vp.xy;
    let pl = img_coord - img_coord_pixel;
    var left = textureGather(1, src, src_sampler, coord);

    let edge_vote = abs(left.z - left.y) + abs(color.y - left.y) + abs(color.y - left.z);
    if (edge_vote > edge_threshold) {
        coord.x += vp.x;

        var right = textureGather(1, src, src_sampler, coord + vec2<f32>(vp.x, 0.0));
        var up_down = vec4<f32>(
            textureGather(1, src, src_sampler, coord + vec2<f32>(0.0, -vp.y)).wz,
            textureGather(1, src, src_sampler, coord + vec2<f32>(0.0, vp.y)).yx,
        );

        let mean = (left.y + left.z + right.x + right.w) * 0.25;
        left = left - vec4<f32>(mean);
        right = right - vec4<f32>(mean);
        up_down = up_down - vec4<f32>(mean);
        color.w = color.y - mean;

        let abs_l = abs(left);
        let abs_r = abs(right);
        let abs_u = abs(up_down);
        let sum = max(dot(abs_l + abs_r + abs_u, vec4<f32>(1.0)), 1e-5);
        let sum_mean = 10.14185 / sum;
        let std_dev = sum_mean * sum_mean;

        // 2x2 diagonal gradient edge direction (UseEdgeDirection from sgsr1_mobile.h)
        let rx_lz = right.x - left.z;
        let rw_ly = right.w - left.y;
        let delta = vec2<f32>(rx_lz + rw_ly, rx_lz - rw_ly);
        let dir = delta * inverseSqrt(dot(delta, delta) + 3.075740e-05);

        let dx = vec4<f32>(pl.x) + vec4<f32>(0.0, 1.0, -1.0, -2.0);
        let dy = vec4<f32>(pl.y) + vec4<f32>(0.0, 1.0, -1.0, -2.0);
        let dx2 = dx * dx;
        let dy2 = dy * dy;
        let dx_dir = dx * dir.y;
        let dy_dir = dy * dir.x;

        let ed_u = dx_dir.xzzx + dy_dir.yyww;
        let ed_l = dx_dir.yxxy + dy_dir.zzxx;
        let ed_r = dx_dir.zwwz + dy_dir.zzxx;

        let c_u = clamp(up_down * up_down * std_dev, vec4<f32>(0.0), vec4<f32>(1.0)) * 0.7 - vec4<f32>(1.0);
        let c_l = clamp(left * left * std_dev, vec4<f32>(0.0), vec4<f32>(1.0)) * 0.7 - vec4<f32>(1.0);
        let c_r = clamp(right * right * std_dev, vec4<f32>(0.0), vec4<f32>(1.0)) * 0.7 - vec4<f32>(1.0);

        let w_u = fast_lanczos4(dx2.xzzx + dy2.yyww + ed_u * ed_u * c_u);
        let w_l = fast_lanczos4(dx2.yxxy + dy2.zzxx + ed_l * ed_l * c_l);
        let w_r = fast_lanczos4(dx2.zwwz + dy2.zzxx + ed_r * ed_r * c_r);

        let weight_sum = dot(w_u + w_l + w_r, vec4<f32>(1.0));
        let weighted_luma = dot(w_u, up_down) + dot(w_l, left) + dot(w_r, right);
        var final_y = weighted_luma / weight_sum;

        let max_y = max(max(left.y, left.z), max(right.x, right.w));
        let min_y = min(min(left.y, left.z), min(right.x, right.w));
        final_y = clamp(edge_sharpness * final_y, min_y, max_y);

        // smooth high contrast input (as in sgsr1_shader_mobile_edge_direction.frag)
        let delta_y = clamp(final_y - color.w, -23.0 / 255.0, 23.0 / 255.0);

        color = vec4<f32>(clamp(color.xyz + vec3<f32>(delta_y), vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
    }

    return color;
}
)""";

struct Params {
  float viewportInfo[4];
};

bool g_failed = false;
wgpu::BindGroupLayout g_paramsLayout;
wgpu::RenderPipeline g_pipeline;
wgpu::Buffer g_paramsBuffer;
wgpu::BindGroup g_paramsBindGroup;
wgpu::Sampler g_sampler;
wgpu::Texture g_cachedTexture;
wgpu::BindGroup g_sourceBindGroup;
uint32_t g_lastSrcWidth = 0;
uint32_t g_lastSrcHeight = 0;
uint32_t g_lastOutWidth = 0;
uint32_t g_lastOutHeight = 0;
Params g_params{};
bool g_paramsWritten = false;

bool ensure_pipeline() {
  if (g_pipeline) return true;
  if (g_failed) return false;
  wgpu::ShaderSourceWGSL sourceDescriptor{};
  sourceDescriptor.code = kShaderSource;
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &sourceDescriptor,
      .label = "SGSR1 Module",
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);
  if (!module) {
    Log.error("Failed to create SGSR1 shader module");
    g_failed = true;
    return false;
  }
  const wgpu::BindGroupLayoutEntry paramsEntry{
      .binding = 0,
      .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
      .buffer =
          wgpu::BufferBindingLayout{
              .type = wgpu::BufferBindingType::Uniform,
              .minBindingSize = sizeof(Params),
          },
  };
  const wgpu::BindGroupLayoutDescriptor paramsLayoutDescriptor{
      .entryCount = 1,
      .entries = &paramsEntry,
  };
  g_paramsLayout = g_device.CreateBindGroupLayout(&paramsLayoutDescriptor);
  const std::array layouts{g_CopyBindGroupLayout, g_paramsLayout};
  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = layouts.size(),
      .bindGroupLayouts = layouts.data(),
  };
  const auto layout = g_device.CreatePipelineLayout(&layoutDescriptor);
  const wgpu::ColorTargetState colorTarget{
      .format = g_graphicsConfig.surfaceConfiguration.format,
      .writeMask = wgpu::ColorWriteMask::All,
  };
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = 1,
      .targets = &colorTarget,
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = "SGSR1",
      .layout = layout,
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
  g_pipeline = g_device.CreateRenderPipeline(&descriptor);
  if (!g_pipeline) {
    Log.error("Failed to create SGSR1 render pipeline");
    g_failed = true;
    return false;
  }
  const wgpu::BufferDescriptor bufferDescriptor{
      .label = "SGSR1 Params",
      .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
      .size = sizeof(Params),
  };
  g_paramsBuffer = g_device.CreateBuffer(&bufferDescriptor);
  const wgpu::BindGroupEntry paramsBinding{
      .binding = 0,
      .buffer = g_paramsBuffer,
      .size = sizeof(Params),
  };
  const wgpu::BindGroupDescriptor paramsBindGroupDescriptor{
      .layout = g_paramsLayout,
      .entryCount = 1,
      .entries = &paramsBinding,
  };
  g_paramsBindGroup = g_device.CreateBindGroup(&paramsBindGroupDescriptor);
  const wgpu::SamplerDescriptor samplerDescriptor{
      .label = "SGSR1 Sampler",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .addressModeW = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
  };
  g_sampler = g_device.CreateSampler(&samplerDescriptor);
  g_paramsWritten = false;
  Log.info("SGSR upscaling ready");
  return true;
}
} // namespace

bool encode_upscale(const wgpu::CommandEncoder& /*encoder*/, const PresentSource& source, const Viewport& viewport) {
  if (!source.texture && !source.bindGroup) return false;
  const auto outW = static_cast<uint32_t>(std::lround(viewport.width));
  const auto outH = static_cast<uint32_t>(std::lround(viewport.height));
  const auto srcW = source.size.width;
  const auto srcH = source.size.height;
  if (outW == 0 || outH == 0 || srcW == 0 || srcH == 0 || (outW <= srcW && outH <= srcH)) return false;
  if (outW > g_graphicsConfig.maxTextureDimension2D || outH > g_graphicsConfig.maxTextureDimension2D) return false;
  if (!ensure_pipeline()) return false;

  if (source.bindGroup) {
    g_cachedTexture = {};
    g_sourceBindGroup = source.bindGroup;
  } else if (source.texture) {
    if (g_cachedTexture.Get() != source.texture.Get() || !g_sourceBindGroup) {
      g_cachedTexture = source.texture;
      g_sourceBindGroup = create_copy_bind_group(source.texture.CreateView(), g_sampler);
    }
  }

  if (g_lastSrcWidth != srcW || g_lastSrcHeight != srcH || g_lastOutWidth != outW || g_lastOutHeight != outH) {
    g_lastSrcWidth = srcW;
    g_lastSrcHeight = srcH;
    g_lastOutWidth = outW;
    g_lastOutHeight = outH;
    Log.info("SGSR output {}x{} from {}x{}", outW, outH, srcW, srcH);
  }

  const auto fSrcW = static_cast<float>(srcW);
  const auto fSrcH = static_cast<float>(srcH);
  const Params params{
      .viewportInfo = {1.0f / fSrcW, 1.0f / fSrcH, fSrcW, fSrcH},
  };
  if (!g_paramsWritten || std::memcmp(&params, &g_params, sizeof(Params)) != 0) {
    g_queue.WriteBuffer(g_paramsBuffer, 0, &params, sizeof(Params));
    g_params = params;
    g_paramsWritten = true;
  }
  return true;
}

void draw(const wgpu::RenderPassEncoder& pass) {
  pass.SetPipeline(g_pipeline);
  pass.SetBindGroup(0, g_sourceBindGroup);
  pass.SetBindGroup(1, g_paramsBindGroup);
  pass.Draw(3);
}

void shutdown() noexcept {
  g_sourceBindGroup = {};
  g_cachedTexture = {};
  g_lastSrcWidth = 0;
  g_lastSrcHeight = 0;
  g_lastOutWidth = 0;
  g_lastOutHeight = 0;
  g_sampler = {};
  g_paramsBindGroup = {};
  g_paramsBuffer = {};
  g_pipeline = {};
  g_paramsLayout = {};
  g_paramsWritten = false;
  g_failed = false;
}
} // namespace aurora::webgpu::sgsr
