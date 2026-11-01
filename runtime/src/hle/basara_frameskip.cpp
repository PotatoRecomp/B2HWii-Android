// Basara's main loop (802BE4F4), native so it can skip drawing a frame. The game advances one step
// per frame, so a phone that needs 26 ms a frame plays it at 0.63x. Frameskip Auto leaves out the
// draw list (802C0144) and the frame end with its GXCopyDisp (80338FBC) of a frame once the game has
// fallen half a frame behind; the screen keeps the last picture and the game logic (the task
// dispatcher, 802BF7B4) still runs every frame. Never two skips in a row. While behind, the next
// retrace may come early (VI_HLE_SetCatchUpRetrace), so the cheap skipped step does not wait out a
// whole interval.
// 30 FPS Lock (frameskip 2) leaves out every other draw: the logic and audio keep 60 steps a second and
// the screen gets an evenly paced new picture every second retrace.
// Config: [video] frameskip = 0 off, 1 auto, 2 30 fps lock. BASARA_FRAMESKIP=0/1/2 overrides it.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "ppc_runtime.h"
#include "memory.h"
#include "runtime_config.h"
#include "runtime_log.h"
#include "thermal.h"

extern "C" bool AuroraGxThreadEnabled();
extern "C" uint64_t AuroraGxThreadMark();
extern "C" void AuroraGxThreadWaitMark(uint64_t ticket);

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kGameState = 0x80564768u;     // pad words at +0x130.., frame counters at +0xF0/+0xF4
constexpr uint32_t kFrameCounterPtr = 0x80662880u;
constexpr uint32_t kGraphicsSystem = 0x80661EE0u;
constexpr uint32_t kSubDrawPending = 0x806626A8u;
constexpr uint32_t kPauseBit = 0x00400000u;      // +0x130 bit 9: hold the frame (step tasks only)
constexpr uint32_t kSubDrawBit = 0x00000020u;    // +0x130 bit 26: also run draw list 1

constexpr double kFrameMs = 1000.0 / 60.0;
constexpr double kMaxDebtMs = 3.0 * kFrameMs;    // a loading stall does not turn into a skip burst
constexpr double kMaxFrameMs = 100.0;
constexpr double kSkipDebtMs = kFrameMs / 2.0;   // half a frame behind: leave the next one undrawn
constexpr double kMinStepMs = 4.0;               // a catch-up step still gets this long
// Thermal Control: while the phone runs hot, draw every second frame (the 30 FPS Lock; logic and audio
// stay at 60) for at least kThermalHoldSeconds, so the CPU and GPU do about half the drawing work.
constexpr double kThermalHoldSeconds = 60.0;
constexpr uint64_t kThermalCheckFrames = 60;

// Skip Boot: from start to the save check the steps run back to back, drawing one in kBootDrawEvery
// so the screen still moves. Ends at the first touch of the save data, or after kBootMaxSeconds.
constexpr uint64_t kBootDrawEvery = 30;
constexpr double kBootMaxSeconds = 90.0;
std::atomic<bool> g_bootSkip{false};
Clock::time_point g_bootStart{};
uint64_t g_bootFrames = 0;

std::atomic<uint64_t> g_frames{0};
std::atomic<uint64_t> g_skipped{0};

int Mode() {
    static const int mode = [] {
        int value = RuntimeConfigFile::Frameskip();
        if (const char* env = std::getenv("BASARA_FRAMESKIP")) {
            value = std::atoi(env);
        }
        value = std::clamp(value, 0, 2);
        RT_LOGF(RT_TAG_VI, "frameskip: %s\n", value == 0 ? "off" : value == 1 ? "auto" : "30 fps lock");
        return value;
    }();
    return mode;
}

void Call(CpuContext* ctx, uint32_t target) {
    InvokeIndirectCpu(target, ctx);
}

void Call(CpuContext* ctx, uint32_t target, uint32_t r3) {
    ctx->gpr[3] = r3;
    InvokeIndirectCpu(target, ctx);
}

} // namespace

void BasaraBootSkip_SaveAccess() {
    static std::atomic<bool> s_reached{false};
    if (s_reached.exchange(true, std::memory_order_relaxed)) return;
    const bool skipped = g_bootSkip.exchange(false, std::memory_order_relaxed);
    RT_LOGF(RT_TAG_VI, "boot: save check reached %.1f s after the main loop started (skip boot %s, %llu steps)\n",
            std::chrono::duration<double>(Clock::now() - g_bootStart).count(), skipped ? "on" : "off",
            static_cast<unsigned long long>(g_frames.load(std::memory_order_relaxed)));
}

void BasaraFrameskip_Stats(uint64_t* frames, uint64_t* skipped) {
    *frames = g_frames.load(std::memory_order_relaxed);
    *skipped = g_skipped.load(std::memory_order_relaxed);
}

extern "C" void BasaraMainLoop_HLE_802be4f4(CpuContext* ctx) {
    // The guest frame: the callees save their link register in it.
    const uint32_t sp = ctx->gpr[1];
    ctx->gpr[1] = sp - 0x20u;
    Memory::Write32(ctx->gpr[1], sp);

    Call(ctx, 0x802BD9A8u);
    Call(ctx, 0x800A0C18u, 0x806626C8u);
    Call(ctx, 0x802BE69Cu);

    const int mode = Mode();
    g_bootStart = Clock::now();
    if (RuntimeConfigFile::SkipBoot()) {
        g_bootSkip.store(true, std::memory_order_relaxed);
        RT_LOGF(RT_TAG_VI, "skip boot: on\n");
    }
    Clock::time_point last{};
    double debtMs = 0.0;
    bool skippedLast = false;
    bool thermalLock = false;
    Clock::time_point thermalSince{};
    uint64_t loopFrames = 0;
    // BASARA_GX_FRAME_SYNC: 2 (default) waits at the top of the frame, 1 after the logic, 0 never.
    const int gxFrameSync = [] {
        const char* env = std::getenv("BASARA_GX_FRAME_SYNC");
        const int value = env != nullptr ? std::clamp(std::atoi(env), 0, 2) : 2;
        if (AuroraGxThreadEnabled()) RT_LOGF(RT_TAG_VI, "gx thread: frame sync %d\n", value);
        return value;
    }();
    uint64_t gxFrameMark = 0;
    for (;;) {
        if (mode != 2 && ++loopFrames % kThermalCheckFrames == 0) {
            const Clock::time_point t = Clock::now();
            const bool hot = Thermal::Warm();
            if (hot && !thermalLock) {
                thermalLock = true;
                thermalSince = t;
                RT_LOGF(RT_TAG_VI, "thermal: warm, drawing at 30 fps (headroom %.2f)\n", Thermal::Headroom());
            } else if (hot) {
                thermalSince = t;  // the hold counts from the last hot reading
            } else if (thermalLock && t - thermalSince > std::chrono::duration<double>(kThermalHoldSeconds)) {
                thermalLock = false;
                RT_LOGF(RT_TAG_VI, "thermal: cooled down, drawing at 60 fps again (headroom %.2f)\n",
                        Thermal::Headroom());
            }
        }
        const int drawMode = thermalLock ? 2 : mode;
        const uint32_t counter = Memory::Read32(kFrameCounterPtr);
        Memory::Write32(counter, Memory::Read32(counter) + 1u);
        const uint32_t parity = Memory::Read32(kGameState + 0xF4u) ^ 1u;
        const uint32_t frame = Memory::Read32(kGameState + 0xF0u);
        Memory::Write32(kGameState + 0xF4u, parity);
        Memory::Write32(kGameState + 0xF0u, frame + 1u);
        bool booting = g_bootSkip.load(std::memory_order_relaxed);
        if (booting && Clock::now() - g_bootStart > std::chrono::duration<double>(kBootMaxSeconds)) {
            BasaraBootSkip_SaveAccess();
            booting = false;
        }
        // Behind schedule, the next step may start as soon as the game is idle instead of a whole
        // retrace interval after this one (VI_HLE_PollRetraceIdle), so a skipped frame catches up.
        if (booting) {
            VI_HLE_SetCatchUpRetrace(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
        } else if (drawMode != 0 && debtMs > 0.5 && last != Clock::time_point{}) {
            const double waitMs = std::max(kMinStepMs, kFrameMs - debtMs);
            VI_HLE_SetCatchUpRetrace(std::chrono::duration_cast<std::chrono::nanoseconds>(
                (last + std::chrono::duration<double, std::milli>(waitMs)).time_since_epoch()).count());
        } else {
            VI_HLE_SetCatchUpRetrace(0);
        }
        ctx->gpr[4] = parity;
        Call(ctx, 0x804070ECu, frame);  // wait for the retrace

        // How far behind 60 steps a second the game is, measured retrace wait to retrace wait.
        const Clock::time_point now = Clock::now();
        if (last != Clock::time_point{}) {
            const double ms = std::chrono::duration<double, std::milli>(now - last).count();
            debtMs = std::clamp(debtMs + std::min(ms, kMaxFrameMs) - kFrameMs, 0.0, kMaxDebtMs);
        }
        last = now;
        const bool skip = booting ? (g_bootFrames++ % kBootDrawEvery) != 0
                                  : !skippedLast && (drawMode == 2 || (drawMode == 1 && debtMs >= kSkipDebtMs));
        skippedLast = skip;
        g_frames.fetch_add(1, std::memory_order_relaxed);
        if (skip) {
            g_skipped.fetch_add(1, std::memory_order_relaxed);
        }

        // GX thread: the previous frame's draws read guest memory (vertex arrays, matrices, textures)
        // on the GX thread after the game has moved on; the game rewrites those buffers for the next
        // frame, which showed as flicker. Wait for that frame before the game starts on the next one.
        if (gxFrameSync == 2) AuroraGxThreadWaitMark(gxFrameMark);
        Call(ctx, 0x80338F38u, Memory::Read32(kGraphicsSystem));
        Call(ctx, 0x8034518Cu, 0x8057199Cu);
        Memory::Write32(kGameState + 0x138u, Memory::Read32(kGameState + 0x130u));
        Memory::Write32(kGameState + 0x13Cu, Memory::Read32(kGameState + 0x134u));
        Memory::Write32(kGameState + 0x158u, Memory::Read32(kGameState + 0x150u));
        Memory::Write32(kGameState + 0x15Cu, Memory::Read32(kGameState + 0x154u));
        Call(ctx, 0x802C1A30u);
        Call(ctx, 0x802C004Cu, 0x80575234u);
        for (;;) {
            Call(ctx, 0x802BF7B4u);  // run the tasks: the game logic
            if ((Memory::Read32(kGameState + 0x130u) & kPauseBit) == 0u) {
                break;
            }
            Call(ctx, 0x802BE924u);
        }
        if (gxFrameSync == 1) AuroraGxThreadWaitMark(gxFrameMark);  // logic overlaps, drawing does not
        Call(ctx, 0x803599B4u);
        if (!skip) {
            Call(ctx, 0x802C0144u, 0u);
        }
        if ((Memory::Read32(kGameState + 0x130u) & kSubDrawBit) != 0u) {
            if (!skip) {
                Call(ctx, 0x802C0144u, 1u);
            }
            Memory::Write32(kSubDrawPending, 0u);
        }
        Call(ctx, 0x802BBC38u);
        Call(ctx, 0x802BCCB0u, 0x80573EF0u);
        if (!skip) {
            Call(ctx, 0x80338FBCu, Memory::Read32(kGraphicsSystem));
        }
        gxFrameMark = AuroraGxThreadMark();
        // The original asserts here (spins forever) on a bad counter pair; it only clears +4.
        Memory::Write32(Memory::Read32(kFrameCounterPtr) + 4u, 0u);
        Call(ctx, 0x802BE924u);
    }
}
PPC_NATIVE_OVERRIDE_VOID(802BE4F4, BasaraMainLoop_HLE_802be4f4, (CpuContext* ctx), (ctx));
