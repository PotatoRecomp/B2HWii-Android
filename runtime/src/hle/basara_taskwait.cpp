// Basara's "wait N frames" for its cooperative tasks (802BF9D4). Each of the 4 task slots at
// 0x80574518 (0x340 bytes) holds an OSThread at +0x18; the guest finds its own slot through the
// current-task pointer *0x80662830, which the dispatcher (802BF7B4) only sets while it runs and
// resets to -1 when done. When the HOME menu closes, its task ends up waiting with the pointer
// already at -1, sleeps on queue -1 + 0x330 and is never woken: a black screen. Same steps here, but
// the slot comes from the running thread when the pointer is not a slot.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "ppc_runtime.h"
#include "memory.h"
#include "runtime_log.h"

#include <cstdint>

extern "C" void OSResumeThread_HLE_801aa58c(CpuContext* ctx);
extern "C" void OSSuspendThread_HLE_801aa6a8(CpuContext* ctx);
extern "C" void OSSleepThread_HLE_801aa9b8(CpuContext* ctx);

namespace {

constexpr uint32_t kSlots = 0x80574518u;
constexpr uint32_t kSlotSize = 0x340u;
constexpr uint32_t kSlotCount = 4u;
constexpr uint32_t kSlotThread = 0x18u;
constexpr uint32_t kSlotQueue = 0x330u;
constexpr uint32_t kCurrentTask = 0x80662830u;
constexpr uint32_t kMainThread = 0x80662834u;
constexpr uint32_t kRunningThread = 0x800000E4u;
constexpr uint32_t kTaskSemaphore = 0x80575218u;

bool IsSlot(uint32_t task) {
    return task >= kSlots && task < kSlots + kSlotCount * kSlotSize && (task - kSlots) % kSlotSize == 0u;
}

} // namespace

extern "C" void BasaraTaskWait_HLE_802bf9d4(CpuContext* ctx) {
    const uint32_t frames = ctx->gpr[3];
    if (frames == 0u) {
        return;
    }
    uint32_t task = Memory::Read32(kCurrentTask);
    if (!IsSlot(task)) {
        task = Memory::Read32(kRunningThread) - kSlotThread;
        if (!IsSlot(task)) {
            RT_LOGF(RT_TAG_OS, "task wait: thread 0x%08X is not a task; not waiting\n", task + kSlotThread);
            return;
        }
        Memory::Write32(kCurrentTask, task);
    }
    Memory::Write16(task + 0xAu, static_cast<uint16_t>(frames));
    Memory::Write8(task + 4u, static_cast<uint8_t>((Memory::Read8(task + 4u) & 0x80u) | 2u));
    if (const uint32_t main = Memory::Read32(kMainThread)) {
        ctx->gpr[3] = main;
        OSResumeThread_HLE_801aa58c(ctx);
    }
    if (Memory::Read8(task + 5u) >= 0x10u) {
        ctx->gpr[3] = kTaskSemaphore;
        InvokeIndirectCpu(0x803625A4u, ctx);
    }
    ctx->gpr[3] = task + kSlotQueue;
    OSSleepThread_HLE_801aa9b8(ctx);
    if (const uint32_t main = Memory::Read32(kMainThread)) {
        ctx->gpr[3] = main;
        OSSuspendThread_HLE_801aa6a8(ctx);
    }
    InvokeIndirectCpu(0x803807ECu, ctx);
}
PPC_NATIVE_OVERRIDE_VOID(802BF9D4, BasaraTaskWait_HLE_802bf9d4, (CpuContext* ctx), (ctx));

// HOME menu sound shutdown: nw4hbm::snd::SoundThread::Shutdown (804770F4). The AX callback
// (804771EC) posts a tick (1) to the sound thread's 8-slot queue every audio frame, without
// blocking. When the sound thread falls behind, the queue is full, the stop message (2) sent with
// NOBLOCK fails, Shutdown returns 0 and SoundSystem panics (snd_SoundSystem.cpp:141) as the HOME
// menu closes. Same steps here, but stale ticks are dropped to make room for the stop message.
namespace {

constexpr uint32_t kHbmSoundThread = 0x80655428u;    // SoundThread object; OSThread at +0
constexpr uint32_t kHbmQueue = kHbmSoundThread + 0x2338u;
constexpr uint32_t kHbmCreated = kHbmSoundThread + 0x2394u;
constexpr uint32_t kHbmStaticInit = 0x80663098u;      // guard byte of the object's static init

} // namespace

extern "C" void BasaraHbmSoundShutdown_HLE_804770f4(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    if (Memory::Read8(self + 0x2394u) == 0u) {
        ctx->gpr[3] = 0;
        return;
    }
    if (self != kHbmSoundThread || Memory::Read8(kHbmStaticInit) == 0u) {
        // Not the object this override knows; leave it to the guest copy's own logic.
        RT_LOGF(RT_TAG_OS, "hbm sound: unexpected shutdown target 0x%08X\n", self);
        ctx->gpr[3] = 0;
        return;
    }
    bool sent = false;
    int dropped = 0;
    for (int attempt = 0; attempt < 16 && !sent; ++attempt) {
        ctx->gpr[3] = kHbmQueue;
        ctx->gpr[4] = 2;  // stop
        ctx->gpr[5] = 0;  // OS_MESSAGE_NOBLOCK
        InvokeIndirectCpu(0x80360738u, ctx);  // OSSendMessage
        sent = ctx->gpr[3] != 0;
        if (!sent) {
            ctx->gpr[3] = kHbmQueue;
            ctx->gpr[4] = 0;  // drop the message
            ctx->gpr[5] = 0;  // OS_MESSAGE_NOBLOCK
            InvokeIndirectCpu(0x80360800u, ctx);  // OSReceiveMessage
            dropped += ctx->gpr[3] != 0 ? 1 : 0;
        }
    }
    if (dropped != 0) {
        RT_LOGF(RT_TAG_OS, "hbm sound: queue was full, dropped %d tick(s) to send stop (%s)\n", dropped,
                sent ? "sent" : "still full");
    }
    if (!sent) {
        ctx->gpr[3] = 0;
        return;
    }
    ctx->gpr[3] = self;  // OSThread at +0
    ctx->gpr[4] = 0;
    InvokeIndirectCpu(0x803633B0u, ctx);  // OSJoinThread
    if (ctx->gpr[3] == 0) {
        RT_LOGF(RT_TAG_OS, "hbm sound: OSJoinThread failed\n");
    }
    Memory::Write8(kHbmCreated, 0);
    ctx->gpr[3] = 1;
}
PPC_NATIVE_OVERRIDE_VOID(804770F4, BasaraHbmSoundShutdown_HLE_804770f4, (CpuContext* ctx), (ctx));
