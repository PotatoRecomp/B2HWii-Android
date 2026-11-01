// Basara's blocking CRI file load (8002D010): opens the file, allocates, starts the read, then polls
// ADXM (804070F0), the disc error check (803597D8) and the file status (803ED6A0) until it is 3. The
// CRI file server advances one step per retrace, so a character's files (voice, portrait) held the
// game ~680 ms on the character select at 60 retraces a second, and the voice started in that
// stall came out slow and broken. Same steps here, with retraces every 2 ms while it polls (as at
// startup: no frame is drawn meanwhile). BASARA_FAST_LOAD=0 keeps the normal retrace rate.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "ppc_runtime.h"
#include "memory.h"
#include "runtime_log.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>

namespace {

bool FastLoad() {
    static const bool on = [] {
        const char* value = std::getenv("BASARA_FAST_LOAD");
        return !(value && *value == '0');
    }();
    return on;
}

} // namespace

extern "C" void BasaraCriLoad_HLE_8002d010(CpuContext* ctx) {
    const uint32_t dest = ctx->gpr[5];
    const uint32_t sizeOut = ctx->gpr[6];

    ctx->gpr[3] = ctx->gpr[7];
    InvokeIndirectCpu(0x803EC6C0u, ctx);  // open
    const uint32_t handle = ctx->gpr[3];
    InvokeIndirectCpu(0x803ED5A4u, ctx);  // size in sectors
    const uint32_t bytes = ctx->gpr[3] << 11;

    ctx->gpr[3] = 0x80573FACu;
    ctx->gpr[4] = bytes;
    ctx->gpr[5] = dest;
    ctx->gpr[6] = 0;
    ctx->gpr[7] = 0x40;
    ctx->gpr[8] = 0x80493370u;
    ctx->gpr[9] = 0x200;
    InvokeIndirectCpu(0x80168378u, ctx);  // buffer
    const uint32_t buffer = ctx->gpr[3];

    ctx->gpr[3] = handle;
    ctx->gpr[4] = static_cast<uint32_t>(static_cast<int32_t>(bytes) / 2048);
    ctx->gpr[5] = buffer;
    InvokeIndirectCpu(0x803ED120u, ctx);  // start the read

    const auto start = std::chrono::steady_clock::now();
    const int64_t previous = FastLoad() ? VI_HLE_BeginLoadRetraces() : 0;
    uint32_t polls = 0;
    try {
        do {
            InvokeIndirectCpu(0x804070F0u, ctx);
            ctx->gpr[3] = 0x80590304u;
            InvokeIndirectCpu(0x803597D8u, ctx);
            ctx->gpr[3] = handle;
            InvokeIndirectCpu(0x803ED6A0u, ctx);
            ++polls;
        } while (static_cast<int32_t>(ctx->gpr[3]) != 3);
    } catch (...) {
        if (FastLoad()) {
            VI_HLE_SetCatchUpRetrace(previous);
        }
        throw;
    }
    if (FastLoad()) {
        VI_HLE_SetCatchUpRetrace(previous);
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    static int s_logs = 0;
    if (ms > 30.0 && s_logs < 200) {
        ++s_logs;
        RT_LOGF(RT_TAG_VI, "cri load: %u KB in %.1f ms (%u polls)\n", bytes / 1024u, ms, polls);
    }

    ctx->gpr[3] = handle;
    InvokeIndirectCpu(0x803ECA08u, ctx);  // close
    if (sizeOut != 0) {
        Memory::Write32(sizeOut, bytes);
    }
    ctx->gpr[3] = buffer;
}
PPC_NATIVE_OVERRIDE_VOID(8002D010, BasaraCriLoad_HLE_8002d010, (CpuContext* ctx), (ctx));
