#include "texture_replacement.hpp"

#include "../internal.hpp"
#include "../gx/gx.hpp"
#include "../webgpu/gpu.hpp"
#include "dds_io.hpp"
#include "texture_convert.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cstdlib>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iterator>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#define BCDEC_IMPLEMENTATION
#include "../../extern/bcdec/bcdec.h"

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "texture_upscale.hpp"

#include "png_io.hpp"
#include "../fs_helper.hpp"

using namespace aurora::gx;
using aurora::webgpu::g_device;

namespace aurora::gfx::texture_replacement {
Module Log("aurora::gfx::texture_replacement");

struct RuntimeTextureKey {
  uint64_t textureHash = 0;
  uint64_t tlutHash = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool hasTlut = false;
  uint32_t format = 0;

  bool operator==(const RuntimeTextureKey& rhs) const = default;

  template <typename H>
  friend H AbslHashValue(H h, const RuntimeTextureKey& key) {
    return H::combine(std::move(h), key.textureHash, key.tlutHash, key.width, key.height, key.hasTlut, key.format);
  }
};

struct TlutMetadata {
  uint32_t size = 0;
  uint32_t format = 0;
  uint16_t entries = 0;
  bool valid = false;
  // Object the palette was registered from. GXLoadTlut consumes the registration, so this is what
  // lets the same object be loaded into more than one hardware slot.
  const GXTlutObj* source = nullptr;
  ByteBuffer data;
};

struct CachedReplacement {
  gfx::TextureHandle handle;
  uint64_t bytes = 0;
  std::list<RuntimeTextureKey>::iterator lruIt;
};

struct ReplacementIndexEntry {
  std::filesystem::path path;
};

absl::flat_hash_map<RuntimeTextureKey, ReplacementIndexEntry> s_replacementIndex;
absl::flat_hash_map<RuntimeTextureKey, CachedReplacement> s_replacementCache;
absl::flat_hash_set<RuntimeTextureKey> s_failedKeys;
absl::flat_hash_set<RuntimeTextureKey> s_reportedMisses;
absl::flat_hash_map<const GXTlutObj*, TlutMetadata> s_pendingTluts;
std::array<TlutMetadata, MaxTluts> s_loadedTluts{};
std::list<RuntimeTextureKey> s_replacementLru;
std::filesystem::path s_replacementRoot;
std::filesystem::path s_dumpRoot;
uint64_t s_replacementCacheBytes = 0;
constexpr uint64_t kReplacementCacheBudgetBytes = 4294967296; // 4GB, reasonable for modern hardware?
// With HD Textures on, every texture gets a 4x-sized replacement: keep fewer of them on the GPU.
constexpr uint64_t kHdReplacementCacheBudgetBytes = 384ull << 20;

// AURORA_REPLACEMENT_CACHE_MB: a smaller budget on a phone with little RAM (set by the app).
uint64_t replacement_budget() noexcept {
  static const uint64_t s_override = [] {
    const char* v = std::getenv("AURORA_REPLACEMENT_CACHE_MB");
    const long long mb = v != nullptr ? std::atoll(v) : 0;
    return mb > 0 ? static_cast<uint64_t>(mb) << 20 : 0;
  }();
  const uint64_t budget =
      g_config.hdTextureMode != 0 ? kHdReplacementCacheBudgetBytes : kReplacementCacheBudgetBytes;
  return s_override != 0 ? std::min(budget, s_override) : budget;
}

// --- HD Textures (live): upscale on a background thread, save to hd-cache, index when done -------

std::string format_replacement_filename(const RuntimeTextureKey& key);

struct HdJob {
  RuntimeTextureKey key;
  std::vector<uint8_t> rgba;
  uint32_t width = 0, height = 0, mips = 1;
  bool wrapS = false, wrapT = false;
};

std::filesystem::path s_hdRoot;
std::mutex s_hdMutex;
std::condition_variable s_hdWake;
std::deque<HdJob> s_hdQueue;  // guarded by s_hdMutex
std::vector<std::pair<RuntimeTextureKey, std::filesystem::path>> s_hdDone;  // guarded by s_hdMutex
std::atomic<bool> s_hdHasDone{false};
std::atomic<uint64_t> s_hdDiskBytes{0};
std::atomic<uint32_t> s_hdMade{0};
bool s_hdStop = false;  // guarded by s_hdMutex
std::thread s_hdThread;
constexpr size_t kHdMaxQueued = 128;

// The renderer looks textures up each time its own caches miss (hundreds a second in a battle), and
// the key hashes the whole texture. The caller's content digest stands in for that hash until the
// index changes, i.e. until an HD texture is finished.
struct LookupMemoKey {
  uint64_t digest = 0;
  uint64_t tlutDigest = 0;
  uint32_t size = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;

  bool operator==(const LookupMemoKey& rhs) const = default;
  template <typename H>
  friend H AbslHashValue(H h, const LookupMemoKey& key) {
    return H::combine(std::move(h), key.digest, key.tlutDigest, key.size, key.width, key.height, key.format);
  }
};
struct LookupMemo {
  uint32_t generation = 0;
  bool found = false;
  RuntimeTextureKey key;
};
absl::flat_hash_map<LookupMemoKey, LookupMemo> s_lookupMemo;
uint32_t s_indexGeneration = 0;
constexpr size_t kLookupMemoMax = 16384;

uint64_t hd_disk_budget() noexcept {
  return static_cast<uint64_t>(g_config.hdCacheMegabytes != 0 ? g_config.hdCacheMegabytes : 1024) << 20;
}

void hd_worker() noexcept {
#if defined(__linux__)
  // Below the game's threads: it only fills in textures, the game must not wait on it.
  setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
  for (;;) {
    HdJob job;
    {
      std::unique_lock lock(s_hdMutex);
      s_hdWake.wait(lock, [] { return s_hdStop || !s_hdQueue.empty(); });
      if (s_hdStop) return;
      job = std::move(s_hdQueue.front());
      s_hdQueue.pop_front();
    }
    if (s_hdDiskBytes.load() >= hd_disk_budget()) continue;

    std::vector<uint8_t> level =
        texture_upscale::upscale2x(job.rgba.data(), job.width, job.height, job.wrapS, job.wrapT);
    uint32_t w = job.width * 2, h = job.height * 2;
    auto base = s_hdRoot / format_replacement_filename(job.key);
    base.replace_extension(".png");
    // One more level than the game's, so the smallest matches it; written before the base image,
    // which is what the index looks for, so a half-written set is never picked up.
    std::vector<std::pair<std::filesystem::path, std::vector<uint8_t>>> levels;
    const auto stem = fs_path_to_string(base.stem());
    uint64_t bytes = 0;
    bool ok = true;
    {
      std::vector<uint8_t> cur = level;
      uint32_t cw = w, ch = h;
      for (uint32_t mip = 1; mip <= job.mips && (cw > 1 || ch > 1); ++mip) {
        cur = texture_upscale::half(cur.data(), cw, ch);
        cw = std::max(cw / 2, 1u);
        ch = std::max(ch / 2, 1u);
        const auto path = s_hdRoot / fmt::format("{}_mip{}.png", stem, mip);
        ok = ok && png::write_rgba8_png(path, cw, ch, cur.data());
        std::error_code ec;
        bytes += std::filesystem::file_size(path, ec);
      }
    }
    ok = ok && png::write_rgba8_png(base, w, h, level.data());
    if (!ok) continue;
    std::error_code ec;
    bytes += std::filesystem::file_size(base, ec);
    s_hdDiskBytes += bytes;
    const uint32_t made = ++s_hdMade;
    if (made % 50 == 0) {
      Log.info("hd textures: {} made this session, cache {} MB", made, s_hdDiskBytes.load() >> 20);
    }
    {
      std::lock_guard lock(s_hdMutex);
      s_hdDone.emplace_back(job.key, base);
    }
    s_hdHasDone = true;
  }
}
constexpr uint64_t kReplacementWildcardTextureHash = 0xFFFFFFFFFFFFFFFFull;
constexpr uint64_t kReplacementWildcardTlutHash = 0xFFFFFFFFFFFFFFFEull;

bool iequals_ascii(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i]))) {
      return false;
    }
  }
  return true;
}

bool is_relative_to(const std::filesystem::path& path, const std::filesystem::path& root) noexcept {
  if (root.empty()) {
    return false;
  }
  auto pathIt = path.begin();
  auto rootIt = root.begin();
  for (; rootIt != root.end(); ++rootIt, ++pathIt) {
    if (pathIt == path.end() || !iequals_ascii(fs_path_to_string(*pathIt), fs_path_to_string(*rootIt))) {
      return false;
    }
  }
  return true;
}

bool is_sidecar_mip(std::string_view stem) noexcept {
  constexpr std::string_view tag = "_mip";
  size_t i = stem.size();
  while (i > 0 && stem[i - 1] >= '0' && stem[i - 1] <= '9') {
    --i;
  }

  if (i == stem.size() || i < tag.size()) {
    return false;
  }

  return stem.substr(i - tag.size(), tag.size()) == tag;
}

std::optional<uint64_t> parse_hex(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  uint64_t value = 0;
  for (const char ch : text) {
    value <<= 4;
    if (ch >= '0' && ch <= '9') {
      value |= static_cast<uint64_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      value |= static_cast<uint64_t>(ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      value |= static_cast<uint64_t>(ch - 'A' + 10);
    } else {
      return std::nullopt;
    }
  }
  return value;
}

std::optional<uint32_t> parse_u32(std::string_view text, int base = 10) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }

  uint32_t value = 0;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value, base);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::pair<uint32_t, uint32_t>> parse_dimensions(std::string_view text) noexcept {
  const size_t sep = text.find('x');
  if (sep == std::string_view::npos) {
    return std::nullopt;
  }

  const auto width = parse_u32(text.substr(0, sep));
  const auto height = parse_u32(text.substr(sep + 1));
  if (!width.has_value() || !height.has_value()) {
    return std::nullopt;
  }
  return std::pair{*width, *height};
}

uint32_t texture_base_level_size(const GXTexObj_& obj) noexcept {
  switch (obj.format()) {
  case GX_TF_R8_PC:
    return obj.width() * obj.height();
  case GX_TF_RGBA8_PC:
    return obj.width() * obj.height() * 4;
  default:
    return GXGetTexBufferSize(obj.width(), obj.height(), obj.format(), false, 0);
  }
}

// The palette the draw actually uses, set by find_replacement(obj, tlut). Basara repoints one palette
// buffer for many textures (GXInitTlutObjData) and loads model palettes from display lists, so the
// mirror filled by register_tlut/load_tlut is empty or stale for its 3D textures.
thread_local const GXTlutObj_* s_drawTlut = nullptr;
thread_local TlutMetadata s_drawTlutMeta;

const TlutMetadata* active_tlut(const GXTexObj_& obj) noexcept {
  if (!is_palette_format(obj.format())) {
    return nullptr;
  }
  if (s_drawTlut != nullptr && s_drawTlut->data != nullptr && s_drawTlut->numEntries != 0) {
    const size_t sz = static_cast<size_t>(s_drawTlut->numEntries) * 2;
    if (s_drawTlutMeta.data.size() != sz) {
      s_drawTlutMeta.data = ByteBuffer{sz};
    }
    std::memcpy(s_drawTlutMeta.data.data(), s_drawTlut->data, sz);
    s_drawTlutMeta.size = static_cast<uint32_t>(sz);
    s_drawTlutMeta.format = static_cast<uint32_t>(s_drawTlut->format);
    s_drawTlutMeta.entries = s_drawTlut->numEntries;
    s_drawTlutMeta.valid = true;
    return &s_drawTlutMeta;
  }
  if (obj.tlut >= s_loadedTluts.size() || !s_loadedTluts[obj.tlut].valid) {
    return nullptr;
  }
  return &s_loadedTluts[obj.tlut];
}

std::optional<uint64_t> compute_referenced_tlut_hash(const GXTexObj_& obj) noexcept {
  const TlutMetadata* active = active_tlut(obj);
  if (active == nullptr) {
    return std::nullopt;
  }

  const auto& tlut = *active;
  const uint32_t textureSize = texture_base_level_size(obj);
  const auto* textureData = static_cast<const uint8_t*>(obj.data);
  if (!tlut.valid || textureData == nullptr || textureSize == 0) {
    return std::nullopt;
  }

  uint32_t minIndex = 0xffff;
  uint32_t maxIndex = 0;
  switch (obj.format()) {
  case GX_TF_C4:
    for (uint32_t i = 0; i < textureSize; ++i) {
      const uint32_t lowNibble = textureData[i] & 0xf;
      const uint32_t highNibble = textureData[i] >> 4;
      minIndex = std::min({minIndex, lowNibble, highNibble});
      maxIndex = std::max({maxIndex, lowNibble, highNibble});
    }
    break;
  case GX_TF_C8:
    for (uint32_t i = 0; i < textureSize; ++i) {
      const uint32_t index = textureData[i];
      minIndex = std::min(minIndex, index);
      maxIndex = std::max(maxIndex, index);
    }
    break;
  case GX_TF_C14X2:
    for (uint32_t i = 0; i + sizeof(uint16_t) <= textureSize; i += sizeof(uint16_t)) {
      uint16_t value = 0;
      std::memcpy(&value, textureData + i, sizeof(value));
      const uint32_t index = bswap(value) & 0x3fff;
      minIndex = std::min(minIndex, index);
      maxIndex = std::max(maxIndex, index);
    }
    break;
  default:
    return std::nullopt;
  }

  size_t tlutSize = 2 * (static_cast<size_t>(maxIndex) + 1 - minIndex);
  const size_t tlutOffset = 2 * static_cast<size_t>(minIndex);
  if (tlutOffset + tlutSize > tlut.data.size()) {
    return std::nullopt;
  }
  return XXH64(tlut.data.data() + tlutOffset, tlutSize, 0);
}

const TlutMetadata* get_loaded_tlut(const GXTexObj_& obj) noexcept { return active_tlut(obj); }

bool ensure_directory(const std::filesystem::path& dir) noexcept {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
}

RuntimeTextureKey build_runtime_key(const GXTexObj_& obj) noexcept {
  RuntimeTextureKey key{
      .width = obj.width(),
      .height = obj.height(),
      .hasTlut = is_palette_format(obj.format()),
      .format = obj.format(),
  };

  const uint32_t textureSize = texture_base_level_size(obj);
  if (obj.data != nullptr && textureSize != 0) {
    key.textureHash = XXH64(obj.data, textureSize, 0);
  }
  if (key.hasTlut) {
    key.tlutHash = compute_referenced_tlut_hash(obj).value_or(0);
  }
  return key;
}

std::string format_replacement_filename(const RuntimeTextureKey& key) {
  if (key.hasTlut) {
    return fmt::format("tex1_{}x{}_{:016x}_{:016x}_{}.dds", key.width, key.height,
                       key.textureHash, key.tlutHash, key.format);
  }
  return fmt::format("tex1_{}x{}_{:016x}_{}.dds", key.width, key.height, key.textureHash, key.format);
}

std::optional<RuntimeTextureKey> parse_replacement_filename(std::string_view filename) noexcept {
  const size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos) {
    return std::nullopt;
  }

  if (!iequals_ascii(filename.substr(dot), ".dds") && !iequals_ascii(filename.substr(dot), ".png")) {
    return std::nullopt;
  }

  const std::string_view stem = filename.substr(0, dot);
  constexpr std::string_view prefix = "tex1_";
  if (!stem.starts_with(prefix)) {
    return std::nullopt;
  }

  std::array<std::string_view, 6> parts{};
  size_t partCount = 0;
  size_t offset = 0;
  bool consumedAll = false;
  while (offset <= stem.size() && partCount < parts.size()) {
    const size_t next = stem.find('_', offset);
    parts[partCount++] = stem.substr(offset, next == std::string_view::npos ? stem.size() - offset : next - offset);
    if (next == std::string_view::npos) {
      consumedAll = true;
      break;
    }
    offset = next + 1;
  }
  if (!consumedAll || partCount < 4 || partCount > 6 || parts[0] != "tex1") {
    return std::nullopt;
  }

  const auto dimensions = parse_dimensions(parts[1]);
  if (!dimensions.has_value()) {
    return std::nullopt;
  }

  size_t index = 2;
  if (parts[index] == "m") {
    ++index;
  }

  size_t remaining = partCount - index;
  if (remaining != 2 && remaining != 3) {
    return std::nullopt;
  }

  uint64_t textureHash = 0;
  if (parts[index] == "$") {
    textureHash = kReplacementWildcardTextureHash;
  } else {
    const auto parsedTex = parse_hex(parts[index]);
    if (!parsedTex.has_value()) {
      return std::nullopt;
    }
    textureHash = *parsedTex;
  }

  auto formatPart = parts[partCount - 1];
  if (formatPart == "arb") {
    formatPart = parts[partCount - 2];
    remaining -= 1;
  }
  const auto format = parse_u32(formatPart);
  if (!format.has_value()) {
    return std::nullopt;
  }

  uint64_t tlutHash = 0;
  const bool hasTlut = remaining == 3;
  if (hasTlut) {
    const std::string_view tlutPart = parts[index + 1];
    if (tlutPart == "$") {
      tlutHash = kReplacementWildcardTlutHash;
    } else {
      const auto parsedTlutHash = parse_hex(tlutPart);
      if (!parsedTlutHash.has_value()) {
        return std::nullopt;
      }
      tlutHash = *parsedTlutHash;
    }
  }

  return RuntimeTextureKey{
      .textureHash = textureHash,
      .tlutHash = tlutHash,
      .width = dimensions->first,
      .height = dimensions->second,
      .hasTlut = hasTlut,
      .format = *format,
  };
}

static std::optional<ConvertedTexture> load_texture_file(const std::filesystem::path& path) {
  if (path.extension() == ".png") {
    return png::load_png_file(path);
  } else {
    return dds::load_dds_file(path);
  }
}

constexpr bool isUnsupportedTextureFormat(const ConvertedTexture& texture) {
  switch (texture.format) {
  case wgpu::TextureFormat::BC1RGBAUnorm:
  case wgpu::TextureFormat::BC1RGBAUnormSrgb:
  case wgpu::TextureFormat::BC2RGBAUnorm:
  case wgpu::TextureFormat::BC2RGBAUnormSrgb:
  case wgpu::TextureFormat::BC3RGBAUnorm:
  case wgpu::TextureFormat::BC3RGBAUnormSrgb:
  case wgpu::TextureFormat::BC4RUnorm:
  case wgpu::TextureFormat::BC4RSnorm:
  case wgpu::TextureFormat::BC5RGUnorm:
  case wgpu::TextureFormat::BC5RGSnorm:
  case wgpu::TextureFormat::BC6HRGBUfloat:
  case wgpu::TextureFormat::BC6HRGBFloat:
  case wgpu::TextureFormat::BC7RGBAUnorm:
  case wgpu::TextureFormat::BC7RGBAUnormSrgb:
    return !webgpu::g_bcTexturesSupported;
  default:
    return false;
  }
}

// GPUs without BC (most Mali, Qualcomm's own driver): the blocks are decoded to RGBA8 here, on the
// loader thread, so a DDS pack still shows there (at 8x BC1's memory, 4x BC3/BC7's). BC6H (float)
// is left out.
static std::optional<ConvertedTexture> decode_bc(const ConvertedTexture& in) noexcept {
  using F = wgpu::TextureFormat;
  uint32_t blockSize = 16;
  switch (in.format) {
  case F::BC1RGBAUnorm:
  case F::BC4RUnorm:
    blockSize = 8;
    break;
  case F::BC2RGBAUnorm:
  case F::BC3RGBAUnorm:
  case F::BC5RGUnorm:
  case F::BC7RGBAUnorm:
    break;
  default:
    return std::nullopt;
  }
  uint64_t inSize = 0;
  uint64_t outSize = 0;
  for (uint32_t m = 0; m < in.mips; ++m) {
    const uint32_t w = std::max(in.width >> m, 1u);
    const uint32_t h = std::max(in.height >> m, 1u);
    inSize += static_cast<uint64_t>((w + 3) / 4) * ((h + 3) / 4) * blockSize;
    outSize += static_cast<uint64_t>(w) * h * 4;
  }
  if (in.data.size() < inSize) {
    return std::nullopt;
  }
  ByteBuffer out{static_cast<size_t>(outSize)};
  const uint8_t* src = in.data.data();
  uint8_t* dst = out.data();
  uint8_t block[4 * 4 * 4];
  uint8_t channels[4 * 4 * 2];
  for (uint32_t m = 0; m < in.mips; ++m) {
    const uint32_t w = std::max(in.width >> m, 1u);
    const uint32_t h = std::max(in.height >> m, 1u);
    for (uint32_t by = 0; by < h; by += 4) {
      for (uint32_t bx = 0; bx < w; bx += 4) {
        switch (in.format) {
        case F::BC1RGBAUnorm:
          bcdec_bc1(src, block, 16);
          break;
        case F::BC2RGBAUnorm:
          bcdec_bc2(src, block, 16);
          break;
        case F::BC3RGBAUnorm:
          bcdec_bc3(src, block, 16);
          break;
        case F::BC7RGBAUnorm:
          bcdec_bc7(src, block, 16);
          break;
        case F::BC4RUnorm:  // sampled as (r, 0, 0, 1)
          bcdec_bc4(src, channels, 4);
          for (int i = 0; i < 16; ++i) {
            block[i * 4] = channels[i];
            block[i * 4 + 1] = 0;
            block[i * 4 + 2] = 0;
            block[i * 4 + 3] = 255;
          }
          break;
        default:  // BC5: (r, g, 0, 1)
          bcdec_bc5(src, channels, 8);
          for (int i = 0; i < 16; ++i) {
            block[i * 4] = channels[i * 2];
            block[i * 4 + 1] = channels[i * 2 + 1];
            block[i * 4 + 2] = 0;
            block[i * 4 + 3] = 255;
          }
          break;
        }
        src += blockSize;
        const uint32_t cw = std::min(4u, w - bx);
        const uint32_t ch = std::min(4u, h - by);
        for (uint32_t y = 0; y < ch; ++y) {
          std::memcpy(dst + (static_cast<size_t>(by + y) * w + bx) * 4, block + y * 16, cw * 4);
        }
      }
    }
    dst += static_cast<size_t>(w) * h * 4;
  }
  return ConvertedTexture{
      .format = F::RGBA8Unorm,
      .width = in.width,
      .height = in.height,
      .mips = in.mips,
      .data = std::move(out),
      .hasArbitraryMips = in.hasArbitraryMips,
  };
}

static std::optional<ConvertedTexture> load_replacement_levels(const ReplacementIndexEntry& entry) noexcept {
  auto base = load_texture_file(entry.path);
  if (!base.has_value()) {
    Log.warn("texture_replacement: failed to load texture {}", fs_path_to_string(entry.path));
    return std::nullopt;
  }

  std::vector<ConvertedTexture> more;
  std::error_code ec;
  for (uint32_t mipLevel = 1;; ++mipLevel) {
    const auto mipPath = entry.path.parent_path() / fmt::format("{}_mip{}{}", fs_path_to_string(entry.path.stem()), mipLevel, fs_path_to_string(entry.path.extension()));
    if (!std::filesystem::is_regular_file(mipPath, ec)) {
      break;
    }

    auto lvl = load_texture_file(mipPath);
    const uint32_t ew = std::max(base->width >> mipLevel, 1u);
    const uint32_t eh = std::max(base->height >> mipLevel, 1u);
    const bool ok = lvl.has_value() && lvl->format == base->format && lvl->width == ew && lvl->height == eh;
    if (!ok) {
      if (!lvl.has_value()) {
        Log.warn("texture_replacement: could not load mip {}", fs_path_to_string(mipPath));
      } else {
        Log.warn("texture_replacement: expected {}x{} for mip {}, got {}x{}", ew, eh, fs_path_to_string(mipPath),
                 lvl->width, lvl->height);
      }

      break;
    }
    more.push_back(std::move(*lvl));
  }

  if (more.empty()) {
    return base;
  }

  const uint32_t mips = 1u + static_cast<uint32_t>(more.size());
  const uint64_t n = calc_texture_size(base->format, base->width, base->height, mips);
  if (n == 0) {
    return std::nullopt;
  }

  ByteBuffer blob{static_cast<size_t>(n)};
  uint8_t* const dst = blob.data();
  uint64_t o = 0;
  const auto append = [&](const ByteBuffer& d) noexcept -> bool {
    if (o + d.size() > n) {
      return false;
    }
    std::memcpy(dst + o, d.data(), d.size());
    o += d.size();
    return true;
  };
  if (!append(base->data)) {
    return std::nullopt;
  }
  for (const auto& mip : more) {
    if (!append(mip.data)) {
      return std::nullopt;
    }
  }
  if (o != n) {
    return std::nullopt;
  }

  return ConvertedTexture{
      .format = base->format,
      .width = base->width,
      .height = base->height,
      .mips = mips,
      .data = std::move(blob),
  };
}

std::optional<ConvertedTexture> load_replacement(const ReplacementIndexEntry& entry) noexcept {
  auto texture = load_replacement_levels(entry);
  if (!texture.has_value() || !isUnsupportedTextureFormat(*texture)) {
    return texture;
  }
  auto decoded = decode_bc(*texture);
  if (!decoded.has_value()) {
    Log.warn("texture_replacement: failed to load texture {} due to unsupported format: {}",
             fs_path_to_string(entry.path), static_cast<uint32_t>(texture->format));
  }
  return decoded;
}

void touch_cached_replacement(decltype(s_replacementCache)::iterator it) noexcept {
  if (it->second.lruIt != s_replacementLru.begin()) {
    s_replacementLru.splice(s_replacementLru.begin(), s_replacementLru, it->second.lruIt);
    it->second.lruIt = s_replacementLru.begin();
  }
}

void evict_replacement_cache_if_needed() noexcept {
  while (s_replacementCacheBytes > replacement_budget() && !s_replacementLru.empty()) {
    const RuntimeTextureKey key = s_replacementLru.back();
    s_replacementLru.pop_back();

    const auto it = s_replacementCache.find(key);
    if (it == s_replacementCache.end()) {
      continue;
    }

    const uint64_t entryBytes = it->second.bytes;
    s_replacementCache.erase(it);
    s_replacementCacheBytes -= std::min(s_replacementCacheBytes, entryBytes);
  }
}

void index_folder(const std::filesystem::path& root) noexcept {
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(
           root,
           std::filesystem::directory_options::skip_permission_denied |
               std::filesystem::directory_options::follow_directory_symlink,
           ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      break;
    }

    if (!it->is_regular_file()) {
      continue;
    }

    const auto& path = it->path();

    if (is_relative_to(path, s_dumpRoot)) {
      continue;
    }

    if (!iequals_ascii(fs_path_to_string(path.extension()), ".dds") && !iequals_ascii(fs_path_to_string(path.extension()), ".png")) {
      continue;
    }

    if (is_sidecar_mip(fs_path_to_string(path.stem()))) {
      continue;
    }

    const auto parsed = parse_replacement_filename(fs_path_to_string(path.filename()));
    if (!parsed.has_value()) {
      continue;
    }

    s_replacementIndex.try_emplace(*parsed, path);
  }
}

void build_index() noexcept {
  if (!g_config.allowTextureReplacements) {
    return;
  }

  auto userPath = fs_path_from_string(g_config.userPath);
  auto cachePath = fs_path_from_string(g_config.cachePath);

  if (g_config.textureRoot != nullptr && *g_config.textureRoot != '\0') {
    s_replacementRoot = fs_path_from_string(g_config.textureRoot);
    s_dumpRoot = s_replacementRoot / "dump";  // skipped by the index below
  } else {
    s_replacementRoot = userPath / "texture_replacements";
    s_dumpRoot = cachePath / "texture_dumps";
  }
  if (g_config.hdTextureMode != 0) {
    s_hdRoot = g_config.hdCacheRoot != nullptr && *g_config.hdCacheRoot != '\0'
                   ? fs_path_from_string(g_config.hdCacheRoot)
                   : s_replacementRoot / "hd-cache";
  }

  if (!ensure_directory(s_replacementRoot)) {
    return;
  }
  if (g_config.allowTextureDumps && !ensure_directory(s_dumpRoot)) {
    return;
  }

  index_folder(s_replacementRoot);
  // The HD cache, when it is not already inside the folder just indexed. Own packs come first.
  if (!s_hdRoot.empty() && !is_relative_to(s_hdRoot, s_replacementRoot) && ensure_directory(s_hdRoot)) {
    index_folder(s_hdRoot);
  }

  Log.info("Indexed {} texture replacements", s_replacementIndex.size());
}

const ReplacementIndexEntry* find_replacement_path(const RuntimeTextureKey& key) noexcept {
  if (const auto it = s_replacementIndex.find(key); it != s_replacementIndex.end()) {
    return &it->second;
  }

  if (key.hasTlut) {
    RuntimeTextureKey tlutWildcardKey = key;
    tlutWildcardKey.tlutHash = kReplacementWildcardTlutHash;
    if (const auto it = s_replacementIndex.find(tlutWildcardKey); it != s_replacementIndex.end()) {
      return &it->second;
    }
  }

  RuntimeTextureKey textureWildcardKey = key;
  textureWildcardKey.textureHash = kReplacementWildcardTextureHash;
  if (const auto it = s_replacementIndex.find(textureWildcardKey); it != s_replacementIndex.end()) {
    return &it->second;
  }

  return nullptr;
}

const gfx::TextureHandle* find_cached_replacement(const RuntimeTextureKey& key) noexcept {
  const auto cached = s_replacementCache.find(key);
  if (cached == s_replacementCache.end()) {
    return nullptr;
  }

  touch_cached_replacement(cached);
  return &cached->second.handle;
}

gfx::TextureHandle make_replacement_texture(const RuntimeTextureKey& key,
                                            const std::optional<ConvertedTexture>& replacement) noexcept {
  if (!replacement.has_value()) {
    s_failedKeys.insert(key);
    return {};
  }

  const auto label = fmt::format("TextureReplacement {}", format_replacement_filename(key));
  const wgpu::Extent3D size{
      .width = replacement->width,
      .height = replacement->height,
      .depthOrArrayLayers = 1,
  };
  const wgpu::TextureDescriptor textureDescriptor{
      .label = label.c_str(),
      .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = replacement->format,
      .mipLevelCount = replacement->mips,
      .sampleCount = 1,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);
  const auto viewLabel = fmt::format("{} view", label);
  const wgpu::TextureViewDescriptor textureViewDescriptor{
      .label = viewLabel.c_str(),
      .format = replacement->format,
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = replacement->mips,
  };
  auto textureView = texture.CreateView(&textureViewDescriptor);
  auto handle = std::make_shared<gfx::TextureRef>(std::move(texture), std::move(textureView), wgpu::TextureView{}, size,
                                                  replacement->format, replacement->mips, gfx::InvalidTextureFormat);
  handle->isReplacement = true;
  gfx::write_texture(*handle, replacement->data);
  return handle;
}

gfx::TextureHandle load_replacement_texture(const RuntimeTextureKey& key, const ReplacementIndexEntry& entry) noexcept {
  return make_replacement_texture(key, load_replacement(entry));
}

void cache_replacement(const RuntimeTextureKey& key, const gfx::TextureHandle& handle) noexcept;

// --- Loading replacement files off the renderer thread ---------------------------------------------
// A PNG of an HD texture takes 30-45 ms to decode on a phone; decoded here instead, the game keeps
// drawing the original texture until the file is ready and the next lookup uploads it.
struct LoadJob {
  RuntimeTextureKey key;
  ReplacementIndexEntry entry;
};
std::mutex s_loadMutex;
std::condition_variable s_loadWake;
std::deque<LoadJob> s_loadQueue;                                                        // guarded by s_loadMutex
std::vector<std::pair<RuntimeTextureKey, std::optional<ConvertedTexture>>> s_loadDone;  // guarded by s_loadMutex
std::atomic<bool> s_loadHasDone{false};
bool s_loadStop = false;  // guarded by s_loadMutex
std::thread s_loadThread;
absl::flat_hash_set<RuntimeTextureKey> s_loadPending;  // renderer thread only
constexpr size_t kMaxUploadsPerLookup = 2;

void load_worker() noexcept {
  for (;;) {
    LoadJob job;
    {
      std::unique_lock lock(s_loadMutex);
      s_loadWake.wait(lock, [] { return s_loadStop || !s_loadQueue.empty(); });
      if (s_loadStop) return;
      job = std::move(s_loadQueue.front());
      s_loadQueue.pop_front();
    }
    auto converted = load_replacement(job.entry);
    {
      std::lock_guard lock(s_loadMutex);
      s_loadDone.emplace_back(job.key, std::move(converted));
    }
    s_loadHasDone = true;
  }
}

void queue_load(const RuntimeTextureKey& key, const ReplacementIndexEntry& entry) noexcept {
  if (!s_loadPending.insert(key).second) return;
  {
    std::lock_guard lock(s_loadMutex);
    s_loadQueue.push_back(LoadJob{key, entry});
  }
  s_loadWake.notify_one();
}

/** Uploads files the loader finished, a few per call so a burst does not stall one frame. */
void take_load_done() noexcept {
  if (!s_loadHasDone.load()) return;
  std::vector<std::pair<RuntimeTextureKey, std::optional<ConvertedTexture>>> done;
  {
    std::lock_guard lock(s_loadMutex);
    const size_t n = std::min(s_loadDone.size(), kMaxUploadsPerLookup);
    std::move(s_loadDone.begin(), s_loadDone.begin() + static_cast<std::ptrdiff_t>(n), std::back_inserter(done));
    s_loadDone.erase(s_loadDone.begin(), s_loadDone.begin() + static_cast<std::ptrdiff_t>(n));
    s_loadHasDone = !s_loadDone.empty();
  }
  for (auto& [key, converted] : done) {
    s_loadPending.erase(key);
    if (auto handle = make_replacement_texture(key, converted)) {
      cache_replacement(key, handle);
    }
  }
}

void start_loader() noexcept {
  if (!g_config.allowTextureReplacements || s_loadThread.joinable()) return;
  s_loadStop = false;
  s_loadThread = std::thread(load_worker);
}

void stop_loader() noexcept {
  if (!s_loadThread.joinable()) return;
  {
    std::lock_guard lock(s_loadMutex);
    s_loadStop = true;
    s_loadQueue.clear();
    s_loadDone.clear();
  }
  s_loadWake.notify_all();
  s_loadThread.join();
  s_loadHasDone = false;
  s_loadPending.clear();
}

void cache_replacement(const RuntimeTextureKey& key, const gfx::TextureHandle& handle) noexcept {
  const uint64_t replacementBytes =
      calc_texture_size(handle->format, handle->size.width, handle->size.height, handle->mipCount);
  s_replacementLru.push_front(key);
  s_replacementCache.emplace(
      key, CachedReplacement{.handle = handle, .bytes = replacementBytes, .lruIt = s_replacementLru.begin()});
  s_replacementCacheBytes += replacementBytes;
  evict_replacement_cache_if_needed();
}

/** The base level as RGBA8 (empty when it cannot be decoded, e.g. a palette texture without its palette). */
std::optional<ConvertedTexture> decode_rgba(const GXTexObj_& obj) noexcept {
  const ArrayRef<uint8_t> texData{static_cast<const uint8_t*>(obj.data), UINT32_MAX};
  const uint32_t texWidth = obj.width();
  const uint32_t texHeight = obj.height();

  ConvertedTexture pixels;
  if (is_palette_format(obj.format())) {
    const TlutMetadata* tlut = get_loaded_tlut(obj);
    if (tlut == nullptr) {
      return std::nullopt;
    }
    pixels =
        convert_texture_palette(obj.format(), texWidth, texHeight, 1, texData, static_cast<GXTlutFmt>(tlut->format),
                                tlut->entries, {tlut->data.data(), tlut->data.size()});
  } else {
    pixels = convert_texture(obj.format(), texWidth, texHeight, 1, texData);
  }

  const uint64_t rgbaBytes = calc_texture_size(wgpu::TextureFormat::RGBA8Unorm, texWidth, texHeight, 1);
  if (pixels.data.empty() || pixels.format != wgpu::TextureFormat::RGBA8Unorm || pixels.data.size() != rgbaBytes) {
    return std::nullopt;
  }
  return pixels;
}

// --- Texture dumps, written off the renderer thread ------------------------------------------------
// Encoding a PNG takes up to 200 ms on a phone, and movie frames are a new texture every frame, so
// dumping on the renderer thread slowed the game to a fifth. The worker writes; the renderer only
// decodes and copies. One size and format giving many textures is a movie: dumped kMaxDumpsPerShape
// times, then skipped.
struct DumpJob {
  std::filesystem::path path;
  std::vector<uint8_t> rgba;
  uint32_t width;
  uint32_t height;
};
std::mutex s_dumpMutex;
std::condition_variable s_dumpWake;
std::deque<DumpJob> s_dumpQueue;  // guarded by s_dumpMutex
bool s_dumpStop = false;          // guarded by s_dumpMutex
std::thread s_dumpThread;
absl::flat_hash_map<uint64_t, uint32_t> s_dumpsPerShape;  // renderer thread only
constexpr uint32_t kMaxDumpsPerShape = 48;
constexpr size_t kMaxDumpQueue = 32;

void dump_worker() noexcept {
#if defined(__linux__)
  setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
  for (;;) {
    DumpJob job;
    {
      std::unique_lock lock(s_dumpMutex);
      s_dumpWake.wait(lock, [] { return s_dumpStop || !s_dumpQueue.empty(); });
      if (s_dumpStop) return;
      job = std::move(s_dumpQueue.front());
      s_dumpQueue.pop_front();
    }
    png::write_rgba8_png(job.path, job.width, job.height, job.rgba.data());
  }
}

void start_dumper() noexcept {
  if (!g_config.allowTextureDumps || s_dumpRoot.empty() || s_dumpThread.joinable()) return;
  s_dumpStop = false;
  s_dumpThread = std::thread(dump_worker);
}

void stop_dumper() noexcept {
  if (!s_dumpThread.joinable()) return;
  {
    std::lock_guard lock(s_dumpMutex);
    s_dumpStop = true;
    s_dumpQueue.clear();
  }
  s_dumpWake.notify_all();
  s_dumpThread.join();
  s_dumpsPerShape.clear();
}

bool dump_editable_texture_dds(const RuntimeTextureKey& key, const GXTexObj_& obj) noexcept {
  if (!s_dumpThread.joinable()) return false;
  const uint64_t shape = (uint64_t{obj.width()} << 32) | (uint64_t{obj.height()} << 16) | key.format;
  uint32_t& count = s_dumpsPerShape[shape];
  // Palette textures share one size across many atlases and palettes, so only direct colors count.
  if (!key.hasTlut && count >= kMaxDumpsPerShape) {
    if (count == kMaxDumpsPerShape) {
      Log.info("texture dump: {}x{} fmt {} changes every frame (a movie?), no more dumps of it", obj.width(),
               obj.height(), key.format);
      ++count;
    }
    return false;
  }
  {
    std::lock_guard lock(s_dumpMutex);
    if (s_dumpQueue.size() >= kMaxDumpQueue) return false;  // a burst: the next miss of it tries again
  }
  const auto pixels = decode_rgba(obj);
  if (!pixels) {
    return false;
  }
  ++count;
  // PNG: opens in any image editor, and the index takes .png and .dds under the same name.
  auto path = s_dumpRoot / format_replacement_filename(key);
  path.replace_extension(".png");
  {
    std::lock_guard lock(s_dumpMutex);
    s_dumpQueue.push_back(DumpJob{std::move(path), std::vector<uint8_t>(pixels->data.data(),
                                  pixels->data.data() + pixels->data.size()), obj.width(), obj.height()});
  }
  s_dumpWake.notify_one();
  return true;
}

bool is_power_of_two(uint32_t v) noexcept { return v != 0 && (v & (v - 1)) == 0; }

/**
 * Queues a texture for HD Textures. Only power-of-two sizes from 8 to 512: movie frames (512x448)
 * are new textures every frame, and the smallest ones gain nothing.
 */
void queue_hd(const RuntimeTextureKey& key, const GXTexObj_& obj) noexcept {
  if (g_config.hdTextureMode == 0 || !s_hdThread.joinable()) return;
  const uint32_t w = obj.width(), h = obj.height();
  if (!is_power_of_two(w) || !is_power_of_two(h) || std::min(w, h) < 8 || std::max(w, h) > 512) return;
  if (s_hdDiskBytes.load() >= hd_disk_budget()) return;
  {
    std::lock_guard lock(s_hdMutex);
    if (s_hdQueue.size() >= kHdMaxQueued) return;
  }
  auto pixels = decode_rgba(obj);
  if (!pixels) return;
  HdJob job{
      .key = key,
      .rgba = std::vector<uint8_t>(pixels->data.data(), pixels->data.data() + pixels->data.size()),
      .width = w,
      .height = h,
      .mips = obj.mip_count(),
      .wrapS = obj.wrap_s() != GX_CLAMP,
      .wrapT = obj.wrap_t() != GX_CLAMP,
  };
  {
    std::lock_guard lock(s_hdMutex);
    s_hdQueue.push_back(std::move(job));
  }
  s_hdWake.notify_one();
}

/** Textures the HD worker finished since the last call go into the index. */
void take_hd_done() noexcept {
  if (!s_hdHasDone.exchange(false)) return;
  std::vector<std::pair<RuntimeTextureKey, std::filesystem::path>> done;
  {
    std::lock_guard lock(s_hdMutex);
    done.swap(s_hdDone);
  }
  for (auto& [key, path] : done) {
    s_replacementIndex.try_emplace(key, ReplacementIndexEntry{std::move(path)});
  }
  if (!done.empty()) {
    ++s_indexGeneration;
  }
}

bool report_missing_key(const RuntimeTextureKey& key, const GXTexObj_& obj) noexcept {
  if (!s_reportedMisses.insert(key).second) {
    return false;
  }

  if (g_config.reportTextureKeys) {
    Log.info("texture key {} ({}x{} fmt {})", format_replacement_filename(key), key.width, key.height, key.format);
  }
  if (g_config.allowTextureDumps) {
    dump_editable_texture_dds(key, obj);
  }
  queue_hd(key, obj);
  return true;
}

void start_hd() noexcept {
  if (g_config.hdTextureMode == 0 || s_hdRoot.empty()) return;
  if (!ensure_directory(s_hdRoot)) return;
  uint64_t bytes = 0;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(s_hdRoot, ec); !ec && it != std::filesystem::directory_iterator();
       it.increment(ec)) {
    if (it->is_regular_file(ec)) bytes += it->file_size(ec);
  }
  s_hdDiskBytes = bytes;
  s_hdStop = false;
  s_hdThread = std::thread(hd_worker);
  Log.info("hd textures: live, cache {} MB of {} MB", bytes >> 20, hd_disk_budget() >> 20);
}

void stop_hd() noexcept {
  if (!s_hdThread.joinable()) return;
  {
    std::lock_guard lock(s_hdMutex);
    s_hdStop = true;
    s_hdQueue.clear();
    s_hdDone.clear();
  }
  s_hdWake.notify_all();
  s_hdThread.join();
  s_hdHasDone = false;
  s_hdRoot.clear();
}

void initialize() noexcept {
  build_index();
  start_hd();
  start_loader();
  start_dumper();
}

void shutdown() noexcept {
  stop_dumper();
  stop_loader();
  stop_hd();
  s_replacementIndex.clear();
  s_replacementCache.clear();
  s_failedKeys.clear();
  s_reportedMisses.clear();
  s_lookupMemo.clear();
  s_pendingTluts.clear();
  for (auto& tlut : s_loadedTluts) {
    tlut = {};
  }
  s_replacementLru.clear();
  s_replacementCacheBytes = 0;
  s_replacementRoot.clear();
  s_dumpRoot.clear();
}

// Mirrors of the guest palettes, kept only so find_replacement can hash and the DDS dump decode a
// CI palette. The renderer uploads from the guest pointer, so skip this work when replacement is off.
void register_tlut(const GXTlutObj* obj, const void* data, GXTlutFmt format, uint16_t entries) noexcept {
  if (!g_config.allowTextureReplacements) {
    return;
  }
  if (obj == nullptr || data == nullptr) {
    return;
  }

  // A registration is only consumed by a matching load_tlut, and the map is keyed by a guest address
  // the game may free and reuse, so a table this large means stale entries: drop them wholesale.
  constexpr size_t kMaxPendingTluts = 4096;
  if (s_pendingTluts.size() >= kMaxPendingTluts && !s_pendingTluts.contains(obj)) {
    s_pendingTluts.clear();
  }

  const size_t sz = static_cast<size_t>(entries) * 2;
  ByteBuffer buffer{sz};
  std::memcpy(buffer.data(), static_cast<const uint8_t*>(data), sz);
  s_pendingTluts[obj] = {
      .size = static_cast<uint32_t>(entries) * 2,
      .format = static_cast<uint32_t>(format),
      .entries = entries,
      .valid = true,
      .source = obj,
      .data = std::move(buffer),
  };
}

void load_tlut(const GXTlutObj* obj, uint32_t idx) noexcept {
  if (!g_config.allowTextureReplacements) {
    return;
  }
  if (idx >= s_loadedTluts.size()) {
    return;
  }

  // Consume the pending registration. The map is keyed by a guest object address that can be freed
  // or reused at any time, and entries used to live until shutdown.
  if (const auto it = s_pendingTluts.find(obj); it != s_pendingTluts.end()) {
    s_loadedTluts[idx] = std::move(it->second);
    s_pendingTluts.erase(it);
    return;
  }

  // The same object may legally be loaded into several hardware slots; only the first load finds the
  // pending entry, so recover the palette from whichever slot already holds it.
  for (const auto& loaded : s_loadedTluts) {
    if (!loaded.valid || loaded.source != obj) {
      continue;
    }
    s_loadedTluts[idx] = {
        .size = loaded.size,
        .format = loaded.format,
        .entries = loaded.entries,
        .valid = loaded.valid,
        .source = loaded.source,
        .data = loaded.data.clone(),
    };
    return;
  }

  s_loadedTluts[idx] = {};
}

// Time the renderer thread spends here, logged every 2 s while it is busy (HD Textures cost).
struct ReplacementStats {
  uint32_t calls = 0, memoHits = 0, misses = 0, ramHits = 0, diskLoads = 0;
  double keyMs = 0, missMs = 0, diskMs = 0;
  std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
} s_stats;

double ms_since(std::chrono::steady_clock::time_point t) noexcept {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

void log_stats() noexcept {
  const auto now = std::chrono::steady_clock::now();
  if (now - s_stats.last < std::chrono::seconds(2)) return;
  if (s_stats.calls != 0) {
    size_t queued = 0;
    {
      std::lock_guard lock(s_hdMutex);
      queued = s_hdQueue.size();
    }
    Log.info("texture replacement: {} lookups ({} memo, key {:.1f} ms), {} new ({:.1f} ms), {} ram, {} disk "
             "({:.1f} ms), ram cache {} MB, hd queue {}",
             s_stats.calls, s_stats.memoHits, s_stats.keyMs, s_stats.misses, s_stats.missMs, s_stats.ramHits, s_stats.diskLoads,
             s_stats.diskMs, s_replacementCacheBytes >> 20, queued);
  }
  s_stats = ReplacementStats{};
  s_stats.last = now;
}

std::optional<TextureHandle> find_replacement_impl(const GXTexObj_& obj, const LookupMemoKey* memoKey) noexcept {
  ZoneScoped;

  if (!g_config.allowTextureReplacements) {
    return std::nullopt;
  }

  log_stats();
  ++s_stats.calls;
  take_hd_done();
  take_load_done();
  if (memoKey != nullptr && memoKey->size < texture_base_level_size(obj)) {
    memoKey = nullptr;  // the digest must cover everything the key hashes
  }
  RuntimeTextureKey key;
  bool memoized = false;
  if (memoKey != nullptr) {
    if (const auto it = s_lookupMemo.find(*memoKey);
        it != s_lookupMemo.end() && it->second.generation == s_indexGeneration) {
      ++s_stats.memoHits;
      if (!it->second.found) {
        return std::nullopt;
      }
      key = it->second.key;
      memoized = true;
    }
  }
  auto t0 = std::chrono::steady_clock::now();
  if (!memoized) {
    key = build_runtime_key(obj);
  }
  const auto* path = find_replacement_path(key);
  s_stats.keyMs += ms_since(t0);
  if (memoKey != nullptr && !memoized) {
    if (s_lookupMemo.size() >= kLookupMemoMax) {
      s_lookupMemo.clear();
    }
    s_lookupMemo[*memoKey] = LookupMemo{.generation = s_indexGeneration, .found = path != nullptr, .key = key};
  }
  if (path == nullptr) {
    t0 = std::chrono::steady_clock::now();
    if (report_missing_key(key, obj)) {
      ++s_stats.misses;
    }
    s_stats.missMs += ms_since(t0);
    return std::nullopt;
  }

  if (const auto* cached = find_cached_replacement(key); cached != nullptr) {
    ++s_stats.ramHits;
    return *cached;
  }

  if (s_failedKeys.contains(key)) {
    return std::nullopt;
  }

  if (s_loadThread.joinable()) {
    if (!s_loadPending.contains(key)) {
      ++s_stats.diskLoads;
    }
    queue_load(key, *path);
    return std::nullopt;  // the original until the file is ready
  }
  t0 = std::chrono::steady_clock::now();
  auto handle = load_replacement_texture(key, *path);
  ++s_stats.diskLoads;
  s_stats.diskMs += ms_since(t0);
  if (!handle) {
    return std::nullopt;
  }

  cache_replacement(key, handle);
  return handle;
}

std::optional<TextureHandle> find_replacement(const GXTexObj_& obj) noexcept {
  return find_replacement_impl(obj, nullptr);
}

std::optional<TextureHandle> find_replacement(const GXTexObj_& obj, const GXTlutObj_& tlut) noexcept {
  s_drawTlut = &tlut;
  auto result = find_replacement_impl(obj, nullptr);
  s_drawTlut = nullptr;
  return result;
}

std::optional<TextureHandle> find_replacement(const GXTexObj_& obj, uint64_t digest, uint32_t digestSize) noexcept {
  const LookupMemoKey memoKey{.digest = digest, .size = digestSize, .width = obj.width(), .height = obj.height(),
                              .format = static_cast<uint32_t>(obj.format())};
  return find_replacement_impl(obj, digestSize != 0 ? &memoKey : nullptr);
}

std::optional<TextureHandle> find_replacement(const GXTexObj_& obj, const GXTlutObj_& tlut, uint64_t digest,
                                              uint32_t digestSize, uint64_t tlutDigest) noexcept {
  const LookupMemoKey memoKey{.digest = digest, .tlutDigest = tlutDigest, .size = digestSize, .width = obj.width(),
                              .height = obj.height(), .format = static_cast<uint32_t>(obj.format())};
  s_drawTlut = &tlut;
  auto result = find_replacement_impl(obj, digestSize != 0 && tlutDigest != 0 ? &memoKey : nullptr);
  s_drawTlut = nullptr;
  return result;
}

std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept {
  const RuntimeTextureKey key = build_runtime_key(obj);
  return format_replacement_filename(key);
}

} // namespace aurora::gfx::texture_replacement
