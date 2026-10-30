#include "cpu_boost.h"

#include "runtime_log.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <sched.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

namespace CpuBoost {

#if defined(__ANDROID__)
namespace {

constexpr int kMaxCpus = 32;
constexpr int64_t kFrameTargetNanos = 16'666'667;

long long MaxFreqKhz(int cpu) {
    char path[96];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return -1;
    }
    long long khz = -1;
    if (std::fscanf(file, "%lld", &khz) != 1) {
        khz = -1;
    }
    std::fclose(file);
    return khz;
}

// Phones mix core types (little, mid, prime). The scheduler starts a busy thread on a mid core and
// may keep it there, which on a Snapdragon 8 Gen 2 is the difference between 60 fps and 35 in a
// crowded battle. The game thread is the one that has to finish within the frame, so it is pinned to
// the fastest cluster, or, where the kernel refuses that (some vendors reject a prime-core-only mask),
// to every core but the slowest cluster. Everything else keeps the default placement.
cpu_set_t g_pinMask;
bool g_pinned = false;
int g_repins = 0;

bool PinTo(const long long* freq, long long minKhz, int known) {
    cpu_set_t set;
    CPU_ZERO(&set);
    int chosen = 0;
    for (int cpu = 0; cpu < kMaxCpus; ++cpu) {
        if (freq[cpu] >= minKhz) {
            CPU_SET(cpu, &set);
            ++chosen;
        }
    }
    if (chosen == 0 || chosen == known) {
        return false;
    }
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        RT_LOGF(RT_TAG_RUNTIME, "cpu boost: %d cores from %lld MHz refused (%s)\n", chosen, minKhz / 1000,
                std::strerror(errno));
        return false;
    }
    RT_LOGF(RT_TAG_RUNTIME, "cpu boost: game thread %d on %d of %d cores (%lld MHz and up)\n", gettid(),
            chosen, known, minKhz / 1000);
    g_pinMask = set;
    g_pinned = true;
    return true;
}

// Android resets a thread's affinity to its cpuset when the app moves between cgroups (top-app,
// foreground): the logs of a Dimensity 7300 and a Snapdragon 8 Gen 2 show the game thread back on
// little cores minutes after a successful pin. Check about once a second and pin again.
void KeepPinned() {
    static int s_frames = 0;
    if (!g_pinned || ++s_frames < 60) {
        return;
    }
    s_frames = 0;
    cpu_set_t current;
    if (sched_getaffinity(0, sizeof(current), &current) != 0 || CPU_EQUAL(&current, &g_pinMask)) {
        return;
    }
    if (sched_setaffinity(0, sizeof(g_pinMask), &g_pinMask) != 0) {
        return;
    }
    if (++g_repins <= 3 || g_repins % 100 == 0) {
        RT_LOGF(RT_TAG_RUNTIME, "cpu boost: affinity was reset (%d cores allowed), pinned again (%d times)\n",
                CPU_COUNT(&current), g_repins);
    }
}

void PinToFastestCores() {
    long long freq[kMaxCpus];
    long long fastest = -1, slowest = -1;
    int known = 0;
    for (int cpu = 0; cpu < kMaxCpus; ++cpu) {
        freq[cpu] = MaxFreqKhz(cpu);
        if (freq[cpu] > 0) {
            ++known;
            fastest = freq[cpu] > fastest ? freq[cpu] : fastest;
            slowest = slowest < 0 || freq[cpu] < slowest ? freq[cpu] : slowest;
        }
    }
    if (known < 2 || fastest == slowest) {
        return;  // one core type: nothing to choose
    }
    if (PinTo(freq, fastest, known)) {
        return;
    }
    PinTo(freq, slowest + 1, known);
}

// ADPF (Android 13+), looked up at run time so older systems still load the library.
using GetManagerFn = void* (*)();
using CreateSessionFn = void* (*)(void*, const int32_t*, size_t, int64_t);
using ReportFn = int (*)(void*, int64_t);

void* g_session = nullptr;
ReportFn g_report = nullptr;

void CreateHintSession() {
    void* lib = dlopen("libandroid.so", RTLD_NOW);
    if (lib == nullptr) {
        return;
    }
    const auto getManager = reinterpret_cast<GetManagerFn>(dlsym(lib, "APerformanceHint_getManager"));
    const auto createSession = reinterpret_cast<CreateSessionFn>(dlsym(lib, "APerformanceHint_createSession"));
    g_report = reinterpret_cast<ReportFn>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
    if (getManager == nullptr || createSession == nullptr || g_report == nullptr) {
        RT_LOGF(RT_TAG_RUNTIME, "cpu boost: no performance hint API on this system\n");
        return;
    }
    void* manager = getManager();
    const int32_t tid = gettid();
    g_session = manager != nullptr ? createSession(manager, &tid, 1, kFrameTargetNanos) : nullptr;
    RT_LOGF(RT_TAG_RUNTIME, "cpu boost: performance hint session %s\n", g_session ? "on" : "unavailable");
}

bool Enabled() {
    const char* value = std::getenv("BASARA_CPU_BOOST");
    return value == nullptr || std::strcmp(value, "0") != 0;
}

} // namespace

void ReportFrameWork(uint64_t workNanos) {
    static const bool enabled = [] {
        if (!Enabled()) {
            RT_LOGF(RT_TAG_RUNTIME, "cpu boost: off (BASARA_CPU_BOOST=0)\n");
            return false;
        }
        PinToFastestCores();
        CreateHintSession();
        return true;
    }();
    if (enabled) {
        KeepPinned();
    }
    if (enabled && g_session != nullptr && workNanos > 0) {
        g_report(g_session, static_cast<int64_t>(workNanos));
    }
}
#else
void ReportFrameWork(uint64_t) {}
#endif

} // namespace CpuBoost
