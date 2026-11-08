#pragma once

#include "gpu.hpp"

namespace aurora::webgpu::sgsr {
// Qualcomm Snapdragon Game Super Resolution 1: single-pass spatial edge-aware upscale + sharpen
// (mode 3 in fsr::mode()). Prepares the uniform buffer and linear-clamped bind group in
// encode_upscale, and draws straight into the presentation render pass in draw.
bool encode_upscale(const wgpu::CommandEncoder& encoder, const PresentSource& source, const Viewport& viewport);
void draw(const wgpu::RenderPassEncoder& pass);
void shutdown() noexcept;
} // namespace aurora::webgpu::sgsr
