#pragma once

#include <cstdint>

// Android: keeps the game thread on the phone's fastest CPU cores and tells the system how long each
// frame's work took (ADPF performance hint), so the governor raises clocks before frames run late.
// Elsewhere these do nothing.
namespace CpuBoost {

// Called on the game thread once per presented frame with the frame's work, idle waits excluded.
// The first call sets up the thread (affinity, hint session); BASARA_CPU_BOOST=0 turns both off.
void ReportFrameWork(uint64_t workNanos);

} // namespace CpuBoost
