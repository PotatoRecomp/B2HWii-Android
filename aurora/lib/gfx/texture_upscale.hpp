#pragma once

#include <cstdint>
#include <vector>

namespace aurora::gfx::texture_upscale {
// 2x upscale of an RGBA8 image: Catmull-Rom on premultiplied colour (no dark fringes at cut-outs),
// then a light unsharp mask on the colour. `wrapS`/`wrapT`: the texture repeats along that axis,
// so its edges sample the other side instead of clamping.
std::vector<uint8_t> upscale2x(const uint8_t* rgba, uint32_t width, uint32_t height, bool wrapS, bool wrapT);

// The next mip level (2x2 box filter) of an RGBA8 image; sizes halve, never below 1.
std::vector<uint8_t> half(const uint8_t* rgba, uint32_t width, uint32_t height);
} // namespace aurora::gfx::texture_upscale
