#include "auto_tune.h"

#include "display_settings.h"
#include "hle_stubs.h"
#include "runtime_log.h"

#include <aurora/aurora.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>

// After ARMSX2's AutoTune (pcsx2/AutoTune): the game's speed (its main-loop steps a second against 60)
// is judged a second at a time. Four slow seconds in a row (below 95%) start a trial: one lever goes a
// step lighter, and after a settling second its median speed over the next windows is compared with the
// slow seconds'. Kept when it is at least 5% faster, put back otherwise, and never tried again this run.
// It only ever lowers, never raises again by itself, and lets go of a lever the player changes.
// Levers, cheapest picture loss first: Upscaler FSR Full/SGSR -> FSR Light, Texture Detail -> Original,
// Dynamic Resolution on, Frameskip Auto. Only for this run; the launcher's settings stay as they are.
namespace AutoTune {
namespace {

using Clock = std::chrono::steady_clock;

enum Lever { kUpscaler, kTextureFilter, kDynamicResolution, kFrameskip, kLeverCount };
constexpr const char* kLeverNames[kLeverCount] = {"upscaler", "texture detail", "dynamic resolution",
                                                  "frameskip"};
// Windows measured per trial: dynamic resolution steps once a second with gaps, so it needs longer.
constexpr int kTrialWindows[kLeverCount] = {4, 4, 8, 4};
constexpr double kWindowSeconds = 1.0;
constexpr double kWarmupSeconds = 15.0;  // boot, Skip Boot's unpaced run and the first loads
constexpr double kSlowSpeed = 0.95;
constexpr int kSlowWindows = 4;
constexpr double kKeepGain = 1.05;
constexpr double kGapSeconds = 0.5;      // a frame this far from the last: paused or loading, drop the window

bool g_started = false;
bool g_finished = false;
Clock::time_point g_start{}, g_windowStart{}, g_lastFrame{};
uint64_t g_windowSteps = 0;
bool g_discard = false;
std::vector<double> g_slow;
int g_trial = -1;        // the lever on trial
int g_trialBefore = 0;   // its value before the trial
bool g_settling = false;
double g_baseline = 0;
std::vector<double> g_trialSpeeds;
bool g_done[kLeverCount] = {};
int g_kept[kLeverCount] = {-1, -1, -1, -1};

int Current(int lever) {
    switch (lever) {
    case kUpscaler: return aurora_get_fsr();
    case kTextureFilter: return aurora_get_texture_filter();
    case kDynamicResolution: return DisplaySettings::DynamicResolutionEnabled() ? 1 : 0;
    default: return BasaraFrameskip_Mode();
    }
}

// The next lighter value, or -1 when the lever is already as light as this goes.
int Lighter(int lever, int value) {
    switch (lever) {
    case kUpscaler: return value == 2 || value == 3 ? 1 : -1;  // FSR Full / SGSR -> FSR Light
    case kTextureFilter: return value > 0 ? 0 : -1;
    case kDynamicResolution: return value == 0 ? 1 : -1;
    default: return value == 0 ? 1 : -1;  // off -> auto; the 30 FPS lock stays
    }
}

void Set(int lever, int value) {
    switch (lever) {
    case kUpscaler: aurora_set_fsr(value); break;
    case kTextureFilter: aurora_set_texture_filter(value); break;
    case kDynamicResolution: DisplaySettings::SetDynamicResolution(value != 0); break;
    default: BasaraFrameskip_SetMode(value); break;
    }
}

double Median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return n == 0 ? 0.0 : n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

void StartTrial() {
    for (int lever = 0; lever < kLeverCount; ++lever) {
        if (g_done[lever]) continue;
        const int now = Current(lever);
        const int lighter = Lighter(lever, now);
        g_done[lever] = true;  // one try a run, whatever comes of it
        if (lighter < 0) continue;
        g_trial = lever;
        g_trialBefore = now;
        g_settling = true;
        g_trialSpeeds.clear();
        Set(lever, lighter);
        RT_LOGF(RT_TAG_VI, "auto tune: speed %.0f%%, trying %s %d -> %d\n", g_baseline * 100.0,
                kLeverNames[lever], now, lighter);
        return;
    }
    g_finished = true;
    RT_LOGF(RT_TAG_VI, "auto tune: speed %.0f%% and nothing lighter left to try\n", g_baseline * 100.0);
}

void EndTrial() {
    const double speed = Median(g_trialSpeeds);
    const int lever = g_trial;
    g_trial = -1;
    if (speed >= g_baseline * kKeepGain) {
        g_kept[lever] = Current(lever);
        RT_LOGF(RT_TAG_VI, "auto tune: kept %s %d (speed %.0f%% -> %.0f%%)\n", kLeverNames[lever], g_kept[lever],
                g_baseline * 100.0, speed * 100.0);
    } else {
        Set(lever, g_trialBefore);
        RT_LOGF(RT_TAG_VI, "auto tune: put %s back to %d (speed %.0f%% -> %.0f%%, no real gain)\n",
                kLeverNames[lever], g_trialBefore, g_baseline * 100.0, speed * 100.0);
    }
}

void Window(double speed) {
    // The player changed a lever this kept (the in-game menu, or back from the Start Menu): theirs now.
    for (int lever = 0; lever < kLeverCount; ++lever) {
        if (g_kept[lever] >= 0 && Current(lever) != g_kept[lever]) {
            RT_LOGF(RT_TAG_VI, "auto tune: %s changed by the player, left alone\n", kLeverNames[lever]);
            g_kept[lever] = -1;
        }
    }
    if (g_trial >= 0) {
        if (g_settling) {
            g_settling = false;  // the window the change landed in
            return;
        }
        g_trialSpeeds.push_back(speed);
        if (static_cast<int>(g_trialSpeeds.size()) >= kTrialWindows[g_trial]) {
            EndTrial();
            g_slow.clear();
        }
        return;
    }
    if (speed >= kSlowSpeed) {
        g_slow.clear();
        return;
    }
    g_slow.push_back(speed);
    if (static_cast<int>(g_slow.size()) >= kSlowWindows) {
        g_baseline = Median(g_slow);
        g_slow.clear();
        StartTrial();
    }
}

} // namespace

void OnFrame() {
    static const bool enabled = [] {
        const char* v = std::getenv("BASARA_AUTOTUNE");
        const bool on = v != nullptr && *v == '1';
        if (on) RT_LOGF(RT_TAG_VI, "auto tune: on\n");
        return on;
    }();
    if (!enabled || g_finished) return;
    const Clock::time_point now = Clock::now();
    uint64_t steps = 0, skipped = 0;
    BasaraFrameskip_Stats(&steps, &skipped);
    if (!g_started) {
        g_started = true;
        g_start = g_windowStart = g_lastFrame = now;
        g_windowSteps = steps;
        return;
    }
    if (std::chrono::duration<double>(now - g_lastFrame).count() > kGapSeconds) g_discard = true;
    g_lastFrame = now;
    const double window = std::chrono::duration<double>(now - g_windowStart).count();
    if (window < kWindowSeconds) return;
    const double speed = (steps - g_windowSteps) / window / 60.0;
    const bool discard = g_discard || std::chrono::duration<double>(now - g_start).count() < kWarmupSeconds;
    g_windowStart = now;
    g_windowSteps = steps;
    g_discard = false;
    if (discard) {
        g_slow.clear();  // slow windows must come in a row
        return;
    }
    Window(speed);
}

} // namespace AutoTune
