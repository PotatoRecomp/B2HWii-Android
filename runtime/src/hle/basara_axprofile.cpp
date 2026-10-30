// Basara's __AXGetCurrentProfile (80375FF4): when the profile flag *0x80662D28 is set it returns
// &buffer[index * 0x38] (buffer *0x80662D34, index *0x80662D2C wrapping at *0x80662D30) and the AX
// frame code copies an AXPROFILE (OSTime stamps) there. Nothing in the DOL writes the flag, the
// buffer or the count (AXInitProfile is not linked), so profiling is always off in the game; in
// the SJ01 demo scene those words get stray values and the copies land in .data/.text (the render
// bucket counts at 0x8049C270 among them). Always "no profile" here; stray values are logged once.
#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"

#include <atomic>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kProfileFlagAddr = 0x80662D28u;
std::atomic<bool> g_logged{false};

} // namespace

extern "C" void BasaraAxProfile_HLE_80375ff4(CpuContext* ctx) {
    const uint32_t flag = Memory::Read32(kProfileFlagAddr);
    if (flag != 0 && !g_logged.exchange(true, std::memory_order_relaxed)) {
        std::cerr << "[basara] AX profile words are set (game never sets them): flag=0x" << std::hex
                  << flag << " index=0x" << Memory::Read32(kProfileFlagAddr + 4u) << " count=0x"
                  << Memory::Read32(kProfileFlagAddr + 8u) << " buffer=0x"
                  << Memory::Read32(kProfileFlagAddr + 12u) << " lr=0x" << ctx->lr << std::dec
                  << std::endl;
    }
    ctx->gpr[0] = flag;
    ctx->gpr[3] = 0;
}
PPC_NATIVE_OVERRIDE_VOID(80375FF4, BasaraAxProfile_HLE_80375ff4, (CpuContext* ctx), (ctx));
