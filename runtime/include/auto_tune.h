#pragma once

// Automatic tuning (BASARA_AUTOTUNE=1, Settings > Performance): while the game keeps running below full
// speed, tries one lighter setting at a time and keeps it only when the game got faster. See auto_tune.cpp.
namespace AutoTune {

// Once per presented frame, on the game thread.
void OnFrame();

} // namespace AutoTune
