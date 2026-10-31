#pragma once

#include <cstdint>

struct CpuContext;

namespace AxDspHle {

void Init();
void InitForAXOut(CpuContext* ctx);
void Stop();

uint32_t CheckInit();
uint32_t AddTask(uint32_t taskPtr);
void SendMailToDSP(uint32_t mail);
uint32_t CheckMailToDSP();
uint32_t CheckMailFromDSP();
uint32_t ReadMailFromDSP();
uint32_t AssertTask(uint32_t taskPtr);
void ServiceDeferredCallbacks();

// INVARIANT: no guest code may observe mix output before this returns. Called from
// Audio_HLE_Tick before the AI DMA block reaches the backend and before the AI callback
// runs __AXOutNewFrame, the two places guest code reads the mix's PB/output/aux data.
// Cheap when the mix already finished (the normal case).
void JoinMixWorker();
// True while a mix is queued or running on the worker (JoinMixWorker would block).
bool MixWorkerBusy();
// BASARA_LOG_FPS: since the last call, time spent mixing on the worker, time the guest waited for it,
// the number of mixes, and the CPU the last one ran on (-1 if unknown).
void TakeMixStats(uint64_t* mixNanos, uint64_t* waitNanos, uint32_t* mixes, int* lastCpu);
// BASARA_LOG_FPS: voice frames mixed since the last call that were loud near-Nyquist noise, and the
// last such voice's sample format, SRC ratio (16.16) and rms.
uint32_t TakeNoisyVoiceStats(uint32_t* format, uint32_t* ratio, uint32_t* rms);

// Joins and tears down the mix worker thread. Call before the guest memory map
// is destroyed or re-created.
void ShutdownMixWorker();

void SetMixWorkerEnabled(bool enabled);

// NSMBW: the AI block at [addr, addr + bytes) as the speakers should play it, with the Music and
// Sound Effects volumes applied, or nullptr to play guest memory as it is (both at 100%, or the
// block isn't the mix's). Call after JoinMixWorker, from the thread that pushes AI blocks.
const uint8_t* SpeakerBlock(uint32_t addr, uint32_t bytes);

void InitAram();

}
