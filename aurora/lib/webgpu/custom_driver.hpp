#pragma once

#include <functional>
#include <string>

// A player-installed Vulkan driver loaded through libadrenotools (Android). See custom_driver.cpp.
namespace aurora::webgpu::custom_driver {
// Loads the driver the launcher named, once, before the first WebGPU instance. True when it is active.
bool begin();
bool active();
// Stops using it (later instances get the system loader) and tells the launcher why.
void fail(const std::string& reason);
// After the device is up: once it has run 20 s without `lost()`, the launcher keeps it for next time.
void running(std::function<bool()> lost);
} // namespace aurora::webgpu::custom_driver
