#include "det_clock.h"
#include "display_settings.h"

#include "memory.h"
#include "runtime_log.h"

#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <atomic>
#include <mutex>

#include <aurora/gfx.h>

namespace DisplaySettings {
namespace {

// Basara: NSMBW widened its EGG::Screen / dScreen / mVideo canvas here by writing their statics
// (0x803504E0, 0x8042AF0C, 0x8042A058, ...). Those are Basara .text, so only the host side (the
// renderer and aurora's 3D projection) follows the aspect; the game's own canvas is left alone.
constexpr float kLinesPerScale = 480.0f;
constexpr float kRatio16x9 = 16.0f / 9.0f;

std::mutex g_mutex;
Settings g_current;
Settings g_pending;
bool g_hasPending = false;
bool g_bootWidescreen = true;
bool g_rendererDirty = true;
uint32_t g_surfaceWidth = 0;
uint32_t g_surfaceHeight = 0;

// Dynamic resolution: a factor on the player's render lines, lowered while the renderer holds the
// game back and raised again once it has room. The player's choice stays the ceiling.
constexpr float kDynSteps[] = {1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f};
constexpr int kDynStepCount = static_cast<int>(sizeof(kDynSteps) / sizeof(kDynSteps[0]));
constexpr float kDynMinLines = 240.0f;        // never below 0.5x
constexpr uint32_t kDynWindowFrames = 60;     // one decision a second at 60 fps
constexpr double kDynDownBusyMs = 15.0;       // worker this busy a cycle: it is the limit
constexpr double kDynDownWaitMs = 2.0;        // ...or the game waits this long a frame for it
constexpr double kDynDownWaitBusyMs = 13.0;  //    while the worker is at least this busy
constexpr double kDynUpBusyMs = 11.5;         // predicted worker time at the next step up
constexpr int kDynUpWindows = 3;              // calm windows in a row before stepping up
std::atomic<bool> g_dynEnabled{false};
int g_dynStep = 0;
int g_dynCalm = 0;
uint32_t g_dynFrames = 0;
bool g_dynSkipWindow = true;                  // the window after a resize carries its stall
AuroraFrameWorkerTotals g_dynLast{};

// The game booted in 4:3: neither widescreen mode exists until the next start.
int32_t EffectiveAspect(int32_t aspect) {
    return g_bootWidescreen ? aspect : kAspect4x3;
}

bool SurfaceWiderThan169() {
    return g_surfaceWidth != 0 && g_surfaceHeight != 0 &&
           static_cast<float>(g_surfaceWidth) / static_cast<float>(g_surfaceHeight) > kRatio16x9;
}

// Render scale 0 ("match screen resolution"): exactly as many lines as the picture is tall on the
// surface, so it renders 1:1 (a 2560x1080 surface in Fill renders 2560x1080, with the game's view
// widened to match), not the window size with the EFB's workspace rows squeezed in.
float RenderLines(float renderScale, int32_t aspect) {
    if (renderScale > 0.0f || g_surfaceWidth == 0 || g_surfaceHeight == 0) {
        return LinesForScale(renderScale);
    }
    const float w = static_cast<float>(g_surfaceWidth), h = static_cast<float>(g_surfaceHeight);
    const bool fillsSurface = aspect == kAspectFill;
    const float pictureAspect = aspect == kAspect4x3 ? 4.0f / 3.0f : kRatio16x9;
    return fillsSurface ? h : std::min(h, w / pictureAspect);
}

void ApplyRenderer(const Settings& settings) {
    const int32_t aspect = EffectiveAspect(settings.aspect);
    if (aspect == kAspect4x3) {
        AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
        VILockAspectRatio(4, 3);
    } else if (aspect == kAspect16x9) {
        VIUnlockAspectRatio();
        AuroraSetViewportPolicy(AURORA_VIEWPORT_16_9);
    } else {
        // The framebuffer takes the surface's aspect; the game draws the wider view into it.
        VIUnlockAspectRatio();
        AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
    }
    const float lines = RenderLines(settings.renderScale, aspect);
    const float factor = g_dynEnabled.load(std::memory_order_relaxed) ? kDynSteps[g_dynStep] : 1.0f;
    const float dynLines = factor < 1.0f && lines > 0.0f ? std::max(std::min(lines, kDynMinLines), lines * factor)
                                                         : lines;
    VISetFrameBufferLines(dynLines);
    RT_LOGF(RT_TAG_VI, "display: aspect %d%s, render %.0f lines (%.2gx, dynamic %.0f%%), surface %ux%u\n", aspect,
            aspect != settings.aspect ? " (game booted in 4:3)" : "", dynLines, settings.renderScale,
            factor * 100.0f, g_surfaceWidth, g_surfaceHeight);
}

// Once a window: steps the dynamic factor from the frame worker's time. Returns true when it changed.
bool DynamicResolutionTick() {
    if (!g_dynEnabled.load(std::memory_order_relaxed)) {
        if (g_dynStep != 0) {
            g_dynStep = 0;
            g_dynCalm = 0;
            return true;
        }
        return false;
    }
    if (++g_dynFrames < kDynWindowFrames) {
        return false;
    }
    AuroraFrameWorkerTotals now{};
    aurora_get_frame_worker_totals(&now);
    const uint64_t cycles = now.cycles - g_dynLast.cycles;
    const double busyMs = cycles ? (now.busyNanos - g_dynLast.busyNanos) / 1e6 / cycles : 0.0;
    const double waitMs = (now.producerWaitNanos - g_dynLast.producerWaitNanos) / 1e6 / g_dynFrames;
    g_dynLast = now;
    g_dynFrames = 0;
    if (g_dynSkipWindow || cycles == 0) {
        g_dynSkipWindow = false;
        return false;
    }

    int step = g_dynStep;
    static const bool s_cycleTest = [] {
        const char* v = std::getenv("BASARA_DYNRES");
        return v && *v == '2';
    }();
    if (s_cycleTest) {
        // BASARA_DYNRES=2 (test): walk down every step and back up, one a window.
        static int s_dir = 1;
        if (step + s_dir < 0 || step + s_dir >= kDynStepCount) s_dir = -s_dir;
        step += s_dir;
    } else if (busyMs > kDynDownBusyMs || (waitMs > kDynDownWaitMs && busyMs > kDynDownWaitBusyMs)) {
        g_dynCalm = 0;
        step = std::min(step + 1, kDynStepCount - 1);
    } else if (step > 0) {
        // Render cost follows the pixel count, the square of the line factor.
        const float ratio = kDynSteps[step - 1] / kDynSteps[step];
        if (busyMs * ratio * ratio < kDynUpBusyMs && ++g_dynCalm >= kDynUpWindows) {
            g_dynCalm = 0;
            step -= 1;
        } else if (busyMs * ratio * ratio >= kDynUpBusyMs) {
            g_dynCalm = 0;
        }
    }
    if (step == g_dynStep) {
        return false;
    }
    RT_LOGF(RT_TAG_VI, "dynamic resolution: %.0f%% -> %.0f%% (worker %.2f ms a cycle, game waited %.2f ms a frame)\n",
            kDynSteps[g_dynStep] * 100.0f, kDynSteps[step] * 100.0f, busyMs, waitMs);
    g_dynStep = step;
    g_dynSkipWindow = true;
    return true;
}

// Fill: only aurora's 3D projection widens to the surface (Hor+); see the Basara note above.
void ApplyGameCanvas(int32_t aspect) {
    // 3D: cameras set up for 16:9 follow the surface's aspect (aurora's projection), the same view
    // height with more or less to the sides, instead of being stretched or squashed onto it.
    const bool fitted = aspect == kAspectFill && g_surfaceWidth != 0 && g_surfaceHeight != 0;
    AuroraSetWidePerspectiveAspect(
        fitted ? static_cast<float>(g_surfaceWidth) / static_cast<float>(g_surfaceHeight) : 0.0f);
}

} // namespace

float LinesForScale(float renderScale) {
    return renderScale > 0.0f ? kLinesPerScale * renderScale : 0.0f;
}

// Deterministic mode (network play) keeps every device on the same 16:9 canvas: how wide the game
// draws decides which enemies it wakes, so Fill's screen-dependent width would desync the session.
Settings Lockstep(Settings settings) {
    if (DetClock::Enabled()) {
        settings.aspect = kAspect16x9;
    }
    return settings;
}

void Initialize(const Settings& requested) {
    const Settings settings = Lockstep(requested);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current = settings;
    g_bootWidescreen = settings.aspect != kAspect4x3;
    g_rendererDirty = true;
}

void Request(const Settings& settings) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending = Lockstep(settings);
    g_hasPending = true;
}

void OnFrameBoundary(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    Settings settings;
    bool renderer = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_hasPending) {
            g_current = g_pending;
            g_hasPending = false;
            g_rendererDirty = true;
        }
        if (surfaceWidth != 0 && surfaceHeight != 0 &&
            (surfaceWidth != g_surfaceWidth || surfaceHeight != g_surfaceHeight)) {
            g_surfaceWidth = surfaceWidth;
            g_surfaceHeight = surfaceHeight;
            // A "match screen" render follows every size.
            if (g_current.renderScale <= 0.0f) {
                g_rendererDirty = true;
            }
        }
        if (DynamicResolutionTick()) {
            g_rendererDirty = true;
        }
        settings = g_current;
        renderer = g_rendererDirty;
        g_rendererDirty = false;
    }
    if (renderer) {
        ApplyRenderer(settings);
    }
    ApplyGameCanvas(EffectiveAspect(settings.aspect));
}

void SetDynamicResolution(bool enabled) {
    if (g_dynEnabled.exchange(enabled, std::memory_order_relaxed) != enabled) {
        RT_LOGF(RT_TAG_VI, "dynamic resolution %s\n", enabled ? "on" : "off");
    }
}

Settings Current() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_current;
}

Settings Latest() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_hasPending ? g_pending : g_current;
}

bool GameWidescreen() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_bootWidescreen;
}

bool SurfaceWide() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return SurfaceWiderThan169();
}

} // namespace DisplaySettings
