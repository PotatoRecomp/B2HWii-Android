#include "det_clock.h"
#include "display_settings.h"
#include "hle_stubs.h"

#include "memory.h"
#include "runtime_config.h"
#include "runtime_log.h"
#include "thermal.h"

#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <atomic>
#include <mutex>

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <android/native_window.h>
#include <dlfcn.h>
#endif

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
bool g_rendererDirty = true;
uint32_t g_surfaceWidth = 0;
uint32_t g_surfaceHeight = 0;

// Dynamic resolution: a factor on the player's render lines, lowered while the renderer holds the
// game back and raised again once it has room. The player's choice stays the ceiling.
constexpr float kDynSteps[] = {1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f};
constexpr int kDynStepCount = static_cast<int>(sizeof(kDynSteps) / sizeof(kDynSteps[0]));
// Never below the Wii's own 480 lines: at 240 ("0.5x") the picture went visibly coarse on Mali
// phones. At 1x and below dynamic resolution therefore saves nothing; the heat ladder sheds work
// there with the 30 fps drawing of Thermal::Warm instead (basara_frameskip.cpp).
constexpr float kDynMinLines = 480.0f;
constexpr uint32_t kDynWindowFrames = 60;     // one decision a second at 60 fps
constexpr double kDynDownBusyMs = 15.0;       // worker this busy a cycle: it is the limit
constexpr double kDynDownWaitMs = 2.0;        // ...or the game waits this long a frame for it
constexpr double kDynDownWaitBusyMs = 13.0;  //    while the worker is at least this busy
constexpr double kDynUpBusyMs = 11.5;         // predicted worker time at the next step up
// Vulkan: the GPU's own time a presented frame (timestamps). The worker there hands the GPU its
// work and goes on, so a GPU that fills the frame barely shows in the worker's time.
constexpr double kDynDownGpuMs = 15.5;        // GPU this busy a frame: it is the limit
constexpr double kDynUpGpuMs = 14.0;          // predicted GPU time at the next step up
constexpr int kDynUpWindows = 3;              // calm windows in a row before stepping up
constexpr int kDynUpWindowsMax = 24;          // ...raised up to this while steps up keep bouncing back
constexpr int kDynBounceWindows = 5;          // a step down this soon after a step up is a bounce
constexpr int kDynMinGapWindows = 5;          // windows between two dynamic steps (each one rebuilds the targets)
int g_dynSinceChange = 1 << 20;
// While the phone runs hot (Thermal::Hot, ten seconds ahead), at least this step: the GPU shares the
// SoC's heat budget with the CPU, so fewer pixels keep the CPU clocks up in a long battle.
constexpr int kDynHotStep = 2;                // 80%
// ...but only while the GPU does a real share of the frame: a CPU-bound game gains nothing from it.
constexpr double kDynHotBusyMs = 8.0;
std::atomic<bool> g_dynEnabled{false};
int g_dynStep = 0;
int g_dynCalm = 0;
int g_dynUpNeed = kDynUpWindows;              // calm windows needed now (grows on bounces)
int g_dynSinceUp = 1 << 20;                   // windows since the last step up
uint32_t g_dynFrames = 0;
bool g_dynSkipWindow = true;                  // the window after a resize carries its stall
AuroraFrameWorkerTotals g_dynLast{};

// Basara's cameras are all 4:3 and widescreen is the host's (ApplyGuestCameraAspect, aurora's wide
// projection), so every aspect applies while playing: 4:3 writes the stock camera aspect back.
int32_t EffectiveAspect(int32_t aspect) {
    return aspect;
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
    const float factor = kDynSteps[g_dynStep];  // 0 (100%) unless dynamic resolution or the heat lowered it
    const float dynLines = factor < 1.0f && lines > 0.0f ? std::max(std::min(lines, kDynMinLines), lines * factor)
                                                         : lines;
    VISetFrameBufferLines(dynLines);
    RT_LOGF(RT_TAG_VI, "display: aspect %d%s, render %.0f lines (%.2gx, dynamic %.0f%%), surface %ux%u\n", aspect,
            aspect != settings.aspect ? " (game booted in 4:3)" : "", dynLines, settings.renderScale,
            factor * 100.0f, g_surfaceWidth, g_surfaceHeight);
}

// Once a window: steps the dynamic factor from the frame worker's time. Returns true when it changed.
bool DynamicResolutionTick() {
    const bool dynamic = g_dynEnabled.load(std::memory_order_relaxed);
    const bool hot = Thermal::Hot();
    if (!dynamic && !hot && g_dynStep == 0) {
        g_dynFrames = 0;
        g_dynSkipWindow = true;  // the worker totals since now are stale when this wakes up
        return false;
    }
    if (!dynamic && !hot) {
        // Neither the player's dynamic resolution nor the heat: back to full, a step a window.
        if (++g_dynFrames < kDynWindowFrames) {
            return false;
        }
        g_dynFrames = 0;
        RT_LOGF(RT_TAG_VI, "dynamic resolution: %.0f%% -> %.0f%% (cool again)\n", kDynSteps[g_dynStep] * 100.0f,
                kDynSteps[g_dynStep - 1] * 100.0f);
        g_dynStep -= 1;
        g_dynCalm = 0;
        return true;
    }
    if (++g_dynFrames < kDynWindowFrames) {
        return false;
    }
    AuroraFrameWorkerTotals now{};
    aurora_get_frame_worker_totals(&now);
    const uint64_t cycles = now.cycles - g_dynLast.cycles;
    const double busyMs = cycles ? (now.busyNanos - g_dynLast.busyNanos) / 1e6 / cycles : 0.0;
    const double waitMs = (now.producerWaitNanos - g_dynLast.producerWaitNanos) / 1e6 / g_dynFrames;
    const uint64_t gpuFrames = now.gpuFrames - g_dynLast.gpuFrames;
    // Per frame the worker rendered, not per present: the GPU time of frames the presenter dropped
    // lands on the next present, and a Mali-G57 MP1 then read 80-140 ms "a frame" and went to 50%.
    const uint64_t gpuDivisor = std::max<uint64_t>(gpuFrames, cycles);
    const double gpuMs = gpuFrames ? (now.gpuNanos - g_dynLast.gpuNanos) / 1e6 / gpuDivisor : 0.0;
    g_dynLast = now;
    g_dynFrames = 0;
    if (g_dynSinceUp < (1 << 20)) {
        ++g_dynSinceUp;
        if (g_dynSinceUp % 30 == 0 && g_dynUpNeed > kDynUpWindows) {
            g_dynUpNeed = std::max(g_dynUpNeed / 2, kDynUpWindows);  // the steps up hold again
        }
    }
    if (g_dynSinceChange < (1 << 20)) {
        ++g_dynSinceChange;
    }
    if (g_dynSkipWindow || cycles == 0) {
        g_dynSkipWindow = false;
        return false;
    }

    int step = g_dynStep;
    const char* reason = "";
    // The thresholds are for a 60 fps picture. Under the 30 FPS Lock a picture has two retraces, so the
    // worker and the GPU get twice the time: a GPU at 16-22 ms there costs no frame, and the 60 fps
    // thresholds took a Mali-G52 MC2 at a steady 30 fps from 720 down to 360 lines for nothing.
    const double budget = BasaraFrameskip_Mode() == 2 ? 2.0 : 1.0;
    static const bool s_cycleTest = [] {
        const char* v = std::getenv("BASARA_DYNRES");
        return v && *v == '2';
    }();
    if (s_cycleTest) {
        // BASARA_DYNRES=2 (test): walk down every step and back up, one a window.
        static int s_dir = 1;
        if (step + s_dir < 0 || step + s_dir >= kDynStepCount) s_dir = -s_dir;
        step += s_dir;
    } else if (dynamic && (busyMs > kDynDownBusyMs * budget || gpuMs > kDynDownGpuMs * budget ||
                           (waitMs > kDynDownWaitMs && busyMs > kDynDownWaitBusyMs * budget))) {
        g_dynCalm = 0;
        step = std::min(step + 1, kDynStepCount - 1);
        if (step != g_dynStep && g_dynSinceUp <= kDynBounceWindows) {
            g_dynUpNeed = std::min(g_dynUpNeed * 2, kDynUpWindowsMax);  // the last step up did not hold
        }
    } else if (hot && step < kDynHotStep && std::max(busyMs, gpuMs) >= kDynHotBusyMs * budget) {
        g_dynCalm = 0;
        step += 1;
        reason = ", running hot";
    } else if (hot && step <= kDynHotStep) {
        g_dynCalm = 0;  // hold: no step up while hot (CPU-bound: stays where it is, often 100%)
    } else if (!dynamic) {
        step -= 1;  // hot only: below the heat's step from an earlier dynamic run, back up to it
    } else if (step > 0) {
        // Predicted worker time one step up. Measured on Mali (G81, ~9,000 draws) the worker grows about
        // linearly with the line factor, not with the pixel count: most of it is per draw, not per pixel
        // (50% 8 ms, 100% 16.7 ms). The square kept it at 50% for good (8.2 ms -> 11.8 > 11.5).
        const float ratio = kDynSteps[step - 1] / kDynSteps[step];
        const double predicted = busyMs * ratio;
        // Linear like the worker's: on the Redmi (Adreno 618) a calm battle took 12.8 ms at 100% and still
        // 10.5 ms at 60%, most of it in passes at the display's size. The square kept it at 60% for good.
        const double predictedGpu = gpuMs * ratio;
        const bool fits = predicted < kDynUpBusyMs * budget && predictedGpu < kDynUpGpuMs * budget;
        if (fits && ++g_dynCalm >= g_dynUpNeed) {
            g_dynCalm = 0;
            step -= 1;
            g_dynSinceUp = 0;
        } else if (!fits) {
            g_dynCalm = 0;
        }
    }
    // At the 480-line floor a step down changes nothing on screen: hold instead of stepping in the
    // dark (a step up from there would then wait out windows that bought nothing).
    if (step > g_dynStep && !s_cycleTest) {
        const float lines = RenderLines(g_current.renderScale, EffectiveAspect(g_current.aspect));
        if (lines > 0.0f && lines * kDynSteps[g_dynStep] <= kDynMinLines) {
            return false;
        }
    }
    if (step == g_dynStep) {
        return false;
    }
    if (dynamic && !s_cycleTest && g_dynSinceChange < kDynMinGapWindows) {
        return false;  // too soon after the last step: wait (the calm count is kept)
    }
    g_dynSinceChange = 0;
    RT_LOGF(RT_TAG_VI,
            "dynamic resolution: %.0f%% -> %.0f%% (worker %.2f ms a cycle, gpu %.2f ms a frame, game waited %.2f ms a "
            "frame%s)\n",
            kDynSteps[g_dynStep] * 100.0f, kDynSteps[step] * 100.0f, busyMs, gpuMs, waitMs, reason);
    g_dynStep = step;
    g_dynSkipWindow = true;
    return true;
}

// Basara's only MTXPerspective wrapper (802BF3F4, every camera) passes the .sdata2 aspect 10/7 and
// then scales m11 by 0.9333 (448/480), a 4:3 view. Rewriting that constant makes the game's own
// projection wide (Hor+, like the PS2 SLPM-66848 widescreen patch), so everything the game derives
// from it (markers over enemies) follows. BASARA_WIDE_GUEST=0 leaves it (aurora alone then widens).
// 803586AC's C_MTXLightPerspective (a screen effect projecting the camera's EFB copy) reads its own
// 10/7 copy (0x80668B08); it has to match the camera or the effect slides off the picture.
constexpr uint32_t kCameraAspectAddrs[] = {0x80667784u, 0x80668B08u};
constexpr float kCameraAspectStock = 10.0f / 7.0f;
constexpr float kCameraHeightScale = 0.93333334f;

// Real widescreen (Hor+, 1.1) only when Config.toml real_widescreen asks (BASARA_WIDE_GUEST). Off,
// the default: 16:9 and Fill stretch the 4:3 picture as 1.0 did, without the extra enemies the wider
// camera wakes (the sound broke up on slower phones). Network play always uses it: the camera's width decides which enemies wake.
bool RealWidescreen() {
    static const bool s_on = RuntimeConfigFile::RealWidescreen();
    return s_on || DetClock::Enabled();
}

void ApplyGuestCameraAspect(float pictureAspect) {
    if (!RealWidescreen()) {
        pictureAspect = 4.0f / 3.0f;  // writes the stock aspect back if it was changed
    }
    const float value = pictureAspect > 4.0f / 3.0f + 0.01f ? pictureAspect / kCameraHeightScale : kCameraAspectStock;
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (const uint32_t addr : kCameraAspectAddrs) {
        uint32_t current = 0;
        if (!Memory::TryRead32(addr, current) || current == bits) {
            continue;
        }
        float before = 0.0f;
        std::memcpy(&before, &current, sizeof(before));
        // Only ever replace the stock value or one written here (a sane aspect), never unrelated data.
        if (!(before > 1.3f && before < 3.5f)) {
            continue;
        }
        if (Memory::TryWrite32(addr, bits) && addr == kCameraAspectAddrs[0]) {
            RT_LOGF(RT_TAG_VI, "display: camera aspect %.3f (picture %.3f)\n", value * kCameraHeightScale,
                    pictureAspect);
        }
    }
}

// Fill: aurora's 3D projection widens to the surface (Hor+); Basara's camera aspect follows too.
void ApplyGameCanvas(int32_t aspect) {
    // 3D: cameras set up for 16:9 follow the surface's aspect (aurora's projection), the same view
    // height with more or less to the sides, instead of being stretched or squashed onto it.
    // Basara's cameras are all 4:3, so 16:9 widens them too (Hor+) instead of stretching the picture.
    const bool fitted = aspect == kAspectFill && g_surfaceWidth != 0 && g_surfaceHeight != 0;
    AuroraSetWidePerspectiveAspect(
        !RealWidescreen() ? 0.0f
        : fitted          ? static_cast<float>(g_surfaceWidth) / static_cast<float>(g_surfaceHeight)
        : aspect == kAspect16x9 ? kRatio16x9 : 0.0f);
    // BASARA_FILL_2D=1 (test): 2D (HUD, text, menus) keeps its 16:9 shape in the middle instead of
    // stretching to the edges. Off: players found the narrowed HUD and the bars in menus worse.
    static const bool s_fill2D = [] {
        const char* v = std::getenv("BASARA_FILL_2D");
        return v && *v == '1';
    }();
    AuroraSetFill2D(fitted && s_fill2D);
    ApplyGuestCameraAspect(fitted ? static_cast<float>(g_surfaceWidth) / static_cast<float>(g_surfaceHeight)
                                  : aspect == kAspect16x9 ? kRatio16x9 : 4.0f / 3.0f);
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
    g_rendererDirty = true;
}

void Request(const Settings& settings) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending = Lockstep(settings);
    g_hasPending = true;
}

#if defined(__ANDROID__)
// On 90/120 Hz panels the compositor and the panel otherwise run at the high rate for a 60 fps game.
// ANativeWindow_setFrameRate (API 30) asks for 60; with frame interpolation on (120-240 fps
// output) the preference is cleared so the panel can go high. Checked once a second because the
// window behind the SDL window changes when the surface is recreated. BASARA_SURFACE_FPS=0: off.
void ApplySurfaceFrameRate() {
    static int s_frames = 0;
    if (++s_frames < 60) {
        return;
    }
    s_frames = 0;
    using SetFrameRateFn = int32_t (*)(ANativeWindow*, float, int8_t);
    static const SetFrameRateFn s_set = [] {
        const char* env = std::getenv("BASARA_SURFACE_FPS");
        if (env != nullptr && std::strcmp(env, "0") == 0) {
            return static_cast<SetFrameRateFn>(nullptr);
        }
        void* lib = dlopen("libandroid.so", RTLD_NOW);
        return lib ? reinterpret_cast<SetFrameRateFn>(dlsym(lib, "ANativeWindow_setFrameRate")) : nullptr;
    }();
    if (s_set == nullptr) {
        return;
    }
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_Window* sdlWindow = windows != nullptr && count > 0 ? windows[0] : nullptr;
    SDL_free(windows);
    auto* window = sdlWindow != nullptr ? static_cast<ANativeWindow*>(SDL_GetPointerProperty(
                                              SDL_GetWindowProperties(sdlWindow),
                                              SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr))
                                        : nullptr;
    const float want = aurora_get_frame_interpolation_fps() != 0 ? 0.0f : 60.0f;
    static ANativeWindow* s_window = nullptr;
    static float s_want = -1.0f;
    if (window == nullptr || (window == s_window && want == s_want)) {
        return;
    }
    const int32_t result = s_set(window, want, 1 /* ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE */);
    s_window = window;
    s_want = want;
    RT_LOGF(RT_TAG_VI, "display: surface frame rate %s (%d)\n", want > 0.0f ? "60 Hz" : "free", result);
}
#endif

void OnFrameBoundary(uint32_t surfaceWidth, uint32_t surfaceHeight) {
#if defined(__ANDROID__)
    ApplySurfaceFrameRate();
#endif
    // BASARA_TEX_FILTER_FILE=path (test): the digit in that file (0-2) is the Texture Detail mode,
    // read every 10 frames, so one scene can be compared with and without it.
    static const char* s_texFilterFile = std::getenv("BASARA_TEX_FILTER_FILE");
    static uint32_t s_texFilterFrames = 0;
    if (s_texFilterFile && *s_texFilterFile && ++s_texFilterFrames % 10 == 0) {
        if (FILE* f = std::fopen(s_texFilterFile, "rb")) {
            const int c = std::fgetc(f);
            std::fclose(f);
            if (c >= '0' && c <= '2' && aurora_get_texture_filter() != c - '0') {
                aurora_set_texture_filter(c - '0');
                RT_LOGF(RT_TAG_VI, "texture filter test: mode %d\n", c - '0');
            }
        }
    }
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

bool DynamicResolutionEnabled() {
    return g_dynEnabled.load(std::memory_order_relaxed);
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
    return (g_hasPending ? g_pending : g_current).aspect != kAspect4x3;
}

bool SurfaceWide() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return SurfaceWiderThan169();
}

} // namespace DisplaySettings
