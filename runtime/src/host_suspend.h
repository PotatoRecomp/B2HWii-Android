#pragma once

#include <cstdint>

struct CpuContext;

// NSMBW: the game paused while its app is in the background (Android: the activity left the screen).
//
// The platform asks for a pause from any thread; the game thread acts on it at its next VI retrace. With
// a backup asked for, it first writes the game's own Quick Save of the progress so far (the same routine
// the world map's + menu uses, SaveGame(_, true)), lets the game's NAND thread finish writing, then
// blocks until resumed. If Android kills the app meanwhile, the next start offers that Quick Save:
// the player continues on the world map, at the course they were in. Back in the app, the backup is
// deleted again, so it can never override a save made later.
//
// The guest's clocks (time base, retrace deadlines) skip the paused time: the game resumes where it
// stopped, with nothing to catch up.
namespace HostSuspend {

void RequestPause(bool backup);
void RequestResume();
bool Paused();

// The game thread, at each retrace (before VI computes due retraces). Returns how long it was blocked,
// in nanoseconds (0 when it wasn't).
uint64_t OnRetrace();

// Total time the game has spent paused: subtracted from the guest's time base.
uint64_t PausedNanoseconds();

} // namespace HostSuspend
