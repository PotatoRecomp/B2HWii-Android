#pragma once

#include "texture.hpp"
#include <optional>

namespace aurora::gfx::texture_replacement {
void initialize() noexcept;
void shutdown() noexcept;
void register_tlut(const GXTlutObj* obj, const void* data, GXTlutFmt format, uint16_t entries) noexcept;
void load_tlut(const GXTlutObj* obj, uint32_t idx) noexcept;
std::optional<TextureHandle> find_replacement(const GXTexObj_& obj) noexcept;
// For palette textures: hashes the palette the draw uses instead of the GXLoadTlut mirror.
std::optional<TextureHandle> find_replacement(const GXTexObj_& obj, const GXTlutObj_& tlut) noexcept;
// Same, with the caller's digest of the texture bytes (digestSize >= the base level) standing in for
// the full-texture hash until the index changes. A zero size or palette digest skips the shortcut.
std::optional<TextureHandle> find_replacement(const GXTexObj_& obj, uint64_t digest, uint32_t digestSize) noexcept;
std::optional<TextureHandle> find_replacement(const GXTexObj_& obj, const GXTlutObj_& tlut, uint64_t digest,
                                              uint32_t digestSize, uint64_t tlutDigest) noexcept;
std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept;
} // namespace aurora::gfx::texture_replacement
