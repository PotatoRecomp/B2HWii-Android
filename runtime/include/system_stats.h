#pragma once

#include <cstdint>
#include <vector>

// Device readings for the performance overlay (settings_overlay.cpp) and the fps log (vi.cpp),
// refreshed at most once a second; callable from any thread. A value the system does not expose to apps (most of sysfs on some phones) stays unknown:
// -1 for numbers, 0 for byte counts.
struct SystemStats {
    float processCpuPercent = -1.0f;  // this process, of all cores together
    int cores = 0;
    std::vector<int> clusterMHz;      // current clock of each CPU cluster (cpufreq policy)
    float cpuTempC = -1.0f;           // hottest CPU sensor
    int gpuBusyPercent = -1;
    int gpuMHz = -1;
    float gpuTempC = -1.0f;
    uint64_t appRamBytes = 0;
    uint64_t ramAvailableBytes = 0;
    uint64_t ramTotalBytes = 0;
    int batteryPercent = -1;
    float batteryTempC = -1.0f;
};

SystemStats SampleSystemStats();
