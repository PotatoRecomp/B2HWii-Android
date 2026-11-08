// A player-installed Vulkan driver (Turnip and the like, from an adrenotools driver zip) loaded through
// libadrenotools: a second copy of the system Vulkan loader, in its own linker namespace, whose hooks
// point it at the driver. Dawn opens libvulkan.so itself; its archive's dlopen/dlclose are renamed to
// aurora_dawn_dlopen/dlclose (aurora_vk.cmake), so while a custom driver is active Dawn gets that copy.
//
// The launcher passes (Android only):
//   BASARA_GPU_DRIVER         the driver's .so (in the app's private files, never shared storage)
//   BASARA_GPU_HOOK_DIR       where adrenotools' hook libraries were copied
//   BASARA_GPU_DRIVER_MARKER  a file the launcher created before the start; removed once the driver
//                             has run for a while, so a start that crashes leaves it behind and the
//                             launcher goes back to the system driver the next time.
// A failure here falls back to the system driver, and the reason is written to <marker>.failed.
// What the driver offers is written to caps.txt beside it (GpuDrivers.java reads it for the Renderer
// menu): its device and version, whether Dawn's adapter rules and aurora's Vulkan backend's minimum
// pass on it, and how each renderer that was tried on it went.
#include "custom_driver.hpp"

#include "../internal.hpp"

#if defined(__ANDROID__)
#include <adrenotools/driver.h>
#include <android_linker_ns.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace {
aurora::Module Log("aurora::gpu::driver");

std::atomic<void*> g_loader{nullptr};
bool g_tried = false;
std::string g_marker;

// caps.txt beside the driver: key=value lines, rewritten whole on each change.
std::mutex g_capsMutex;
std::string g_capsPath;
std::map<std::string, std::string> g_caps;

void SaveCapsLocked() {
  if (g_capsPath.empty()) return;
  std::string text;
  for (const auto& [key, value] : g_caps) {
    text += key + "=" + value + "\n";
  }
  const std::string tmp = g_capsPath + ".tmp";
  if (FILE* f = std::fopen(tmp.c_str(), "w")) {
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    std::rename(tmp.c_str(), g_capsPath.c_str());
  }
}

void LoadCaps(const std::string& path) {
  std::lock_guard lock(g_capsMutex);
  g_capsPath = path;
  g_caps.clear();
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    char line[512];
    while (std::fgets(line, sizeof line, f) != nullptr) {
      std::string s{line};
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
      const size_t eq = s.find('=');
      if (eq != std::string::npos) g_caps[s.substr(0, eq)] = s.substr(eq + 1);
    }
    std::fclose(f);
  }
}

void SetCaps(std::initializer_list<std::pair<const char*, std::string>> values) {
  std::lock_guard lock(g_capsMutex);
  for (const auto& [key, value] : values) g_caps[key] = value;
  SaveCapsLocked();
}

std::string VersionText(uint32_t v) {
  return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
         std::to_string(VK_API_VERSION_PATCH(v));
}

// What the driver offers, read once per start through its own loader copy (a few ms). Dawn refuses an
// adapter without 1x and 4x MSAA for colour and depth, robustBufferAccess, fragmentStoresAndAtomics,
// fullDrawIndexUint32, BC or ETC2 + ASTC, or Vulkan 1.1 (malisx2 has 1x MSAA only); aurora's Vulkan
// backend needs Vulkan 1.1 (tier 0). The launcher trusts a renderer's real outcome over this guess.
void Probe(void* loader) {
  const auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
  const auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
  if (createInstance == nullptr) return;
  const VkApplicationInfo app{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "basara-driver-probe",
      .apiVersion = VK_API_VERSION_1_1,
  };
  const VkInstanceCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
  };
  VkInstance instance = VK_NULL_HANDLE;
  if (createInstance(&info, nullptr, &instance) != VK_SUCCESS) {
    Log.warn("Custom Vulkan driver: no Vulkan instance for the probe");
    SetCaps({{"probe", "no instance"}, {"vulkan", "0"}, {"dawn", "0"}});
    return;
  }
#define PROBE_FN(name) const auto name = reinterpret_cast<PFN_##name>(gipa(instance, #name))
  PROBE_FN(vkDestroyInstance);
  PROBE_FN(vkEnumeratePhysicalDevices);
  PROBE_FN(vkGetPhysicalDeviceProperties);
  PROBE_FN(vkGetPhysicalDeviceProperties2);
  PROBE_FN(vkGetPhysicalDeviceFeatures);
#undef PROBE_FN
  uint32_t count = 1;
  VkPhysicalDevice gpu = VK_NULL_HANDLE;
  const VkResult listed = vkEnumeratePhysicalDevices(instance, &count, &gpu);
  if ((listed != VK_SUCCESS && listed != VK_INCOMPLETE) || count == 0 || gpu == VK_NULL_HANDLE) {
    Log.warn("Custom Vulkan driver: it lists no GPU");
    SetCaps({{"probe", "no gpu"}, {"vulkan", "0"}, {"dawn", "0"}});
    vkDestroyInstance(instance, nullptr);
    return;
  }
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(gpu, &props);
  std::string driverText = VersionText(props.driverVersion);
  if (props.apiVersion >= VK_API_VERSION_1_2 && vkGetPhysicalDeviceProperties2 != nullptr) {
    VkPhysicalDeviceDriverProperties driver{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
    vkGetPhysicalDeviceProperties2(gpu, &props2);
    driverText = std::string{driver.driverName} + " " + driver.driverInfo;
  }
  VkPhysicalDeviceFeatures f{};
  vkGetPhysicalDeviceFeatures(gpu, &f);
  const VkSampleCountFlags msaa = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
  std::string missing;
  const auto need = [&](bool ok, const char* what) {
    if (!ok) missing += missing.empty() ? what : std::string{", "} + what;
  };
  const bool vulkan = props.apiVersion >= VK_API_VERSION_1_1;
  need(vulkan, "Vulkan 1.1");
  need((props.limits.framebufferColorSampleCounts & msaa) == msaa &&
           (props.limits.framebufferDepthSampleCounts & msaa) == msaa,
       "4x MSAA");
  need(f.robustBufferAccess, "robustBufferAccess");
  need(f.fragmentStoresAndAtomics, "fragmentStoresAndAtomics");
  need(f.fullDrawIndexUint32, "fullDrawIndexUint32");
  need(f.textureCompressionBC || (f.textureCompressionETC2 && f.textureCompressionASTC_LDR), "BC or ETC2+ASTC");
  const bool dawn = missing.empty();
  SetCaps({{"probe", "ok"},
           {"device", props.deviceName},
           {"driver", driverText},
           {"api", VersionText(props.apiVersion)},
           {"vulkan", vulkan ? "1" : "0"},
           {"dawn", dawn ? "1" : "0"},
           {"dawn_missing", missing}});
  Log.info("Custom Vulkan driver: {} | {} | Vulkan {} | Dawn {}{} | Vulkan renderer {}", props.deviceName,
           driverText, VersionText(props.apiVersion), dawn ? "yes" : "no", dawn ? "" : " (missing " + missing + ")",
           vulkan ? "yes" : "no");
  vkDestroyInstance(instance, nullptr);
}

bool IsVulkanLoaderName(const char* filename) {
  if (filename == nullptr) return false;
  const char* slash = std::strrchr(filename, '/');
  const char* base = slash != nullptr ? slash + 1 : filename;
  return std::strcmp(base, "libvulkan.so") == 0 || std::strcmp(base, "libvulkan.so.1") == 0;
}

void WriteText(const std::string& path, const std::string& text) {
  if (FILE* f = std::fopen(path.c_str(), "w")) {
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
  }
}
// Which of adrenotools' steps fails (it returns only nullptr): the same steps, logged.
std::string Diagnose(const std::string& hookDir, const std::string& redirect) {
  if (!linkernsbypass_load_status()) return "linker namespace bypass unavailable";
  struct stat st {};
  if (::stat(redirect.c_str(), &st) != 0) return "no redirect folder";
  if (::stat((hookDir + "libhook_impl.so").c_str(), &st) != 0) return "no hooks in " + hookDir;
  auto* ns = android_create_namespace("basara-diag", hookDir.c_str(), nullptr, ANDROID_NAMESPACE_TYPE_SHARED,
                                      nullptr, nullptr);
  if (ns == nullptr) return "namespace creation failed";
  if (!linkernsbypass_link_namespace_to_default_all_libs(ns)) return "namespace link failed";
  void* impl = linkernsbypass_namespace_dlopen("libhook_impl.so", RTLD_NOW, ns);
  if (impl == nullptr) {
    const char* err = dlerror();
    return std::string{"hook_impl: "} + (err ? err : "?");
  }
  void* hook = linkernsbypass_namespace_dlopen("libmain_hook.so", RTLD_GLOBAL, ns);
  if (hook == nullptr) {
    const char* err = dlerror();
    return std::string{"main_hook: "} + (err ? err : "?");
  }
  void* vk = linkernsbypass_namespace_dlopen_unique("/system/lib64/libvulkan.so", nullptr, RTLD_NOW, ns);
  if (vk == nullptr) {
    const char* err = dlerror();
    return std::string{"libvulkan copy: "} + (err ? err : "?");
  }
  return "every step works alone";
}
} // namespace

// Dawn's archive calls these instead of dlopen/dlclose (aurora/cmake/aurora_vk.cmake renames them).
extern "C" void* aurora_dawn_dlopen(const char* filename, int flags) {
  void* loader = g_loader.load(std::memory_order_acquire);
  if (loader != nullptr && IsVulkanLoaderName(filename)) {
    return loader;
  }
  return dlopen(filename, flags);
}

// For aurora's Vulkan backend (lib/vulkan/vk_webgpu.cpp), which resolves its entry points itself.
extern "C" void* aurora_custom_vulkan_loader() { return g_loader.load(std::memory_order_acquire); }

extern "C" int aurora_dawn_dlclose(void* handle) {
  // The loader copy stays for the whole process: Dawn closes it with its instance, and a fallback or a
  // second instance must not find it unloaded.
  if (handle != nullptr && handle == g_loader.load(std::memory_order_acquire)) {
    return 0;
  }
  return dlclose(handle);
}

namespace aurora::webgpu::custom_driver {

bool begin() {
  if (g_tried) return active();
  g_tried = true;
  const char* driver = std::getenv("BASARA_GPU_DRIVER");
  const char* hooks = std::getenv("BASARA_GPU_HOOK_DIR");
  if (const char* marker = std::getenv("BASARA_GPU_DRIVER_MARKER")) g_marker = marker;
  if (driver == nullptr || *driver == '\0') return false;
  if (hooks == nullptr || *hooks == '\0') {
    fail("no hook folder was given");
    return false;
  }
  std::string path{driver};
  const size_t slash = path.rfind('/');
  if (slash == std::string::npos) {
    fail("the driver path is not absolute");
    return false;
  }
  std::string dir = path.substr(0, slash + 1);
  const std::string name = path.substr(slash + 1);
  std::string hookDir{hooks};
  if (hookDir.back() != '/') hookDir += '/';
  // Writable folder for the driver's own files (shader dumps, its config), next to the driver.
  const std::string redirect = dir + "redirect/";
  ::mkdir(redirect.c_str(), 0700);
  void* loader = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL,
                                            ADRENOTOOLS_DRIVER_CUSTOM | ADRENOTOOLS_DRIVER_FILE_REDIRECT, nullptr,
                                            hookDir.c_str(), dir.c_str(), name.c_str(), redirect.c_str(), nullptr);
  if (loader == nullptr || dlsym(loader, "vkGetInstanceProcAddr") == nullptr) {
    fail("adrenotools could not load " + path + " (" + Diagnose(hookDir, redirect) + ")");
    return false;
  }
  g_loader.store(loader, std::memory_order_release);
  Log.info("Custom Vulkan driver {} (hooks {})", path, hookDir);
  if (const char* tu = std::getenv("TU_DEBUG"); tu != nullptr && *tu != '\0') {
    Log.info("Custom Vulkan driver: TU_DEBUG={}", tu);
  }
  LoadCaps(dir + "caps.txt");
  Probe(loader);
  return true;
}

void outcome(bool vulkanBackend, bool ok) {
  if (!active()) return;
  SetCaps({{vulkanBackend ? "vulkan10_run" : "dawn10_run", ok ? "ok" : "fail"}});
}

bool active() { return g_loader.load(std::memory_order_acquire) != nullptr; }

void fail(const std::string& reason) {
  Log.warn("Custom Vulkan driver not used: {}; using the system driver", reason);
  g_loader.store(nullptr, std::memory_order_release);
  if (!g_marker.empty()) {
    WriteText(g_marker + ".failed", reason + "\n");
    ::unlink(g_marker.c_str());
  }
}

void running(std::function<bool()> lost) {
  if (!active() || g_marker.empty()) return;
  std::thread([lost = std::move(lost)] {
    std::this_thread::sleep_for(std::chrono::seconds(20));
    if (!lost()) {
      ::unlink(g_marker.c_str());
      Log.info("Custom Vulkan driver ran 20 s; kept for the next start");
    }
  }).detach();
}

} // namespace aurora::webgpu::custom_driver

#else

namespace aurora::webgpu::custom_driver {
bool begin() { return false; }
bool active() { return false; }
void fail(const std::string&) {}
void running(std::function<bool()>) {}
void outcome(bool, bool) {}
} // namespace aurora::webgpu::custom_driver

#endif
