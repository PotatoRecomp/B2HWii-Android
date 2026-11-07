#include "vertex_loader.hpp"

#include <bit>
#include <cmath>
#include <cstring>

namespace aurora::gx {
namespace {
enum class Src : u8 { Direct, Index8, Index16 };

constexpr bool valid_vec_type(u8 compType) noexcept { return compType <= GX_F32; }
constexpr bool valid_color_type(u8 compType) noexcept { return compType <= GX_RGBA8; }

constexpr u32 vec_comp_size(u8 compType) noexcept {
  switch (compType) {
  case GX_U8:
  case GX_S8:
    return 1;
  case GX_U16:
  case GX_S16:
    return 2;
  default:
    return 4;
  }
}

constexpr u32 color_size(u8 compType) noexcept {
  switch (compType) {
  case GX_RGB565:
  case GX_RGBA4:
    return 2;
  case GX_RGB8:
  case GX_RGBA6:
    return 3;
  default:
    return 4;
  }
}

template <bool LE>
inline u16 read16(const u8* p) noexcept {
  u16 v;
  std::memcpy(&v, p, 2);
  if constexpr (!LE) v = static_cast<u16>((v >> 8) | (v << 8));
  return v;
}

template <bool LE>
inline u32 read32(const u8* p) noexcept {
  u32 v;
  std::memcpy(&v, p, 4);
  if constexpr (!LE) v = __builtin_bswap32(v);
  return v;
}

template <typename T, bool LE>
inline float read_comp(const u8* p) noexcept {
  if constexpr (std::is_same_v<T, u8>) {
    return static_cast<float>(p[0]);
  } else if constexpr (std::is_same_v<T, s8>) {
    return static_cast<float>(static_cast<s8>(p[0]));
  } else if constexpr (std::is_same_v<T, u16>) {
    return static_cast<float>(read16<LE>(p));
  } else if constexpr (std::is_same_v<T, s16>) {
    return static_cast<float>(static_cast<s16>(read16<LE>(p)));
  } else {
    return std::bit_cast<float>(read32<LE>(p));
  }
}

// Returns the element address of an indexed attribute, or nullptr when the index leaves the array.
template <Src S>
inline const u8* element(const VertexDecodeStep& step, const u8* vtx, const u8* base, u32 limit, u32 bytes) noexcept {
  if constexpr (S == Src::Direct) {
    return vtx;
  } else {
    // Indices in the vertex stream are big-endian like the rest of it.
    const u32 idx = S == Src::Index8 ? vtx[0] : (static_cast<u32>(vtx[0]) << 8 | vtx[1]);
    const u32 off = idx * step.arrayStride + step.elemOffset;
    if (base == nullptr || off + bytes > limit) UNLIKELY {
      return nullptr;
    }
    return base + off;
  }
}

template <typename T, int NIn, int NOut, Src S, bool LE>
void decode_vec(const VertexDecodeStep& step, const u8* src, u32 rawStride, u32 count, u8* dst, u32 dstStride) {
  const auto& array = g_gxState.arrays[step.attr];
  const u8* base = static_cast<const u8*>(array.data);
  const u32 limit = array.size;
  constexpr u32 bytes = NIn * sizeof(T);
  const float scale = step.scale;
  src += step.srcOffset;
  dst += step.dstOffset;
  for (u32 i = 0; i < count; ++i, src += rawStride, dst += dstStride) {
    float out[NOut]{};
    if (const u8* p = element<S>(step, src, base, limit, bytes)) LIKELY {
      for (int c = 0; c < NIn; ++c) {
        if constexpr (std::is_same_v<T, float>) {
          out[c] = read_comp<T, LE>(p + c * sizeof(T));
        } else {
          out[c] = read_comp<T, LE>(p + c * sizeof(T)) * scale;
        }
      }
    }
    std::memcpy(dst, out, sizeof(out));
  }
}

// Expands an n-bit channel to 8 bits the way round(v / max * 255) does.
template <u32 Bits>
constexpr u8 expand(u32 v) noexcept {
  constexpr u32 max = (1u << Bits) - 1;
  return static_cast<u8>((v * 255u + max / 2) / max);
}

template <u8 CompType, Src S, bool LE>
void decode_color(const VertexDecodeStep& step, const u8* src, u32 rawStride, u32 count, u8* dst, u32 dstStride) {
  const auto& array = g_gxState.arrays[step.attr];
  const u8* base = static_cast<const u8*>(array.data);
  const u32 limit = array.size;
  constexpr u32 bytes = color_size(CompType);
  src += step.srcOffset;
  dst += step.dstOffset;
  for (u32 i = 0; i < count; ++i, src += rawStride, dst += dstStride) {
    u8 out[4]{0, 0, 0, 0};
    if (const u8* p = element<S>(step, src, base, limit, bytes)) LIKELY {
      if constexpr (CompType == GX_RGB565) {
        const u32 v = read16<LE>(p);
        out[0] = expand<5>((v >> 11) & 0x1F);
        out[1] = expand<6>((v >> 5) & 0x3F);
        out[2] = expand<5>(v & 0x1F);
        out[3] = 255;
      } else if constexpr (CompType == GX_RGB8 || CompType == GX_RGBX8) {
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        out[3] = 255;
      } else if constexpr (CompType == GX_RGBA4) {
        const u32 v = read16<LE>(p);
        out[0] = static_cast<u8>(((v >> 12) & 0xF) * 17);
        out[1] = static_cast<u8>(((v >> 8) & 0xF) * 17);
        out[2] = static_cast<u8>(((v >> 4) & 0xF) * 17);
        out[3] = static_cast<u8>((v & 0xF) * 17);
      } else if constexpr (CompType == GX_RGBA6) {
        const u32 v = LE ? (p[0] | p[1] << 8 | p[2] << 16) : (p[0] << 16 | p[1] << 8 | p[2]);
        out[0] = expand<6>((v >> 18) & 0x3F);
        out[1] = expand<6>((v >> 12) & 0x3F);
        out[2] = expand<6>((v >> 6) & 0x3F);
        out[3] = expand<6>(v & 0x3F);
      } else {
        std::memcpy(out, p, 4);
      }
    }
    std::memcpy(dst, out, 4);
  }
}

// One uint8x4 group of raw matrix-index bytes.
void decode_mtx(const VertexDecodeStep& step, const u8* src, u32 rawStride, u32 count, u8* dst, u32 dstStride) {
  // srcOffset, arrayStride, attr and the low byte of elemOffset carry the four source offsets.
  const u8 offs[4] = {step.srcOffset, step.arrayStride, step.attr, static_cast<u8>(step.elemOffset)};
  dst += step.dstOffset;
  for (u32 i = 0; i < count; ++i, src += rawStride, dst += dstStride) {
    for (int c = 0; c < 4; ++c) {
      dst[c] = offs[c] == NativeVertexLayout::None ? 0 : src[offs[c]];
    }
  }
}

template <int NIn, int NOut, Src S, bool LE>
VertexDecodeStep::Fn pick_vec_type(u8 compType) noexcept {
  switch (compType) {
  case GX_U8:
    return decode_vec<u8, NIn, NOut, S, LE>;
  case GX_S8:
    return decode_vec<s8, NIn, NOut, S, LE>;
  case GX_U16:
    return decode_vec<u16, NIn, NOut, S, LE>;
  case GX_S16:
    return decode_vec<s16, NIn, NOut, S, LE>;
  default:
    return decode_vec<float, NIn, NOut, S, LE>;
  }
}

template <int NIn, int NOut>
VertexDecodeStep::Fn pick_vec_src(u8 compType, Src src, bool le) noexcept {
  switch (src) {
  case Src::Direct:
    return pick_vec_type<NIn, NOut, Src::Direct, false>(compType);
  case Src::Index8:
    return le ? pick_vec_type<NIn, NOut, Src::Index8, true>(compType)
              : pick_vec_type<NIn, NOut, Src::Index8, false>(compType);
  default:
    return le ? pick_vec_type<NIn, NOut, Src::Index16, true>(compType)
              : pick_vec_type<NIn, NOut, Src::Index16, false>(compType);
  }
}

VertexDecodeStep::Fn pick_vec(u8 compType, u8 nin, u8 nout, Src src, bool le) noexcept {
  if (nout == 3) {
    return nin == 2 ? pick_vec_src<2, 3>(compType, src, le) : pick_vec_src<3, 3>(compType, src, le);
  }
  return nin == 1 ? pick_vec_src<1, 2>(compType, src, le) : pick_vec_src<2, 2>(compType, src, le);
}

template <Src S, bool LE>
VertexDecodeStep::Fn pick_color_type(u8 compType) noexcept {
  switch (compType) {
  case GX_RGB565:
    return decode_color<GX_RGB565, S, LE>;
  case GX_RGB8:
    return decode_color<GX_RGB8, S, LE>;
  case GX_RGBX8:
    return decode_color<GX_RGBX8, S, LE>;
  case GX_RGBA4:
    return decode_color<GX_RGBA4, S, LE>;
  case GX_RGBA6:
    return decode_color<GX_RGBA6, S, LE>;
  default:
    return decode_color<GX_RGBA8, S, LE>;
  }
}

VertexDecodeStep::Fn pick_color(u8 compType, Src src, bool le) noexcept {
  switch (src) {
  case Src::Direct:
    return pick_color_type<Src::Direct, false>(compType);
  case Src::Index8:
    return le ? pick_color_type<Src::Index8, true>(compType) : pick_color_type<Src::Index8, false>(compType);
  default:
    return le ? pick_color_type<Src::Index16, true>(compType) : pick_color_type<Src::Index16, false>(compType);
  }
}

Src source_of(const AttrConfig& mapping) noexcept {
  return mapping.attrType == GX_INDEX8 ? Src::Index8 : mapping.attrType == GX_INDEX16 ? Src::Index16 : Src::Direct;
}

constexpr u8 format_size(NativeVertexLayout::Format format) noexcept {
  switch (format) {
  case NativeVertexLayout::Float32x3:
    return 12;
  case NativeVertexLayout::Float32x2:
    return 8;
  default:
    return 4;
  }
}
} // namespace

NativeVertexLayout native_vertex_layout(const ShaderConfig& config) noexcept {
  NativeVertexLayout layout{};
  layout.mtxSlot.fill(NativeVertexLayout::None);
  layout.clrLocation.fill(NativeVertexLayout::None);
  layout.texLocation.fill(NativeVertexLayout::None);
  bool fits = true;
  const auto add = [&](NativeVertexLayout::Format format) -> u8 {
    if (layout.locationCount >= NativeVertexLayout::MaxLocations) {
      fits = false;
      return NativeVertexLayout::None;
    }
    const u8 location = layout.locationCount++;
    layout.locations[location] = {layout.stride, format};
    layout.stride = static_cast<u8>(layout.stride + format_size(format));
    return location;
  };

  u8 mtxCount = 0;
  for (int a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7MTXIDX; ++a) {
    const auto type = config.attrs[a].attrType;
    if (type == GX_NONE) continue;
    if (type != GX_DIRECT) return {}; // GX only streams matrix indices directly
    layout.mtxSlot[a] = mtxCount++;
  }
  for (u8 g = 0; g < (mtxCount + 3) / 4; ++g) add(NativeVertexLayout::Uint8x4);

  if (const auto& pos = config.attrs[GX_VA_POS]; pos.attrType != GX_NONE) {
    if (!valid_vec_type(pos.compType) || (pos.cnt != 2 && pos.cnt != 3)) return {};
    layout.posLocation = add(NativeVertexLayout::Float32x3);
  }
  if (const auto& nrm = config.attrs[GX_VA_NRM]; nrm.attrType != GX_NONE) {
    if (!valid_vec_type(nrm.compType) || (nrm.cnt != 3 && nrm.cnt != 9)) return {};
    layout.nrmCount = nrm.cnt == 9 ? 3 : 1;
    layout.nrmLocation = add(NativeVertexLayout::Float32x3);
    for (u8 g = 1; g < layout.nrmCount; ++g) add(NativeVertexLayout::Float32x3);
  }
  for (int c = 0; c < 2; ++c) {
    const auto& clr = config.attrs[GX_VA_CLR0 + c];
    if (clr.attrType == GX_NONE) continue;
    if (!valid_color_type(clr.compType)) return {};
    layout.clrLocation[c] = add(NativeVertexLayout::Unorm8x4);
  }
  for (u32 t = 0; t < MaxTexCoord; ++t) {
    const auto& tex = config.attrs[GX_VA_TEX0 + t];
    if (tex.attrType == GX_NONE) continue;
    if (!valid_vec_type(tex.compType) || (tex.cnt != 1 && tex.cnt != 2)) return {};
    layout.texLocation[t] = add(NativeVertexLayout::Float32x2);
  }
  layout.valid = fits && layout.locationCount > 0;
  return layout;
}

void build_vertex_decoder(VertexDecoder& out, const ShaderConfig& config, const NativeVertexLayout& layout) noexcept {
  out = {};
  if (!layout.valid) return;
  out.rawStride = config.vtxStride;
  out.nativeStride = layout.stride;
  const auto push = [&](const VertexDecodeStep& step) { out.steps[out.stepCount++] = step; };
  const auto vec_step = [&](GXAttr attr, u8 nin, u8 nout, u8 srcOffset, u16 elemOffset, u8 location) {
    const auto& mapping = config.attrs[attr];
    const Src src = source_of(mapping);
    push(VertexDecodeStep{
        .fn = pick_vec(mapping.compType, nin, nout, src, src != Src::Direct && mapping.le),
        .scale = mapping.compType == GX_F32 ? 1.f : std::ldexp(1.f, -static_cast<int>(mapping.frac)),
        .elemOffset = elemOffset,
        .attr = static_cast<u8>(attr),
        .srcOffset = srcOffset,
        .dstOffset = layout.locations[location].offset,
        .arrayStride = mapping.stride,
    });
  };

  // Matrix-index groups: the four source offsets ride in srcOffset/arrayStride/attr/elemOffset.
  std::array<u8, 12> mtxSrc{};
  mtxSrc.fill(NativeVertexLayout::None);
  for (int a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7MTXIDX; ++a) {
    if (layout.mtxSlot[a] != NativeVertexLayout::None) mtxSrc[layout.mtxSlot[a]] = config.attrs[a].offset;
  }
  u8 location = 0;
  for (u8 g = 0; location < layout.locationCount && layout.locations[location].format == NativeVertexLayout::Uint8x4;
       ++g, ++location) {
    push(VertexDecodeStep{
        .fn = decode_mtx,
        .elemOffset = mtxSrc[g * 4 + 3],
        .attr = mtxSrc[g * 4 + 2],
        .srcOffset = mtxSrc[g * 4 + 0],
        .dstOffset = layout.locations[location].offset,
        .arrayStride = mtxSrc[g * 4 + 1],
    });
  }

  if (layout.posLocation != NativeVertexLayout::None) {
    const auto& pos = config.attrs[GX_VA_POS];
    vec_step(GX_VA_POS, pos.cnt, 3, pos.offset, 0, layout.posLocation);
  }
  if (layout.nrmLocation != NativeVertexLayout::None) {
    const auto& nrm = config.attrs[GX_VA_NRM];
    const u32 groupBytes = 3 * vec_comp_size(nrm.compType);
    const Src src = source_of(nrm);
    const u32 indexSize = src == Src::Index16 ? 2 : 1;
    for (u8 g = 0; g < layout.nrmCount; ++g) {
      u32 srcOffset = nrm.offset;
      u32 elemOffset = 0;
      if (src == Src::Direct) {
        srcOffset += g * groupBytes;
      } else if (nrm.nrmIndexCount == 3) {
        srcOffset += g * indexSize; // GX_NRM_NBT3: one index per group
      } else {
        elemOffset = g * groupBytes;
      }
      vec_step(GX_VA_NRM, 3, 3, static_cast<u8>(srcOffset), static_cast<u16>(elemOffset),
               static_cast<u8>(layout.nrmLocation + g));
    }
  }
  for (int c = 0; c < 2; ++c) {
    if (layout.clrLocation[c] == NativeVertexLayout::None) continue;
    const auto attr = static_cast<GXAttr>(GX_VA_CLR0 + c);
    const auto& clr = config.attrs[attr];
    const Src src = source_of(clr);
    push(VertexDecodeStep{
        .fn = pick_color(clr.compType, src, src != Src::Direct && clr.le),
        .attr = static_cast<u8>(attr),
        .srcOffset = clr.offset,
        .dstOffset = layout.locations[layout.clrLocation[c]].offset,
        .arrayStride = clr.stride,
    });
  }
  for (u32 t = 0; t < MaxTexCoord; ++t) {
    if (layout.texLocation[t] == NativeVertexLayout::None) continue;
    const auto attr = static_cast<GXAttr>(GX_VA_TEX0 + t);
    vec_step(attr, config.attrs[attr].cnt, 2, config.attrs[attr].offset, 0, layout.texLocation[t]);
  }
}

void decode_vertices(const VertexDecoder& decoder, const u8* src, u32 vtxCount, u8* dst) noexcept {
  for (u32 i = 0; i < decoder.stepCount; ++i) {
    const auto& step = decoder.steps[i];
    step.fn(step, src, decoder.rawStride, vtxCount, dst, decoder.nativeStride);
  }
}
} // namespace aurora::gx
