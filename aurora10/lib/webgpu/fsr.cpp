#include "fsr.hpp"

#include "../internal.hpp"

#include <atomic>
#include <cmath>
#include <cstring>

// A WGSL port of AMD FidelityFX Super Resolution 1 (ffx_fsr1.h, MIT licence): EASU in one pass into a
// texture the size of the presentation viewport, then RCAS while drawing that texture to the screen.
// The 12 EASU taps use textureLoad instead of gathers, which keeps the tap layout readable.

namespace aurora::webgpu::fsr {
namespace {
Module Log("aurora::fsr");

constexpr wgpu::TextureFormat kUpscaledFormat = wgpu::TextureFormat::RGBA8Unorm;
// RCAS strength in stops; 0 is the strongest. 0.2 is AMD's suggested default.
constexpr float kSharpnessStops = 0.2f;

constexpr const char* kShaderSource = R"""(
struct Params {
    out_size: vec2<f32>,
    offset: vec2<f32>,
    sharpness: f32,
};

@group(0) @binding(0) var src_sampler: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(1) @binding(0) var<uniform> params: Params;

var<private> positions: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -3.0),
    vec2(3.0, 1.0),
);

@vertex
fn vs_main(@builtin(vertex_index) vtxIdx: u32) -> @builtin(position) vec4<f32> {
    return vec4<f32>(positions[vtxIdx], 0.0, 1.0);
}

var<private> last_texel: vec2<i32>;

fn ld(p: vec2<i32>) -> vec3<f32> {
    return textureLoad(src, clamp(p, vec2<i32>(0), last_texel), 0).rgb;
}

fn luma(c: vec3<f32>) -> f32 {
    return c.b * 0.5 + (c.r * 0.5 + c.g);
}

// Direction and edge length of one of the four bilinear neighbours, weighted by w.
// a above, b left, c centre, d right, e below.
fn easu_set(dir: ptr<function, vec2<f32>>, len: ptr<function, f32>, w: f32,
            la: f32, lb: f32, lc: f32, ld_: f32, le: f32) {
    let dir_x = ld_ - lb;
    var len_x = abs(dir_x) / max(max(abs(ld_ - lc), abs(lc - lb)), 1e-5);
    len_x = clamp(len_x, 0.0, 1.0);
    let dir_y = le - la;
    var len_y = abs(dir_y) / max(max(abs(le - lc), abs(lc - la)), 1e-5);
    len_y = clamp(len_y, 0.0, 1.0);
    *dir += vec2<f32>(dir_x, dir_y) * w;
    *len += (len_x * len_x + len_y * len_y) * w;
}

fn easu_tap(acc: ptr<function, vec3<f32>>, acc_w: ptr<function, f32>, off: vec2<f32>, dir: vec2<f32>,
            len2: vec2<f32>, lob: f32, clp: f32, c: vec3<f32>) {
    let v = vec2<f32>(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len2;
    let d2 = min(dot(v, v), clp);
    var wb = 0.4 * d2 - 1.0;
    var wa = lob * d2 - 1.0;
    wb = wb * wb;
    wa = wa * wa;
    wb = 1.5625 * wb - 0.5625;
    let w = wb * wa;
    *acc += c * w;
    *acc_w += w;
}

@fragment
fn fs_easu(@builtin(position) frag: vec4<f32>) -> @location(0) vec4<f32> {
    let in_size = vec2<f32>(textureDimensions(src));
    last_texel = vec2<i32>(in_size) - vec2<i32>(1);
    var pp = (floor(frag.xy) + 0.5) * (in_size / params.out_size) - 0.5;
    let fp = floor(pp);
    pp -= fp;
    let o = vec2<i32>(fp);
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    let b = ld(o + vec2<i32>(0, -1));
    let c = ld(o + vec2<i32>(1, -1));
    let e = ld(o + vec2<i32>(-1, 0));
    let f = ld(o);
    let g = ld(o + vec2<i32>(1, 0));
    let h = ld(o + vec2<i32>(2, 0));
    let i = ld(o + vec2<i32>(-1, 1));
    let j = ld(o + vec2<i32>(0, 1));
    let k = ld(o + vec2<i32>(1, 1));
    let l = ld(o + vec2<i32>(2, 1));
    let n = ld(o + vec2<i32>(0, 2));
    let p = ld(o + vec2<i32>(1, 2));
    let bl = luma(b);
    let cl = luma(c);
    let el = luma(e);
    let fl = luma(f);
    let gl = luma(g);
    let hl = luma(h);
    let il = luma(i);
    let jl = luma(j);
    let kl = luma(k);
    let ll = luma(l);
    let nl = luma(n);
    let pl = luma(p);

    var dir = vec2<f32>(0.0);
    var len = 0.0;
    easu_set(&dir, &len, (1.0 - pp.x) * (1.0 - pp.y), bl, el, fl, gl, jl);
    easu_set(&dir, &len, pp.x * (1.0 - pp.y), cl, fl, gl, hl, kl);
    easu_set(&dir, &len, (1.0 - pp.x) * pp.y, fl, il, jl, kl, nl);
    easu_set(&dir, &len, pp.x * pp.y, gl, jl, kl, ll, pl);

    let dir_r2 = dot(dir, dir);
    let flat_area = dir_r2 < 1.0 / 32768.0;
    let dir_r = select(inverseSqrt(max(dir_r2, 1e-12)), 1.0, flat_area);
    dir.x = select(dir.x, 1.0, flat_area);
    dir *= dir_r;
    len = len * 0.5;
    len = len * len;
    let stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    let len2 = vec2<f32>(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    let lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    let clp = 1.0 / lob;

    var acc = vec3<f32>(0.0);
    var acc_w = 0.0;
    easu_tap(&acc, &acc_w, vec2<f32>(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    easu_tap(&acc, &acc_w, vec2<f32>(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    easu_tap(&acc, &acc_w, vec2<f32>(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    easu_tap(&acc, &acc_w, vec2<f32>(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    easu_tap(&acc, &acc_w, vec2<f32>(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    easu_tap(&acc, &acc_w, vec2<f32>(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    easu_tap(&acc, &acc_w, vec2<f32>(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    easu_tap(&acc, &acc_w, vec2<f32>(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    easu_tap(&acc, &acc_w, vec2<f32>(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    easu_tap(&acc, &acc_w, vec2<f32>(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    easu_tap(&acc, &acc_w, vec2<f32>(1.0, 2.0) - pp, dir, len2, lob, clp, p);
    easu_tap(&acc, &acc_w, vec2<f32>(0.0, 2.0) - pp, dir, len2, lob, clp, n);

    // Deringing: stay within the four texels around the sample point.
    let mn = min(min(f, g), min(j, k));
    let mx = max(max(f, g), max(j, k));
    return vec4<f32>(clamp(acc / acc_w, mn, mx), 1.0);
}

@fragment
fn fs_rcas(@builtin(position) frag: vec4<f32>) -> @location(0) vec4<f32> {
    last_texel = vec2<i32>(textureDimensions(src)) - vec2<i32>(1);
    let o = vec2<i32>(floor(frag.xy - params.offset));
    return vec4<f32>(rcas(ld(o + vec2<i32>(0, -1)), ld(o + vec2<i32>(-1, 0)), ld(o), ld(o + vec2<i32>(1, 0)),
                          ld(o + vec2<i32>(0, 1))), 1.0);
}

// The light mode: RCAS over a bilinear upscale, sampled straight from the game image in one pass.
@fragment
fn fs_lite(@builtin(position) frag: vec4<f32>) -> @location(0) vec4<f32> {
    let px = 1.0 / params.out_size;
    let uv = (frag.xy - params.offset) * px;
    let b = textureSampleLevel(src, src_sampler, uv - vec2<f32>(0.0, px.y), 0.0).rgb;
    let d = textureSampleLevel(src, src_sampler, uv - vec2<f32>(px.x, 0.0), 0.0).rgb;
    let e = textureSampleLevel(src, src_sampler, uv, 0.0).rgb;
    let f = textureSampleLevel(src, src_sampler, uv + vec2<f32>(px.x, 0.0), 0.0).rgb;
    let h = textureSampleLevel(src, src_sampler, uv + vec2<f32>(0.0, px.y), 0.0).rgb;
    return vec4<f32>(rcas(b, d, e, f, h), 1.0);
}

//    b
//  d e f
//    h
fn rcas(b: vec3<f32>, d: vec3<f32>, e: vec3<f32>, f: vec3<f32>, h: vec3<f32>) -> vec3<f32> {
    let mn4 = min(min(b, d), min(f, h));
    let mx4 = max(max(b, d), max(f, h));
    // The strongest negative lobe that keeps every channel inside [0, 1] around this pixel.
    let hit_min = min(mn4, e) / max(4.0 * mx4, vec3<f32>(1e-5));
    let hit_max = (vec3<f32>(1.0) - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3<f32>(-1e-5));
    let lobe_rgb = max(-hit_min, hit_max);
    var lobe = max(-0.1875, min(max(lobe_rgb.r, max(lobe_rgb.g, lobe_rgb.b)), 0.0)) * params.sharpness;
    // Sharpen less where the neighbourhood looks like noise.
    let bl = luma(b);
    let dl = luma(d);
    let el = luma(e);
    let fl = luma(f);
    let hl = luma(h);
    let range = max(max(max(bl, dl), max(el, fl)), hl) - min(min(min(bl, dl), min(el, fl)), hl);
    let noise = clamp(abs(0.25 * (bl + dl + fl + hl) - el) / max(range, 1e-5), 0.0, 1.0);
    lobe *= 1.0 - 0.5 * noise;
    return (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
}
)""";

struct Params {
  float outSize[2];
  float offset[2];
  float sharpness;
  float pad[3];
};

std::atomic<int> g_mode{ModeOff};
bool g_failed = false;
wgpu::BindGroupLayout g_paramsLayout;
wgpu::RenderPipeline g_easuPipeline;
wgpu::RenderPipeline g_rcasPipeline;
wgpu::RenderPipeline g_litePipeline;
wgpu::BindGroup g_liteSource;
bool g_lite = false;
wgpu::Buffer g_paramsBuffer;
wgpu::BindGroup g_paramsBindGroup;
wgpu::Sampler g_sampler;
wgpu::Texture g_upscaled;
wgpu::BindGroup g_upscaledBindGroup;
uint32_t g_upscaledWidth = 0;
uint32_t g_upscaledHeight = 0;
Params g_params{};
bool g_paramsWritten = false;

wgpu::RenderPipeline create_pipeline(const wgpu::ShaderModule& module, const wgpu::PipelineLayout& layout,
                                     const char* entryPoint, wgpu::TextureFormat format, const char* label) {
  const wgpu::ColorTargetState colorTarget{
      .format = format,
      .writeMask = wgpu::ColorWriteMask::All,
  };
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = entryPoint,
      .targetCount = 1,
      .targets = &colorTarget,
  };
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = label,
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
  return g_device.CreateRenderPipeline(&descriptor);
}

bool ensure_pipelines() {
  if (g_rcasPipeline) return true;
  if (g_failed) return false;
  wgpu::ShaderSourceWGSL sourceDescriptor{};
  sourceDescriptor.code = kShaderSource;
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &sourceDescriptor,
      .label = "FSR1 Module",
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);
  if (!module) {
    g_failed = true;
    return false;
  }
  const wgpu::BindGroupLayoutEntry paramsEntry{
      .binding = 0,
      .visibility = wgpu::ShaderStage::Fragment,
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
  g_easuPipeline = create_pipeline(module, layout, "fs_easu", kUpscaledFormat, "FSR1 EASU");
  g_rcasPipeline = create_pipeline(module, layout, "fs_rcas", g_graphicsConfig.surfaceConfiguration.format,
                                   "FSR1 RCAS");
  g_litePipeline = create_pipeline(module, layout, "fs_lite", g_graphicsConfig.surfaceConfiguration.format,
                                   "FSR1 Lite");
  const wgpu::BufferDescriptor bufferDescriptor{
      .label = "FSR1 Params",
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
      .magFilter = wgpu::FilterMode::Nearest,
      .minFilter = wgpu::FilterMode::Nearest,
  };
  g_sampler = g_device.CreateSampler(&samplerDescriptor);
  g_paramsWritten = false;
  Log.info("FSR1 upscaling ready");
  return true;
}

void ensure_upscaled_texture(uint32_t width, uint32_t height) {
  if (g_upscaled && g_upscaledWidth == width && g_upscaledHeight == height) return;
  const wgpu::TextureDescriptor descriptor{
      .label = "FSR1 Upscaled",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
      .dimension = wgpu::TextureDimension::e2D,
      .size = {width, height, 1},
      .format = kUpscaledFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  g_upscaled = g_device.CreateTexture(&descriptor);
  g_upscaledBindGroup = create_copy_bind_group(g_upscaled.CreateView(), g_sampler);
  g_upscaledWidth = width;
  g_upscaledHeight = height;
  Log.info("FSR1 output {}x{}", width, height);
}
} // namespace

void set_mode(int mode) noexcept { g_mode.store(mode, std::memory_order_relaxed); }
int mode() noexcept { return g_mode.load(std::memory_order_relaxed); }

bool encode_upscale(const wgpu::CommandEncoder& encoder, const wgpu::BindGroup& source,
                    const wgpu::Extent3D& sourceSize, const Viewport& viewport) {
  const int current = mode();
  if ((current != ModeLite && current != ModeFull) || !source) return false;
  const auto width = static_cast<uint32_t>(std::lround(viewport.width));
  const auto height = static_cast<uint32_t>(std::lround(viewport.height));
  // Same size or smaller: the plain copy is already sharp.
  if (width == 0 || height == 0 || (width <= sourceSize.width && height <= sourceSize.height)) return false;
  if (width > g_graphicsConfig.maxTextureDimension2D || height > g_graphicsConfig.maxTextureDimension2D) return false;
  if (!ensure_pipelines()) return false;
  g_lite = current == ModeLite;

  const Params params{
      .outSize = {static_cast<float>(width), static_cast<float>(height)},
      .offset = {viewport.left, viewport.top},
      .sharpness = std::exp2(-kSharpnessStops),
  };
  if (!g_paramsWritten || std::memcmp(&params, &g_params, sizeof(Params)) != 0) {
    g_queue.WriteBuffer(g_paramsBuffer, 0, &params, sizeof(Params));
    g_params = params;
    g_paramsWritten = true;
  }
  if (g_lite) {
    g_liteSource = source;
    return true;
  }
  ensure_upscaled_texture(width, height);

  const wgpu::RenderPassColorAttachment attachment{
      .view = g_upscaled.CreateView(),
      .loadOp = wgpu::LoadOp::Clear,
      .storeOp = wgpu::StoreOp::Store,
  };
  const wgpu::RenderPassDescriptor descriptor{
      .label = "FSR1 EASU pass",
      .colorAttachmentCount = 1,
      .colorAttachments = &attachment,
  };
  const auto pass = encoder.BeginRenderPass(&descriptor);
  pass.SetPipeline(g_easuPipeline);
  pass.SetBindGroup(0, source);
  pass.SetBindGroup(1, g_paramsBindGroup);
  pass.Draw(3);
  pass.End();
  return true;
}

void draw_sharpened(const wgpu::RenderPassEncoder& pass) {
  pass.SetPipeline(g_lite ? g_litePipeline : g_rcasPipeline);
  pass.SetBindGroup(0, g_lite ? g_liteSource : g_upscaledBindGroup);
  pass.SetBindGroup(1, g_paramsBindGroup);
  pass.Draw(3);
}

void shutdown() noexcept {
  g_upscaledBindGroup = {};
  g_upscaled = {};
  g_upscaledWidth = 0;
  g_upscaledHeight = 0;
  g_sampler = {};
  g_paramsBindGroup = {};
  g_paramsBuffer = {};
  g_liteSource = {};
  g_litePipeline = {};
  g_rcasPipeline = {};
  g_easuPipeline = {};
  g_paramsLayout = {};
  g_paramsWritten = false;
  g_failed = false;
}
} // namespace aurora::webgpu::fsr
