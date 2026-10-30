#pragma once

#include "gpu.hpp"

namespace aurora::webgpu::fsr {
// AMD FidelityFX Super Resolution 1 (EASU edge-adaptive upscale + RCAS sharpen), for presenting a
// game image rendered below the screen's resolution.
// ModeLite: RCAS over a bilinear upscale in the final pass (cheap enough for small phone GPUs).
// ModeFull: EASU into a texture the size of the viewport, then RCAS.
enum Mode : int { ModeOff = 0, ModeLite = 1, ModeFull = 2 };
void set_mode(int mode) noexcept;
int mode() noexcept;
// Full mode upscales `source` (bound with the copy bind group layout) into an internal texture the
// size of `viewport`; light mode only latches `source`. Returns false when FSR is off or the image
// is not being enlarged; then the caller draws with the plain copy pipeline as before.
bool encode_upscale(const wgpu::CommandEncoder& encoder, const wgpu::BindGroup& source,
                    const wgpu::Extent3D& sourceSize, const Viewport& viewport);
// Draws the (upscaled and) sharpened result of the last encode_upscale into `pass`, whose viewport
// must be the same `viewport` and whose target has the surface format.
void draw_sharpened(const wgpu::RenderPassEncoder& pass);
void shutdown() noexcept;
} // namespace aurora::webgpu::fsr
