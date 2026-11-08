#include <cstdio>
#include "thermal.h"

#include "runtime_log.h"
#include "system_stats.h"

#include <atomic>

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#endif

namespace Thermal {
namespace {

std::atomic<float> g_headroom{-1.0f};
std::atomic<int> g_status{-1};
std::atomic<bool> g_hot{false};
std::atomic<bool> g_warm{false};
std::atomic<bool> g_enabled{true};

#if defined(__ANDROID__)
using AcquireFn = void* (*)();
using HeadroomFn = float (*)(void*, int);
using StatusFn = int (*)(void*);

// Hysteresis on one level: on from enter, off again below leave.
bool Level(bool on, float value, float enter, float leave) { return on ? value >= leave : value >= enter; }

void SetLevels(bool warm, bool hot, const char* source, float value) {
    g_warm.store(warm, std::memory_order_relaxed);
    if (g_hot.exchange(hot, std::memory_order_relaxed) != hot) {
        RT_LOGF(RT_TAG_RUNTIME, "thermal: %s (%s %.2f)\n", hot ? "hot" : "cool again", source, value);
    }
}

// The headroom call is a binder round trip, and Android answers NaN when asked more often than once
// in 10 s: the 2 s poll here before read NaN in every Redmi log, so Thermal Control never saw the
// heat. Headroom now runs every 10 s and keeps its last valid value; status and the battery
// fallback run every 2 s. With no valid headroom for 30 s (no API, or a thermal HAL without skin
// sensors), the battery temperature decides instead.
void Poll(void* manager, HeadroomFn headroom, StatusFn status) {
    pthread_setname_np(pthread_self(), "Thermal");
    setpriority(PRIO_PROCESS, 0, 10);
    int logged = -1;
    // Testing: BASARA_THERMAL_FAKE=H:S reports headroom H for the first S seconds, then the real one.
    float fake = -1.0f;
    int fakeSeconds = 0;
    if (const char* env = std::getenv("BASARA_THERMAL_FAKE")) {
        std::sscanf(env, "%f:%d", &fake, &fakeSeconds);
        RT_LOGF(RT_TAG_RUNTIME, "thermal: fake headroom %.2f for %d s\n", fake, fakeSeconds);
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    Clock::time_point lastAsk{};
    Clock::time_point lastValid = start;
    bool haveValid = false;
    bool usingBattery = false;
    for (;;) {
        const auto now = Clock::now();
        int s = -1;
        if (manager != nullptr && status != nullptr) {
            s = status(manager);
            g_status.store(s, std::memory_order_relaxed);
            if (s != logged) {
                if (logged >= 0) {
                    RT_LOGF(RT_TAG_RUNTIME, "thermal: status %d -> %d\n", logged, s);
                }
                logged = s;
            }
        }
        const bool faking = fake >= 0.0f && now - start < std::chrono::seconds(fakeSeconds);
        if (manager != nullptr && headroom != nullptr &&
            (faking || lastAsk == Clock::time_point{} || now - lastAsk >= std::chrono::seconds(10))) {
            const float h = faking ? fake : headroom(manager, 10);
            if (!faking) lastAsk = now;
            // ADPF: a severe forecast while the status still says NONE is a headroom not updated yet.
            const bool stale = !faking && s == 0 && h >= kHotHeadroom;
            if (!std::isnan(h) && h >= 0.0f && !stale) {
                g_headroom.store(h, std::memory_order_relaxed);
                lastValid = now;
                if (!haveValid || usingBattery) {
                    RT_LOGF(RT_TAG_RUNTIME, "thermal: headroom %.2f (system forecast)\n", h);
                }
                haveValid = true;
                usingBattery = false;
                SetLevels(Level(g_warm.load(std::memory_order_relaxed), h, kWarmHeadroom, kWarmCoolHeadroom),
                          Level(g_hot.load(std::memory_order_relaxed), h, kHotHeadroom, kCoolHeadroom),
                          "headroom", h);
            }
        }
        if (now - lastValid >= std::chrono::seconds(30)) {
            const float c = SampleSystemStats().batteryTempC;
            if (c > 0.0f) {
                if (!usingBattery) {
                    RT_LOGF(RT_TAG_RUNTIME, "thermal: no valid headroom, using the battery temperature (%.1f C)\n", c);
                    usingBattery = true;
                }
                SetLevels(Level(g_warm.load(std::memory_order_relaxed), c, kWarmBatteryC, kWarmCoolBatteryC),
                          Level(g_hot.load(std::memory_order_relaxed), c, kHotBatteryC, kCoolBatteryC),
                          "battery C", c);
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
#endif

} // namespace

void Start() {
#if defined(__ANDROID__)
    static const bool s_started = [] {
        const char* env = std::getenv("BASARA_THERMAL");
        if (env != nullptr && std::strcmp(env, "0") == 0) {
            RT_LOGF(RT_TAG_RUNTIME, "thermal: off (BASARA_THERMAL=0)\n");
            return false;
        }
        void* lib = dlopen("libandroid.so", RTLD_NOW);
        const auto acquire = lib ? reinterpret_cast<AcquireFn>(dlsym(lib, "AThermal_acquireManager")) : nullptr;
        const auto headroom = lib ? reinterpret_cast<HeadroomFn>(dlsym(lib, "AThermal_getThermalHeadroom")) : nullptr;
        const auto status = lib ? reinterpret_cast<StatusFn>(dlsym(lib, "AThermal_getCurrentThermalStatus")) : nullptr;
        void* manager = acquire ? acquire() : nullptr;
        if (manager == nullptr || headroom == nullptr) {
            RT_LOGF(RT_TAG_RUNTIME, "thermal: no headroom API on this system, battery temperature only\n");
        }
        std::thread(Poll, manager, headroom, status).detach();
        return true;
    }();
    (void)s_started;
#endif
}

float Headroom() { return g_headroom.load(std::memory_order_relaxed); }
int Status() { return g_status.load(std::memory_order_relaxed); }
bool Hot() { return g_enabled.load(std::memory_order_relaxed) && g_hot.load(std::memory_order_relaxed); }
bool Warm() { return g_enabled.load(std::memory_order_relaxed) && g_warm.load(std::memory_order_relaxed); }

void SetEnabled(bool enabled) {
    if (g_enabled.exchange(enabled, std::memory_order_relaxed) != enabled) {
        RT_LOGF(RT_TAG_RUNTIME, "thermal control %s\n", enabled ? "on" : "off");
    }
}

} // namespace Thermal
