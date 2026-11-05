#include <cstdio>
#include "thermal.h"

#include "runtime_log.h"

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

// The headroom call is a binder round trip and Android answers NaN to calls less than a second
// apart, so it runs here and the game only reads the atomics.
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
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        float h = headroom(manager, 10);
        if (fake >= 0.0f && std::chrono::steady_clock::now() - start < std::chrono::seconds(fakeSeconds)) {
            h = fake;
        }
        if (!std::isnan(h)) {
            g_headroom.store(h, std::memory_order_relaxed);
            const bool warm = g_warm.load(std::memory_order_relaxed);
            g_warm.store(warm ? h >= kWarmCoolHeadroom : h >= kWarmHeadroom, std::memory_order_relaxed);
            const bool hot = g_hot.load(std::memory_order_relaxed);
            const bool now = hot ? h >= kCoolHeadroom : h >= kHotHeadroom;
            if (now != hot) {
                g_hot.store(now, std::memory_order_relaxed);
                RT_LOGF(RT_TAG_RUNTIME, "thermal: %s (headroom %.2f in 10 s)\n", now ? "hot" : "cool again", h);
            }
        }
        if (status != nullptr) {
            const int s = status(manager);
            g_status.store(s, std::memory_order_relaxed);
            if (s != logged) {
                if (logged >= 0) {
                    RT_LOGF(RT_TAG_RUNTIME, "thermal: status %d -> %d\n", logged, s);
                }
                logged = s;
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
            RT_LOGF(RT_TAG_RUNTIME, "thermal: no headroom API on this system\n");
            return false;
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
