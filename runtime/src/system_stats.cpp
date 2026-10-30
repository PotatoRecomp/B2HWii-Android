#include "system_stats.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

#if !defined(_WIN32)
// One line of a small sysfs/procfs file, or false when it is missing or not readable (SELinux).
bool ReadLine(const char* path, char* out, size_t size) {
    FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return false;
    }
    const bool ok = std::fgets(out, static_cast<int>(size), file) != nullptr;
    std::fclose(file);
    return ok;
}

bool ReadLong(const char* path, long long* value) {
    char line[128];
    if (!ReadLine(path, line, sizeof(line))) {
        return false;
    }
    char* end = nullptr;
    *value = std::strtoll(line, &end, 10);
    return end != line;
}

// Thermal zones report millidegrees (a few, degrees); a reading outside 0..150 C is not a temperature.
float ZoneCelsius(long long raw) {
    const float c = raw > 1000 || raw < -1000 ? raw / 1000.0f : static_cast<float>(raw);
    return c > 0.0f && c < 150.0f ? c : -1.0f;
}

struct ThermalZones {
    std::vector<std::string> cpu, gpu;
};

// Found once: phones have a hundred zones or so, and only their names say which are CPU or GPU.
const ThermalZones& FindThermalZones() {
    static const ThermalZones zones = [] {
        ThermalZones found;
        for (int i = 0; i < 200; ++i) {
            char path[96], type[64];
            std::snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", i);
            if (!ReadLine(path, type, sizeof(type))) {
                continue;
            }
            std::snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
            long long raw = 0;
            if (!ReadLong(path, &raw) || ZoneCelsius(raw) < 0.0f) {
                continue;
            }
            if (std::strncmp(type, "cpu", 3) == 0 && found.cpu.size() < 12) {
                found.cpu.emplace_back(path);
            } else if (std::strstr(type, "gpu") != nullptr && found.gpu.size() < 4) {
                found.gpu.emplace_back(path);
            }
        }
        return found;
    }();
    return zones;
}

float HottestZone(const std::vector<std::string>& paths) {
    float hottest = -1.0f;
    for (const auto& path : paths) {
        long long raw = 0;
        if (ReadLong(path.c_str(), &raw)) {
            hottest = (std::max)(hottest, ZoneCelsius(raw));
        }
    }
    return hottest;
}

uint64_t MemInfoBytes(const char* text, const char* key) {
    const char* at = std::strstr(text, key);
    return at != nullptr ? std::strtoull(at + std::strlen(key), nullptr, 10) * 1024 : 0;
}
#endif

void Sample(SystemStats& stats, double wallSeconds) {
    stats.cores = static_cast<int>((std::max)(1u, std::thread::hardware_concurrency()));
#if defined(_WIN32)
    static uint64_t s_lastCpu = 0;
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        const auto ticks = [](const FILETIME& t) { return (uint64_t{t.dwHighDateTime} << 32) | t.dwLowDateTime; };
        const uint64_t cpu = ticks(kernel) + ticks(user);  // 100 ns units
        if (s_lastCpu != 0 && wallSeconds > 0.0) {
            stats.processCpuPercent =
                static_cast<float>((cpu - s_lastCpu) / 1e7 / wallSeconds / stats.cores * 100.0);
        }
        s_lastCpu = cpu;
    }
    PROCESS_MEMORY_COUNTERS memory{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        stats.appRamBytes = memory.WorkingSetSize;
    }
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        stats.ramAvailableBytes = status.ullAvailPhys;
        stats.ramTotalBytes = status.ullTotalPhys;
    }
#else
    char line[1024];
    static long long s_lastTicks = -1;
    if (ReadLine("/proc/self/stat", line, sizeof(line))) {
        // utime and stime are the 12th and 13th fields after the parenthesised command name.
        if (const char* p = std::strrchr(line, ')')) {
            unsigned long long utime = 0, stime = 0;
            if (std::sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &utime, &stime) == 2) {
                const long long ticks = static_cast<long long>(utime + stime);
                const long hz = sysconf(_SC_CLK_TCK);
                if (s_lastTicks >= 0 && wallSeconds > 0.0 && hz > 0) {
                    stats.processCpuPercent = static_cast<float>(
                        (ticks - s_lastTicks) / static_cast<double>(hz) / wallSeconds / stats.cores * 100.0);
                }
                s_lastTicks = ticks;
            }
        }
    }
    stats.clusterMHz.clear();
    for (int policy = 0; policy < 16; ++policy) {
        char path[96];
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq", policy);
        long long khz = 0;
        if (ReadLong(path, &khz) && khz > 0) {
            stats.clusterMHz.push_back(static_cast<int>(khz / 1000));
        }
    }
    const ThermalZones& zones = FindThermalZones();
    stats.cpuTempC = HottestZone(zones.cpu);
    stats.gpuTempC = HottestZone(zones.gpu);
    long long value = 0;
    // Adreno (kgsl); other GPUs keep these unknown.
    stats.gpuBusyPercent =
        ReadLong("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage", &value) ? static_cast<int>(value) : -1;
    if (ReadLong("/sys/class/kgsl/kgsl-3d0/gpuclk", &value) && value > 0) {
        stats.gpuMHz = static_cast<int>(value / 1000000);
    } else if (ReadLong("/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq", &value) && value > 0) {
        stats.gpuMHz = static_cast<int>(value / 1000000);
    } else {
        stats.gpuMHz = -1;
    }
    stats.batteryPercent =
        ReadLong("/sys/class/power_supply/battery/capacity", &value) ? static_cast<int>(value) : -1;
    stats.batteryTempC =
        ReadLong("/sys/class/power_supply/battery/temp", &value) ? static_cast<float>(value) / 10.0f : -1.0f;
    unsigned long long size = 0, resident = 0;
    if (ReadLine("/proc/self/statm", line, sizeof(line)) && std::sscanf(line, "%llu %llu", &size, &resident) == 2) {
        stats.appRamBytes = resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    }
    if (FILE* file = std::fopen("/proc/meminfo", "r")) {
        char text[512];
        const size_t n = std::fread(text, 1, sizeof(text) - 1, file);
        std::fclose(file);
        text[n] = '\0';
        stats.ramTotalBytes = MemInfoBytes(text, "MemTotal:");
        stats.ramAvailableBytes = MemInfoBytes(text, "MemAvailable:");
    }
#endif
}

} // namespace

SystemStats SampleSystemStats() {
    static std::mutex s_mutex;
    std::lock_guard<std::mutex> lock(s_mutex);
    static SystemStats s_stats;
    static Clock::time_point s_last{};
    const Clock::time_point now = Clock::now();
    if (s_last == Clock::time_point{} || now - s_last >= std::chrono::seconds(1)) {
        const double wall = s_last == Clock::time_point{} ? 0.0 : std::chrono::duration<double>(now - s_last).count();
        Sample(s_stats, wall);
        s_last = now;
    }
    return s_stats;
}
