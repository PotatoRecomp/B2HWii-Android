#include "texture_upscale.hpp"

#include <algorithm>
#include <cmath>

namespace aurora::gfx::texture_upscale {
namespace {
// A 2x output pixel sits a quarter texel from a source texel: Catmull-Rom weights at t = 0.25
// (the phase nearer the left/top texel) and t = 0.75, taps at -1, 0, +1, +2.
constexpr float kNear[4] = {-0.0703125f, 0.8671875f, 0.2265625f, -0.0234375f};
constexpr float kFar[4] = {-0.0234375f, 0.2265625f, 0.8671875f, -0.0703125f};

int wrapIndex(int i, int n, bool repeat) {
  if (repeat) {
    i %= n;
    return i < 0 ? i + n : i;
  }
  return std::clamp(i, 0, n - 1);
}
} // namespace

std::vector<uint8_t> upscale2x(const uint8_t* rgba, uint32_t width, uint32_t height, bool wrapS, bool wrapT) {
  const int w = static_cast<int>(width), h = static_cast<int>(height);
  const int ow = w * 2, oh = h * 2;

  // Premultiplied float source.
  std::vector<float> src(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    const float a = rgba[i * 4 + 3] / 255.f;
    src[i * 4 + 0] = rgba[i * 4 + 0] / 255.f * a;
    src[i * 4 + 1] = rgba[i * 4 + 1] / 255.f * a;
    src[i * 4 + 2] = rgba[i * 4 + 2] / 255.f * a;
    src[i * 4 + 3] = a;
  }

  // Horizontal pass: w -> ow, h rows.
  std::vector<float> mid(static_cast<size_t>(ow) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int ox = 0; ox < ow; ++ox) {
      // Output ox covers source x = ox/2 - 0.25: even ox lies left of texel ox/2, odd ox right of it.
      const int base = (ox >> 1) - ((ox & 1) == 0 ? 1 : 0);
      const float* k = (ox & 1) == 0 ? kFar : kNear;
      float acc[4] = {};
      for (int t = 0; t < 4; ++t) {
        const float* p = &src[(static_cast<size_t>(y) * w + wrapIndex(base - 1 + t, w, wrapS)) * 4];
        for (int c = 0; c < 4; ++c) acc[c] += p[c] * k[t];
      }
      float* o = &mid[(static_cast<size_t>(y) * ow + ox) * 4];
      for (int c = 0; c < 4; ++c) o[c] = acc[c];
    }
  }

  // Vertical pass: h -> oh.
  std::vector<float> big(static_cast<size_t>(ow) * oh * 4);
  for (int oy = 0; oy < oh; ++oy) {
    const int base = (oy >> 1) - ((oy & 1) == 0 ? 1 : 0);
    const float* k = (oy & 1) == 0 ? kFar : kNear;
    for (int x = 0; x < ow; ++x) {
      float acc[4] = {};
      for (int t = 0; t < 4; ++t) {
        const float* p = &mid[(static_cast<size_t>(wrapIndex(base - 1 + t, h, wrapT)) * ow + x) * 4];
        for (int c = 0; c < 4; ++c) acc[c] += p[c] * k[t];
      }
      float* o = &big[(static_cast<size_t>(oy) * ow + x) * 4];
      o[3] = std::clamp(acc[3], 0.f, 1.f);
      for (int c = 0; c < 3; ++c) o[c] = std::clamp(acc[c], 0.f, o[3]);
    }
  }

  // Unsharp mask (3x3 blur, amount 0.35) on the premultiplied colour, then back to straight alpha.
  constexpr float kAmount = 0.35f;
  std::vector<uint8_t> out(static_cast<size_t>(ow) * oh * 4);
  for (int y = 0; y < oh; ++y) {
    for (int x = 0; x < ow; ++x) {
      const float* c0 = &big[(static_cast<size_t>(y) * ow + x) * 4];
      float blur[3] = {};
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const float* p = &big[(static_cast<size_t>(wrapIndex(y + dy, oh, wrapT)) * ow +
                                 wrapIndex(x + dx, ow, wrapS)) * 4];
          const float wgt = (dx == 0 ? 2.f : 1.f) * (dy == 0 ? 2.f : 1.f) / 16.f;
          for (int c = 0; c < 3; ++c) blur[c] += p[c] * wgt;
        }
      }
      const float a = c0[3];
      uint8_t* o = &out[(static_cast<size_t>(y) * ow + x) * 4];
      for (int c = 0; c < 3; ++c) {
        const float sharp = std::clamp(c0[c] + (c0[c] - blur[c]) * kAmount, 0.f, a);
        o[c] = a > 0.f ? static_cast<uint8_t>(std::lround(sharp / a * 255.f)) : 0;
      }
      o[3] = static_cast<uint8_t>(std::lround(a * 255.f));
    }
  }
  return out;
}

std::vector<uint8_t> half(const uint8_t* rgba, uint32_t width, uint32_t height) {
  const uint32_t ow = std::max(width / 2, 1u), oh = std::max(height / 2, 1u);
  std::vector<uint8_t> out(static_cast<size_t>(ow) * oh * 4);
  for (uint32_t y = 0; y < oh; ++y) {
    for (uint32_t x = 0; x < ow; ++x) {
      const uint32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
      const uint32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
      const uint8_t* p[4] = {&rgba[(y0 * width + x0) * 4], &rgba[(y0 * width + x1) * 4],
                             &rgba[(y1 * width + x0) * 4], &rgba[(y1 * width + x1) * 4]};
      // Alpha-weighted colour, so transparent texels do not bleed their (often black) colour in.
      uint32_t a = 0, rgb[3] = {};
      for (const uint8_t* q : p) {
        a += q[3];
        for (int c = 0; c < 3; ++c) rgb[c] += q[c] * q[3];
      }
      uint8_t* o = &out[(static_cast<size_t>(y) * ow + x) * 4];
      for (int c = 0; c < 3; ++c) {
        o[c] = a > 0 ? static_cast<uint8_t>((rgb[c] + a / 2) / a) : static_cast<uint8_t>((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
      }
      o[3] = static_cast<uint8_t>((a + 2) / 4);
    }
  }
  return out;
}
} // namespace aurora::gfx::texture_upscale
