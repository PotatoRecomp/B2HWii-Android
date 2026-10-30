// Basara's movie player (Sofdec) converts each decoded YUV 4:2:0 picture into a GX RGBA8 texture
// through 80407C38. The default path (80407CD4) looks every Y, U and V byte up in float tables and
// writes pixels two at a time with psq_st; translated, that was a fifth of the CPU during a movie
// and a G81 phone decoded about 22 of the 30 pictures a second. Same tables and arithmetic here, on
// host pointers. The custom-table path (80408134) still runs translated.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "ppc_runtime.h"
#include "memory.h"
#include "runtime_log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kTables = 0x806287C0u;     // 5 x 256 floats: Y, G(U), B(U), R(V), G(V)
constexpr uint32_t kSavedGqr4 = kTables + 0x1400u;
constexpr uint32_t kAlpha = 0x804A4E48u;
constexpr uint32_t kCustomTablePath = 0x80408134u;

float LoadFloat(const uint8_t* p) {
    uint32_t bits = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// psq_st with GQR type u8, scale 0: clamp, then truncate.
uint8_t Quantize(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
}

int32_t Div4(int32_t value) {
    return value / 4;  // srawi 2 + addze: rounds toward zero
}

void ConvertDefault(uint32_t src, uint32_t dst) {
    const uint32_t yPlane = Memory::Read32(src + 4u);
    const uint32_t uPlane = Memory::Read32(src + 0x14u);
    const uint32_t vPlane = Memory::Read32(src + 0x24u);
    const int32_t width = static_cast<int32_t>(Memory::Read32(src + 0x10u));
    const int32_t chromaPitch = static_cast<int32_t>(Memory::Read32(src + 0x20u));
    uint32_t out = Memory::Read32(dst + 4u);
    const int32_t height = static_cast<int32_t>(Memory::Read32(dst + 0xCu));
    const int32_t outWidth = static_cast<int32_t>(Memory::Read32(dst + 0x10u));

    float table[5][256];
    const uint8_t* raw = Memory::GetPointer(kTables, sizeof(table));
    for (int t = 0; t < 5; ++t) {
        for (int i = 0; i < 256; ++i) {
            table[t][i] = LoadFloat(raw + (t * 256 + i) * 4);
        }
    }
    const float* tY = table[0];
    const float* tGU = table[1];
    const float* tBU = table[2];
    const float* tRV = table[3];
    const float* tGV = table[4];
    const uint8_t alpha = Quantize(LoadFloat(Memory::GetPointer(kAlpha, 4)));

    const int32_t tiles = Div4(width);
    const int32_t tileRows = Div4(height);
    const uint32_t yStride = static_cast<uint32_t>(width) & ~3u;
    const uint32_t yStride2 = (static_cast<uint32_t>(width) << 1) & ~7u;
    const uint32_t yRowAdvance = ((static_cast<uint32_t>(width) << 2) & ~15u) - yStride;
    const uint32_t chromaNext = static_cast<uint32_t>(chromaPitch) & ~1u;
    const uint32_t chromaAdvance = (static_cast<uint32_t>(chromaPitch) << 1) & ~3u;
    const uint32_t outSkip = static_cast<uint32_t>(Div4(outWidth - width)) << 6;

    uint32_t y = yPlane;
    uint32_t u = uPlane;
    uint32_t v = vPlane;
    for (int32_t row = 0; row < tileRows; ++row) {
        if (tiles > 0) {
            const uint32_t n = static_cast<uint32_t>(tiles);
            const uint8_t* yRows[4] = {
                Memory::GetPointer(y, n * 4u),
                Memory::GetPointer(y + yStride, n * 4u),
                Memory::GetPointer(y + yStride2, n * 4u),
                Memory::GetPointer(y + yStride2 + yStride, n * 4u),
            };
            // Rows 0-1 share the first chroma row, rows 2-3 the next one.
            const uint8_t* uRows[2] = {Memory::GetPointer(u, n * 2u), Memory::GetPointer(u + chromaNext, n * 2u)};
            const uint8_t* vRows[2] = {Memory::GetPointer(v, n * 2u), Memory::GetPointer(v + chromaNext, n * 2u)};
            uint8_t* tile = Memory::GetPointer(out, n * 64u);
            for (uint32_t i = 0; i < n; ++i, tile += 64) {
                uint8_t* ar = tile;
                uint8_t* gb = tile + 32;
                for (int r = 0; r < 4; ++r) {
                    const uint8_t* yb = yRows[r] + i * 4u;
                    const uint8_t* ub = uRows[r >> 1] + i * 2u;
                    const uint8_t* vb = vRows[r >> 1] + i * 2u;
                    for (int k = 0; k < 4; ++k) {
                        const float luma = tY[yb[k]];
                        const uint8_t cu = ub[k >> 1];
                        const uint8_t cv = vb[k >> 1];
                        const int o = r * 8 + k * 2;
                        ar[o] = alpha;
                        ar[o + 1] = Quantize(luma + tRV[cv]);
                        gb[o] = Quantize(luma + (tGV[cv] + tGU[cu]));
                        gb[o + 1] = Quantize(luma + tBU[cu]);
                    }
                }
            }
            y += n * 4u;
            out += n * 64u;
        }
        y += yRowAdvance;
        u += chromaAdvance;
        v += chromaAdvance;
        out += outSkip;
    }
}

} // namespace

extern "C" void BasaraMovieToRgba8_HLE_80407c38(CpuContext* ctx) {
    const uint32_t src = ctx->gpr[3];
    const uint32_t dst = ctx->gpr[4];
    const uint32_t customTables = Memory::Read32(ctx->gpr[5]);
    if (customTables == 0u) {
        // The guest leaves its GQR4 here while it borrows the register; keep that visible.
        Memory::Write32(kSavedGqr4, ctx->gqr[4]);
        ConvertDefault(src, dst);
        return;
    }

    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        RT_LOGF(RT_TAG_OS, "movie: custom YUV tables 0x%08X, converting through the guest path\n", customTables);
    }
    // Same frame as the original: the source fields at sp+0x20, the destination fields at sp+8.
    const uint32_t savedSp = ctx->gpr[1];
    const uint32_t sp = savedSp - 0x50u;
    Memory::Write32(sp, savedSp);
    static constexpr uint32_t kSrcFields[6] = {4u, 0x14u, 0x24u, 0x10u, 0x20u, 0x30u};
    for (uint32_t i = 0; i < 6; ++i) {
        Memory::Write32(sp + 0x20u + i * 4u, Memory::Read32(src + kSrcFields[i]));
    }
    for (uint32_t i = 0; i < 4; ++i) {
        Memory::Write32(sp + 8u + i * 4u, Memory::Read32(dst + 4u + i * 4u));
    }
    ctx->gpr[1] = sp;
    ctx->gpr[3] = sp + 0x20u;
    ctx->gpr[4] = sp + 8u;
    ctx->gpr[5] = customTables;
    InvokeIndirectCpu(kCustomTablePath, ctx);
    ctx->gpr[1] = savedSp;
}
PPC_NATIVE_OVERRIDE_VOID(80407C38, BasaraMovieToRgba8_HLE_80407c38, (CpuContext* ctx), (ctx));
