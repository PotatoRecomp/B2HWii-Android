#pragma once

#include "abi_bridge.h"

// hle/gx/gx_fatal_stubs.cpp includes nothing but this header and reaches
// std::fprintf / std::snprintf / std::abort through it.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// VI Utils
void VI_HLE_ForceRetrace(CpuContext* ctx);
void VI_HLE_PollRetrace(CpuContext* ctx);
void VI_HLE_PollRetraceBusy(CpuContext* ctx);
// From the scheduler's idle loop: like VI_HLE_PollRetrace, but a frameskip catch-up
// (VI_HLE_SetCatchUpRetrace) may deliver the next retrace before its interval is up.
void VI_HLE_PollRetraceIdle(CpuContext* ctx);
// Frameskip: while the game runs behind, its next retrace may come as early as this steady_clock
// instant (nanoseconds since the clock's epoch) once the game is idle; 0 turns it off.
void VI_HLE_SetCatchUpRetrace(int64_t earliestNanos);
// Basara: while the game blocks on a CRI file load, retraces come every 2 ms as at startup (nothing
// is drawn meanwhile). Returns the previous catch-up value, for VI_HLE_SetCatchUpRetrace afterwards.
int64_t VI_HLE_BeginLoadRetraces();
void VI_HLE_ProcessRetracesDeferred(int maxToProcess);
void VI_HLE_WaitForNextRetracePoll();
// BASARA_LOG_FPS: the scheduler had no guest thread to run for this long (the fps log reports it;
// this includes the audio DMA callbacks its idle loop runs).
bool VI_HLE_IdleTimingEnabled();
// Basara: whether CRI's ADX server thread owes itself another pass for a retrace it missed (vi.cpp).
bool VI_HLE_TakeOwedCriServerPass();
// `ender`: what made a thread runnable (0 sleep timer, 1 audio, 2 retrace, 3 fiber timer, 4 alarm,
// 5 already pending); `thread`: the guest thread that had just stopped running.
void VI_HLE_NoteSchedulerIdle(uint64_t nanos, int ender, uint32_t thread);
// Every scheduler idle wait, timed always (the CPU boost hint's frame work excludes them).
void VI_HLE_AddSchedulerIdle(uint64_t nanos);
// dMain::mainThread slept on `queue`, called from guest `lr`, for the same report.
void VI_HLE_NoteMainThreadWait(uint32_t queue, uint32_t lr, uint64_t blockedNanos, uint64_t idleNanos);
uint64_t VI_HLE_IdleNanosTotal();
// Performance overlay: idle timing on while it shows, and the game's presented frames since start.
void VI_HLE_SetOverlayTiming(bool enabled);
uint32_t VI_HLE_PresentedFramesTotal();
// Deterministic mode (det_clock.h): the virtual tick of the next VI retrace, DetClock::kNever
// before VIInit.
uint64_t VI_HLE_DetNextRetraceTicks();
// Single owner of the Aurora frame presentation sequence (seal, optional pace
// to the VI retrace boundary, pre-warm the next frame). paceToRetrace is true
// for the GXCopyDisp producer path and false for retrace-context presents.
void VI_HLE_PresentFrame(bool presentedXfb, bool paceToRetrace);
bool VI_HLE_IsAdvancingRetrace();
void VI_HLE_SetXfbReady(uint32_t xfbAddr); // Called by GXCopyDisp to signal EFB→XFB copy
void Audio_HLE_Tick(CpuContext* ctx, uint32_t deltaMicros);
void Audio_HLE_Poll(CpuContext* ctx);
// Deferred twin of Audio_HLE_Poll for the long host waits that already service
// retraces and alarms (the VI retrace pacing loop, the Aurora frame-worker wait
// callback). Runs the AI DMA tick on an isolated register file the way
// OS_HLE_ProcessAlarmsDeferred does, so it is safe to call from the middle of an
// arbitrary translated function.
void Audio_HLE_PollDeferred();
// Basara's main loop (basara_frameskip.cpp): its passes since boot and how many skipped drawing.
void BasaraFrameskip_Stats(uint64_t* frames, uint64_t* skipped);
// The in-game settings: 0 off, 1 auto, 2 30 FPS lock (ignored when BASARA_FRAMESKIP is set).
void BasaraFrameskip_SetMode(int mode);
// Skip Boot: the game runs undrawn and unpaced from start until it first touches its save data
// (nand_fs.cpp calls this for every path under the game's own NAND title directory).
void BasaraBootSkip_SaveAccess();
// Deterministic mode: the virtual tick at which the AI DMA block in flight completes, or
// DetClock::kNever while audio DMA is stopped.
uint64_t Audio_HLE_DetNextEventTicks();
bool OS_HLE_InterruptsEnabled() noexcept;
extern "C" void OS_HLE_ProcessAlarmsDeferred(int maxToProcess);
extern "C" void OS_HLE_BeginDeferredGuestCallbacks();
extern "C" void OS_HLE_EndDeferredGuestCallbacks();


// Defines and registers a faithful native reimplementation that REPLACES the translated function
// at a PPC address (not a stub; genuine not-yet-implemented entries live in hle/gx/gx_fatal_stubs.cpp
// and abort). The translator regex-parses these macro names to skip that address at build time, so
// renaming requires updating Translator.Cli/Program.cs, RuntimeNativeGuestEffectAnalyzer.cs,
// TranslatedBuildShardEmitter.cs and RuntimeNativeFunctionAbiProvider.cs together.

#define PPC_NATIVE_OVERRIDE(addr_hex, name, ret_type, arg_list, call_list) \
    extern "C" ret_type func_##addr_hex arg_list { return name call_list; } \
    REGISTER_NATIVE_FUNCTION(0x##addr_hex, name)

#define PPC_NATIVE_OVERRIDE_VOID(addr_hex, name, arg_list, call_list) \
    extern "C" void func_##addr_hex arg_list { name call_list; } \
    REGISTER_NATIVE_FUNCTION(0x##addr_hex, name)
