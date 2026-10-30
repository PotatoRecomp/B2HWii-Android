// Basara's angle wrap (802BF564): while f1 >= pi subtract 2pi, while f1 < -pi add 2pi, in single
// precision. An angle as large as -1.98e14 (seen in the SJ01 demo scene after ~3 min idle, via
// 8016B200 = obj->rot + delta) never changes when 2pi is added, so the guest loop spins forever and
// the game hangs. Same steps for ordinary inputs; a far-out input is first reduced with fmod, and the
// first ones are logged with the caller so the source of the bad angle can be traced.
#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kPiAddr = 0x8066776Cu;     // f0 = pi
constexpr uint32_t kTwoPiAddr = 0x80667768u;  // f2 = 2pi
constexpr uint32_t kNegPiAddr = 0x80667770u;  // f3 = -pi
constexpr uint32_t kLogLimit = 24;
std::atomic<uint32_t> g_logged{0};

void LogFarAngle(CpuContext* ctx, float value) {
    if (g_logged.fetch_add(1, std::memory_order_relaxed) >= kLogLimit) return;
    const uint32_t lr = ctx->lr;
    std::cerr << "[basara] angle wrap: far input " << value << std::hex << " lr=0x" << lr
              << " r3=0x" << ctx->gpr[3] << " r4=0x" << ctx->gpr[4] << " r5=0x" << ctx->gpr[5]
              << " r29=0x" << ctx->gpr[29] << " r30=0x" << ctx->gpr[30] << " r31=0x" << ctx->gpr[31];
    // 8016B200: rot(obj->0xB0 +20..+31) + delta(r5) -> stack r1+8..; caller lr at r1+0x34.
    if (lr >= 0x8016B284u && lr <= 0x8016B2A0u) {
        const uint32_t sp = ctx->gpr[1];
        const uint32_t node = Memory::Read32(ctx->gpr[29] + 0xB0u);
        std::cerr << " caller=0x" << Memory::Read32(sp + 0x34u) << std::dec << " node.rot=("
                  << Memory::ReadFloat32(node + 20u) << "," << Memory::ReadFloat32(node + 24u) << ","
                  << Memory::ReadFloat32(node + 28u) << ") delta=(" << Memory::ReadFloat32(ctx->gpr[5])
                  << "," << Memory::ReadFloat32(ctx->gpr[5] + 4u) << ","
                  << Memory::ReadFloat32(ctx->gpr[5] + 8u) << ")";
    }
    std::cerr << std::dec << std::endl;
}

} // namespace

extern "C" void BasaraAngleWrap_HLE_802bf564(CpuContext* ctx) {
    const float pi = Memory::ReadFloat32(kPiAddr);
    const float twoPi = Memory::ReadFloat32(kTwoPiAddr);
    const float negPi = Memory::ReadFloat32(kNegPiAddr);
    float value = static_cast<float>(ctx->fpr[1].d);
    if (std::isfinite(value) && std::fabs(value) > 4096.0f * twoPi) {
        LogFarAngle(ctx, value);
        value = static_cast<float>(std::remainder(static_cast<double>(value), static_cast<double>(twoPi)));
    } else if (std::isinf(value)) {
        LogFarAngle(ctx, value);
        value = 0.0f;
    }
    if (value >= pi) {
        do {
            value = value - twoPi;
        } while (value >= pi);
    } else if (value < negPi) {
        do {
            value = value + twoPi;
        } while (value < negPi);
    }
    ctx->fpr[0].d = pi;
    ctx->fpr[2].d = twoPi;
    ctx->fpr[3].d = negPi;
    ctx->fpr[1].d = value;
}
PPC_NATIVE_OVERRIDE_VOID(802BF564, BasaraAngleWrap_HLE_802bf564, (CpuContext* ctx), (ctx));
