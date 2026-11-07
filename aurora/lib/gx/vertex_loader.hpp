#pragma once

// CPU vertex loader (modeled on Dolphin's VideoCommon/VertexLoader): GX vertex streams and the
// indexed arrays they reference are decoded on the CPU into a fixed native layout that the vertex
// shader receives through ordinary vertex inputs. The storage-buffer vertex pulling path reads raw
// big-endian bytes per vertex in the shader instead, which mobile GPUs (Adreno 6xx above all) run
// through their texture/L1 path, far slower than their fixed-function vertex fetch.
//
// Native layout, in location order (every element is 4-byte aligned):
//   matrix indices  uint8x4 per group of four present PNMTXIDX/TEXnMTXIDX bytes (raw values)
//   POS             float32x3 (XY formats get z = 0)
//   NRM             float32x3, plus binormal and tangent for NBT formats
//   CLR0, CLR1      unorm8x4
//   TEX0..TEX7      float32x2 (S formats get t = 0)

#include "gx.hpp"

#include <array>

namespace aurora::gx {
struct NativeVertexLayout {
  static constexpr u8 None = 0xFF;
  static constexpr u32 MaxLocations = 16;

  enum Format : u8 { Uint8x4, Unorm8x4, Float32x2, Float32x3 };
  struct Location {
    u8 offset = 0;
    Format format = Uint8x4;
  };

  u8 stride = 0;
  u8 locationCount = 0;
  std::array<Location, MaxLocations> locations{};
  // Byte index of each matrix-index attribute (GX_VA_PNMTXIDX..GX_VA_TEX7MTXIDX) within the packed
  // matrix block, None when absent; its location is slot / 4 and its component slot % 4.
  std::array<u8, GX_VA_TEX7MTXIDX + 1> mtxSlot{};
  u8 posLocation = None;
  u8 nrmLocation = None; // NBT formats use nrmLocation .. nrmLocation + 2
  u8 nrmCount = 0;
  std::array<u8, 2> clrLocation{};
  std::array<u8, MaxTexCoord> texLocation{};
  bool valid = false;
};

// Layout for a draw described by config.attrs. valid is false when the format needs more vertex
// locations than WebGPU guarantees.
NativeVertexLayout native_vertex_layout(const ShaderConfig& config) noexcept;

struct VertexDecodeStep {
  using Fn = void (*)(const VertexDecodeStep& step, const u8* src, u32 rawStride, u32 count, u8* dst,
                      u32 dstStride);
  Fn fn = nullptr;
  float scale = 1.f;
  u16 elemOffset = 0; // added to the array element address (NBT groups)
  u8 attr = 0;        // GX attribute whose array an indexed step reads
  u8 srcOffset = 0;   // offset of the direct data, or of the index, in the raw vertex
  u8 dstOffset = 0;
  u8 arrayStride = 0;
};

// A decoder for one vertex format, derived from the same ShaderConfig attrs as its shader.
struct VertexDecoder {
  u32 rawStride = 0;
  u32 nativeStride = 0;
  u32 stepCount = 0;
  std::array<VertexDecodeStep, 24> steps{};
};

void build_vertex_decoder(VertexDecoder& out, const ShaderConfig& config, const NativeVertexLayout& layout) noexcept;
// Decodes vtxCount raw vertices (big-endian GX stream) into dst (vtxCount * nativeStride bytes),
// reading indexed attributes from the arrays currently bound in g_gxState.
void decode_vertices(const VertexDecoder& decoder, const u8* src, u32 vtxCount, u8* dst) noexcept;
} // namespace aurora::gx
