#ifndef AURORA_GFX_H
#define AURORA_GFX_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include "stddef.h"
#include "stdint.h"
#endif

#ifndef NDEBUG
#define AURORA_GFX_DEBUG_GROUPS
#endif

void aurora_push_debug_group(const char* label);
void aurora_pop_debug_group();

typedef struct {
  uint32_t queuedPipelines;
  uint32_t createdPipelines;
  uint32_t drawCallCount;
  uint32_t mergedDrawCallCount;
  uint32_t lastVertSize;
  uint32_t lastUniformSize;
  uint32_t lastIndexSize;
  uint32_t lastStorageSize;
  uint32_t lastTextureUploadSize;
  uint32_t presentedFrameCount;
  uint32_t interpolatedFrameCount;
} AuroraStats;

typedef struct {
  uint64_t totalPresentCount;
  uint32_t sampleCount;
  double framesPerSecond;
  double averageFrameTimeMs;
  double p95FrameTimeMs;
  double jitterMs;
  // framesPerSecond with duplicated presentation slots scaled out, so this is the rate of frames
  // that carried new motion. Equal to framesPerSecond when every slot replayed real interpolation.
  double effectiveFramesPerSecond;
  // Longest gap between two frames with new motion in the last second (what a stutter feels like).
  double worstFrameTimeMs;
  // Presents with new motion since start (not duplicated slots), for exact rates over any window.
  uint64_t totalNewMotionCount;
} AuroraPresentTiming;

const AuroraStats* aurora_get_stats();
void aurora_get_present_timing(AuroraPresentTiming* timing);

// Frame worker totals since start: its cycles, its own time (seal, encode, submit, prepare), of which
// preparing the next frame (where a GPU that has fallen behind holds it), and the time the game
// thread spent waiting for it.
typedef struct {
  uint64_t cycles;
  uint64_t busyNanos;
  uint64_t prepareNanos;
  uint64_t producerWaitNanos;
  // GPU time of the presented frames and their count, from the Vulkan backend's timestamps (both 0
  // on Dawn or with AURORA_VK_GPU_TIMING=0).
  uint64_t gpuNanos;
  uint64_t gpuFrames;
  // Rendered frames the presenter threw away because Present() fell behind (any backend).
  uint64_t droppedPresents;
} AuroraFrameWorkerTotals;
void aurora_get_frame_worker_totals(AuroraFrameWorkerTotals* totals);
// One line: live host textures and the size of each texture cache.
void aurora_texture_memory_report(char* buffer, size_t size);

// Interpolation health: the per-frame fields describe the last sealed frame, the counters
// accumulate since it was configured. This answers "output FPS dropped but the game held 60".
typedef struct {
  uint32_t targetFps;          // configured target, 0 when interpolation is off
  uint32_t targetSamples;      // slots the pacing controller currently aims for
  uint32_t activeSamples;      // slots latched for the latest sealed frame
  uint32_t candidates;         // perspective draws in the latest sealed frame
  uint32_t matchable;          // candidates whose identity also existed last frame
  uint32_t matches;            // draws matched to the previous frame
  uint32_t eligible;           // latest frame inserted interpolated slots
  uint32_t replaySafe;         // latest frame could replay its command stream
  uint64_t framesSealed;
  uint64_t framesLowMatch;
  uint64_t framesReplayUnsafe;
  uint64_t slotReductions;
  uint64_t lateSealDrops;
} AuroraFrameInterpolationDiagnostics;

void aurora_get_frame_interpolation_diagnostics(AuroraFrameInterpolationDiagnostics* diagnostics);

// Generates transform-interpolated perspective frames between consecutive 60 Hz logical frames.
// Supported targets are 0 (off), 120, 180 and 240. Guest simulation and VI timing are unchanged.
void aurora_set_frame_interpolation_fps(uint32_t targetFps);
// NSMBW: V-Sync on (Fifo) or off (Immediate, else Mailbox); off by default.
void aurora_set_vsync(bool enabled);
bool aurora_get_vsync();
uint32_t aurora_get_frame_interpolation_fps();

// Newly encountered GX pipelines compile on the bounded worker queue. Draws whose pipeline is not
// ready are skipped rather than stalling submission, and pick it up once compilation finishes.
void aurora_set_skip_unready_pipelines(bool enabled);
bool aurora_get_skip_unready_pipelines();
uint32_t aurora_get_queued_pipeline_count();

// Controls whether display copies bypass the Wii's vertical copy filter.
void aurora_set_disable_copy_filter(bool disabled);
bool aurora_get_disable_copy_filter();
// Settings > Performance > Disable Fog: draw without GX fog (Dolphin's Disable Fog).
void aurora_set_disable_fog(bool disabled);
bool aurora_get_disable_fog();
// Settings > Performance > Reduce Effects (Experimental): skip additive 3D effect draws.
void aurora_set_reduce_effects(bool on);
bool aurora_get_reduce_effects();

// Guest-RAM write tracking. `generation` changes whenever guest RAM covering a host range was
// written (or returns AURORA_GUEST_WRITE_UNTRACKED); `notify` reports writes aurora made itself.
#define AURORA_GUEST_WRITE_UNTRACKED UINT64_MAX
typedef uint64_t (*AuroraGuestWriteGenerationCallback)(const void* hostPtr, size_t size);
typedef void (*AuroraGuestWriteNotifyCallback)(const void* hostPtr, size_t size);
void aurora_set_guest_write_hooks(AuroraGuestWriteGenerationCallback generation,
                                  AuroraGuestWriteNotifyCallback notify);

#ifdef __cplusplus
}
#endif

#endif
