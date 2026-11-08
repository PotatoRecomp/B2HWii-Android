#pragma once

#include <filesystem>
#include <optional>

#include "texture_convert.hpp"

namespace aurora::gfx::png {
std::optional<ConvertedTexture> load_png_file(const std::filesystem::path& path) noexcept;
// Tightly packed RGBA8 rows; written to a temporary file first, then renamed into place.
bool write_rgba8_png(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* pixels) noexcept;
}
