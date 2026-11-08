// The subset of the WebGPU C API that aurora uses, implemented directly on Vulkan 1.3. Aurora keeps
// talking to webgpu_cpp.h; this file replaces Dawn underneath it. WGSL is compiled to SPIR-V with the
// Tint that is linked from the Dawn package.
//
// Synchronization is deliberately coarse: every image except the swapchain stays in GENERAL layout,
// and a global memory barrier separates passes and copies, which gives WebGPU's implicit ordering.
// Every object a command buffer uses is referenced until the GPU has finished with it.
#include <dawn/webgpu.h>

#include <vulkan/vulkan_core.h>
struct wl_display;
struct wl_surface;
#include <vulkan/vulkan_wayland.h>
typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long VisualID;
#include <vulkan/vulkan_xlib.h>
#if defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#include <android/api-level.h>
#endif

#include <vk_mem_alloc.h>

#if defined(__ANDROID__)
// Android: every Vulkan call goes through these pointers, filled from either the system loader or a
// copy of it running a Vulkan driver loaded through libadrenotools (AURORA_VK_DRIVER, e.g.
// libvulkan_malisx2.so packed in the APK), so a custom driver can stand in for the vendor one. The system libvulkan also exports only
// Vulkan 1.0/1.1 entry points, and the 1.3 ones must be looked up anyway. Elsewhere the calls bind to
// the loader directly.
extern "C" void* aurora_custom_vulkan_loader();
#include <dlfcn.h>

#include <cstdlib>
#include <string>
#include <vector>

#define AURORA_VK_GLOBAL_FUNCTIONS(X) \
  X(vkCreateInstance) \
  X(vkEnumerateInstanceExtensionProperties)
#define AURORA_VK_FUNCTIONS(X) \
  X(vkAcquireNextImageKHR) \
  X(vkAllocateCommandBuffers) \
  X(vkAllocateDescriptorSets) \
  X(vkBeginCommandBuffer) \
  X(vkCmdBeginRendering) \
  X(vkCmdBindDescriptorSets) \
  X(vkCmdBindIndexBuffer) \
  X(vkCmdBindPipeline) \
  X(vkCmdBindVertexBuffers) \
  X(vkCmdCopyBuffer) \
  X(vkCmdCopyBufferToImage) \
  X(vkCmdCopyImage) \
  X(vkCmdCopyImageToBuffer) \
  X(vkCmdDispatch) \
  X(vkCmdDraw) \
  X(vkCmdDrawIndexed) \
  X(vkCmdEndRendering) \
  X(vkCmdPipelineBarrier) \
  X(vkCmdPipelineBarrier2) \
  X(vkCmdResetQueryPool) \
  X(vkCmdSetBlendConstants) \
  X(vkCmdSetScissor) \
  X(vkCmdSetStencilReference) \
  X(vkCmdSetViewport) \
  X(vkCmdWriteTimestamp) \
  X(vkCreateCommandPool) \
  X(vkCreateComputePipelines) \
  X(vkCreateDescriptorPool) \
  X(vkCreateDescriptorSetLayout) \
  X(vkCreateDevice) \
  X(vkCreateGraphicsPipelines) \
  X(vkCreateImageView) \
  X(vkCreatePipelineCache) \
  X(vkCreatePipelineLayout) \
  X(vkCreateQueryPool) \
  X(vkCreateSampler) \
  X(vkCreateSemaphore) \
  X(vkCreateShaderModule) \
  X(vkCreateSwapchainKHR) \
  X(vkDestroyCommandPool) \
  X(vkDestroyDescriptorPool) \
  X(vkDestroyDescriptorSetLayout) \
  X(vkDestroyDevice) \
  X(vkDestroyImageView) \
  X(vkDestroyPipeline) \
  X(vkDestroyPipelineCache) \
  X(vkDestroyPipelineLayout) \
  X(vkDestroyQueryPool) \
  X(vkDestroySampler) \
  X(vkDestroySemaphore) \
  X(vkDestroyShaderModule) \
  X(vkDestroySurfaceKHR) \
  X(vkDestroySwapchainKHR) \
  X(vkDeviceWaitIdle) \
  X(vkEndCommandBuffer) \
  X(vkEnumerateDeviceExtensionProperties) \
  X(vkEnumeratePhysicalDevices) \
  X(vkFreeDescriptorSets) \
  X(vkGetDeviceQueue) \
  X(vkGetPhysicalDeviceFeatures2) \
  X(vkGetPhysicalDeviceProperties) \
  X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
  X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
  X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
  X(vkGetPipelineCacheData) \
  X(vkGetQueryPoolResults) \
  X(vkGetSemaphoreCounterValue) \
  X(vkGetSwapchainImagesKHR) \
  X(vkQueuePresentKHR) \
  X(vkQueueSubmit) \
  X(vkQueueSubmit2) \
  X(vkQueueWaitIdle) \
  X(vkResetCommandPool) \
  X(vkUpdateDescriptorSets) \
  X(vkWaitSemaphores) \
  X(vkCmdBeginRenderPass) \
  X(vkCmdEndRenderPass) \
  X(vkCreateRenderPass) \
  X(vkDestroyRenderPass) \
  X(vkCreateFramebuffer) \
  X(vkDestroyFramebuffer) \
  X(vkCreateFence) \
  X(vkDestroyFence) \
  X(vkGetFenceStatus) \
  X(vkWaitForFences)

namespace {
#define AURORA_DECLARE(name) PFN_##name g_##name = nullptr;
AURORA_VK_GLOBAL_FUNCTIONS(AURORA_DECLARE)
AURORA_VK_FUNCTIONS(AURORA_DECLARE)
#undef AURORA_DECLARE
PFN_vkGetInstanceProcAddr g_getInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr g_getDeviceProcAddr = nullptr;

void Log(const char* fmt, ...);

// The entry point to resolve everything from: a second copy of the system loader that libadrenotools
// points at the driver AURORA_VK_DRIVER names (a Vulkan HAL such as malisx2, packed in the APK), else
// the system loader itself. Going through the loader keeps Android's own surface and swapchain, which a
// HAL driver does not implement.
void SelectVulkanImplementation(bool allowCustom) {
  g_getInstanceProcAddr = ::vkGetInstanceProcAddr;
  // The player's driver, already loaded by aurora/lib/webgpu/custom_driver.cpp through adrenotools.
  void* loader = allowCustom ? aurora_custom_vulkan_loader() : nullptr;
  const auto procAddr =
      loader != nullptr ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr")) : nullptr;
  if (procAddr != nullptr) {
    Log("custom Vulkan driver through adrenotools");
    g_getInstanceProcAddr = procAddr;
  }
#define AURORA_LOAD(name) g_##name = reinterpret_cast<PFN_##name>(g_getInstanceProcAddr(VK_NULL_HANDLE, #name));
  AURORA_VK_GLOBAL_FUNCTIONS(AURORA_LOAD)
#undef AURORA_LOAD
}

bool UsingCustomDriver() { return g_getInstanceProcAddr != ::vkGetInstanceProcAddr; }

void LoadInstanceFunctions(VkInstance instance) {
#define AURORA_LOAD(name) g_##name = reinterpret_cast<PFN_##name>(g_getInstanceProcAddr(instance, #name));
  AURORA_VK_FUNCTIONS(AURORA_LOAD)
#undef AURORA_LOAD
  g_getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(g_getInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
}

// Device-level entry points straight from the driver, skipping the instance dispatch; instance-level
// ones resolve to null here and keep their instance pointer.
// Without VK_KHR_synchronization2 (malisx2 has none) the sync2 calls are translated to their Vulkan
// 1.0 forms. The backend only uses stage and access bits whose values are the same in both.
void VKAPI_CALL CmdPipelineBarrier2Compat(VkCommandBuffer cb, const VkDependencyInfo* dep) {
  // A 1.0 barrier has one pair of stage masks for all its barriers, so each memory barrier gets a
  // call of its own: merged, a narrow barrier would wait on the stages of the others.
  for (uint32_t i = 0; i < dep->memoryBarrierCount; ++i) {
    const VkMemoryBarrier2& b = dep->pMemoryBarriers[i];
    const VkMemoryBarrier memory{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                 .srcAccessMask = static_cast<VkAccessFlags>(b.srcAccessMask),
                                 .dstAccessMask = static_cast<VkAccessFlags>(b.dstAccessMask)};
    g_vkCmdPipelineBarrier(cb, b.srcStageMask != 0 ? static_cast<VkPipelineStageFlags>(b.srcStageMask)
                                                   : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           b.dstStageMask != 0 ? static_cast<VkPipelineStageFlags>(b.dstStageMask)
                                               : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           dep->dependencyFlags, 1, &memory, 0, nullptr, 0, nullptr);
  }
  if (dep->bufferMemoryBarrierCount == 0 && dep->imageMemoryBarrierCount == 0) {
    return;
  }
  VkPipelineStageFlags src = 0;
  VkPipelineStageFlags dst = 0;
  std::vector<VkBufferMemoryBarrier> buffers;
  for (uint32_t i = 0; i < dep->bufferMemoryBarrierCount; ++i) {
    const VkBufferMemoryBarrier2& b = dep->pBufferMemoryBarriers[i];
    src |= static_cast<VkPipelineStageFlags>(b.srcStageMask);
    dst |= static_cast<VkPipelineStageFlags>(b.dstStageMask);
    buffers.push_back({.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                       .srcAccessMask = static_cast<VkAccessFlags>(b.srcAccessMask),
                       .dstAccessMask = static_cast<VkAccessFlags>(b.dstAccessMask),
                       .srcQueueFamilyIndex = b.srcQueueFamilyIndex,
                       .dstQueueFamilyIndex = b.dstQueueFamilyIndex,
                       .buffer = b.buffer,
                       .offset = b.offset,
                       .size = b.size});
  }
  std::vector<VkImageMemoryBarrier> images;
  for (uint32_t i = 0; i < dep->imageMemoryBarrierCount; ++i) {
    const VkImageMemoryBarrier2& b = dep->pImageMemoryBarriers[i];
    src |= static_cast<VkPipelineStageFlags>(b.srcStageMask);
    dst |= static_cast<VkPipelineStageFlags>(b.dstStageMask);
    images.push_back({.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                      .srcAccessMask = static_cast<VkAccessFlags>(b.srcAccessMask),
                      .dstAccessMask = static_cast<VkAccessFlags>(b.dstAccessMask),
                      .oldLayout = b.oldLayout,
                      .newLayout = b.newLayout,
                      .srcQueueFamilyIndex = b.srcQueueFamilyIndex,
                      .dstQueueFamilyIndex = b.dstQueueFamilyIndex,
                      .image = b.image,
                      .subresourceRange = b.subresourceRange});
  }
  g_vkCmdPipelineBarrier(cb, src != 0 ? src : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         dst != 0 ? dst : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, dep->dependencyFlags, 0, nullptr,
                         static_cast<uint32_t>(buffers.size()), buffers.data(), static_cast<uint32_t>(images.size()),
                         images.data());
}

// False on a tier 0 device (no VK_KHR_timeline_semaphore): its submissions carry binary semaphores only,
// and the timeline values struct is left out of the chain.
bool g_chainTimelineInfo = true;

VkResult VKAPI_CALL QueueSubmit2Compat(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence) {
  struct Converted {
    std::vector<VkSemaphore> waits, signals;
    std::vector<uint64_t> waitValues, signalValues;
    std::vector<VkPipelineStageFlags> waitStages;
    std::vector<VkCommandBuffer> cbs;
    VkTimelineSemaphoreSubmitInfo timeline{};
  };
  std::vector<Converted> converted(count);
  std::vector<VkSubmitInfo> infos(count);
  for (uint32_t i = 0; i < count; ++i) {
    const VkSubmitInfo2& in = submits[i];
    Converted& c = converted[i];
    for (uint32_t w = 0; w < in.waitSemaphoreInfoCount; ++w) {
      const VkSemaphoreSubmitInfo& info = in.pWaitSemaphoreInfos[w];
      c.waits.push_back(info.semaphore);
      c.waitValues.push_back(info.value);
      c.waitStages.push_back(info.stageMask != 0 ? static_cast<VkPipelineStageFlags>(info.stageMask)
                                                 : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }
    for (uint32_t b = 0; b < in.commandBufferInfoCount; ++b) {
      c.cbs.push_back(in.pCommandBufferInfos[b].commandBuffer);
    }
    for (uint32_t g = 0; g < in.signalSemaphoreInfoCount; ++g) {
      c.signals.push_back(in.pSignalSemaphoreInfos[g].semaphore);
      c.signalValues.push_back(in.pSignalSemaphoreInfos[g].value);
    }
    // Values for binary semaphores are ignored, so one list covers both kinds.
    c.timeline = {.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
                  .waitSemaphoreValueCount = static_cast<uint32_t>(c.waitValues.size()),
                  .pWaitSemaphoreValues = c.waitValues.data(),
                  .signalSemaphoreValueCount = static_cast<uint32_t>(c.signalValues.size()),
                  .pSignalSemaphoreValues = c.signalValues.data()};
    infos[i] = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = g_chainTimelineInfo ? &c.timeline : nullptr,
                .waitSemaphoreCount = static_cast<uint32_t>(c.waits.size()),
                .pWaitSemaphores = c.waits.data(),
                .pWaitDstStageMask = c.waitStages.data(),
                .commandBufferCount = static_cast<uint32_t>(c.cbs.size()),
                .pCommandBuffers = c.cbs.data(),
                .signalSemaphoreCount = static_cast<uint32_t>(c.signals.size()),
                .pSignalSemaphores = c.signals.data()};
  }
  return g_vkQueueSubmit(queue, count, infos.data(), fence);
}

// Below Vulkan 1.3 the 1.2/1.3 entry points come from their extensions, under the KHR names.
void LoadDeviceFunctions(VkDevice device, bool api13, bool hasSync2) {
#define AURORA_LOAD(name)                                                                                    \
  if (const auto fn = reinterpret_cast<PFN_##name>(g_getDeviceProcAddr(device, #name))) {                     \
    g_##name = fn;                                                                                           \
  }
  AURORA_VK_FUNCTIONS(AURORA_LOAD)
#undef AURORA_LOAD
  if (!api13) {
#define AURORA_LOAD_KHR(name) g_##name = reinterpret_cast<PFN_##name>(g_getDeviceProcAddr(device, #name "KHR"));
    AURORA_LOAD_KHR(vkCmdBeginRendering)
    AURORA_LOAD_KHR(vkCmdEndRendering)
    if (hasSync2) {
      AURORA_LOAD_KHR(vkCmdPipelineBarrier2)
      AURORA_LOAD_KHR(vkQueueSubmit2)
    } else {
      g_vkCmdPipelineBarrier2 = CmdPipelineBarrier2Compat;
      g_vkQueueSubmit2 = QueueSubmit2Compat;
    }
    AURORA_LOAD_KHR(vkWaitSemaphores)
    AURORA_LOAD_KHR(vkGetSemaphoreCounterValue)
#undef AURORA_LOAD_KHR
  }
}
} // namespace
#define vkCreateInstance g_vkCreateInstance
#define vkEnumerateInstanceExtensionProperties g_vkEnumerateInstanceExtensionProperties
#define vkAcquireNextImageKHR g_vkAcquireNextImageKHR
#define vkAllocateCommandBuffers g_vkAllocateCommandBuffers
#define vkAllocateDescriptorSets g_vkAllocateDescriptorSets
#define vkBeginCommandBuffer g_vkBeginCommandBuffer
#define vkCmdBeginRendering g_vkCmdBeginRendering
#define vkCmdBindDescriptorSets g_vkCmdBindDescriptorSets
#define vkCmdBindIndexBuffer g_vkCmdBindIndexBuffer
#define vkCmdBindPipeline g_vkCmdBindPipeline
#define vkCmdBindVertexBuffers g_vkCmdBindVertexBuffers
#define vkCmdCopyBuffer g_vkCmdCopyBuffer
#define vkCmdCopyBufferToImage g_vkCmdCopyBufferToImage
#define vkCmdCopyImage g_vkCmdCopyImage
#define vkCmdCopyImageToBuffer g_vkCmdCopyImageToBuffer
#define vkCmdDispatch g_vkCmdDispatch
#define vkCmdDraw g_vkCmdDraw
#define vkCmdDrawIndexed g_vkCmdDrawIndexed
#define vkCmdEndRendering g_vkCmdEndRendering
#define vkCmdPipelineBarrier2 g_vkCmdPipelineBarrier2
#define vkCmdSetBlendConstants g_vkCmdSetBlendConstants
#define vkCmdSetScissor g_vkCmdSetScissor
#define vkCmdSetStencilReference g_vkCmdSetStencilReference
#define vkCmdSetViewport g_vkCmdSetViewport
#define vkCreateCommandPool g_vkCreateCommandPool
#define vkCreateComputePipelines g_vkCreateComputePipelines
#define vkCreateDescriptorPool g_vkCreateDescriptorPool
#define vkCreateDescriptorSetLayout g_vkCreateDescriptorSetLayout
#define vkCreateDevice g_vkCreateDevice
#define vkCreateGraphicsPipelines g_vkCreateGraphicsPipelines
#define vkCreateImageView g_vkCreateImageView
#define vkCreatePipelineCache g_vkCreatePipelineCache
#define vkCreatePipelineLayout g_vkCreatePipelineLayout
#define vkCreateSampler g_vkCreateSampler
#define vkCreateSemaphore g_vkCreateSemaphore
#define vkCreateShaderModule g_vkCreateShaderModule
#define vkCreateSwapchainKHR g_vkCreateSwapchainKHR
#define vkDestroyCommandPool g_vkDestroyCommandPool
#define vkDestroyDescriptorPool g_vkDestroyDescriptorPool
#define vkDestroyDescriptorSetLayout g_vkDestroyDescriptorSetLayout
#define vkDestroyDevice g_vkDestroyDevice
#define vkDestroyImageView g_vkDestroyImageView
#define vkDestroyPipeline g_vkDestroyPipeline
#define vkDestroyPipelineCache g_vkDestroyPipelineCache
#define vkDestroyPipelineLayout g_vkDestroyPipelineLayout
#define vkDestroySampler g_vkDestroySampler
#define vkDestroySemaphore g_vkDestroySemaphore
#define vkDestroyShaderModule g_vkDestroyShaderModule
#define vkDestroySurfaceKHR g_vkDestroySurfaceKHR
#define vkDestroySwapchainKHR g_vkDestroySwapchainKHR
#define vkDeviceWaitIdle g_vkDeviceWaitIdle
#define vkEndCommandBuffer g_vkEndCommandBuffer
#define vkEnumerateDeviceExtensionProperties g_vkEnumerateDeviceExtensionProperties
#define vkEnumeratePhysicalDevices g_vkEnumeratePhysicalDevices
#define vkFreeDescriptorSets g_vkFreeDescriptorSets
#define vkGetDeviceQueue g_vkGetDeviceQueue
#define vkGetPhysicalDeviceFeatures2 g_vkGetPhysicalDeviceFeatures2
#define vkGetPhysicalDeviceProperties g_vkGetPhysicalDeviceProperties
#define vkGetPhysicalDeviceQueueFamilyProperties g_vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR g_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR g_vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkGetPhysicalDeviceSurfacePresentModesKHR g_vkGetPhysicalDeviceSurfacePresentModesKHR
#define vkGetPipelineCacheData g_vkGetPipelineCacheData
#define vkGetSemaphoreCounterValue g_vkGetSemaphoreCounterValue
#define vkGetSwapchainImagesKHR g_vkGetSwapchainImagesKHR
#define vkQueuePresentKHR g_vkQueuePresentKHR
#define vkQueueSubmit2 g_vkQueueSubmit2
#define vkQueueWaitIdle g_vkQueueWaitIdle
#define vkResetCommandPool g_vkResetCommandPool
#define vkUpdateDescriptorSets g_vkUpdateDescriptorSets
#define vkWaitSemaphores g_vkWaitSemaphores
#define vkCmdResetQueryPool g_vkCmdResetQueryPool
#define vkCmdWriteTimestamp g_vkCmdWriteTimestamp
#define vkCreateQueryPool g_vkCreateQueryPool
#define vkDestroyQueryPool g_vkDestroyQueryPool
#define vkGetQueryPoolResults g_vkGetQueryPoolResults
#define vkCmdBeginRenderPass g_vkCmdBeginRenderPass
#define vkCmdEndRenderPass g_vkCmdEndRenderPass
#define vkCreateRenderPass g_vkCreateRenderPass
#define vkDestroyRenderPass g_vkDestroyRenderPass
#define vkCreateFramebuffer g_vkCreateFramebuffer
#define vkDestroyFramebuffer g_vkDestroyFramebuffer
#define vkCreateFence g_vkCreateFence
#define vkDestroyFence g_vkDestroyFence
#define vkGetFenceStatus g_vkGetFenceStatus
#define vkWaitForFences g_vkWaitForFences
#define AURORA_GET_INSTANCE_PROC_ADDR g_getInstanceProcAddr
#define AURORA_GET_DEVICE_PROC_ADDR g_getDeviceProcAddr
#else
#define AURORA_GET_INSTANCE_PROC_ADDR vkGetInstanceProcAddr
#define AURORA_GET_DEVICE_PROC_ADDR vkGetDeviceProcAddr
#endif

#include "src/tint/api/tint.h"
#include "src/tint/lang/spirv/writer/writer.h"
#include "src/tint/lang/wgsl/reader/reader.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// Set by wgpuSurfaceConfigure, read by aurora_vk_surface_rotation.
static std::atomic<int> g_surfaceRotation{0};

namespace {

constexpr VkMemoryPropertyFlags kCachedStagingFlags =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

// One pass of writes the way the GX conversion fills its staging: 28-byte vertices as 4-byte stores,
// in order, across the buffer. Nanoseconds.
int64_t TimeStagingWrites(uint8_t* base, size_t size) {
  volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(base);
  const size_t words = size / 4;
  const auto start = std::chrono::steady_clock::now();
  uint32_t v = 0x3f800000u;
  for (size_t i = 0; i + 7 <= words; i += 7) {
    p[i] = v;
    p[i + 1] = v + 1;
    p[i + 2] = v + 2;
    p[i + 3] = 0xffffffffu;
    p[i + 4] = v + 3;
    p[i + 5] = v + 4;
    p[i + 6] = static_cast<uint32_t>(i);
    v += 5;
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
}

void Log(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::fputs("[vkgpu] ", stderr);
  std::vfprintf(stderr, fmt, args);
  std::fputc('\n', stderr);
  va_end(args);
}

[[noreturn]] void Fatal(const char* what, VkResult result) {
  Log("fatal: %s failed (VkResult %d)", what, static_cast<int>(result));
  std::abort();
}

#define VK_CHECK(expr)                                                                                                 \
  do {                                                                                                                 \
    const VkResult _r = (expr);                                                                                        \
    if (_r != VK_SUCCESS) {                                                                                            \
      Fatal(#expr, _r);                                                                                                \
    }                                                                                                                  \
  } while (0)

std::string_view ToView(WGPUStringView view) {
  if (view.data == nullptr) {
    return {};
  }
  return view.length == WGPU_STRLEN ? std::string_view{view.data} : std::string_view{view.data, view.length};
}

WGPUStringView MakeView(const char* text) { return WGPUStringView{text, text != nullptr ? std::strlen(text) : 0}; }

char* DupString(std::string_view text) {
  char* copy = static_cast<char*>(std::malloc(text.size() + 1));
  std::memcpy(copy, text.data(), text.size());
  copy[text.size()] = 0;
  return copy;
}

const WGPUChainedStruct* FindChain(const WGPUChainedStruct* chain, WGPUSType type) {
  for (; chain != nullptr; chain = chain->next) {
    if (chain->sType == type) {
      return chain;
    }
  }
  return nullptr;
}

// Objects that a command buffer can keep alive.
struct Object {
  std::atomic<int32_t> refs{1};
  // The encoder that last referenced this object, so an encoder references each object once.
  std::atomic<uint64_t> encoderStamp{0};
  virtual ~Object() = default;
  void AddRef() { refs.fetch_add(1, std::memory_order_relaxed); }
  void Release() {
    if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }
};

struct FormatInfo {
  VkFormat vk = VK_FORMAT_UNDEFINED;
  uint32_t blockBytes = 0;
  uint32_t blockWidth = 1;
  uint32_t blockHeight = 1;
  bool depth = false;
  bool stencil = false;
  bool sint = false;
  bool uint = false;
};

FormatInfo GetFormat(WGPUTextureFormat format) {
  switch (format) {
  case WGPUTextureFormat_R8Unorm:
    return {VK_FORMAT_R8_UNORM, 1};
  case WGPUTextureFormat_RG8Unorm:
    return {VK_FORMAT_R8G8_UNORM, 2};
  case WGPUTextureFormat_R16Sint:
    return {VK_FORMAT_R16_SINT, 2, 1, 1, false, false, true};
  case WGPUTextureFormat_R16Uint:
    return {VK_FORMAT_R16_UINT, 2, 1, 1, false, false, false, true};
  case WGPUTextureFormat_R16Float:
    return {VK_FORMAT_R16_SFLOAT, 2};
  case WGPUTextureFormat_R32Float:
    return {VK_FORMAT_R32_SFLOAT, 4};
  case WGPUTextureFormat_R32Uint:
    return {VK_FORMAT_R32_UINT, 4, 1, 1, false, false, false, true};
  case WGPUTextureFormat_R32Sint:
    return {VK_FORMAT_R32_SINT, 4, 1, 1, false, false, true};
  case WGPUTextureFormat_RG16Float:
    return {VK_FORMAT_R16G16_SFLOAT, 4};
  case WGPUTextureFormat_RGBA8Unorm:
    return {VK_FORMAT_R8G8B8A8_UNORM, 4};
  case WGPUTextureFormat_RGBA8UnormSrgb:
    return {VK_FORMAT_R8G8B8A8_SRGB, 4};
  case WGPUTextureFormat_BGRA8Unorm:
    return {VK_FORMAT_B8G8R8A8_UNORM, 4};
  case WGPUTextureFormat_BGRA8UnormSrgb:
    return {VK_FORMAT_B8G8R8A8_SRGB, 4};
  case WGPUTextureFormat_RGB10A2Unorm:
    return {VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4};
  case WGPUTextureFormat_RGBA16Float:
    return {VK_FORMAT_R16G16B16A16_SFLOAT, 8};
  case WGPUTextureFormat_RGBA32Float:
    return {VK_FORMAT_R32G32B32A32_SFLOAT, 16};
  case WGPUTextureFormat_Depth16Unorm:
    return {VK_FORMAT_D16_UNORM, 2, 1, 1, true};
  case WGPUTextureFormat_Depth32Float:
    return {VK_FORMAT_D32_SFLOAT, 4, 1, 1, true};
  case WGPUTextureFormat_Depth24Plus:
    return {VK_FORMAT_D32_SFLOAT, 4, 1, 1, true};
  case WGPUTextureFormat_Depth24PlusStencil8:
    return {VK_FORMAT_D32_SFLOAT_S8_UINT, 8, 1, 1, true, true};
  case WGPUTextureFormat_Depth32FloatStencil8:
    return {VK_FORMAT_D32_SFLOAT_S8_UINT, 8, 1, 1, true, true};
  case WGPUTextureFormat_BC1RGBAUnorm:
    return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8, 4, 4};
  case WGPUTextureFormat_BC1RGBAUnormSrgb:
    return {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 8, 4, 4};
  case WGPUTextureFormat_BC2RGBAUnorm:
    return {VK_FORMAT_BC2_UNORM_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC2RGBAUnormSrgb:
    return {VK_FORMAT_BC2_SRGB_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC3RGBAUnorm:
    return {VK_FORMAT_BC3_UNORM_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC3RGBAUnormSrgb:
    return {VK_FORMAT_BC3_SRGB_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC4RUnorm:
    return {VK_FORMAT_BC4_UNORM_BLOCK, 8, 4, 4};
  case WGPUTextureFormat_BC4RSnorm:
    return {VK_FORMAT_BC4_SNORM_BLOCK, 8, 4, 4};
  case WGPUTextureFormat_BC5RGUnorm:
    return {VK_FORMAT_BC5_UNORM_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC5RGSnorm:
    return {VK_FORMAT_BC5_SNORM_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC6HRGBUfloat:
    return {VK_FORMAT_BC6H_UFLOAT_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC6HRGBFloat:
    return {VK_FORMAT_BC6H_SFLOAT_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC7RGBAUnorm:
    return {VK_FORMAT_BC7_UNORM_BLOCK, 16, 4, 4};
  case WGPUTextureFormat_BC7RGBAUnormSrgb:
    return {VK_FORMAT_BC7_SRGB_BLOCK, 16, 4, 4};
  default:
    return {};
  }
}

WGPUTextureFormat FromVkFormat(VkFormat format) {
  switch (format) {
  case VK_FORMAT_B8G8R8A8_UNORM:
    return WGPUTextureFormat_BGRA8Unorm;
  case VK_FORMAT_B8G8R8A8_SRGB:
    return WGPUTextureFormat_BGRA8UnormSrgb;
  case VK_FORMAT_R8G8B8A8_UNORM:
    return WGPUTextureFormat_RGBA8Unorm;
  case VK_FORMAT_R8G8B8A8_SRGB:
    return WGPUTextureFormat_RGBA8UnormSrgb;
  case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    return WGPUTextureFormat_RGB10A2Unorm;
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return WGPUTextureFormat_RGBA16Float;
  default:
    return WGPUTextureFormat_Undefined;
  }
}

VkImageAspectFlags AspectFor(const FormatInfo& info, WGPUTextureAspect aspect) {
  if (aspect == WGPUTextureAspect_DepthOnly) {
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  if (aspect == WGPUTextureAspect_StencilOnly) {
    return VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  if (info.depth) {
    return VK_IMAGE_ASPECT_DEPTH_BIT | (info.stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
  }
  return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkCompareOp ToVk(WGPUCompareFunction f) {
  switch (f) {
  case WGPUCompareFunction_Never:
    return VK_COMPARE_OP_NEVER;
  case WGPUCompareFunction_Less:
    return VK_COMPARE_OP_LESS;
  case WGPUCompareFunction_Equal:
    return VK_COMPARE_OP_EQUAL;
  case WGPUCompareFunction_LessEqual:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case WGPUCompareFunction_Greater:
    return VK_COMPARE_OP_GREATER;
  case WGPUCompareFunction_NotEqual:
    return VK_COMPARE_OP_NOT_EQUAL;
  case WGPUCompareFunction_GreaterEqual:
    return VK_COMPARE_OP_GREATER_OR_EQUAL;
  default:
    return VK_COMPARE_OP_ALWAYS;
  }
}

VkBlendFactor ToVk(WGPUBlendFactor f) {
  switch (f) {
  case WGPUBlendFactor_Zero:
    return VK_BLEND_FACTOR_ZERO;
  case WGPUBlendFactor_One:
    return VK_BLEND_FACTOR_ONE;
  case WGPUBlendFactor_Src:
    return VK_BLEND_FACTOR_SRC_COLOR;
  case WGPUBlendFactor_OneMinusSrc:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case WGPUBlendFactor_SrcAlpha:
    return VK_BLEND_FACTOR_SRC_ALPHA;
  case WGPUBlendFactor_OneMinusSrcAlpha:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case WGPUBlendFactor_Dst:
    return VK_BLEND_FACTOR_DST_COLOR;
  case WGPUBlendFactor_OneMinusDst:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case WGPUBlendFactor_DstAlpha:
    return VK_BLEND_FACTOR_DST_ALPHA;
  case WGPUBlendFactor_OneMinusDstAlpha:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case WGPUBlendFactor_SrcAlphaSaturated:
    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
  case WGPUBlendFactor_Constant:
    return VK_BLEND_FACTOR_CONSTANT_COLOR;
  case WGPUBlendFactor_OneMinusConstant:
    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
  case WGPUBlendFactor_Src1:
    return VK_BLEND_FACTOR_SRC1_COLOR;
  case WGPUBlendFactor_OneMinusSrc1:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
  case WGPUBlendFactor_Src1Alpha:
    return VK_BLEND_FACTOR_SRC1_ALPHA;
  case WGPUBlendFactor_OneMinusSrc1Alpha:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
  default:
    return VK_BLEND_FACTOR_ONE;
  }
}

VkBlendOp ToVk(WGPUBlendOperation op) {
  switch (op) {
  case WGPUBlendOperation_Subtract:
    return VK_BLEND_OP_SUBTRACT;
  case WGPUBlendOperation_ReverseSubtract:
    return VK_BLEND_OP_REVERSE_SUBTRACT;
  case WGPUBlendOperation_Min:
    return VK_BLEND_OP_MIN;
  case WGPUBlendOperation_Max:
    return VK_BLEND_OP_MAX;
  default:
    return VK_BLEND_OP_ADD;
  }
}

VkStencilOp ToVk(WGPUStencilOperation op) {
  switch (op) {
  case WGPUStencilOperation_Zero:
    return VK_STENCIL_OP_ZERO;
  case WGPUStencilOperation_Replace:
    return VK_STENCIL_OP_REPLACE;
  case WGPUStencilOperation_Invert:
    return VK_STENCIL_OP_INVERT;
  case WGPUStencilOperation_IncrementClamp:
    return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
  case WGPUStencilOperation_DecrementClamp:
    return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
  case WGPUStencilOperation_IncrementWrap:
    return VK_STENCIL_OP_INCREMENT_AND_WRAP;
  case WGPUStencilOperation_DecrementWrap:
    return VK_STENCIL_OP_DECREMENT_AND_WRAP;
  default:
    return VK_STENCIL_OP_KEEP;
  }
}

VkSamplerAddressMode ToVk(WGPUAddressMode mode) {
  switch (mode) {
  case WGPUAddressMode_Repeat:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case WGPUAddressMode_MirrorRepeat:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  default:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
}

VkFormat ToVk(WGPUVertexFormat format) {
  switch (format) {
  case WGPUVertexFormat_Uint8x2:
    return VK_FORMAT_R8G8_UINT;
  case WGPUVertexFormat_Uint8x4:
    return VK_FORMAT_R8G8B8A8_UINT;
  case WGPUVertexFormat_Sint8x2:
    return VK_FORMAT_R8G8_SINT;
  case WGPUVertexFormat_Sint8x4:
    return VK_FORMAT_R8G8B8A8_SINT;
  case WGPUVertexFormat_Unorm8x2:
    return VK_FORMAT_R8G8_UNORM;
  case WGPUVertexFormat_Unorm8x4:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case WGPUVertexFormat_Snorm8x2:
    return VK_FORMAT_R8G8_SNORM;
  case WGPUVertexFormat_Snorm8x4:
    return VK_FORMAT_R8G8B8A8_SNORM;
  case WGPUVertexFormat_Uint16x2:
    return VK_FORMAT_R16G16_UINT;
  case WGPUVertexFormat_Uint16x4:
    return VK_FORMAT_R16G16B16A16_UINT;
  case WGPUVertexFormat_Sint16x2:
    return VK_FORMAT_R16G16_SINT;
  case WGPUVertexFormat_Sint16x4:
    return VK_FORMAT_R16G16B16A16_SINT;
  case WGPUVertexFormat_Unorm16x2:
    return VK_FORMAT_R16G16_UNORM;
  case WGPUVertexFormat_Unorm16x4:
    return VK_FORMAT_R16G16B16A16_UNORM;
  case WGPUVertexFormat_Snorm16x2:
    return VK_FORMAT_R16G16_SNORM;
  case WGPUVertexFormat_Snorm16x4:
    return VK_FORMAT_R16G16B16A16_SNORM;
  case WGPUVertexFormat_Float16x2:
    return VK_FORMAT_R16G16_SFLOAT;
  case WGPUVertexFormat_Float16x4:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case WGPUVertexFormat_Float32:
    return VK_FORMAT_R32_SFLOAT;
  case WGPUVertexFormat_Float32x2:
    return VK_FORMAT_R32G32_SFLOAT;
  case WGPUVertexFormat_Float32x3:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case WGPUVertexFormat_Float32x4:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  case WGPUVertexFormat_Uint32:
    return VK_FORMAT_R32_UINT;
  case WGPUVertexFormat_Uint32x2:
    return VK_FORMAT_R32G32_UINT;
  case WGPUVertexFormat_Uint32x3:
    return VK_FORMAT_R32G32B32_UINT;
  case WGPUVertexFormat_Uint32x4:
    return VK_FORMAT_R32G32B32A32_UINT;
  case WGPUVertexFormat_Sint32:
    return VK_FORMAT_R32_SINT;
  case WGPUVertexFormat_Sint32x2:
    return VK_FORMAT_R32G32_SINT;
  case WGPUVertexFormat_Sint32x3:
    return VK_FORMAT_R32G32B32_SINT;
  case WGPUVertexFormat_Sint32x4:
    return VK_FORMAT_R32G32B32A32_SINT;
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

VkShaderStageFlags ToVk(WGPUShaderStage stages) {
  VkShaderStageFlags out = 0;
  if (stages & WGPUShaderStage_Vertex) {
    out |= VK_SHADER_STAGE_VERTEX_BIT;
  }
  if (stages & WGPUShaderStage_Fragment) {
    out |= VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  if (stages & WGPUShaderStage_Compute) {
    out |= VK_SHADER_STAGE_COMPUTE_BIT;
  }
  return out;
}

// GPU timestamps of one command buffer: query 0 at its start, the last at its end, and a pair around
// each render pass. Read back when the submission retires (AURORA_VK_GPU_TIMING=0 turns them off).
struct SlotTiming {
  static constexpr uint32_t kQueries = 256;
  // AURORA_VK_DRAW_TIMING=1: a timestamp after every draw too (use with TU_DEBUG=sysmem, where a
  // pass's draws run in order once instead of once a tile).
  static constexpr uint32_t kDrawQueries = 4096;
  struct Draw {
    uint32_t before;
    uint32_t after;
    uint32_t elements;
    uint32_t instances;
    uint32_t pipeline;
    bool blend;
  };
  struct Pass {
    uint32_t query;
    uint32_t draws;
    uint32_t width;
    uint32_t height;
  };
  VkQueryPool pool = VK_NULL_HANDLE;
  uint32_t capacity = kQueries;
  uint32_t used = 0;
  bool present = false;
  std::vector<Pass> passes;
  std::vector<Draw> draws;
};

struct CmdSlot {
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  SlotTiming* timing = nullptr;
};

// What the timestamps add up to between two log lines.
struct GpuTimingStats {
  std::mutex mutex;
  std::chrono::steady_clock::time_point windowStart{};
  uint64_t frames = 0;
  double busyMs = 0;
  double passMs = 0;
  uint64_t passes = 0;
  uint64_t draws = 0;
  double frameMs = 0;
  double worstFrameMs = 0;
  double heaviestPassMs = 0;
  SlotTiming::Pass heaviestPass{};
  struct PipelineCost {
    double ms = 0;
    uint64_t draws = 0;
    uint64_t elements = 0;
    bool blend = false;
  };
  std::unordered_map<uint32_t, PipelineCost> pipelines;
  double heaviestDrawMs = 0;
  SlotTiming::Draw heaviestDraw{};
  // Windows whose GPU busy time added up to more than the wall time, and whether the device's
  // timestamps were given up on for that (see AddGpuTiming).
  int implausibleWindows = 0;
  bool implausible = false;
};

struct StagingChunk {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation alloc = nullptr;
  uint8_t* mapped = nullptr;
  uint64_t size = 0;
  uint64_t used = 0;
};

// A submission's resources, released when the timeline reaches its serial.
struct InFlight {
  uint64_t serial = 0;
  std::vector<CmdSlot> slots;
  std::vector<Object*> refs;
  std::vector<StagingChunk*> chunks;
};

struct FutureRec {
  uint64_t id = 0;
  uint64_t serial = 0;
  WGPUCallbackMode mode = WGPUCallbackMode_AllowSpontaneous;
  std::function<void()> fire;
};

// Tier 0: the fence one submission signals, in place of the timeline value. Shared so a thread waiting
// on it keeps it alive after PollCompleted has dropped it; never reset or reused.
struct SerialFence {
  VkDevice device = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  ~SerialFence() {
    if (fence != VK_NULL_HANDLE) {
      vkDestroyFence(device, fence, nullptr);
    }
  }
};

} // namespace

struct WGPUInstanceImpl : Object {};
struct WGPUAdapterImpl : Object {};
struct WGPUSurfaceImpl;
struct WGPUTextureImpl;
void DestroyViewFramebuffers(WGPUDeviceImpl* dev, VkImageView view);

struct WGPUDeviceImpl : Object {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VmaAllocator vma = nullptr;
  VkSemaphore timeline = VK_NULL_HANDLE;
  VkPipelineCache pipelineCache = VK_NULL_HANDLE;
  std::filesystem::path pipelineCachePath;
  std::atomic<uint32_t> pipelinesSinceSave{0};
  VkPhysicalDeviceProperties props{};
  bool hasBC = false;
  bool hasAnisotropy = false;
  bool hasDepthClamp = false;
  // Vulkan 1.1/1.2 devices run with the 1.3 features taken from extensions instead.
  bool api13 = false;
  bool hasStoreOpNone = false;
  bool hasDemote = false;
  // malisx2 on a job-manager Mali (v7: Mali-G52/G76) reads an indexed draw's indices on the CPU, so
  // it drops draws whose indices a copy in the same command buffer writes. Index buffers are then
  // host-visible and a copy into one gives it fresh memory filled by the CPU (RenameIndexCopy).
  bool renameIndexCopies = false;
  // MapWrite staging in cached, coherent memory instead of write-combined (ChooseCachedStaging).
  bool cachedStaging = false;
  std::mutex backingMutex;
  std::vector<std::tuple<uint64_t, VkBuffer, VmaAllocation, uint8_t*>> freeBackings;
  bool hasSync2 = false;
  // Tier 0 (Android 10 and older drivers with Vulkan 1.1 but no dynamic rendering or timeline
  // semaphores: Adreno 5xx, PowerVR GE8320): passes are VkRenderPass + VkFramebuffer and every
  // submission signals a fence. False everywhere else, where nothing below is used.
  bool legacy = false;
  // Tier 0 on PowerVR Rogue: no CLEAR load op in any pass; clears go through vkCmdClearAttachments.
  bool clearByAttachments = false;
  // VK_EXT_memory_budget is on: VMA tracks the driver's budget, logged when an allocation fails.
  bool hasMemoryBudget = false;
  std::mutex renderPassMutex;
  std::unordered_map<std::string, VkRenderPass> renderPasses;
  struct CachedFramebuffer {
    VkFramebuffer fb = VK_NULL_HANDLE;
    std::vector<VkImageView> views;
  };
  std::mutex framebufferMutex;
  std::unordered_map<std::string, CachedFramebuffer> framebuffers;
  // Submitted fences in serial order, and the last serial seen finished. Guarded by fenceMutex.
  std::mutex fenceMutex;
  std::deque<std::pair<uint64_t, std::shared_ptr<SerialFence>>> serialFences;
  uint64_t fenceCompletedSerial = 0;
  bool gpuTiming = false;
  bool drawTiming = false;
  std::atomic<uint32_t> pipelineIds{0};
  uint64_t timestampMask = 0;
  GpuTimingStats gpuStats;

  std::recursive_mutex queueMutex;
  std::atomic<uint64_t> submittedSerial{0};
  std::atomic<uint64_t> completedSerial{0};
  CmdSlot upload{};
  bool uploadOpen = false;
  std::vector<StagingChunk*> uploadChunks;
  std::vector<Object*> uploadRefs;
  VkSemaphore pendingAcquire = VK_NULL_HANDLE;
  // The surface slot that records which submission waited on pendingAcquire.
  uint64_t* pendingAcquireSerial = nullptr;
  VkSemaphore pendingPresent = VK_NULL_HANDLE;
  bool presentSignaled = false;

  std::mutex slotMutex;
  std::vector<CmdSlot> freeSlots;
  std::mutex chunkMutex;
  std::vector<StagingChunk*> freeChunks;

  std::mutex inflightMutex;
  std::deque<InFlight> inflight;

  std::mutex futureMutex;
  std::condition_variable futureCv;
  std::vector<FutureRec> futures;
  uint64_t nextFutureId = 1;

  std::mutex poolMutex;
  std::vector<VkDescriptorPool> descriptorPools;

  std::mutex spirvMutex;
  std::unordered_map<uint64_t, std::vector<uint32_t>> spirvCache;

  std::thread completionThread;
  std::atomic<bool> stopping{false};
  std::atomic<uint64_t> encoderIds{0};

  WGPUUncapturedErrorCallbackInfo errorCallback{};
  WGPULoggingCallbackInfo loggingCallback{};

  ~WGPUDeviceImpl() override;
};

// aurora_get_frame_worker_totals: the GPU time of the presented frames (dynamic resolution).
namespace aurora {
extern std::atomic<uint64_t> g_gpuNanos;
extern std::atomic<uint64_t> g_gpuFrames;
} // namespace aurora

namespace {

WGPUDeviceImpl* g_device = nullptr;
WGPUInstanceImpl* g_instanceObj = nullptr;
VkInstance g_vkInstance = VK_NULL_HANDLE;

void ReportError(WGPUDeviceImpl* dev, const std::string& message) {
  Log("error: %s", message.c_str());
  if (dev != nullptr && dev->errorCallback.callback != nullptr) {
    WGPUDevice handle = dev;
    dev->errorCallback.callback(&handle, WGPUErrorType_Validation, WGPUStringView{message.data(), message.size()},
                                dev->errorCallback.userdata1, dev->errorCallback.userdata2);
  }
}

// Writes the next timestamp of the slot and returns its query, or UINT32_MAX without timing or room.
// `need` queries are kept free (a pass needs its end too), plus one for the closing timestamp unless
// this is it.
uint32_t WriteTimestamp(CmdSlot& slot, bool closing = false, uint32_t need = 1) {
  SlotTiming* timing = slot.timing;
  if (timing == nullptr || timing->used + need + (closing ? 0 : 1) > timing->capacity) {
    return UINT32_MAX;
  }
  vkCmdWriteTimestamp(slot.cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing->pool, timing->used);
  return timing->used++;
}

CmdSlot AcquireSlot(WGPUDeviceImpl* dev) {
  CmdSlot slot;
  {
    std::lock_guard lock(dev->slotMutex);
    if (!dev->freeSlots.empty()) {
      slot = dev->freeSlots.back();
      dev->freeSlots.pop_back();
    }
  }
  if (slot.pool == VK_NULL_HANDLE) {
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = dev->queueFamily,
    };
    VK_CHECK(vkCreateCommandPool(dev->device, &poolInfo, nullptr, &slot.pool));
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = slot.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(dev->device, &allocInfo, &slot.cb));
  } else {
    VK_CHECK(vkResetCommandPool(dev->device, slot.pool, 0));
  }
  const VkCommandBufferBeginInfo begin{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  VK_CHECK(vkBeginCommandBuffer(slot.cb, &begin));
  if (dev->gpuTiming) {
    if (slot.timing == nullptr) {
      slot.timing = new SlotTiming;
      slot.timing->capacity = dev->drawTiming ? SlotTiming::kDrawQueries : SlotTiming::kQueries;
      const VkQueryPoolCreateInfo info{
          .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
          .queryType = VK_QUERY_TYPE_TIMESTAMP,
          .queryCount = slot.timing->capacity,
      };
      VK_CHECK(vkCreateQueryPool(dev->device, &info, nullptr, &slot.timing->pool));
    }
    slot.timing->used = 0;
    slot.timing->present = false;
    slot.timing->passes.clear();
    slot.timing->draws.clear();
    vkCmdResetQueryPool(slot.cb, slot.timing->pool, 0, slot.timing->capacity);
    WriteTimestamp(slot);
  }
  return slot;
}

// Ends a command buffer, with its closing timestamp.
void EndSlot(CmdSlot& slot) {
  WriteTimestamp(slot, true);
  VK_CHECK(vkEndCommandBuffer(slot.cb));
}

void RecycleSlot(WGPUDeviceImpl* dev, CmdSlot slot) {
  std::lock_guard lock(dev->slotMutex);
  dev->freeSlots.push_back(slot);
}

void GlobalBarrier(VkCommandBuffer cb) {
  const VkMemoryBarrier2 barrier{
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
  };
  const VkDependencyInfo dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cb, &dep);
}

// Between passes and copies of an encoder. GlobalBarrier there made the next pass's vertex and tiling
// work wait for the previous pass's fragment work, which on a tile-based GPU (Mali) stops the two from
// overlapping. Each barrier below waits only on the stages that can have done the earlier access:
// - transfer and compute writes, which anything later may read (vertex data, uniforms, textures);
// - attachment and fragment-shader writes and reads, which later fragment, transfer and compute work
//   depends on (render-to-texture, EFB copies, attachment reuse) but vertex work never does;
// - vertex-stage reads, which a later transfer or compute write must not overtake.
void PassBarrier(VkCommandBuffer cb, bool memory = true, const std::vector<VkImageMemoryBarrier2>* images = nullptr) {
  constexpr VkPipelineStageFlags2 kFragmentStages =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  constexpr VkPipelineStageFlags2 kWriteStages = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  const std::array<VkMemoryBarrier2, 3> barriers{{
      {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
       .srcStageMask = kWriteStages,
       .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
       .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
       .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT},
      {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
       .srcStageMask = kFragmentStages,
       .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
       .dstStageMask = kFragmentStages | kWriteStages,
       .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT},
      {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
       .srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
       .dstStageMask = kWriteStages},
  }};
  const bool hasImages = images != nullptr && !images->empty();
  const VkDependencyInfo dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = memory ? static_cast<uint32_t>(barriers.size()) : 0u,
      .pMemoryBarriers = barriers.data(),
      .imageMemoryBarrierCount = hasImages ? static_cast<uint32_t>(images->size()) : 0u,
      .pImageMemoryBarriers = hasImages ? images->data() : nullptr,
  };
  vkCmdPipelineBarrier2(cb, &dep);
}

// After a queue write's copy: only the transfer has to finish before anything later reads the data.
void TransferBarrier(VkCommandBuffer cb) {
  const VkMemoryBarrier2 barrier{
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
  };
  const VkDependencyInfo dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cb, &dep);
}

void HostReadBarrier(VkCommandBuffer cb) {
  const VkMemoryBarrier2 barrier{
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
      .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
  };
  const VkDependencyInfo dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cb, &dep);
}

void ImageLayoutBarrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                        VkImageLayout to) {
  const VkImageMemoryBarrier2 barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
      .oldLayout = from,
      .newLayout = to,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS},
  };
  const VkDependencyInfo dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cb, &dep);
}

// Image layouts. Every texture rests in a "home" layout between operations, chosen from its usage:
// sampled -> SHADER_READ_ONLY (DEPTH_STENCIL_READ_ONLY for depth), attachment only -> its attachment
// layout, storage or copy only -> GENERAL. An encoder moves a subresource out of home only for the
// operation that needs it and returns it before any pass that might sample it and before Finish, so
// every command buffer starts and ends with all textures at home and submission order never matters.
// Bind groups name the home layout. GENERAL everywhere can keep tile-based GPUs (Mali AFBC, Adreno
// UBWC on the proprietary driver) from compressing; AURORA_VK_LAYOUTS=0 brings it back for comparison.
bool LayoutsEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("AURORA_VK_LAYOUTS");
    return v == nullptr || v[0] != '0';
  }();
  return enabled;
}

VkImageLayout OpLayout(VkImageLayout layout) { return LayoutsEnabled() ? layout : VK_IMAGE_LAYOUT_GENERAL; }

struct LayoutScope {
  VkPipelineStageFlags2 stages;
  VkAccessFlags2 access;
};

LayoutScope ScopeFor(VkImageLayout layout) {
  constexpr VkPipelineStageFlags2 kShaders = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  constexpr VkPipelineStageFlags2 kTests =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  switch (layout) {
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    return {kTests, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
    return {kTests | kShaders, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT};
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    return {kShaders, VK_ACCESS_2_SHADER_READ_BIT};
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    return {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    return {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
  case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
  case VK_IMAGE_LAYOUT_UNDEFINED:
    // ALL_COMMANDS so a swapchain image's first transition still waits on the acquire semaphore.
    return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
  default:
    return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
  }
}

VkImageMemoryBarrier2 LayoutTransition(VkImage image, VkImageAspectFlags aspect, uint32_t mip, uint32_t mipCount,
                                       uint32_t layer, uint32_t layerCount, VkImageLayout from, VkImageLayout to) {
  constexpr VkAccessFlags2 kWrites = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
                                     VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
  const LayoutScope src = ScopeFor(from);
  const LayoutScope dst = ScopeFor(to);
  return VkImageMemoryBarrier2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = src.stages,
      .srcAccessMask = src.access & kWrites,
      .dstStageMask = dst.stages,
      .dstAccessMask = dst.access,
      .oldLayout = from,
      .newLayout = to,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {aspect, mip, mipCount, layer, layerCount},
  };
}

void ImageLayoutBarrier2(VkCommandBuffer cb, const VkImageMemoryBarrier2& barrier) {
  const VkDependencyInfo dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cb, &dep);
}

// The upload command buffer collects queue writes and image initialization; it is submitted ahead of
// the next queue submit, which is the order WebGPU defines for them. Caller holds queueMutex.
VkCommandBuffer UploadCb(WGPUDeviceImpl* dev) {
  if (!dev->uploadOpen) {
    dev->upload = AcquireSlot(dev);
    dev->uploadOpen = true;
    GlobalBarrier(dev->upload.cb);
  }
  return dev->upload.cb;
}

// Staging space for queue writes. Caller holds queueMutex.
std::pair<StagingChunk*, uint64_t> AllocStaging(WGPUDeviceImpl* dev, uint64_t size, uint64_t align) {
  if (!dev->uploadChunks.empty()) {
    StagingChunk* chunk = dev->uploadChunks.back();
    const uint64_t offset = (chunk->used + align - 1) / align * align;
    if (offset + size <= chunk->size) {
      chunk->used = offset + size;
      return {chunk, offset};
    }
  }
  constexpr uint64_t kChunkSize = 8ull << 20;
  StagingChunk* chunk = nullptr;
  if (size <= kChunkSize) {
    std::lock_guard lock(dev->chunkMutex);
    if (!dev->freeChunks.empty()) {
      chunk = dev->freeChunks.back();
      dev->freeChunks.pop_back();
    }
  }
  if (chunk == nullptr) {
    chunk = new StagingChunk;
    chunk->size = std::max(size, kChunkSize);
    const VkBufferCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = chunk->size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    const VmaAllocationCreateInfo allocInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo result{};
    VK_CHECK(vmaCreateBuffer(dev->vma, &info, &allocInfo, &chunk->buffer, &chunk->alloc, &result));
    chunk->mapped = static_cast<uint8_t*>(result.pMappedData);
  }
  chunk->used = size;
  dev->uploadChunks.push_back(chunk);
  return {chunk, 0};
}

// The acquire semaphore, if this submission is the first to use the swapchain image. Caller holds queueMutex.
VkSemaphore TakeAcquire(WGPUDeviceImpl* dev, bool touchesSwapchain, uint64_t serial) {
  if (!touchesSwapchain || dev->pendingAcquire == VK_NULL_HANDLE) {
    return VK_NULL_HANDLE;
  }
  const VkSemaphore sem = dev->pendingAcquire;
  dev->pendingAcquire = VK_NULL_HANDLE;
  if (dev->pendingAcquireSerial != nullptr) {
    *dev->pendingAcquireSerial = serial;
    dev->pendingAcquireSerial = nullptr;
  }
  return sem;
}

// Tier 0: the fence that submission `serial` signals, or VK_NULL_HANDLE on a timeline device. Caller
// holds queueMutex and passes it to the submit of that serial.
VkFence NewSerialFence(WGPUDeviceImpl* dev, uint64_t serial) {
  if (!dev->legacy) {
    return VK_NULL_HANDLE;
  }
  auto fence = std::make_shared<SerialFence>();
  fence->device = dev->device;
  const VkFenceCreateInfo info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VK_CHECK(vkCreateFence(dev->device, &info, nullptr, &fence->fence));
  const VkFence handle = fence->fence;
  std::lock_guard lock(dev->fenceMutex);
  dev->serialFences.emplace_back(serial, std::move(fence));
  return handle;
}

// Waits up to timeoutNs for submission `serial` to finish.
void WaitSerial(WGPUDeviceImpl* dev, uint64_t serial, uint64_t timeoutNs) {
  if (!dev->legacy) {
    const VkSemaphoreWaitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &dev->timeline,
        .pValues = &serial,
    };
    if (vkWaitSemaphores(dev->device, &wait, timeoutNs) == VK_SUCCESS) {
      // The timeline only grows, so a finished wait proves `serial` complete without another query.
      uint64_t prev = dev->completedSerial.load(std::memory_order_relaxed);
      while (serial > prev && !dev->completedSerial.compare_exchange_weak(prev, serial)) {
      }
    }
    return;
  }
  // The first fence at or after `serial`; none left means it has already finished.
  std::shared_ptr<SerialFence> fence;
  {
    std::lock_guard lock(dev->fenceMutex);
    for (const auto& entry : dev->serialFences) {
      if (entry.first >= serial) {
        fence = entry.second;
        break;
      }
    }
  }
  if (fence != nullptr) {
    vkWaitForFences(dev->device, 1, &fence->fence, VK_TRUE, timeoutNs);
  }
}

uint64_t PollCompleted(WGPUDeviceImpl* dev) {
  uint64_t value = 0;
  if (dev->legacy) {
    // Fences are checked in submission order and stop at the first unfinished one, so the serial
    // reported never runs ahead of an earlier submission.
    std::lock_guard lock(dev->fenceMutex);
    while (!dev->serialFences.empty()) {
      const VkResult status = vkGetFenceStatus(dev->device, dev->serialFences.front().second->fence);
      if (status == VK_NOT_READY) {
        break;
      }
      if (status != VK_SUCCESS) {
        Fatal("vkGetFenceStatus", status);
      }
      dev->fenceCompletedSerial = dev->serialFences.front().first;
      dev->serialFences.pop_front();
    }
    value = dev->fenceCompletedSerial;
  } else {
    VK_CHECK(vkGetSemaphoreCounterValue(dev->device, dev->timeline, &value));
  }
  uint64_t prev = dev->completedSerial.load(std::memory_order_relaxed);
  while (value > prev && !dev->completedSerial.compare_exchange_weak(prev, value)) {
  }
  return std::max(value, prev);
}

// Adds a finished command buffer's timestamps to the GPU time stats, and logs them every 2 s:
// GPU busy time a presented frame, how much of it is inside render passes, and the heaviest pass.
void AddGpuTiming(WGPUDeviceImpl* dev, SlotTiming& timing) {
  if (timing.used < 2) {
    return;
  }
  static thread_local std::vector<uint64_t> stamps;
  stamps.resize(timing.used);
  if (vkGetQueryPoolResults(dev->device, timing.pool, 0, timing.used, timing.used * sizeof(uint64_t), stamps.data(),
                            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
    return;
  }
  const double msPerTick = dev->props.limits.timestampPeriod / 1e6;
  const auto ms = [&](uint32_t a, uint32_t b) {
    return static_cast<double>((stamps[b] - stamps[a]) & dev->timestampMask) * msPerTick;
  };
  GpuTimingStats& s = dev->gpuStats;
  std::lock_guard lock(s.mutex);
  const double busy = ms(0, timing.used - 1);
  s.busyMs += busy;
  s.frameMs += busy;
  for (const SlotTiming::Pass& pass : timing.passes) {
    const double passMs = ms(pass.query, pass.query + 1);
    s.passMs += passMs;
    ++s.passes;
    s.draws += pass.draws;
    if (passMs > s.heaviestPassMs) {
      s.heaviestPassMs = passMs;
      s.heaviestPass = pass;
    }
  }
  for (const SlotTiming::Draw& draw : timing.draws) {
    const double drawMs = ms(draw.before, draw.after);
    auto& cost = s.pipelines[draw.pipeline];
    cost.ms += drawMs;
    ++cost.draws;
    cost.elements += static_cast<uint64_t>(draw.elements) * draw.instances;
    cost.blend = draw.blend;
    if (drawMs > s.heaviestDrawMs) {
      s.heaviestDrawMs = drawMs;
      s.heaviestDraw = draw;
    }
  }
  if (!timing.present) {
    return;
  }
  ++s.frames;
  if (!s.implausible) {
    aurora::g_gpuNanos.fetch_add(static_cast<uint64_t>(s.frameMs * 1e6), std::memory_order_relaxed);
    aurora::g_gpuFrames.fetch_add(1, std::memory_order_relaxed);
  }
  s.worstFrameMs = std::max(s.worstFrameMs, s.frameMs);
  s.frameMs = 0;
  const auto now = std::chrono::steady_clock::now();
  if (s.windowStart.time_since_epoch().count() == 0) {
    s.windowStart = now;
  }
  if (now - s.windowStart < std::chrono::seconds(2)) {
    return;
  }
  // One queue runs one frame after another, so its busy time cannot exceed the wall time. The stock
  // Adreno 618 driver (512.502, tile rendering) reported 1.5-2.2 s of GPU work a second (40 ms frames
  // at 46 fps) while the frame worker never waited on the GPU: its timestamps say nothing about
  // load, and dynamic resolution dropped to 50-60% on them. Two such windows and the device's GPU
  // time is no longer passed on (dynamic resolution and the overlay fall back to the worker's).
  const double wallMs = std::chrono::duration<double, std::milli>(now - s.windowStart).count();
  if (!s.implausible) {
    s.implausibleWindows = s.busyMs > wallMs * 1.1 ? s.implausibleWindows + 1 : 0;
    if (s.implausibleWindows >= 2) {
      s.implausible = true;
      Log("gpu timestamps add up to %.2f s of work a second: not used for dynamic resolution", s.busyMs / wallMs);
    }
  }
  const double frames = static_cast<double>(s.frames);
  Log("gpu: %.2f ms a frame (worst %.2f), passes %.2f ms in %.1f with %.0f draws, outside passes %.2f; "
      "heaviest pass %.2f ms %ux%u %u draws",
      s.busyMs / frames, s.worstFrameMs, s.passMs / frames, s.passes / frames, s.draws / frames,
      (s.busyMs - s.passMs) / frames, s.heaviestPassMs, s.heaviestPass.width, s.heaviestPass.height,
      s.heaviestPass.draws);
  if (!s.pipelines.empty()) {
    std::vector<std::pair<uint32_t, GpuTimingStats::PipelineCost>> costs(s.pipelines.begin(), s.pipelines.end());
    std::sort(costs.begin(), costs.end(), [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    std::string top;
    char item[128];
    for (size_t i = 0; i < std::min<size_t>(costs.size(), 8); ++i) {
      const auto& [id, c] = costs[i];
      std::snprintf(item, sizeof(item), "%s#%u%s %.2f ms/%.1f draws/%.0f el", i ? "; " : "", id, c.blend ? "b" : "",
                    c.ms / frames, c.draws / frames, c.elements / frames);
      top += item;
    }
    Log("gpu draws: %zu pipelines, heaviest draw %.2f ms (#%u%s, %u elements x%u); top: %s", costs.size(),
        s.heaviestDrawMs, s.heaviestDraw.pipeline, s.heaviestDraw.blend ? "b" : "", s.heaviestDraw.elements,
        s.heaviestDraw.instances, top.c_str());
    s.pipelines.clear();
    s.heaviestDrawMs = 0;
  }
  s.windowStart = now;
  s.frames = 0;
  s.busyMs = s.passMs = s.worstFrameMs = s.heaviestPassMs = 0;
  s.passes = s.draws = 0;
}

// The completed serial for the frame worker's hot path. On a timeline device CompletionThread keeps
// completedSerial current (it waits on the oldest submission, then polls), so the worker reads the
// atomic instead of asking the driver: Turnip on KGSL emulates timelines, and its
// vkGetSemaphoreCounterValue blocked the Redmi's frame worker 57% of the time (2026-11-08). Tier 0
// fences stay polled: they are cheap on the stock drivers that use them.
uint64_t KnownCompleted(WGPUDeviceImpl* dev) {
  return dev->legacy ? PollCompleted(dev) : dev->completedSerial.load(std::memory_order_acquire);
}

// Releases the resources of every finished submission. `poll` asks the driver for the latest serial;
// without it the last known one is used (see KnownCompleted).
void Retire(WGPUDeviceImpl* dev, bool poll = true) {
  const uint64_t completed = poll ? PollCompleted(dev) : KnownCompleted(dev);
  std::vector<InFlight> done;
  {
    std::lock_guard lock(dev->inflightMutex);
    while (!dev->inflight.empty() && dev->inflight.front().serial <= completed) {
      done.push_back(std::move(dev->inflight.front()));
      dev->inflight.pop_front();
    }
  }
  for (auto& entry : done) {
    for (CmdSlot slot : entry.slots) {
      if (slot.timing != nullptr) {
        AddGpuTiming(dev, *slot.timing);
      }
      RecycleSlot(dev, slot);
    }
    {
      std::lock_guard lock(dev->chunkMutex);
      for (StagingChunk* chunk : entry.chunks) {
        chunk->used = 0;
        dev->freeChunks.push_back(chunk);
      }
    }
    for (Object* obj : entry.refs) {
      obj->Release();
    }
  }
}

uint64_t AddFuture(WGPUDeviceImpl* dev, uint64_t serial, WGPUCallbackMode mode, std::function<void()> fire) {
  uint64_t id;
  {
    std::lock_guard lock(dev->futureMutex);
    id = dev->nextFutureId++;
    dev->futures.push_back(FutureRec{id, serial, mode, std::move(fire)});
  }
  dev->futureCv.notify_all();
  return id;
}

// Fires the ready futures that `accept` allows; returns how many fired.
size_t FireReady(WGPUDeviceImpl* dev, const std::function<bool(const FutureRec&)>& accept) {
  const uint64_t completed = dev->completedSerial.load(std::memory_order_acquire);
  std::vector<FutureRec> ready;
  {
    std::lock_guard lock(dev->futureMutex);
    auto it = std::stable_partition(dev->futures.begin(), dev->futures.end(),
                                    [&](const FutureRec& f) { return !(f.serial <= completed && accept(f)); });
    std::move(it, dev->futures.end(), std::back_inserter(ready));
    dev->futures.erase(it, dev->futures.end());
  }
  for (auto& f : ready) {
    f.fire();
  }
  return ready.size();
}

void CompletionThread(WGPUDeviceImpl* dev) {
  while (!dev->stopping.load(std::memory_order_acquire)) {
    uint64_t target = 0;
    {
      std::unique_lock lock(dev->inflightMutex);
      if (!dev->inflight.empty()) {
        target = dev->inflight.front().serial;
      }
    }
    // Sleep until a submission or a new future arrives, or the oldest submission finishes. Futures
    // whose work is already done are also fired by ProcessEvents, so a coarse timeout is enough.
    if (target == 0) {
      std::unique_lock lock(dev->futureMutex);
      dev->futureCv.wait_for(lock, std::chrono::milliseconds(50));
    } else {
      WaitSerial(dev, target, 50'000'000);
    }
    // A timeline wait that finished already raised completedSerial (WaitSerial); an unfinished
    // oldest submission means nothing later is done either.
    Retire(dev, dev->legacy);
    FireReady(dev, [](const FutureRec& f) { return f.mode == WGPUCallbackMode_AllowSpontaneous; });
  }
}

VkDescriptorPool NewDescriptorPool(WGPUDeviceImpl* dev) {
  constexpr uint32_t kSets = 2048;
  const std::array<VkDescriptorPoolSize, 7> sizes{{
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSets * 2},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSets * 2},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSets * 2},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kSets * 2},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSets * 8},
      {VK_DESCRIPTOR_TYPE_SAMPLER, kSets * 8},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSets},
  }};
  const VkDescriptorPoolCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = kSets,
      .poolSizeCount = static_cast<uint32_t>(sizes.size()),
      .pPoolSizes = sizes.data(),
  };
  VkDescriptorPool pool;
  VK_CHECK(vkCreateDescriptorPool(dev->device, &info, nullptr, &pool));
  dev->descriptorPools.push_back(pool);
  return pool;
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// Resources

struct WGPUBufferImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation alloc = nullptr;
  uint8_t* mapped = nullptr;
  uint64_t size = 0;
  WGPUBufferUsage usage = 0;
  // A buffer mapped at creation that is not host visible: written through `staging` on unmap.
  std::vector<uint8_t> staging;
  bool stagingMapped = false;
  std::atomic<uint64_t> lastUseSerial{0};

  ~WGPUBufferImpl() override {
    if (buffer != VK_NULL_HANDLE) {
      vmaDestroyBuffer(dev->vma, buffer, alloc);
    }
  }
};

// The memory an index buffer had before RenameIndexCopy replaced it: kept by the command buffers that
// may still read it, then returned to the device's pool.
struct RetiredBacking : Object {
  WGPUDeviceImpl* dev = nullptr;
  uint64_t size = 0;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation alloc = nullptr;
  uint8_t* mapped = nullptr;
  ~RetiredBacking() override {
    std::lock_guard lock(dev->backingMutex);
    dev->freeBackings.emplace_back(size, buffer, alloc, mapped);
  }
};

struct WGPUTextureImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation alloc = nullptr;
  WGPUTextureFormat format = WGPUTextureFormat_Undefined;
  FormatInfo info{};
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t layers = 1;
  uint32_t mips = 1;
  uint32_t samples = 1;
  bool swapchain = false;
  // Swapchain images only: the layout as recorded so far (UNDEFINED after acquire).
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  // Where the image rests between operations; see LayoutsEnabled.
  VkImageLayout home = VK_IMAGE_LAYOUT_GENERAL;
  VkImageAspectFlags aspects = VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageView defaultView = VK_NULL_HANDLE;

  ~WGPUTextureImpl() override {
    if (defaultView != VK_NULL_HANDLE) {
      DestroyViewFramebuffers(dev, defaultView);
      vkDestroyImageView(dev->device, defaultView, nullptr);
    }
    if (image != VK_NULL_HANDLE && !swapchain) {
      vmaDestroyImage(dev->vma, image, alloc);
    }
  }
};

struct WGPUTextureViewImpl : Object {
  WGPUTextureImpl* texture = nullptr;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED; // the view's own format (tier 0 render passes)
  uint32_t baseMip = 0;
  uint32_t baseLayer = 0;
  uint32_t width = 0;
  uint32_t height = 0;

  ~WGPUTextureViewImpl() override {
    if (view != VK_NULL_HANDLE && view != texture->defaultView) {
      DestroyViewFramebuffers(texture->dev, view);
      vkDestroyImageView(texture->dev->device, view, nullptr);
    }
    texture->Release();
  }
};

struct WGPUSamplerImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  VkSampler sampler = VK_NULL_HANDLE;
  ~WGPUSamplerImpl() override { vkDestroySampler(dev->device, sampler, nullptr); }
};

struct WGPUShaderModuleImpl : Object {
  std::string wgsl;
  std::string label;
};

namespace {
enum class BindKind : uint8_t { Uniform, Storage, ReadOnlyStorage, Texture, Sampler, StorageTexture };
struct BindEntry {
  uint32_t binding;
  BindKind kind;
  bool dynamic;
};
} // namespace

struct WGPUBindGroupLayoutImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  std::vector<BindEntry> entries; // sorted by binding
  uint32_t dynamicCount = 0;
  ~WGPUBindGroupLayoutImpl() override { vkDestroyDescriptorSetLayout(dev->device, layout, nullptr); }
};

struct WGPUBindGroupImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  WGPUBindGroupLayoutImpl* layout = nullptr;
  VkDescriptorSet set = VK_NULL_HANDLE;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  std::vector<Object*> refs;
  ~WGPUBindGroupImpl() override {
    {
      std::lock_guard lock(dev->poolMutex);
      vkFreeDescriptorSets(dev->device, pool, 1, &set);
    }
    for (Object* obj : refs) {
      obj->Release();
    }
    layout->Release();
  }
};

struct WGPUPipelineLayoutImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  std::vector<WGPUBindGroupLayoutImpl*> groups;
  std::vector<VkDescriptorSetLayout> ownedEmpty;
  ~WGPUPipelineLayoutImpl() override {
    vkDestroyPipelineLayout(dev->device, layout, nullptr);
    for (VkDescriptorSetLayout empty : ownedEmpty) {
      vkDestroyDescriptorSetLayout(dev->device, empty, nullptr);
    }
    for (auto* group : groups) {
      if (group != nullptr) {
        group->Release();
      }
    }
  }
};

struct WGPURenderPipelineImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  VkPipeline pipeline = VK_NULL_HANDLE;
  WGPUPipelineLayoutImpl* layout = nullptr;
  uint32_t id = 0;
  bool blend = false;
  ~WGPURenderPipelineImpl() override {
    vkDestroyPipeline(dev->device, pipeline, nullptr);
    layout->Release();
  }
};

struct WGPUComputePipelineImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  VkPipeline pipeline = VK_NULL_HANDLE;
  WGPUPipelineLayoutImpl* layout = nullptr;
  ~WGPUComputePipelineImpl() override {
    vkDestroyPipeline(dev->device, pipeline, nullptr);
    layout->Release();
  }
};

struct WGPUQuerySetImpl : Object {};

// ---------------------------------------------------------------------------------------------------
// Command recording

struct WGPUCommandBufferImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  CmdSlot slot{};
  bool submitted = false;
  bool touchesSwapchain = false;
  std::vector<Object*> refs;
  std::vector<WGPUBufferImpl*> mapTracked;
  ~WGPUCommandBufferImpl() override {
    if (!submitted) {
      // Never submitted: nothing on the GPU uses it.
      RecycleSlot(dev, slot);
      for (Object* obj : refs) {
        obj->Release();
      }
    }
  }
};

struct WGPUCommandEncoderImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
  CmdSlot slot{};
  uint64_t id = 0;
  bool needBarrier = true;
  bool finished = false;
  bool touchesSwapchain = false;
  WGPUTextureImpl* swapchainTex = nullptr;
  std::vector<Object*> refs;
  std::vector<WGPUBufferImpl*> mapTracked;

  void Ref(Object* obj) {
    if (obj != nullptr && obj->encoderStamp.load(std::memory_order_relaxed) != id &&
        obj->encoderStamp.exchange(id, std::memory_order_relaxed) != id) {
      obj->AddRef();
      refs.push_back(obj);
    }
  }
  // Subresources (one mip, one layer) currently away from their texture's home layout.
  struct Away {
    WGPUTextureImpl* tex;
    uint32_t mip;
    uint32_t layer;
    VkImageLayout layout;
    bool keep;
  };
  std::vector<Away> away;
  std::vector<VkImageMemoryBarrier2> transitions;

  // Pending layout transitions go out with the pass barrier, in one call.
  void Barrier() {
    if (needBarrier || !transitions.empty()) {
      PassBarrier(slot.cb, needBarrier, &transitions);
      needBarrier = false;
      transitions.clear();
    }
  }
  // Swapchain images move from their acquire/present layout to home before first use.
  void PrepareTexture(WGPUTextureImpl* tex) {
    if (tex->swapchain) {
      touchesSwapchain = true;
      swapchainTex = tex;
    }
    if (tex->swapchain && tex->layout != tex->home) {
      transitions.push_back(
          LayoutTransition(tex->image, tex->aspects, 0, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, tex->home));
      tex->layout = tex->home;
    }
  }
  // Puts the subresources of the coming operation in `layout`; call Settle once all are named.
  void Use(WGPUTextureImpl* tex, uint32_t mip, uint32_t layer, uint32_t layerCount, VkImageLayout layout) {
    PrepareTexture(tex);
    for (uint32_t l = layer; l < layer + layerCount; ++l) {
      auto it = std::find_if(away.begin(), away.end(),
                             [&](const Away& a) { return a.tex == tex && a.mip == mip && a.layer == l; });
      const VkImageLayout current = it != away.end() ? it->layout : tex->home;
      if (current != layout) {
        transitions.push_back(LayoutTransition(tex->image, tex->aspects, mip, 1, l, 1, current, layout));
      }
      if (layout == tex->home) {
        if (it != away.end()) {
          away.erase(it);
        }
      } else if (it != away.end()) {
        it->layout = layout;
        it->keep = true;
      } else {
        away.push_back({tex, mip, l, layout, true});
      }
    }
  }
  // Every subresource not named by Use since the last Settle goes home, then the barrier is recorded.
  void Settle() {
    for (auto it = away.begin(); it != away.end();) {
      if (it->keep) {
        it->keep = false;
        ++it;
        continue;
      }
      transitions.push_back(
          LayoutTransition(it->tex->image, it->tex->aspects, it->mip, 1, it->layer, 1, it->layout, it->tex->home));
      it = away.erase(it);
    }
    Barrier();
  }
  ~WGPUCommandEncoderImpl() override {
    if (!finished) {
      vkEndCommandBuffer(slot.cb);
      RecycleSlot(dev, slot);
      for (Object* obj : refs) {
        obj->Release();
      }
    }
  }
};

namespace {
constexpr uint32_t kMaxGroups = 4;
constexpr uint32_t kMaxDynamic = 8;

struct BindState {
  VkDescriptorSet sets[kMaxGroups]{};
  VkDescriptorSetLayout boundSetLayouts[kMaxGroups]{};
  uint32_t dynamicCount[kMaxGroups]{};
  uint32_t dynamic[kMaxGroups][kMaxDynamic]{};
  uint32_t dirty = 0;
  VkPipelineLayout boundLayout = VK_NULL_HANDLE;

  void Set(uint32_t index, WGPUBindGroupImpl* group, size_t count, const uint32_t* offsets) {
    if (index >= kMaxGroups) {
      return;
    }
    sets[index] = group != nullptr ? group->set : VK_NULL_HANDLE;
    dynamicCount[index] = static_cast<uint32_t>(std::min<size_t>(count, kMaxDynamic));
    if (count > 0) {
      std::memcpy(dynamic[index], offsets, dynamicCount[index] * sizeof(uint32_t));
    }
    dirty |= 1u << index;
  }

  void Flush(VkCommandBuffer cb, VkPipelineBindPoint point, WGPUPipelineLayoutImpl* layout) {
    if (layout == nullptr) {
      return;
    }
    if (layout->layout != boundLayout) {
      boundLayout = layout->layout;
      const uint32_t groupCount = std::min<uint32_t>(static_cast<uint32_t>(layout->groups.size()), kMaxGroups);
      // Adapted from Dolphin StateTracker (GPL-2.0-or-later) / Vulkan pipeline layout compatibility:
      // shared prefix descriptor set layouts (sets 0 and 1 across GX pipelines) stay valid.
      for (uint32_t i = 0; i < groupCount; ++i) {
        const VkDescriptorSetLayout setLayout =
            layout->groups[i] != nullptr ? layout->groups[i]->layout : VK_NULL_HANDLE;
        if (setLayout != boundSetLayouts[i]) {
          for (uint32_t j = i; j < kMaxGroups; ++j) {
            boundSetLayouts[j] =
                (j < groupCount && layout->groups[j] != nullptr) ? layout->groups[j]->layout : VK_NULL_HANDLE;
          }
          dirty |= ((1u << (groupCount - i)) - 1u) << i;
          break;
        }
      }
    }
    const uint32_t groupCount = std::min<uint32_t>(static_cast<uint32_t>(layout->groups.size()), kMaxGroups);
    for (uint32_t i = 0; i < groupCount; ++i) {
      if ((dirty & (1u << i)) && sets[i] != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cb, point, layout->layout, i, 1, &sets[i], dynamicCount[i], dynamic[i]);
      }
    }
    dirty = 0;
  }
};
} // namespace

struct WGPURenderPassEncoderImpl : Object {
  WGPUCommandEncoderImpl* encoder = nullptr;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  WGPURenderPipelineImpl* pipeline = nullptr;
  BindState binds;
  SlotTiming::Pass timed{UINT32_MAX, 0, 0, 0};
  uint32_t lastStamp = UINT32_MAX;
  ~WGPURenderPassEncoderImpl() override { encoder->Release(); }
};

// AURORA_VK_DRAW_TIMING: the GPU time from the pass's previous timestamp to this draw's end.
void TimeDraw(WGPURenderPassEncoderImpl* pass, uint32_t elements, uint32_t instances) {
  if (!pass->encoder->dev->drawTiming || pass->timed.query == UINT32_MAX) {
    return;
  }
  // Two queries stay free: the pass's end and the command buffer's.
  const uint32_t after = WriteTimestamp(pass->encoder->slot, false, 2);
  if (after == UINT32_MAX) {
    return;
  }
  const uint32_t before = pass->lastStamp != UINT32_MAX ? pass->lastStamp : pass->timed.query;
  pass->encoder->slot.timing->draws.push_back(
      {before, after, elements, instances, pass->pipeline->id, pass->pipeline->blend});
  pass->lastStamp = after;
}

struct WGPUComputePassEncoderImpl : Object {
  WGPUCommandEncoderImpl* encoder = nullptr;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  WGPUComputePipelineImpl* pipeline = nullptr;
  BindState binds;
  ~WGPUComputePassEncoderImpl() override { encoder->Release(); }
};

struct WGPUQueueImpl : Object {
  WGPUDeviceImpl* dev = nullptr;
};

// ---------------------------------------------------------------------------------------------------
// Surface

struct WGPUSurfaceImpl : Object {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  WGPUDeviceImpl* dev = nullptr;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  WGPUTextureFormat wgpuFormat = WGPUTextureFormat_Undefined;
  uint32_t width = 0;
  uint32_t height = 0;
  // The swapchain leaves rotation to the compositor (see wgpuSurfaceConfigure), which Android
  // answers with VK_SUBOPTIMAL_KHR on every acquire; that alone must not read as a stale swapchain.
  bool rotatedByCompositor = false;
  // The last swapchain create failed with VK_ERROR_SURFACE_LOST_KHR: acquire reports Lost so aurora
  // recreates the surface rather than reconfiguring the dead one.
  bool surfaceLost = false;
  std::vector<WGPUTextureImpl*> images;
  std::vector<VkSemaphore> acquireSems;
  std::vector<uint64_t> acquireSemSerial;
  std::vector<VkSemaphore> presentSems;
  uint32_t nextAcquire = 0;
  uint32_t current = UINT32_MAX;

  void DestroySwapchain();
  ~WGPUSurfaceImpl() override {
    DestroySwapchain();
    if (surface != VK_NULL_HANDLE) {
      vkDestroySurfaceKHR(g_vkInstance, surface, nullptr);
    }
  }
};

void WGPUSurfaceImpl::DestroySwapchain() {
  if (swapchain == VK_NULL_HANDLE) {
    return;
  }
  {
    std::lock_guard lock(dev->queueMutex);
    vkQueueWaitIdle(dev->queue);
    dev->pendingAcquire = VK_NULL_HANDLE;
    dev->pendingAcquireSerial = nullptr;
    dev->pendingPresent = VK_NULL_HANDLE;
    dev->presentSignaled = false;
  }
  Retire(dev);
  for (auto* image : images) {
    if (image->defaultView != VK_NULL_HANDLE) {
      DestroyViewFramebuffers(dev, image->defaultView);
      vkDestroyImageView(dev->device, image->defaultView, nullptr);
      image->defaultView = VK_NULL_HANDLE;
    }
    image->image = VK_NULL_HANDLE;
    image->Release();
  }
  images.clear();
  for (VkSemaphore sem : acquireSems) {
    vkDestroySemaphore(dev->device, sem, nullptr);
  }
  for (VkSemaphore sem : presentSems) {
    vkDestroySemaphore(dev->device, sem, nullptr);
  }
  acquireSems.clear();
  acquireSemSerial.clear();
  presentSems.clear();
  vkDestroySwapchainKHR(dev->device, swapchain, nullptr);
  swapchain = VK_NULL_HANDLE;
  current = UINT32_MAX;
}

WGPUDeviceImpl::~WGPUDeviceImpl() {
  stopping.store(true);
  futureCv.notify_all();
  if (completionThread.joinable()) {
    completionThread.join();
  }
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
    Retire(this);
    if (pipelineCache != VK_NULL_HANDLE) {
      size_t size = 0;
      if (vkGetPipelineCacheData(device, pipelineCache, &size, nullptr) == VK_SUCCESS && size > 0) {
        std::vector<char> data(size);
        if (vkGetPipelineCacheData(device, pipelineCache, &size, data.data()) == VK_SUCCESS) {
          std::error_code ec;
          std::filesystem::create_directories(pipelineCachePath.parent_path(), ec);
          std::ofstream(pipelineCachePath, std::ios::binary).write(data.data(), static_cast<std::streamsize>(size));
        }
      }
      vkDestroyPipelineCache(device, pipelineCache, nullptr);
    }
    for (CmdSlot slot : freeSlots) {
      vkDestroyCommandPool(device, slot.pool, nullptr);
      if (slot.timing != nullptr) {
        vkDestroyQueryPool(device, slot.timing->pool, nullptr);
        delete slot.timing;
      }
    }
    for (StagingChunk* chunk : freeChunks) {
      vmaDestroyBuffer(vma, chunk->buffer, chunk->alloc);
      delete chunk;
    }
    for (VkDescriptorPool pool : descriptorPools) {
      vkDestroyDescriptorPool(device, pool, nullptr);
    }
    vkDestroySemaphore(device, timeline, nullptr);
    {
      std::lock_guard lock(fenceMutex);
      serialFences.clear();
    }
    for (const auto& [key, fb] : framebuffers) {
      vkDestroyFramebuffer(device, fb.fb, nullptr);
    }
    framebuffers.clear();
    for (const auto& [key, renderPass] : renderPasses) {
      vkDestroyRenderPass(device, renderPass, nullptr);
    }
    renderPasses.clear();
    for (const auto& [size, buffer, alloc, mapped] : freeBackings) {
      vmaDestroyBuffer(vma, buffer, alloc);
    }
    vmaDestroyAllocator(vma);
    vkDestroyDevice(device, nullptr);
  }
  if (g_device == this) {
    g_device = nullptr;
  }
}

// ---------------------------------------------------------------------------------------------------
// Shader compilation

namespace {

uint64_t HashBytes(const void* data, size_t size, uint64_t seed) {
  uint64_t h = 0xcbf29ce484222325ull ^ seed;
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    h = (h ^ bytes[i]) * 0x100000001b3ull;
  }
  return h;
}

// WGSL -> SPIR-V with each @group/@binding mapped to the same set/binding.
bool CompileWgsl(WGPUDeviceImpl* dev, const WGPUShaderModuleImpl* module, std::string_view entry,
                 const WGPUPipelineLayoutImpl* layout, std::vector<uint32_t>& out) {
  uint64_t key = HashBytes(module->wgsl.data(), module->wgsl.size(), 0);
  key = HashBytes(entry.data(), entry.size(), key);
  for (size_t g = 0; g < layout->groups.size(); ++g) {
    if (const auto* group = layout->groups[g]) {
      for (const auto& e : group->entries) {
        const uint32_t word = static_cast<uint32_t>(g) << 24 | e.binding << 4 | static_cast<uint32_t>(e.kind);
        key = HashBytes(&word, sizeof(word), key);
      }
    }
  }
  {
    std::lock_guard lock(dev->spirvMutex);
    if (auto it = dev->spirvCache.find(key); it != dev->spirvCache.end()) {
      out = it->second;
      return true;
    }
  }

  tint::Source::File file(module->label.empty() ? "shader.wgsl" : module->label, module->wgsl);
  // Dawn allows every WGSL language feature it implements (aurora uses unrestricted_pointer_parameters).
  tint::wgsl::reader::Options readerOptions;
  readerOptions.allowed_features = tint::wgsl::AllowedFeatures::Everything();
  auto ir = tint::wgsl::reader::WgslToIR(&file, readerOptions);
  if (ir != tint::Success) {
    ReportError(dev, "WGSL compile failed (" + module->label + "): " + ir.Failure().reason);
    return false;
  }
  tint::spirv::writer::Options options;
  options.entry_point_name = std::string{entry};
  options.disable_robustness = true;
  options.strip_all_names = true;
  options.extensions.use_storage_input_output_16 = false;
  options.extensions.use_demote_to_helper_invocation = dev->hasDemote;
  for (size_t g = 0; g < layout->groups.size(); ++g) {
    const auto* group = layout->groups[g];
    if (group == nullptr) {
      continue;
    }
    for (const auto& e : group->entries) {
      const tint::BindingPoint point{static_cast<uint32_t>(g), e.binding};
      switch (e.kind) {
      case BindKind::Uniform:
        options.bindings.uniform[point] = point;
        break;
      case BindKind::Storage:
      case BindKind::ReadOnlyStorage:
        options.bindings.storage[point] = point;
        break;
      case BindKind::Texture:
        options.bindings.texture[point] = point;
        break;
      case BindKind::Sampler:
        options.bindings.sampler[point] = point;
        break;
      case BindKind::StorageTexture:
        options.bindings.storage_texture[point] = point;
        break;
      }
    }
  }
  auto result = tint::spirv::writer::Generate(ir.Get(), options);
  if (result != tint::Success) {
    ReportError(dev, "SPIR-V generation failed (" + module->label + ", " + std::string{entry} +
                         "): " + result.Failure().reason);
    return false;
  }
  out = std::move(result->spirv);
  std::lock_guard lock(dev->spirvMutex);
  dev->spirvCache.emplace(key, out);
  return true;
}

VkShaderModule MakeModule(WGPUDeviceImpl* dev, const std::vector<uint32_t>& spirv) {
  const VkShaderModuleCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spirv.size() * sizeof(uint32_t),
      .pCode = spirv.data(),
  };
  VkShaderModule module;
  VK_CHECK(vkCreateShaderModule(dev->device, &info, nullptr, &module));
  return module;
}

void SavePipelineCache(WGPUDeviceImpl* dev) {
  if (dev == nullptr || dev->device == VK_NULL_HANDLE || dev->pipelineCache == VK_NULL_HANDLE) {
    return;
  }
  size_t size = 0;
  if (vkGetPipelineCacheData(dev->device, dev->pipelineCache, &size, nullptr) != VK_SUCCESS || size == 0) {
    return;
  }
  std::vector<char> data(size);
  if (vkGetPipelineCacheData(dev->device, dev->pipelineCache, &size, data.data()) != VK_SUCCESS) {
    return;
  }
  std::error_code ec;
  if (dev->pipelineCachePath.has_parent_path()) {
    std::filesystem::create_directories(dev->pipelineCachePath.parent_path(), ec);
  }
  const auto tmp = dev->pipelineCachePath.string() + ".tmp";
  std::ofstream(tmp, std::ios::binary).write(data.data(), static_cast<std::streamsize>(size));
  std::filesystem::rename(tmp, dev->pipelineCachePath, ec);
}

void NotePipelineCreated(WGPUDeviceImpl* dev) {
  // Persist the driver's pipeline cache now and then, not only at shutdown, so a crash keeps it.
  if (dev->pipelinesSinceSave.fetch_add(1) + 1 < 64) {
    return;
  }
  dev->pipelinesSinceSave.store(0);
  SavePipelineCache(dev);
}

std::filesystem::path PipelineCachePath() {
  if (const char* dir = std::getenv("AURORA_VK_CACHE_DIR"); dir != nullptr && *dir != 0) {
    return std::filesystem::path{dir} / "vk_pipeline_cache.bin";
  }
  if (const char* drv = std::getenv("BASARA_GPU_DRIVER"); drv != nullptr && *drv != 0) {
    const std::filesystem::path p{drv};
    if (p.has_parent_path()) {
      return p.parent_path() / "vk_pipeline_cache.bin";
    }
  }
  if (const char* dir = std::getenv("WIICOMPILED_RUNTIME_DIR"); dir != nullptr && *dir != 0) {
    return std::filesystem::path{dir} / "vk_pipeline_cache.bin";
  }
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != 0) {
    return std::filesystem::path{xdg} / "aurora-vk" / "vk_pipeline_cache.bin";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != 0) {
    return std::filesystem::path{home} / ".cache" / "aurora-vk" / "vk_pipeline_cache.bin";
  }
  return "vk_pipeline_cache.bin";
}

// ---------------------------------------------------------------------------------------------------
// Device creation

// Write-combined or cached memory for the MapWrite staging the GX conversion writes into, by measuring
// it here (about 10 ms): ARMSX2 found cached staging 20-30% faster on Adreno 610 but slower on Mali-G615
// and Adreno 650, a property of the CPU and its caches that no memory table shows. Three rounds of each,
// taken in turn so a clock still rising favours neither; cached only when its median is 10% faster.
// AURORA_VK_CACHED_STAGING=0/1 decides instead.
bool ChooseCachedStaging(WGPUDeviceImpl* dev) {
  // Forced cached (Upload Memory: Cached) still needs the memory type: without it every staging
  // allocation would fail, so the test buffers below decide and the timing is skipped.
  bool forced = false;
  if (const char* v = std::getenv("AURORA_VK_CACHED_STAGING"); v != nullptr && *v != 0) {
    if (*v != '1') {
      Log("staging memory: write-combined (AURORA_VK_CACHED_STAGING)");
      return false;
    }
    forced = true;
  }
  constexpr size_t kSize = 1u << 20;
  const VkBufferCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = kSize,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
  };
  VkBuffer buffers[2]{};
  VmaAllocation allocs[2]{};
  VmaAllocationInfo results[2]{};
  for (int i = 0; i < 2; ++i) {
    VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_AUTO};
    allocInfo.flags = (i == 0 ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                              : VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT) |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (i == 1) allocInfo.requiredFlags = kCachedStagingFlags;
    if (vmaCreateBuffer(dev->vma, &info, &allocInfo, &buffers[i], &allocs[i], &results[i]) != VK_SUCCESS ||
        results[i].pMappedData == nullptr) {
      for (int j = 0; j <= i; ++j) {
        if (buffers[j] != VK_NULL_HANDLE) vmaDestroyBuffer(dev->vma, buffers[j], allocs[j]);
      }
      Log("staging memory: write-combined (no cached, coherent memory)");
      return false;
    }
  }
  VkMemoryPropertyFlags wcFlags = 0;
  vmaGetMemoryTypeProperties(dev->vma, results[0].memoryType, &wcFlags);
  bool cached = false;
  if (forced) {
    Log("staging memory: cached (AURORA_VK_CACHED_STAGING)");
    cached = true;
  } else if (results[0].memoryType == results[1].memoryType || (wcFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0) {
    Log("staging memory: cached already (one memory type for both)");
  } else {
    int64_t wc[3], cc[3];
    auto* wcData = static_cast<uint8_t*>(results[0].pMappedData);
    auto* ccData = static_cast<uint8_t*>(results[1].pMappedData);
    TimeStagingWrites(wcData, kSize);  // first touch: page faults go here, not in the rounds
    TimeStagingWrites(ccData, kSize);
    for (int round = 0; round < 3; ++round) {
      if (round % 2 == 0) {
        wc[round] = TimeStagingWrites(wcData, kSize);
        cc[round] = TimeStagingWrites(ccData, kSize);
      } else {
        cc[round] = TimeStagingWrites(ccData, kSize);
        wc[round] = TimeStagingWrites(wcData, kSize);
      }
    }
    std::sort(wc, wc + 3);
    std::sort(cc, cc + 3);
    cached = cc[1] * 11 < wc[1] * 10;
    Log("staging memory: %s (writes a megabyte in %.0f us write-combined, %.0f us cached)",
        cached ? "cached" : "write-combined", wc[1] / 1000.0, cc[1] / 1000.0);
  }
  for (int i = 0; i < 2; ++i) vmaDestroyBuffer(dev->vma, buffers[i], allocs[i]);
  return cached;
}

// ---------------------------------------------------------------------------------------------------
// Tier 0 render passes (WGPUDeviceImpl::legacy)

// One attachment of a tier 0 render pass. A colour slot whose format is VK_FORMAT_UNDEFINED is unused.
struct LegacyAttachment {
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  VkAttachmentStoreOp store = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  VkAttachmentLoadOp stencilLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  VkAttachmentStoreOp stencilStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;
};

// The render pass for these attachments, made once and kept for the device's life. Each attachment
// starts, renders and ends in its own layout: the encoder's barriers make the transitions, as they do
// around dynamic rendering, so the pass changes no layout and needs only the implicit dependencies.
// `resolves` is empty or parallel to `colors`.
VkRenderPass LegacyRenderPass(WGPUDeviceImpl* dev, const std::vector<LegacyAttachment>& colors,
                              const std::vector<LegacyAttachment>& resolves, const LegacyAttachment& depth) {
  std::string key;
  const auto put = [&key](uint32_t value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
  const auto putAttachment = [&put](const LegacyAttachment& a) {
    put(static_cast<uint32_t>(a.format));
    put(static_cast<uint32_t>(a.samples));
    put(static_cast<uint32_t>(a.load));
    put(static_cast<uint32_t>(a.store));
    put(static_cast<uint32_t>(a.stencilLoad));
    put(static_cast<uint32_t>(a.stencilStore));
    put(static_cast<uint32_t>(a.layout));
  };
  put(static_cast<uint32_t>(colors.size()));
  for (const LegacyAttachment& a : colors) {
    putAttachment(a);
  }
  put(static_cast<uint32_t>(resolves.size()));
  for (const LegacyAttachment& a : resolves) {
    putAttachment(a);
  }
  putAttachment(depth);

  std::lock_guard lock(dev->renderPassMutex);
  if (const auto it = dev->renderPasses.find(key); it != dev->renderPasses.end()) {
    return it->second;
  }
  std::vector<VkAttachmentDescription> descs;
  descs.reserve(colors.size() * 2 + 1);
  const auto add = [&descs](const LegacyAttachment& a) {
    descs.push_back(VkAttachmentDescription{
        .format = a.format,
        .samples = a.samples,
        .loadOp = a.load,
        .storeOp = a.store,
        .stencilLoadOp = a.stencilLoad,
        .stencilStoreOp = a.stencilStore,
        .initialLayout = a.layout,
        .finalLayout = a.layout,
    });
    return VkAttachmentReference{static_cast<uint32_t>(descs.size() - 1), a.layout};
  };
  constexpr VkAttachmentReference kUnused{VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_GENERAL};
  std::vector<VkAttachmentReference> colorRefs(colors.size(), kUnused);
  std::vector<VkAttachmentReference> resolveRefs(colors.size(), kUnused);
  bool anyResolve = false;
  for (size_t i = 0; i < colors.size(); ++i) {
    if (colors[i].format != VK_FORMAT_UNDEFINED) {
      colorRefs[i] = add(colors[i]);
    }
  }
  for (size_t i = 0; i < resolves.size() && i < colors.size(); ++i) {
    if (resolves[i].format != VK_FORMAT_UNDEFINED) {
      resolveRefs[i] = add(resolves[i]);
      anyResolve = true;
    }
  }
  VkAttachmentReference depthRef = kUnused;
  const bool hasDepth = depth.format != VK_FORMAT_UNDEFINED;
  if (hasDepth) {
    depthRef = add(depth);
  }
  const VkSubpassDescription subpass{
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = static_cast<uint32_t>(colorRefs.size()),
      .pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data(),
      .pResolveAttachments = anyResolve ? resolveRefs.data() : nullptr,
      .pDepthStencilAttachment = hasDepth ? &depthRef : nullptr,
  };
  const VkRenderPassCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(descs.size()),
      .pAttachments = descs.empty() ? nullptr : descs.data(),
      .subpassCount = 1,
      .pSubpasses = &subpass,
  };
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VK_CHECK(vkCreateRenderPass(dev->device, &info, nullptr, &renderPass));
  dev->renderPasses.emplace(std::move(key), renderPass);
  return renderPass;
}

// The render pass a tier 0 pipeline is created against. Render pass compatibility looks only at the
// attachments' formats and sample counts, so this one serves every pass that draws with the pipeline,
// whatever its load and store operations and layouts.
VkRenderPass LegacyCompatiblePass(WGPUDeviceImpl* dev, const std::vector<VkFormat>& colorFormats, VkFormat depthFormat,
                                  VkSampleCountFlagBits samples) {
  std::vector<LegacyAttachment> colors(colorFormats.size());
  for (size_t i = 0; i < colorFormats.size(); ++i) {
    colors[i].format = colorFormats[i];
    colors[i].samples = samples;
    colors[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  LegacyAttachment depth;
  depth.format = depthFormat;
  depth.samples = samples;
  depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  if (dev->clearByAttachments) {
    // PowerVR Rogue: the pipelines' pass has the same load/store ops as the passes they draw in.
    for (LegacyAttachment& c : colors) {
      c.load = VK_ATTACHMENT_LOAD_OP_LOAD;
      c.store = VK_ATTACHMENT_STORE_OP_STORE;
    }
    depth.load = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.store = VK_ATTACHMENT_STORE_OP_STORE;
  }
  return LegacyRenderPass(dev, colors, {}, depth);
}

// A tier 0 pass's framebuffer, cached on the device and evicted when any of its views is destroyed.
VkFramebuffer GetCachedFramebuffer(WGPUDeviceImpl* dev, VkRenderPass renderPass, const std::vector<VkImageView>& views,
                                   uint32_t width, uint32_t height) {
  std::string key;
  key.reserve(sizeof(renderPass) + sizeof(width) + sizeof(height) + views.size() * sizeof(VkImageView));
  const auto put = [&key](const auto& v) { key.append(reinterpret_cast<const char*>(&v), sizeof(v)); };
  put(renderPass);
  put(width);
  put(height);
  for (VkImageView v : views) {
    put(v);
  }
  std::lock_guard lock(dev->framebufferMutex);
  if (const auto it = dev->framebuffers.find(key); it != dev->framebuffers.end()) {
    return it->second.fb;
  }
  const VkFramebufferCreateInfo fbInfo{
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = renderPass,
      .attachmentCount = static_cast<uint32_t>(views.size()),
      .pAttachments = views.empty() ? nullptr : views.data(),
      .width = width,
      .height = height,
      .layers = 1,
  };
  VkFramebuffer fb = VK_NULL_HANDLE;
  VK_CHECK(vkCreateFramebuffer(dev->device, &fbInfo, nullptr, &fb));
  dev->framebuffers.emplace(std::move(key), WGPUDeviceImpl::CachedFramebuffer{fb, views});
  return fb;
}

} // namespace

void DestroyViewFramebuffers(WGPUDeviceImpl* dev, VkImageView view) {
  if (dev == nullptr || view == VK_NULL_HANDLE || !dev->legacy) {
    return;
  }
  std::lock_guard lock(dev->framebufferMutex);
  for (auto it = dev->framebuffers.begin(); it != dev->framebuffers.end();) {
    const auto& views = it->second.views;
    if (std::find(views.begin(), views.end(), view) != views.end()) {
      vkDestroyFramebuffer(dev->device, it->second.fb, nullptr);
      it = dev->framebuffers.erase(it);
    } else {
      ++it;
    }
  }
}

namespace {

WGPUDeviceImpl* CreateDevice() {
  auto* dev = new WGPUDeviceImpl;
  dev->instance = g_vkInstance;

  uint32_t count = 0;
  vkEnumeratePhysicalDevices(g_vkInstance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(g_vkInstance, &count, devices.data());
  const auto deviceExtensions = [](VkPhysicalDevice phys) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> list(n);
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, list.data());
    return list;
  };
  const auto has = [](const std::vector<VkExtensionProperties>& list, const char* name) {
    return std::any_of(list.begin(), list.end(),
                       [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
  };
  // Below 1.3 these extensions stand in for the 1.3 core features the backend uses; dynamic
  // rendering also needs depth_stencil_resolve and create_renderpass2 on 1.1. Synchronization2 is
  // taken when present and otherwise emulated (LoadDeviceFunctions).
  constexpr const char* kTier1Extensions[] = {
      VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,     VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
      VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
  };
  // Prefer Vulkan 1.3, then a discrete GPU, then integrated, among devices with 1.3 or 1.1 plus the
  // extensions above.
  int bestScore = -1;
  std::vector<VkExtensionProperties> available;
#if defined(__ANDROID__)
  constexpr bool kAllowTier1 = true;
#else
  constexpr bool kAllowTier1 = false;
#endif
  // AURORA_VK_FORCE_1_1 runs a 1.3 device through the 1.1 path too, so drivers compare on the same path.
  const bool forceTier1 = kAllowTier1 && [] {
    const char* v = std::getenv("AURORA_VK_FORCE_1_1");
    return v != nullptr && v[0] != '\0' && v[0] != '0';
  }();
  // Tier 0: Vulkan 1.1 devices without the tier 1 extensions, plus drivers whose dynamic rendering is
  // broken (ARMSX2 GSGPUDriverProfile.cpp: Qualcomm proprietary < 512.801, ARM Mali proprietary < r52,
  // and PowerVR). It renders through VkRenderPass + cached VkFramebuffer and tracks submissions with
  // fences when timeline semaphores are unavailable.
#if defined(__ANDROID__)
  constexpr bool allowTier0 = true;
#else
  constexpr bool allowTier0 = false;
#endif
  // AURORA_VK_FORCE_TIER0=1 runs the chosen device through tier 0 too (Android), to compare.
  const bool forceTier0 = kAllowTier1 && [] {
    const char* v = std::getenv("AURORA_VK_FORCE_TIER0");
    return v != nullptr && v[0] != '\0' && v[0] != '0';
  }();
  // Adapted from ARMSX2/PCSX2 GSGPUDriverProfile.cpp (GPL-3.0): detect drivers with broken dynamic rendering.
  const auto hasBrokenDynamicRendering = [](const VkPhysicalDeviceProperties& p) {
    const std::string_view devName(p.deviceName);
    // PowerVR (Imagination, vendor 0x1010).
    if (p.vendorID == 0x1010) {
      return true;
    }
    // Qualcomm proprietary driver before 512.801. A custom driver loaded through adrenotools is Mesa
    // Turnip under any name ("Turnip Adreno", "PurpleVK-public Adreno"); the stock one names itself
    // "Adreno (TM) ...". Treating Turnip as stock put it on tier 0, whose fence polling cost the frame
    // worker half its time on a Redmi (Adreno 618, PurpleVK T30, 2026-11-08).
    if (p.vendorID == 0x5143 && !UsingCustomDriver() && devName.find("Turnip") == std::string_view::npos &&
        devName.rfind("Adreno", 0) == 0) {
      const uint32_t drv = p.driverVersion;
      if ((drv & 0x80000000u) == 0 || VK_API_VERSION_MAJOR(drv) < 512 ||
          (VK_API_VERSION_MAJOR(drv) == 512 && VK_API_VERSION_MINOR(drv) < 801)) {
        return true;
      }
    }
    // ARM Mali proprietary driver before r52 (PanVK/Panfrost and custom malisx2 wrapper are excluded).
    if (p.vendorID == 0x13B5 && devName.find("PanVK") == std::string_view::npos &&
        devName.find("Panfrost") == std::string_view::npos && !UsingCustomDriver()) {
      const uint32_t major = VK_API_VERSION_MAJOR(p.driverVersion);
      if (major < 52 || major > 100 || VK_API_VERSION_PATCH(p.driverVersion) != 0) {
        return true;
      }
    }
    return false;
  };
  bool chosenTier0 = false;
  for (VkPhysicalDevice candidate : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(candidate, &props);
    auto list = deviceExtensions(candidate);
    const bool brokenDynRender = allowTier0 && hasBrokenDynamicRendering(props);
    // Only Android loads the KHR entry points (LoadDeviceFunctions); elsewhere calls bind to the
    // loader's core names, so a device must have 1.3.
    const bool api13 = props.apiVersion >= VK_API_VERSION_1_3 && !forceTier1 && !brokenDynRender;
    bool tier0 = false;
    if (!api13) {
      std::string missing;
      for (const char* name : kTier1Extensions) {
        if (!has(list, name)) {
          missing += missing.empty() ? name : std::string{", "} + name;
        }
      }
      if (!kAllowTier1 || props.apiVersion < VK_API_VERSION_1_1 || !missing.empty() || brokenDynRender) {
        tier0 = allowTier0 && props.apiVersion >= VK_API_VERSION_1_1;
        if (!tier0) {
          Log("skipping %s (Vulkan %u.%u): missing %s", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
              VK_API_VERSION_MINOR(props.apiVersion), missing.empty() ? "Vulkan 1.3" : missing.c_str());
          continue;
        }
        Log("%s (Vulkan %u.%u) %s: tier 0 (render passes, fences)", props.deviceName,
            VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
            brokenDynRender && missing.empty() ? "driver rule (broken dynamic rendering)"
                                               : ("has no " + missing).c_str());
      }
    }
    // Vulkan 1.3 first, then tier 1, then tier 0; within each, discrete before integrated.
    const int score = (api13 ? 8 : tier0 ? 0 : 4) + (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                                                     : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                                                   : 1);
    if (score > bestScore) {
      bestScore = score;
      dev->phys = candidate;
      dev->props = props;
      dev->api13 = api13;
      chosenTier0 = tier0;
      available = std::move(list);
    }
  }
  if (dev->phys == VK_NULL_HANDLE) {
    Log("no Vulkan 1.3 device, nor a 1.1 device with dynamic rendering and timeline semaphores");
    delete dev;
    return nullptr;
  }
  // A forced tier 0 keeps the device's features and extensions; only its passes and submissions change.
  dev->legacy = chosenTier0 || forceTier0;

  uint32_t familyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(dev->phys, &familyCount, nullptr);
  std::vector<VkQueueFamilyProperties> families(familyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(dev->phys, &familyCount, families.data());
  for (uint32_t i = 0; i < familyCount; ++i) {
    if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
        (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
      dev->queueFamily = i;
      const uint32_t bits = families[i].timestampValidBits;
      dev->timestampMask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
      const char* timing = std::getenv("AURORA_VK_GPU_TIMING");
      dev->gpuTiming = bits > 0 && dev->props.limits.timestampPeriod > 0 && (timing == nullptr || timing[0] != '0');
      const char* drawTiming = std::getenv("AURORA_VK_DRAW_TIMING");
      dev->drawTiming = dev->gpuTiming && drawTiming != nullptr && drawTiming[0] == '1';
      break;
    }
  }

  std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  const auto enableIfPresent = [&](const char* name) {
    const bool present = has(available, name);
    if (present) {
      extensions.push_back(name);
    }
    return present;
  };

  VkPhysicalDeviceVulkan13Features have13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceVulkan12Features have12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                          .pNext = &have13};
  VkPhysicalDeviceVulkan13Features want13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceVulkan12Features want12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                          .pNext = &want13};
  // The per-extension feature structs, used instead of the 1.2/1.3 ones below 1.3.
  VkPhysicalDeviceDynamicRenderingFeatures haveRendering{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
  VkPhysicalDeviceSynchronization2Features haveSync2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
                                                     .pNext = &haveRendering};
  VkPhysicalDeviceTimelineSemaphoreFeatures haveTimeline{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, .pNext = &haveSync2};
  VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures haveDemote{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES, .pNext = &haveTimeline};
  VkPhysicalDeviceMaintenance4Features haveMaint4{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES,
                                                  .pNext = &haveDemote};
  VkPhysicalDeviceVulkanMemoryModelFeatures haveMemoryModel{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES, .pNext = &haveMaint4};
  void* haveChain = dev->api13 ? static_cast<void*>(&have12) : &haveMemoryModel;
  if (chosenTier0) {
    // An Android 10 driver is asked only about the extensions it has.
    haveChain = nullptr;
    const auto link = [&](auto& features, const char* extension) {
      if (has(available, extension)) {
        features.pNext = haveChain;
        haveChain = &features;
      }
    };
    link(haveSync2, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    link(haveDemote, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
    link(haveMaint4, VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
    link(haveMemoryModel, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
  }
  VkPhysicalDeviceFeatures2 have{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = haveChain};
  vkGetPhysicalDeviceFeatures2(dev->phys, &have);

  VkPhysicalDeviceDynamicRenderingFeatures wantRendering{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, .dynamicRendering = VK_TRUE};
  VkPhysicalDeviceSynchronization2Features wantSync2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
                                                     .pNext = &wantRendering,
                                                     .synchronization2 = VK_TRUE};
  VkPhysicalDeviceTimelineSemaphoreFeatures wantTimeline{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, .pNext = &wantRendering,
      .timelineSemaphore = VK_TRUE};
  void* wantChain = &wantTimeline;
  VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures wantDemote{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES};
  VkPhysicalDeviceMaintenance4Features wantMaint4{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES};
  VkPhysicalDeviceVulkanMemoryModelFeatures wantMemoryModel{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES};
  if (dev->api13) {
    want13.dynamicRendering = VK_TRUE;
    want13.synchronization2 = VK_TRUE;
    want13.shaderDemoteToHelperInvocation = have13.shaderDemoteToHelperInvocation;
    want13.maintenance4 = have13.maintenance4;
    want12.timelineSemaphore = VK_TRUE;
    want12.vulkanMemoryModel = have12.vulkanMemoryModel;
    wantChain = &want12;
    dev->hasSync2 = true;
    dev->hasStoreOpNone = true;
    dev->hasDemote = have13.shaderDemoteToHelperInvocation;
  } else if (chosenTier0) {
    // None of the tier 1 extensions: no dynamic rendering or timeline semaphore features to ask for.
    wantChain = nullptr;
  } else {
    for (const char* name : kTier1Extensions) {
      extensions.push_back(name);
    }
  }
  if (!dev->api13) {
    // AURORA_VK_NO_SYNC2 emulates synchronization2 on a driver that has it, to compare with malisx2.
    if (haveSync2.synchronization2 && std::getenv("AURORA_VK_NO_SYNC2") == nullptr &&
        enableIfPresent(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
      wantSync2.pNext = wantChain;
      wantChain = &wantSync2;
      dev->hasSync2 = true;
    }
    dev->hasStoreOpNone = enableIfPresent("VK_KHR_load_store_op_none") ||
                          enableIfPresent(VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME);
    if (haveDemote.shaderDemoteToHelperInvocation &&
        enableIfPresent(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME)) {
      wantDemote.shaderDemoteToHelperInvocation = VK_TRUE;
      wantDemote.pNext = wantChain;
      wantChain = &wantDemote;
      dev->hasDemote = true;
    }
    if (haveMaint4.maintenance4 && enableIfPresent(VK_KHR_MAINTENANCE_4_EXTENSION_NAME)) {
      wantMaint4.maintenance4 = VK_TRUE;
      wantMaint4.pNext = wantChain;
      wantChain = &wantMaint4;
    }
    if (haveMemoryModel.vulkanMemoryModel && enableIfPresent(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME)) {
      wantMemoryModel.vulkanMemoryModel = VK_TRUE;
      wantMemoryModel.pNext = wantChain;
      wantChain = &wantMemoryModel;
    }
  }
  dev->hasMemoryBudget = enableIfPresent(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
  VkPhysicalDeviceFeatures2 want{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = wantChain};
  want.features.textureCompressionBC = have.features.textureCompressionBC;
  want.features.samplerAnisotropy = have.features.samplerAnisotropy;
  want.features.depthClamp = have.features.depthClamp;
  want.features.independentBlend = have.features.independentBlend;
  want.features.dualSrcBlend = have.features.dualSrcBlend;
  want.features.fragmentStoresAndAtomics = have.features.fragmentStoresAndAtomics;
  want.features.vertexPipelineStoresAndAtomics = have.features.vertexPipelineStoresAndAtomics;
  want.features.shaderClipDistance = have.features.shaderClipDistance;
  dev->hasBC = have.features.textureCompressionBC;
  dev->hasAnisotropy = have.features.samplerAnisotropy;
  dev->hasDepthClamp = have.features.depthClamp;

  const float priority = 1.0f;
  const VkDeviceQueueCreateInfo queueInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = dev->queueFamily,
      .queueCount = 1,
      .pQueuePriorities = &priority,
  };
  const VkDeviceCreateInfo deviceInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &want,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queueInfo,
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };
  VK_CHECK(vkCreateDevice(dev->phys, &deviceInfo, nullptr, &dev->device));
  vkGetDeviceQueue(dev->device, dev->queueFamily, 0, &dev->queue);
#if defined(__ANDROID__)
  LoadDeviceFunctions(dev->device, dev->api13, dev->hasSync2);
#endif

  // Only read when VMA loads its functions dynamically (Android, vma_impl.cpp).
  const VmaVulkanFunctions vmaFunctions{
      .vkGetInstanceProcAddr = AURORA_GET_INSTANCE_PROC_ADDR,
      .vkGetDeviceProcAddr = AURORA_GET_DEVICE_PROC_ADDR,
  };
  // VMA reserves 256 MiB blocks on a large heap, and on a phone every heap is the shared RAM. Tier 0
  // devices (Android 10, 2-4 GB) get 32 MiB blocks so a texture burst does not claim a whole new
  // 256 MiB block; AURORA_VK_BLOCK_MB overrides it on any device (0 = VMA default).
  uint64_t blockBytes = dev->legacy ? 32ull << 20 : 0;
  if (const char* v = std::getenv("AURORA_VK_BLOCK_MB"); v != nullptr && *v != '\0') {
    blockBytes = std::strtoull(v, nullptr, 10) << 20;
  }
  const VmaAllocatorCreateInfo vmaInfo{
      .flags = dev->hasMemoryBudget ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0u,
      .physicalDevice = dev->phys,
      .device = dev->device,
      .preferredLargeHeapBlockSize = blockBytes,
      .pVulkanFunctions = &vmaFunctions,
      .instance = g_vkInstance,
      .vulkanApiVersion = dev->api13 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1,
  };
  if (blockBytes != 0 || dev->hasMemoryBudget) {
    Log("memory: VMA block %llu MiB%s", static_cast<unsigned long long>(blockBytes >> 20),
        dev->hasMemoryBudget ? ", VK_EXT_memory_budget" : "");
  }
  VK_CHECK(vmaCreateAllocator(&vmaInfo, &dev->vma));

  if (!dev->legacy) {
    const VkSemaphoreTypeCreateInfo timelineType{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    const VkSemaphoreCreateInfo semInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineType};
    VK_CHECK(vkCreateSemaphore(dev->device, &semInfo, nullptr, &dev->timeline));
  }
#if defined(__ANDROID__)
  g_chainTimelineInfo = !dev->legacy;
#endif

  dev->pipelineCachePath = PipelineCachePath();
  std::vector<char> cacheData;
  if (std::ifstream in{dev->pipelineCachePath, std::ios::binary | std::ios::ate}) {
    cacheData.resize(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    in.read(cacheData.data(), static_cast<std::streamsize>(cacheData.size()));
  }
  // The driver validates the header (vendor, device, cache UUID) and ignores a stale cache.
  const VkPipelineCacheCreateInfo cacheInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
      .initialDataSize = cacheData.size(),
      .pInitialData = cacheData.empty() ? nullptr : cacheData.data(),
  };
  if (vkCreatePipelineCache(dev->device, &cacheInfo, nullptr, &dev->pipelineCache) != VK_SUCCESS) {
    const VkPipelineCacheCreateInfo emptyInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    VK_CHECK(vkCreatePipelineCache(dev->device, &emptyInfo, nullptr, &dev->pipelineCache));
  }

  {
    const std::string name = dev->props.deviceName;
    bool rename = UsingCustomDriver() && (name.find("Mali-G52") != std::string::npos ||
                                          name.find("Mali-G76") != std::string::npos);
    // Adreno 5xx's Vulkan driver reads an indexed draw's indices on the CPU as well (a Redmi Note 7
    // faulted in vkCmdDrawIndexed under Dawn), so a tier 0 Adreno 5xx gets the same CPU-written indices.
    const bool adreno5xx = dev->legacy && name.find("Adreno (TM) 5") != std::string::npos;
    rename = rename || adreno5xx;
    if (const char* v = std::getenv("AURORA_VK_INDEX_RENAME"); v != nullptr && *v != 0) {
      rename = *v == '1';
    }
    dev->renameIndexCopies = rename;
    if (rename) {
      Log("index buffers are written by the CPU (%s)", adreno5xx ? "tier 0 Adreno 5xx" : "job-manager Mali driver");
    }
  }

  dev->cachedStaging = ChooseCachedStaging(dev);

  tint::Initialize();
  dev->completionThread = std::thread(CompletionThread, dev);
  Log("device: %s (Vulkan %u.%u.%u%s), pipeline cache %zu bytes", dev->props.deviceName,
      VK_API_VERSION_MAJOR(dev->props.apiVersion), VK_API_VERSION_MINOR(dev->props.apiVersion),
      VK_API_VERSION_PATCH(dev->props.apiVersion), dev->api13 ? "" : dev->hasSync2 ? ", 1.3 features from extensions" : ", 1.3 features from extensions, synchronization2 emulated",
      cacheData.size());
  if (dev->legacy) {
    Log("tier 0: render passes and fences%s", chosenTier0 ? "" : " (forced, AURORA_VK_FORCE_TIER0)");
    // Dolphin DriverDetails BUG_BROKEN_CLEAR_LOADOP_RENDERPASS (ImgTec): the PowerVR driver breaks
    // when one pass's load ops differ from another's on the same attachments. An OPPO CPH2209
    // (PowerVR Rogue GM9446, driver 1.13) showed only the clear colour on Vulkan. Rogue gets every
    // pass with LOAD and clears through vkCmdClearAttachments; the B-Series rendered fine without.
    // AURORA_VK_CLEAR_ATTACHMENTS=1/0 forces it on/off on any tier 0 device.
    const std::string_view name(dev->props.deviceName);
    bool want = dev->props.vendorID == 0x1010 && name.find("Rogue") != std::string_view::npos;
    if (const char* v = std::getenv("AURORA_VK_CLEAR_ATTACHMENTS"); v != nullptr && v[0] != '\0') {
      want = v[0] == '1';
    }
    dev->clearByAttachments = want;
    if (want) {
      Log("tier 0: passes load and clear with vkCmdClearAttachments (PowerVR clear load op bug)");
    }
  }
  g_device = dev;
  return dev;
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// C API

namespace {
void CreateVkInstance();
} // namespace

extern "C" {

// The adapter reports BC for every device (it has no physical device yet); this is the device's own
// textureCompressionBC, so BC replacement textures are refused on GPUs without it (most Mali, Qualcomm's
// driver) instead of failing at image creation.
bool aurora_vk_has_bc() { return g_device != nullptr && g_device->hasBC; }

void aurora_vk_save_pipeline_cache() {
  if (g_device != nullptr) {
    SavePipelineCache(g_device);
  }
}

WGPUInstance wgpuCreateInstance(const WGPUInstanceDescriptor*) {
#if defined(__ANDROID__)
  // Once with the custom driver if one is named, then with the system driver if that fails.
  for (bool allowCustom : {true, false}) {
    if (g_vkInstance != VK_NULL_HANDLE) {
      break;
    }
    SelectVulkanImplementation(allowCustom);
    if (!allowCustom || UsingCustomDriver()) {
      CreateVkInstance();
    }
    if (g_vkInstance != VK_NULL_HANDLE) {
      LoadInstanceFunctions(g_vkInstance);
    } else if (allowCustom && UsingCustomDriver()) {
      Log("the custom Vulkan driver could not create an instance; using the system driver");
    }
  }
  if (g_vkInstance == VK_NULL_HANDLE) {
    return nullptr;
  }
#else
  CreateVkInstance();
  if (g_vkInstance == VK_NULL_HANDLE) {
    return nullptr;
  }
#endif
  if (g_instanceObj == nullptr) {
    g_instanceObj = new WGPUInstanceImpl;
  } else {
    g_instanceObj->AddRef();
  }
  return g_instanceObj;
}

namespace {
void CreateVkInstance() {
  if (g_vkInstance == VK_NULL_HANDLE) {
    uint32_t available = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &available, nullptr);
    std::vector<VkExtensionProperties> props(available);
    vkEnumerateInstanceExtensionProperties(nullptr, &available, props.data());
    const auto has = [&](const char* name) {
      return std::any_of(props.begin(), props.end(),
                         [&](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
    };
    std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME};
    for (const char* name : {VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#if defined(__ANDROID__)
                             VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#endif
         }) {
      if (has(name)) {
        extensions.push_back(name);
      }
    }
    std::vector<const char*> layers;
    if (const char* debug = std::getenv("AURORA_VK_VALIDATION"); debug != nullptr && *debug == '1') {
      layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    const VkApplicationInfo app{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "aurora",
        .pEngineName = "aurora-vk",
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (vkCreateInstance(&info, nullptr, &g_vkInstance) != VK_SUCCESS) {
      Log("vkCreateInstance failed");
      g_vkInstance = VK_NULL_HANDLE;
    }
  }
}
} // namespace

void wgpuInstanceRelease(WGPUInstance instance) {
  if (instance != nullptr && instance->refs.load() == 1) {
    g_instanceObj = nullptr;
  }
  instance->Release();
}

WGPUSurface wgpuInstanceCreateSurface(WGPUInstance, const WGPUSurfaceDescriptor* descriptor) {
  auto* surface = new WGPUSurfaceImpl;
  if (const auto* wl = reinterpret_cast<const WGPUSurfaceSourceWaylandSurface*>(
          FindChain(descriptor->nextInChain, WGPUSType_SurfaceSourceWaylandSurface))) {
    const VkWaylandSurfaceCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = static_cast<wl_display*>(wl->display),
        .surface = static_cast<wl_surface*>(wl->surface),
    };
    const auto create = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
        AURORA_GET_INSTANCE_PROC_ADDR(g_vkInstance, "vkCreateWaylandSurfaceKHR"));
    if (create == nullptr || create(g_vkInstance, &info, nullptr, &surface->surface) != VK_SUCCESS) {
      Log("vkCreateWaylandSurfaceKHR failed");
    }
  } else if (const auto* x = reinterpret_cast<const WGPUSurfaceSourceXlibWindow*>(
                 FindChain(descriptor->nextInChain, WGPUSType_SurfaceSourceXlibWindow))) {
    const VkXlibSurfaceCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = static_cast<Display*>(x->display),
        .window = static_cast<Window>(x->window),
    };
    const auto create =
        reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(AURORA_GET_INSTANCE_PROC_ADDR(g_vkInstance, "vkCreateXlibSurfaceKHR"));
    if (create == nullptr || create(g_vkInstance, &info, nullptr, &surface->surface) != VK_SUCCESS) {
      Log("vkCreateXlibSurfaceKHR failed");
    }
#if defined(__ANDROID__)
  } else if (const auto* android = reinterpret_cast<const WGPUSurfaceSourceAndroidNativeWindow*>(
                 FindChain(descriptor->nextInChain, WGPUSType_SurfaceSourceAndroidNativeWindow))) {
    const VkAndroidSurfaceCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = static_cast<ANativeWindow*>(android->window),
    };
    const auto create = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        AURORA_GET_INSTANCE_PROC_ADDR(g_vkInstance, "vkCreateAndroidSurfaceKHR"));
    if (create == nullptr || create(g_vkInstance, &info, nullptr, &surface->surface) != VK_SUCCESS) {
      Log("vkCreateAndroidSurfaceKHR failed");
    }
#endif
  }
  if (surface->surface == VK_NULL_HANDLE) {
    delete surface;
    return nullptr;
  }
  return surface;
}

WGPUFuture wgpuInstanceRequestAdapter(WGPUInstance, const WGPURequestAdapterOptions*,
                                      WGPURequestAdapterCallbackInfo callbackInfo) {
  // There is no device yet to own the future, so the callback runs at once whatever the mode.
  auto* adapter = new WGPUAdapterImpl;
  callbackInfo.callback(WGPURequestAdapterStatus_Success, adapter, MakeView(""), callbackInfo.userdata1,
                        callbackInfo.userdata2);
  return WGPUFuture{0};
}

WGPUWaitStatus wgpuInstanceWaitAny(WGPUInstance, size_t futureCount, WGPUFutureWaitInfo* futures,
                                   uint64_t timeoutNS) {
  WGPUDeviceImpl* dev = g_device;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeoutNS);
  while (true) {
    bool any = false;
    uint64_t minSerial = UINT64_MAX;
    for (size_t i = 0; i < futureCount; ++i) {
      if (futures[i].future.id == 0) {
        futures[i].completed = true;
        any = true;
        continue;
      }
      if (dev == nullptr) {
        continue;
      }
      const uint64_t id = futures[i].future.id;
      Retire(dev);
      const size_t fired = FireReady(dev, [id](const FutureRec& f) { return f.id == id; });
      bool pending = false;
      {
        std::lock_guard lock(dev->futureMutex);
        for (const auto& f : dev->futures) {
          if (f.id == id) {
            pending = true;
            minSerial = std::min(minSerial, f.serial);
          }
        }
      }
      if (fired > 0 || !pending) {
        futures[i].completed = true;
        any = true;
      }
    }
    if (any) {
      return WGPUWaitStatus_Success;
    }
    const auto now = std::chrono::steady_clock::now();
    if (timeoutNS == 0 || now >= deadline || dev == nullptr || minSerial == UINT64_MAX) {
      return WGPUWaitStatus_TimedOut;
    }
    const uint64_t remaining =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count());
    WaitSerial(dev, minSerial, std::min<uint64_t>(remaining, 5'000'000));
  }
}

void wgpuInstanceProcessEvents(WGPUInstance) {
  if (WGPUDeviceImpl* dev = g_device) {
    Retire(dev, false);
    FireReady(dev, [](const FutureRec& f) { return f.mode != WGPUCallbackMode_WaitAnyOnly; });
  }
}

void wgpuAdapterRelease(WGPUAdapter adapter) { adapter->Release(); }

WGPUStatus wgpuAdapterGetInfo(WGPUAdapter, WGPUAdapterInfo* info) {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(g_vkInstance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(g_vkInstance, &count, devices.data());
  VkPhysicalDeviceProperties props{};
  if (!devices.empty()) {
    vkGetPhysicalDeviceProperties(devices.front(), &props);
    for (VkPhysicalDevice d : devices) {
      VkPhysicalDeviceProperties p;
      vkGetPhysicalDeviceProperties(d, &p);
      if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        props = p;
        break;
      }
    }
  }
  const std::string driver = std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) + "." +
                             std::to_string(VK_API_VERSION_MINOR(props.apiVersion)) + "." +
                             std::to_string(VK_API_VERSION_PATCH(props.apiVersion)) + " (native Vulkan)";
  const char* vendor = DupString("");
  const char* arch = DupString("");
  const char* device = DupString(props.deviceName);
  const char* description = DupString(driver);
  info->vendor = MakeView(vendor);
  info->architecture = MakeView(arch);
  info->device = MakeView(device);
  info->description = MakeView(description);
  info->backendType = WGPUBackendType_Vulkan;
  info->adapterType = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? WGPUAdapterType_DiscreteGPU
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? WGPUAdapterType_IntegratedGPU
                                                                                    : WGPUAdapterType_Unknown;
  info->vendorID = props.vendorID;
  info->deviceID = props.deviceID;
  info->subgroupMinSize = 8;
  info->subgroupMaxSize = 32;
  return WGPUStatus_Success;
}

void wgpuAdapterInfoFreeMembers(WGPUAdapterInfo info) {
  std::free(const_cast<char*>(info.vendor.data));
  std::free(const_cast<char*>(info.architecture.data));
  std::free(const_cast<char*>(info.device.data));
  std::free(const_cast<char*>(info.description.data));
}

static void FillLimits(WGPULimits* limits) {
  VkPhysicalDeviceProperties props{};
  uint32_t count = 1;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  if (g_device != nullptr) {
    props = g_device->props;
  } else if (vkEnumeratePhysicalDevices(g_vkInstance, &count, &phys) >= 0 && phys != VK_NULL_HANDLE) {
    vkGetPhysicalDeviceProperties(phys, &props);
  }
  const VkPhysicalDeviceLimits& l = props.limits;
  WGPUChainedStruct* next = limits->nextInChain;
  *limits = WGPULimits{};
  limits->nextInChain = next;
  limits->maxTextureDimension1D = l.maxImageDimension1D;
  limits->maxTextureDimension2D = l.maxImageDimension2D;
  limits->maxTextureDimension3D = l.maxImageDimension3D;
  limits->maxTextureArrayLayers = l.maxImageArrayLayers;
  limits->maxBindGroups = kMaxGroups;
  limits->maxBindGroupsPlusVertexBuffers = 24;
  limits->maxBindingsPerBindGroup = 1000;
  limits->maxDynamicUniformBuffersPerPipelineLayout = std::min(l.maxDescriptorSetUniformBuffersDynamic, 8u);
  limits->maxDynamicStorageBuffersPerPipelineLayout = std::min(l.maxDescriptorSetStorageBuffersDynamic, 8u);
  limits->maxSampledTexturesPerShaderStage = std::min(l.maxPerStageDescriptorSampledImages, 64u);
  limits->maxSamplersPerShaderStage = std::min(l.maxPerStageDescriptorSamplers, 64u);
  limits->maxStorageBuffersPerShaderStage = std::min(l.maxPerStageDescriptorStorageBuffers, 16u);
  limits->maxStorageTexturesPerShaderStage = std::min(l.maxPerStageDescriptorStorageImages, 8u);
  limits->maxUniformBuffersPerShaderStage = std::min(l.maxPerStageDescriptorUniformBuffers, 16u);
  limits->maxUniformBufferBindingSize = l.maxUniformBufferRange;
  limits->maxStorageBufferBindingSize = l.maxStorageBufferRange;
  limits->minUniformBufferOffsetAlignment = static_cast<uint32_t>(std::max<VkDeviceSize>(l.minUniformBufferOffsetAlignment, 1));
  limits->minStorageBufferOffsetAlignment = static_cast<uint32_t>(std::max<VkDeviceSize>(l.minStorageBufferOffsetAlignment, 1));
  limits->maxVertexBuffers = std::min(l.maxVertexInputBindings, 16u);
  limits->maxBufferSize = 1ull << 31;
  limits->maxVertexAttributes = std::min(l.maxVertexInputAttributes, 30u);
  limits->maxVertexBufferArrayStride = std::min(l.maxVertexInputBindingStride, 2048u);
  limits->maxInterStageShaderVariables = 16;
  limits->maxColorAttachments = std::min(l.maxColorAttachments, 8u);
  limits->maxColorAttachmentBytesPerSample = 32;
  limits->maxComputeWorkgroupStorageSize = l.maxComputeSharedMemorySize;
  limits->maxComputeInvocationsPerWorkgroup = l.maxComputeWorkGroupInvocations;
  limits->maxComputeWorkgroupSizeX = l.maxComputeWorkGroupSize[0];
  limits->maxComputeWorkgroupSizeY = l.maxComputeWorkGroupSize[1];
  limits->maxComputeWorkgroupSizeZ = l.maxComputeWorkGroupSize[2];
  limits->maxComputeWorkgroupsPerDimension = l.maxComputeWorkGroupCount[0];
  limits->maxImmediateSize = 0;
}

WGPUStatus wgpuAdapterGetLimits(WGPUAdapter, WGPULimits* limits) {
  FillLimits(limits);
  return WGPUStatus_Success;
}

WGPUBool wgpuAdapterHasFeature(WGPUAdapter, WGPUFeatureName feature) {
  return feature == WGPUFeatureName_TextureCompressionBC || feature == WGPUFeatureName_ImplicitDeviceSynchronization;
}

void wgpuAdapterGetFeatures(WGPUAdapter, WGPUSupportedFeatures* features) {
  auto* list = new WGPUFeatureName[2]{WGPUFeatureName_TextureCompressionBC,
                                      WGPUFeatureName_ImplicitDeviceSynchronization};
  features->featureCount = 2;
  features->features = list;
}

void wgpuSupportedFeaturesFreeMembers(WGPUSupportedFeatures features) { delete[] features.features; }

WGPUFuture wgpuAdapterRequestDevice(WGPUAdapter, const WGPUDeviceDescriptor* descriptor,
                                    WGPURequestDeviceCallbackInfo callbackInfo) {
  WGPUDeviceImpl* dev = g_device;
  if (dev != nullptr) {
    dev->AddRef();
  } else {
    dev = CreateDevice();
  }
  if (dev != nullptr && descriptor != nullptr) {
    dev->errorCallback = descriptor->uncapturedErrorCallbackInfo;
  }
  callbackInfo.callback(dev != nullptr ? WGPURequestDeviceStatus_Success : WGPURequestDeviceStatus_Error, dev,
                        MakeView(dev != nullptr ? "" : "no Vulkan 1.3 device"), callbackInfo.userdata1,
                        callbackInfo.userdata2);
  return WGPUFuture{0};
}

void wgpuDeviceAddRef(WGPUDevice device) { device->AddRef(); }
void wgpuDeviceRelease(WGPUDevice device) { device->Release(); }

WGPUStatus wgpuDeviceGetLimits(WGPUDevice, WGPULimits* limits) {
  FillLimits(limits);
  return WGPUStatus_Success;
}

void wgpuDeviceSetLoggingCallback(WGPUDevice device, WGPULoggingCallbackInfo callbackInfo) {
  device->loggingCallback = callbackInfo;
}

WGPUQueue wgpuDeviceGetQueue(WGPUDevice device) {
  auto* queue = new WGPUQueueImpl;
  queue->dev = device;
  device->AddRef();
  return queue;
}

void wgpuQueueRelease(WGPUQueue queue) {
  if (queue->refs.load() == 1) {
    queue->dev->Release();
  }
  queue->Release();
}

// --- buffers ---

WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice dev, const WGPUBufferDescriptor* descriptor) {
  auto* buffer = new WGPUBufferImpl;
  buffer->dev = dev;
  buffer->size = descriptor->size;
  buffer->usage = descriptor->usage;
  VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (descriptor->usage & WGPUBufferUsage_Vertex) {
    usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (descriptor->usage & WGPUBufferUsage_Index) {
    usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (descriptor->usage & WGPUBufferUsage_Uniform) {
    usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if (descriptor->usage & WGPUBufferUsage_Storage) {
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if (descriptor->usage & WGPUBufferUsage_Indirect) {
    usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  }
  const bool mapRead = descriptor->usage & WGPUBufferUsage_MapRead;
  const bool mapWrite = descriptor->usage & WGPUBufferUsage_MapWrite;
  const VkBufferCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = std::max<uint64_t>(descriptor->size, 4),
      .usage = usage,
  };
  VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_AUTO};
  if (mapRead) {
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  } else if (mapWrite && dev->cachedStaging) {
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    allocInfo.requiredFlags = kCachedStagingFlags;
  } else if (mapWrite) {
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  } else if (dev->renameIndexCopies && (descriptor->usage & WGPUBufferUsage_Index)) {
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  } else {
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  }
  VmaAllocationInfo result{};
  VkResult bufRes = vmaCreateBuffer(dev->vma, &info, &allocInfo, &buffer->buffer, &buffer->alloc, &result);
  if (bufRes != VK_SUCCESS && allocInfo.requiredFlags != 0) {
    allocInfo.requiredFlags = 0;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    bufRes = vmaCreateBuffer(dev->vma, &info, &allocInfo, &buffer->buffer, &buffer->alloc, &result);
  }
  VK_CHECK(bufRes);
  buffer->mapped = static_cast<uint8_t*>(result.pMappedData);
  // Adapted from ARMSX2 VKStreamBuffer.cpp (GPL-3.0): pre-touch mapped ring buffers on UMA/mobile GPUs
  // at creation time so first-touch page faults do not hitch during gameplay.
  if (mapWrite && buffer->mapped != nullptr && dev->props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    std::memset(buffer->mapped, 0, result.size);
    vmaFlushAllocation(dev->vma, buffer->alloc, 0, VK_WHOLE_SIZE);
  }
  if (descriptor->mappedAtCreation && buffer->mapped == nullptr) {
    buffer->staging.resize(descriptor->size);
    buffer->stagingMapped = true;
  }
  return buffer;
}

void wgpuBufferAddRef(WGPUBuffer buffer) { buffer->AddRef(); }
void wgpuBufferRelease(WGPUBuffer buffer) { buffer->Release(); }
void wgpuBufferDestroy(WGPUBuffer) {}

void* wgpuBufferGetMappedRange(WGPUBuffer buffer, size_t offset, size_t) {
  if (buffer->stagingMapped) {
    return buffer->staging.data() + offset;
  }
  return buffer->mapped != nullptr ? buffer->mapped + offset : nullptr;
}

const void* wgpuBufferGetConstMappedRange(WGPUBuffer buffer, size_t offset, size_t size) {
  return wgpuBufferGetMappedRange(buffer, offset, size);
}

WGPUFuture wgpuBufferMapAsync(WGPUBuffer buffer, WGPUMapMode mode, size_t, size_t,
                              WGPUBufferMapCallbackInfo callbackInfo) {
  WGPUDeviceImpl* dev = buffer->dev;
  buffer->AddRef();
  const bool read = mode & WGPUMapMode_Read;
  const uint64_t id =
      AddFuture(dev, buffer->lastUseSerial.load(std::memory_order_acquire), callbackInfo.mode, [buffer, callbackInfo, read] {
        if (read) {
          vmaInvalidateAllocation(buffer->dev->vma, buffer->alloc, 0, VK_WHOLE_SIZE);
        }
        callbackInfo.callback(WGPUMapAsyncStatus_Success, MakeView(""), callbackInfo.userdata1,
                              callbackInfo.userdata2);
        buffer->Release();
      });
  return WGPUFuture{id};
}

void wgpuBufferUnmap(WGPUBuffer buffer) {
  WGPUDeviceImpl* dev = buffer->dev;
  if (buffer->stagingMapped) {
    buffer->stagingMapped = false;
    std::lock_guard lock(dev->queueMutex);
    const auto [chunk, offset] = AllocStaging(dev, buffer->staging.size(), 16);
    std::memcpy(chunk->mapped + offset, buffer->staging.data(), buffer->staging.size());
    const VkBufferCopy region{offset, 0, buffer->staging.size()};
    vkCmdCopyBuffer(UploadCb(dev), chunk->buffer, buffer->buffer, 1, &region);
    buffer->AddRef();
    dev->uploadRefs.push_back(buffer);
    buffer->staging = {};
    return;
  }
  if (buffer->mapped != nullptr) {
    vmaFlushAllocation(dev->vma, buffer->alloc, 0, VK_WHOLE_SIZE);
  }
}

// --- textures ---

// Per-heap usage and budget (the driver's figure with VK_EXT_memory_budget, VMA's estimate without).
static void LogHeapBudgets(WGPUDeviceImpl* dev, const char* why) {
  const VkPhysicalDeviceMemoryProperties* props = nullptr;
  vmaGetMemoryProperties(dev->vma, &props);
  VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
  vmaGetHeapBudgets(dev->vma, budgets);
  for (uint32_t i = 0; props != nullptr && i < props->memoryHeapCount; ++i) {
    Log("memory: %s: heap %u usage %llu MiB / budget %llu MiB (aurora blocks %llu MiB)", why, i,
        static_cast<unsigned long long>(budgets[i].usage >> 20),
        static_cast<unsigned long long>(budgets[i].budget >> 20),
        static_cast<unsigned long long>(budgets[i].statistics.blockBytes >> 20));
  }
}

WGPUTexture wgpuDeviceCreateTexture(WGPUDevice dev, const WGPUTextureDescriptor* descriptor) {
  auto* tex = new WGPUTextureImpl;
  tex->dev = dev;
  tex->format = descriptor->format;
  tex->info = GetFormat(descriptor->format);
  tex->width = descriptor->size.width;
  tex->height = descriptor->size.height;
  tex->layers = std::max(1u, descriptor->size.depthOrArrayLayers);
  tex->mips = std::max(1u, descriptor->mipLevelCount);
  tex->samples = std::max(1u, descriptor->sampleCount);
  if (tex->info.vk == VK_FORMAT_UNDEFINED) {
    ReportError(dev, "unsupported texture format " + std::to_string(descriptor->format));
    tex->info = GetFormat(WGPUTextureFormat_RGBA8Unorm);
  }
  VkImageUsageFlags usage = 0;
  if (descriptor->usage & WGPUTextureUsage_CopySrc) {
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if (descriptor->usage & WGPUTextureUsage_CopyDst) {
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  if (descriptor->usage & WGPUTextureUsage_TextureBinding) {
    usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (descriptor->usage & WGPUTextureUsage_StorageBinding) {
    usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  if (descriptor->usage & WGPUTextureUsage_RenderAttachment) {
    usage |= tex->info.depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  const bool is3D = descriptor->dimension == WGPUTextureDimension_3D;
  const VkImageCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = (descriptor->viewFormatCount > 0 ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0u),
      .imageType = is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
      .format = tex->info.vk,
      .extent = {tex->width, tex->height, is3D ? tex->layers : 1u},
      .mipLevels = tex->mips,
      .arrayLayers = is3D ? 1u : tex->layers,
      .samples = static_cast<VkSampleCountFlagBits>(tex->samples),
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  const VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
  VkResult imageResult = vmaCreateImage(dev->vma, &info, &allocInfo, &tex->image, &tex->alloc, nullptr);
  if (imageResult == VK_ERROR_OUT_OF_DEVICE_MEMORY || imageResult == VK_ERROR_OUT_OF_HOST_MEMORY) {
    // ARMSX2 retries a failed texture instead of dying: a new dedicated allocation or block can fail
    // while the blocks already held still have room, so try those before giving up.
    LogHeapBudgets(dev, "texture allocation failed");
    Log("memory: %ux%u format %d (%u mips) out of memory, retrying in existing blocks", tex->width, tex->height,
        static_cast<int>(tex->info.vk), tex->mips);
    const VmaAllocationCreateInfo existingOnly{.flags = VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT,
                                               .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    imageResult = vmaCreateImage(dev->vma, &info, &existingOnly, &tex->image, &tex->alloc, nullptr);
  }
  VK_CHECK(imageResult);
  tex->aspects = AspectFor(tex->info, WGPUTextureAspect_All);
  if (!LayoutsEnabled() || (usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0) {
    tex->home = VK_IMAGE_LAYOUT_GENERAL;
  } else if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0) {
    tex->home =
        tex->info.depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  } else if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
    tex->home = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  } else if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0) {
    tex->home = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  } else {
    tex->home = VK_IMAGE_LAYOUT_GENERAL;
  }
  {
    std::lock_guard lock(dev->queueMutex);
    ImageLayoutBarrier(UploadCb(dev), tex->image, tex->aspects, VK_IMAGE_LAYOUT_UNDEFINED, tex->home);
    // The barrier names the image, so it must outlive the upload submission even if released first.
    tex->AddRef();
    dev->uploadRefs.push_back(tex);
  }
  tex->layout = tex->home;
  return tex;
}

void wgpuTextureAddRef(WGPUTexture texture) { texture->AddRef(); }
void wgpuTextureRelease(WGPUTexture texture) { texture->Release(); }
// The present pass sizes its viewport from the swapchain image (aurora.cpp present_presentation_job).
uint32_t wgpuTextureGetWidth(WGPUTexture texture) { return texture->width; }
uint32_t wgpuTextureGetHeight(WGPUTexture texture) { return texture->height; }

WGPUTextureView wgpuTextureCreateView(WGPUTexture texture, const WGPUTextureViewDescriptor* descriptor) {
  auto* view = new WGPUTextureViewImpl;
  view->texture = texture;
  texture->AddRef();
  if (descriptor == nullptr && texture->defaultView != VK_NULL_HANDLE) {
    view->view = texture->defaultView;
    view->format = texture->info.vk;
    view->baseMip = 0;
    view->width = texture->width;
    view->height = texture->height;
    return view;
  }
  WGPUTextureViewDescriptor desc{};
  if (descriptor != nullptr) {
    desc = *descriptor;
  }
  const FormatInfo info = desc.format != WGPUTextureFormat_Undefined ? GetFormat(desc.format) : texture->info;
  const uint32_t mipCount =
      desc.mipLevelCount == WGPU_MIP_LEVEL_COUNT_UNDEFINED || desc.mipLevelCount == 0 ? texture->mips - desc.baseMipLevel
                                                                                       : desc.mipLevelCount;
  const uint32_t layerCount = desc.arrayLayerCount == WGPU_ARRAY_LAYER_COUNT_UNDEFINED || desc.arrayLayerCount == 0
                                  ? texture->layers - desc.baseArrayLayer
                                  : desc.arrayLayerCount;
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  switch (desc.dimension) {
  case WGPUTextureViewDimension_2DArray:
    type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    break;
  case WGPUTextureViewDimension_Cube:
    type = VK_IMAGE_VIEW_TYPE_CUBE;
    break;
  case WGPUTextureViewDimension_3D:
    type = VK_IMAGE_VIEW_TYPE_3D;
    break;
  case WGPUTextureViewDimension_Undefined:
    type = layerCount > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    break;
  default:
    break;
  }
  VkImageAspectFlags aspect = AspectFor(info, desc.aspect);
  // A sampled depth-stencil view reads one aspect.
  if ((aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && (aspect & VK_IMAGE_ASPECT_STENCIL_BIT) &&
      desc.usage == WGPUTextureUsage_TextureBinding) {
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  const VkImageViewCreateInfo info2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = texture->image,
      .viewType = type,
      .format = info.vk,
      .subresourceRange = {aspect, desc.baseMipLevel, mipCount, desc.baseArrayLayer, layerCount},
  };
  VK_CHECK(vkCreateImageView(texture->dev->device, &info2, nullptr, &view->view));
  if (descriptor == nullptr && texture->swapchain && texture->defaultView == VK_NULL_HANDLE) {
    texture->defaultView = view->view;
  }
  view->format = info.vk;
  view->baseMip = desc.baseMipLevel;
  view->baseLayer = desc.baseArrayLayer;
  view->width = std::max(1u, texture->width >> desc.baseMipLevel);
  view->height = std::max(1u, texture->height >> desc.baseMipLevel);
  return view;
}

void wgpuTextureViewAddRef(WGPUTextureView view) { view->AddRef(); }
void wgpuTextureViewRelease(WGPUTextureView view) { view->Release(); }

WGPUSampler wgpuDeviceCreateSampler(WGPUDevice dev, const WGPUSamplerDescriptor* descriptor) {
  WGPUSamplerDescriptor desc{};
  desc.lodMaxClamp = 32.0f;
  desc.maxAnisotropy = 1;
  if (descriptor != nullptr) {
    desc = *descriptor;
  }
  const bool aniso = dev->hasAnisotropy && desc.maxAnisotropy > 1;
  const VkSamplerCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = desc.magFilter == WGPUFilterMode_Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
      .minFilter = desc.minFilter == WGPUFilterMode_Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
      .mipmapMode = desc.mipmapFilter == WGPUMipmapFilterMode_Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                                     : VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = ToVk(desc.addressModeU),
      .addressModeV = ToVk(desc.addressModeV),
      .addressModeW = ToVk(desc.addressModeW),
      .anisotropyEnable = aniso ? VK_TRUE : VK_FALSE,
      .maxAnisotropy = aniso ? std::min<float>(desc.maxAnisotropy, dev->props.limits.maxSamplerAnisotropy) : 1.0f,
      .compareEnable = desc.compare != WGPUCompareFunction_Undefined ? VK_TRUE : VK_FALSE,
      .compareOp = ToVk(desc.compare),
      .minLod = desc.lodMinClamp,
      .maxLod = desc.lodMaxClamp,
  };
  auto* sampler = new WGPUSamplerImpl;
  sampler->dev = dev;
  VK_CHECK(vkCreateSampler(dev->device, &info, nullptr, &sampler->sampler));
  return sampler;
}

void wgpuSamplerAddRef(WGPUSampler sampler) { sampler->AddRef(); }
void wgpuSamplerRelease(WGPUSampler sampler) { sampler->Release(); }

// --- shaders, layouts, bind groups, pipelines ---

WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice dev, const WGPUShaderModuleDescriptor* descriptor) {
  auto* module = new WGPUShaderModuleImpl;
  module->label = std::string{ToView(descriptor->label)};
  if (const auto* wgsl = reinterpret_cast<const WGPUShaderSourceWGSL*>(
          FindChain(descriptor->nextInChain, WGPUSType_ShaderSourceWGSL))) {
    module->wgsl = std::string{ToView(wgsl->code)};
  } else {
    ReportError(dev, "shader module without WGSL source");
  }
  return module;
}

void wgpuShaderModuleAddRef(WGPUShaderModule module) { module->AddRef(); }
void wgpuShaderModuleRelease(WGPUShaderModule module) { module->Release(); }

WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice dev, const WGPUBindGroupLayoutDescriptor* descriptor) {
  auto* layout = new WGPUBindGroupLayoutImpl;
  layout->dev = dev;
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  for (size_t i = 0; i < descriptor->entryCount; ++i) {
    const auto& e = descriptor->entries[i];
    BindEntry entry{e.binding, BindKind::Uniform, false};
    VkDescriptorType type;
    if (e.buffer.type != WGPUBufferBindingType_BindingNotUsed) {
      entry.dynamic = e.buffer.hasDynamicOffset;
      if (e.buffer.type == WGPUBufferBindingType_Storage) {
        entry.kind = BindKind::Storage;
      } else if (e.buffer.type == WGPUBufferBindingType_ReadOnlyStorage) {
        entry.kind = BindKind::ReadOnlyStorage;
      }
      const bool uniform = entry.kind == BindKind::Uniform;
      type = uniform ? (entry.dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                     : (entry.dynamic ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      layout->dynamicCount += entry.dynamic ? 1 : 0;
    } else if (e.sampler.type != WGPUSamplerBindingType_BindingNotUsed) {
      entry.kind = BindKind::Sampler;
      type = VK_DESCRIPTOR_TYPE_SAMPLER;
    } else if (e.texture.sampleType != WGPUTextureSampleType_BindingNotUsed) {
      entry.kind = BindKind::Texture;
      type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    } else if (e.storageTexture.access != WGPUStorageTextureAccess_BindingNotUsed) {
      entry.kind = BindKind::StorageTexture;
      type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    } else {
      continue;
    }
    layout->entries.push_back(entry);
    bindings.push_back(VkDescriptorSetLayoutBinding{
        .binding = e.binding,
        .descriptorType = type,
        .descriptorCount = 1,
        .stageFlags = ToVk(e.visibility),
    });
  }
  std::sort(layout->entries.begin(), layout->entries.end(),
            [](const BindEntry& a, const BindEntry& b) { return a.binding < b.binding; });
  const VkDescriptorSetLayoutCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<uint32_t>(bindings.size()),
      .pBindings = bindings.data(),
  };
  VK_CHECK(vkCreateDescriptorSetLayout(dev->device, &info, nullptr, &layout->layout));
  return layout;
}

void wgpuBindGroupLayoutAddRef(WGPUBindGroupLayout layout) { layout->AddRef(); }
void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout layout) { layout->Release(); }

WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice dev, const WGPUBindGroupDescriptor* descriptor) {
  auto* group = new WGPUBindGroupImpl;
  group->dev = dev;
  group->layout = descriptor->layout;
  descriptor->layout->AddRef();
  {
    std::lock_guard lock(dev->poolMutex);
    VkDescriptorSetAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor->layout->layout,
    };
    VkResult result = VK_ERROR_OUT_OF_POOL_MEMORY;
    for (auto it = dev->descriptorPools.rbegin(); it != dev->descriptorPools.rend() && result != VK_SUCCESS; ++it) {
      alloc.descriptorPool = *it;
      result = vkAllocateDescriptorSets(dev->device, &alloc, &group->set);
      group->pool = *it;
    }
    if (result != VK_SUCCESS) {
      alloc.descriptorPool = NewDescriptorPool(dev);
      VK_CHECK(vkAllocateDescriptorSets(dev->device, &alloc, &group->set));
      group->pool = alloc.descriptorPool;
    }
  }
  const auto& entries = descriptor->layout->entries;
  std::vector<VkWriteDescriptorSet> writes;
  std::vector<VkDescriptorBufferInfo> bufferInfos;
  std::vector<VkDescriptorImageInfo> imageInfos;
  writes.reserve(descriptor->entryCount);
  bufferInfos.reserve(descriptor->entryCount);
  imageInfos.reserve(descriptor->entryCount);
  for (size_t i = 0; i < descriptor->entryCount; ++i) {
    const auto& e = descriptor->entries[i];
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const BindEntry& b) { return b.binding == e.binding; });
    if (it == entries.end()) {
      continue;
    }
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = group->set,
        .dstBinding = e.binding,
        .descriptorCount = 1,
    };
    switch (it->kind) {
    case BindKind::Uniform:
    case BindKind::Storage:
    case BindKind::ReadOnlyStorage: {
      const bool uniform = it->kind == BindKind::Uniform;
      write.descriptorType = uniform ? (it->dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                     : (it->dynamic ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      const uint64_t range = e.size == WGPU_WHOLE_SIZE ? e.buffer->size - e.offset : e.size;
      bufferInfos.push_back(VkDescriptorBufferInfo{e.buffer->buffer, e.offset, range});
      write.pBufferInfo = &bufferInfos.back();
      e.buffer->AddRef();
      group->refs.push_back(e.buffer);
      break;
    }
    case BindKind::Sampler:
      write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
      imageInfos.push_back(VkDescriptorImageInfo{.sampler = e.sampler->sampler});
      write.pImageInfo = &imageInfos.back();
      e.sampler->AddRef();
      group->refs.push_back(e.sampler);
      break;
    case BindKind::Texture:
    case BindKind::StorageTexture:
      write.descriptorType =
          it->kind == BindKind::Texture ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      imageInfos.push_back(VkDescriptorImageInfo{
          .imageView = e.textureView->view,
          .imageLayout = it->kind == BindKind::Texture ? e.textureView->texture->home : VK_IMAGE_LAYOUT_GENERAL});
      write.pImageInfo = &imageInfos.back();
      e.textureView->AddRef();
      group->refs.push_back(e.textureView);
      break;
    }
    writes.push_back(write);
  }
  vkUpdateDescriptorSets(dev->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  return group;
}

void wgpuBindGroupAddRef(WGPUBindGroup group) { group->AddRef(); }
void wgpuBindGroupRelease(WGPUBindGroup group) { group->Release(); }

WGPUPipelineLayout wgpuDeviceCreatePipelineLayout(WGPUDevice dev, const WGPUPipelineLayoutDescriptor* descriptor) {
  auto* layout = new WGPUPipelineLayoutImpl;
  layout->dev = dev;
  std::vector<VkDescriptorSetLayout> sets;
  for (size_t i = 0; i < descriptor->bindGroupLayoutCount; ++i) {
    WGPUBindGroupLayoutImpl* group = descriptor->bindGroupLayouts[i];
    layout->groups.push_back(group);
    if (group != nullptr) {
      group->AddRef();
      sets.push_back(group->layout);
    } else {
      const VkDescriptorSetLayoutCreateInfo empty{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      VkDescriptorSetLayout handle;
      VK_CHECK(vkCreateDescriptorSetLayout(dev->device, &empty, nullptr, &handle));
      layout->ownedEmpty.push_back(handle);
      sets.push_back(handle);
    }
  }
  const VkPipelineLayoutCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = static_cast<uint32_t>(sets.size()),
      .pSetLayouts = sets.data(),
  };
  VK_CHECK(vkCreatePipelineLayout(dev->device, &info, nullptr, &layout->layout));
  return layout;
}

void wgpuPipelineLayoutAddRef(WGPUPipelineLayout layout) { layout->AddRef(); }
void wgpuPipelineLayoutRelease(WGPUPipelineLayout layout) { layout->Release(); }

WGPURenderPipeline wgpuDeviceCreateRenderPipeline(WGPUDevice dev, const WGPURenderPipelineDescriptor* descriptor) {
  auto* pipeline = new WGPURenderPipelineImpl;
  pipeline->dev = dev;
  pipeline->id = dev->pipelineIds.fetch_add(1, std::memory_order_relaxed) + 1;
  pipeline->layout = descriptor->layout;
  descriptor->layout->AddRef();

  std::vector<uint32_t> vsCode;
  std::vector<uint32_t> fsCode;
  const std::string vsEntry{ToView(descriptor->vertex.entryPoint)};
  const std::string fsEntry = descriptor->fragment != nullptr ? std::string{ToView(descriptor->fragment->entryPoint)} : "";
  if (!CompileWgsl(dev, descriptor->vertex.module, vsEntry, descriptor->layout, vsCode) ||
      (descriptor->fragment != nullptr &&
       !CompileWgsl(dev, descriptor->fragment->module, fsEntry, descriptor->layout, fsCode))) {
    return pipeline; // VK_NULL_HANDLE pipeline: draws with it are skipped
  }
  const VkShaderModule vs = MakeModule(dev, vsCode);
  const VkShaderModule fs = descriptor->fragment != nullptr ? MakeModule(dev, fsCode) : VK_NULL_HANDLE;
  std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vs,
       .pName = vsEntry.c_str()},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fs,
       .pName = fsEntry.c_str()},
  }};

  std::vector<VkVertexInputBindingDescription> vbindings;
  std::vector<VkVertexInputAttributeDescription> vattrs;
  for (size_t i = 0; i < descriptor->vertex.bufferCount; ++i) {
    const auto& buffer = descriptor->vertex.buffers[i];
    if (buffer.attributeCount == 0) {
      continue;
    }
    vbindings.push_back(VkVertexInputBindingDescription{
        static_cast<uint32_t>(i), static_cast<uint32_t>(buffer.arrayStride),
        buffer.stepMode == WGPUVertexStepMode_Instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
    for (size_t a = 0; a < buffer.attributeCount; ++a) {
      const auto& attr = buffer.attributes[a];
      vattrs.push_back(VkVertexInputAttributeDescription{attr.shaderLocation, static_cast<uint32_t>(i),
                                                         ToVk(attr.format), static_cast<uint32_t>(attr.offset)});
    }
  }
  const VkPipelineVertexInputStateCreateInfo vertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = static_cast<uint32_t>(vbindings.size()),
      .pVertexBindingDescriptions = vbindings.data(),
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(vattrs.size()),
      .pVertexAttributeDescriptions = vattrs.data(),
  };
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  switch (descriptor->primitive.topology) {
  case WGPUPrimitiveTopology_PointList:
    topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    break;
  case WGPUPrimitiveTopology_LineList:
    topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    break;
  case WGPUPrimitiveTopology_LineStrip:
    topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    break;
  case WGPUPrimitiveTopology_TriangleStrip:
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    break;
  default:
    break;
  }
  const bool strip = topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP || topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = topology,
      .primitiveRestartEnable = strip ? VK_TRUE : VK_FALSE,
  };
  const VkPipelineViewportStateCreateInfo viewport{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };
  const WGPUDepthStencilState* ds = descriptor->depthStencil;
  const bool depthBias = ds != nullptr && (ds->depthBias != 0 || ds->depthBiasSlopeScale != 0.0f);
  const VkPipelineRasterizationStateCreateInfo raster{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = descriptor->primitive.unclippedDepth && dev->hasDepthClamp ? VK_TRUE : VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = descriptor->primitive.cullMode == WGPUCullMode_Front  ? VK_CULL_MODE_FRONT_BIT
                  : descriptor->primitive.cullMode == WGPUCullMode_Back ? VK_CULL_MODE_BACK_BIT
                                                                         : VK_CULL_MODE_NONE,
      .frontFace = descriptor->primitive.frontFace == WGPUFrontFace_CW ? VK_FRONT_FACE_CLOCKWISE
                                                                        : VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable = depthBias ? VK_TRUE : VK_FALSE,
      .depthBiasConstantFactor = ds != nullptr ? static_cast<float>(ds->depthBias) : 0.0f,
      .depthBiasClamp = ds != nullptr ? ds->depthBiasClamp : 0.0f,
      .depthBiasSlopeFactor = ds != nullptr ? ds->depthBiasSlopeScale : 0.0f,
      .lineWidth = 1.0f,
  };
  const uint32_t sampleMask = descriptor->multisample.mask;
  const VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = static_cast<VkSampleCountFlagBits>(std::max(1u, descriptor->multisample.count)),
      .pSampleMask = &sampleMask,
      .alphaToCoverageEnable = descriptor->multisample.alphaToCoverageEnabled ? VK_TRUE : VK_FALSE,
  };
  VkPipelineDepthStencilStateCreateInfo depthStencil{.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
  if (ds != nullptr) {
    const FormatInfo info = GetFormat(ds->format);
    depthFormat = info.depth ? info.vk : VK_FORMAT_UNDEFINED;
    stencilFormat = info.stencil ? info.vk : VK_FORMAT_UNDEFINED;
    const auto face = [](const WGPUStencilFaceState& s, uint32_t readMask, uint32_t writeMask) {
      return VkStencilOpState{ToVk(s.failOp), ToVk(s.passOp), ToVk(s.depthFailOp), ToVk(s.compare), readMask, writeMask, 0};
    };
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = ds->depthWriteEnabled == WGPUOptionalBool_True ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = ToVk(ds->depthCompare);
    depthStencil.stencilTestEnable = info.stencil ? VK_TRUE : VK_FALSE;
    depthStencil.front = face(ds->stencilFront, ds->stencilReadMask, ds->stencilWriteMask);
    depthStencil.back = face(ds->stencilBack, ds->stencilReadMask, ds->stencilWriteMask);
    depthStencil.maxDepthBounds = 1.0f;
  }
  std::vector<VkPipelineColorBlendAttachmentState> blends;
  std::vector<VkFormat> colorFormats;
  if (descriptor->fragment != nullptr) {
    for (size_t i = 0; i < descriptor->fragment->targetCount; ++i) {
      const auto& target = descriptor->fragment->targets[i];
      colorFormats.push_back(GetFormat(target.format).vk);
      VkPipelineColorBlendAttachmentState blend{.colorWriteMask = static_cast<VkColorComponentFlags>(target.writeMask & 0xF)};
      if (target.blend != nullptr) {
        pipeline->blend = true;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = ToVk(target.blend->color.srcFactor);
        blend.dstColorBlendFactor = ToVk(target.blend->color.dstFactor);
        blend.colorBlendOp = ToVk(target.blend->color.operation);
        blend.srcAlphaBlendFactor = ToVk(target.blend->alpha.srcFactor);
        blend.dstAlphaBlendFactor = ToVk(target.blend->alpha.dstFactor);
        blend.alphaBlendOp = ToVk(target.blend->alpha.operation);
      }
      blends.push_back(blend);
    }
  }
  const VkPipelineColorBlendStateCreateInfo colorBlend{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(blends.size()),
      .pAttachments = blends.data(),
  };
  const std::array<VkDynamicState, 4> dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                               VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
  const VkPipelineDynamicStateCreateInfo dynamic{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamics.size()),
      .pDynamicStates = dynamics.data(),
  };
  const VkPipelineRenderingCreateInfo rendering{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
      .pColorAttachmentFormats = colorFormats.data(),
      .depthAttachmentFormat = depthFormat,
      .stencilAttachmentFormat = stencilFormat,
  };
  // Tier 0: no dynamic rendering; the pipeline is made against a compatible render pass instead.
  const VkRenderPass legacyPass =
      dev->legacy ? LegacyCompatiblePass(dev, colorFormats,
                                         depthFormat != VK_FORMAT_UNDEFINED ? depthFormat : stencilFormat,
                                         multisample.rasterizationSamples)
                  : VK_NULL_HANDLE;
  const VkGraphicsPipelineCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = dev->legacy ? nullptr : &rendering,
      .stageCount = descriptor->fragment != nullptr ? 2u : 1u,
      .pStages = stages.data(),
      .pVertexInputState = &vertexInput,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewport,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depthStencil,
      .pColorBlendState = &colorBlend,
      .pDynamicState = &dynamic,
      .layout = descriptor->layout->layout,
      .renderPass = legacyPass,
      .subpass = 0,
  };
  const VkResult result = vkCreateGraphicsPipelines(dev->device, dev->pipelineCache, 1, &info, nullptr, &pipeline->pipeline);
  if (result != VK_SUCCESS) {
    ReportError(dev, "vkCreateGraphicsPipelines failed: " + std::to_string(result) + " (" +
                         std::string{ToView(descriptor->label)} + ")");
    pipeline->pipeline = VK_NULL_HANDLE;
  }
  vkDestroyShaderModule(dev->device, vs, nullptr);
  if (fs != VK_NULL_HANDLE) {
    vkDestroyShaderModule(dev->device, fs, nullptr);
  }
  NotePipelineCreated(dev);
  return pipeline;
}

void wgpuRenderPipelineAddRef(WGPURenderPipeline pipeline) { pipeline->AddRef(); }
void wgpuRenderPipelineRelease(WGPURenderPipeline pipeline) { pipeline->Release(); }

WGPUComputePipeline wgpuDeviceCreateComputePipeline(WGPUDevice dev, const WGPUComputePipelineDescriptor* descriptor) {
  auto* pipeline = new WGPUComputePipelineImpl;
  pipeline->dev = dev;
  pipeline->layout = descriptor->layout;
  descriptor->layout->AddRef();
  const std::string entry{ToView(descriptor->compute.entryPoint)};
  std::vector<uint32_t> code;
  if (!CompileWgsl(dev, descriptor->compute.module, entry, descriptor->layout, code)) {
    return pipeline;
  }
  const VkShaderModule module = MakeModule(dev, code);
  const VkComputePipelineCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = module,
                .pName = entry.c_str()},
      .layout = descriptor->layout->layout,
  };
  if (vkCreateComputePipelines(dev->device, dev->pipelineCache, 1, &info, nullptr, &pipeline->pipeline) != VK_SUCCESS) {
    ReportError(dev, "vkCreateComputePipelines failed");
    pipeline->pipeline = VK_NULL_HANDLE;
  }
  vkDestroyShaderModule(dev->device, module, nullptr);
  NotePipelineCreated(dev);
  return pipeline;
}

void wgpuComputePipelineRelease(WGPUComputePipeline pipeline) { pipeline->Release(); }

// --- command encoding ---

WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice dev, const WGPUCommandEncoderDescriptor*) {
  auto* encoder = new WGPUCommandEncoderImpl;
  encoder->dev = dev;
  encoder->id = dev->encoderIds.fetch_add(1, std::memory_order_relaxed) + 1;
  encoder->slot = AcquireSlot(dev);
  return encoder;
}

void wgpuCommandEncoderRelease(WGPUCommandEncoder encoder) { encoder->Release(); }

WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder encoder, const WGPUCommandBufferDescriptor*) {
  static const bool debugClear = [] {
    const char* v = std::getenv("AURORA_VK_DEBUG_CLEAR");
    return v != nullptr && v[0] == '1';
  }();
  if (encoder->swapchainTex != nullptr && !debugClear) {
    // Transition the swapchain image directly to PRESENT_SRC_KHR at the end of the presentation
    // encoder so wgpuQueueSubmit can signal the present semaphore and wgpuSurfacePresent needs
    // no second command buffer or vkQueueSubmit2.
    encoder->Use(encoder->swapchainTex, 0, 0, 1, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    encoder->needBarrier = false;
    encoder->Barrier();
    encoder->away.erase(
        std::remove_if(encoder->away.begin(), encoder->away.end(),
                       [&](const auto& a) { return a.tex == encoder->swapchainTex; }),
        encoder->away.end());
    encoder->swapchainTex->layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if (encoder->slot.timing != nullptr) {
      encoder->slot.timing->present = true;
    }
  }
  if (!encoder->away.empty() || !encoder->transitions.empty()) {
    encoder->needBarrier = false;
    encoder->Settle();
  }
  if (!encoder->mapTracked.empty()) {
    HostReadBarrier(encoder->slot.cb);
  }
  EndSlot(encoder->slot);
  encoder->finished = true;
  auto* cmd = new WGPUCommandBufferImpl;
  cmd->dev = encoder->dev;
  cmd->slot = encoder->slot;
  cmd->refs = std::move(encoder->refs);
  cmd->mapTracked = std::move(encoder->mapTracked);
  cmd->touchesSwapchain = encoder->touchesSwapchain;
  return cmd;
}

void wgpuCommandBufferRelease(WGPUCommandBuffer cmd) { cmd->Release(); }

void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder encoder, WGPUBuffer source, uint64_t sourceOffset,
                                          WGPUBuffer destination, uint64_t destinationOffset, uint64_t size) {
  if (size == WGPU_WHOLE_SIZE) {
    size = source->size - sourceOffset;
  }
  if (size == 0) {
    return;
  }
  WGPUDeviceImpl* dev = encoder->dev;
  // Tier 0 never renames an index buffer that is also bound as storage (GX index pulling): its bind
  // group holds the VkBuffer and would go on reading the old memory.
  const bool boundAsStorage = dev->legacy && (destination->usage & WGPUBufferUsage_Storage) != 0;
  if (dev->renameIndexCopies && (destination->usage & WGPUBufferUsage_Index) && !boundAsStorage &&
      source->mapped != nullptr && destination->mapped != nullptr) {
    // RenameIndexCopy: the draws recorded after this bind the new memory, the ones before keep the old.
    auto* old = new RetiredBacking;
    old->dev = dev;
    old->size = destination->size;
    old->buffer = destination->buffer;
    old->alloc = destination->alloc;
    old->mapped = destination->mapped;
    encoder->Ref(old);
    old->Release();
    destination->buffer = VK_NULL_HANDLE;
    {
      std::lock_guard lock(dev->backingMutex);
      for (auto it = dev->freeBackings.begin(); it != dev->freeBackings.end(); ++it) {
        if (std::get<0>(*it) == destination->size) {
          std::tie(std::ignore, destination->buffer, destination->alloc, destination->mapped) = *it;
          dev->freeBackings.erase(it);
          break;
        }
      }
    }
    if (destination->buffer == VK_NULL_HANDLE) {
      const VkBufferCreateInfo info{
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = std::max<uint64_t>(destination->size, 4),
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   ((destination->usage & WGPUBufferUsage_Vertex) ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : 0u) |
                   ((destination->usage & WGPUBufferUsage_Storage) ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : 0u),
      };
      const VmaAllocationCreateInfo allocInfo{
          .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          .usage = VMA_MEMORY_USAGE_AUTO,
      };
      VmaAllocationInfo result{};
      VK_CHECK(vmaCreateBuffer(dev->vma, &info, &allocInfo, &destination->buffer, &destination->alloc, &result));
      destination->mapped = static_cast<uint8_t*>(result.pMappedData);
    }
    vmaInvalidateAllocation(dev->vma, source->alloc, sourceOffset, size);
    std::memcpy(destination->mapped + destinationOffset, source->mapped + sourceOffset, size);
    vmaFlushAllocation(dev->vma, destination->alloc, destinationOffset, size);
    encoder->Ref(source);
    return;
  }
  encoder->Barrier();
  const VkBufferCopy region{sourceOffset, destinationOffset, size};
  vkCmdCopyBuffer(encoder->slot.cb, source->buffer, destination->buffer, 1, &region);
  encoder->needBarrier = true;
  encoder->Ref(source);
  encoder->Ref(destination);
  for (WGPUBufferImpl* buffer : {source, destination}) {
    if (buffer->usage & (WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite)) {
      encoder->mapTracked.push_back(buffer);
    }
  }
}

static VkBufferImageCopy BufferImageRegion(const WGPUTextureImpl* tex, const WGPUTexelCopyTextureInfo* texInfo,
                                           const WGPUTexelCopyBufferLayout& layout, const WGPUExtent3D* size) {
  const FormatInfo& info = tex->info;
  const uint32_t blocksPerRow = layout.bytesPerRow == WGPU_COPY_STRIDE_UNDEFINED ? 0 : layout.bytesPerRow / info.blockBytes;
  return VkBufferImageCopy{
      .bufferOffset = layout.offset,
      .bufferRowLength = blocksPerRow * info.blockWidth,
      .bufferImageHeight = layout.rowsPerImage == WGPU_COPY_STRIDE_UNDEFINED ? 0 : layout.rowsPerImage * info.blockHeight,
      .imageSubresource = {AspectFor(info, texInfo->aspect) & (info.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
                           texInfo->mipLevel, texInfo->origin.z, std::max(1u, size->depthOrArrayLayers)},
      .imageOffset = {static_cast<int32_t>(texInfo->origin.x), static_cast<int32_t>(texInfo->origin.y), 0},
      .imageExtent = {size->width, size->height, 1},
  };
}

void wgpuCommandEncoderCopyTextureToBuffer(WGPUCommandEncoder encoder, const WGPUTexelCopyTextureInfo* source,
                                           const WGPUTexelCopyBufferInfo* destination, const WGPUExtent3D* copySize) {
  if (copySize->width == 0 || copySize->height == 0) {
    return;
  }
  const VkImageLayout srcLayout = OpLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  encoder->Use(source->texture, source->mipLevel, source->origin.z, std::max(1u, copySize->depthOrArrayLayers),
               srcLayout);
  encoder->Settle();
  const VkBufferImageCopy region = BufferImageRegion(source->texture, source, destination->layout, copySize);
  vkCmdCopyImageToBuffer(encoder->slot.cb, source->texture->image, srcLayout, destination->buffer->buffer,
                         1, &region);
  encoder->needBarrier = true;
  encoder->Ref(source->texture);
  encoder->Ref(destination->buffer);
  if (destination->buffer->usage & WGPUBufferUsage_MapRead) {
    encoder->mapTracked.push_back(destination->buffer);
  }
}

void wgpuCommandEncoderCopyTextureToTexture(WGPUCommandEncoder encoder, const WGPUTexelCopyTextureInfo* source,
                                            const WGPUTexelCopyTextureInfo* destination, const WGPUExtent3D* copySize) {
  if (copySize->width == 0 || copySize->height == 0) {
    return;
  }
  // A copy within one texture keeps both sides in GENERAL.
  const bool same = source->texture == destination->texture;
  const VkImageLayout srcLayout = OpLayout(same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  const VkImageLayout dstLayout = OpLayout(same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  const uint32_t copyLayers = std::max(1u, copySize->depthOrArrayLayers);
  encoder->Use(source->texture, source->mipLevel, source->origin.z, copyLayers, srcLayout);
  encoder->Use(destination->texture, destination->mipLevel, destination->origin.z, copyLayers, dstLayout);
  encoder->Settle();
  const VkImageAspectFlags srcAspect =
      AspectFor(source->texture->info, source->aspect) & (source->texture->info.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
  const VkImageAspectFlags dstAspect = AspectFor(destination->texture->info, destination->aspect) &
                                       (destination->texture->info.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
  const uint32_t layers = std::max(1u, copySize->depthOrArrayLayers);
  const VkImageCopy region{
      .srcSubresource = {srcAspect, source->mipLevel, source->origin.z, layers},
      .srcOffset = {static_cast<int32_t>(source->origin.x), static_cast<int32_t>(source->origin.y), 0},
      .dstSubresource = {dstAspect, destination->mipLevel, destination->origin.z, layers},
      .dstOffset = {static_cast<int32_t>(destination->origin.x), static_cast<int32_t>(destination->origin.y), 0},
      .extent = {copySize->width, copySize->height, 1},
  };
  vkCmdCopyImage(encoder->slot.cb, source->texture->image, srcLayout, destination->texture->image, dstLayout, 1,
                 &region);
  encoder->needBarrier = true;
  encoder->Ref(source->texture);
  encoder->Ref(destination->texture);
}

WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder encoder,
                                                        const WGPURenderPassDescriptor* descriptor) {
  const VkImageLayout colorLayout = OpLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  VkImageLayout depthLayout = VK_IMAGE_LAYOUT_GENERAL;
  if (const auto* d = descriptor->depthStencilAttachment) {
    const bool readOnly = d->depthReadOnly && (d->stencilReadOnly || !d->view->texture->info.stencil);
    depthLayout = OpLayout(readOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                    : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    encoder->Use(d->view->texture, d->view->baseMip, d->view->baseLayer, 1, depthLayout);
  }
  for (size_t i = 0; i < descriptor->colorAttachmentCount; ++i) {
    const auto& a = descriptor->colorAttachments[i];
    if (a.view != nullptr) {
      encoder->Use(a.view->texture, a.view->baseMip, a.view->baseLayer, 1, colorLayout);
      if (a.resolveTarget != nullptr) {
        encoder->Use(a.resolveTarget->texture, a.resolveTarget->baseMip, a.resolveTarget->baseLayer, 1, colorLayout);
      }
    }
  }
  // Everything else goes home: the pass may sample it.
  encoder->Settle();
  VkCommandBuffer cb = encoder->slot.cb;
  std::vector<VkRenderingAttachmentInfo> colors(descriptor->colorAttachmentCount);
  uint32_t width = 0;
  uint32_t height = 0;
  for (size_t i = 0; i < descriptor->colorAttachmentCount; ++i) {
    const auto& a = descriptor->colorAttachments[i];
    auto& out = colors[i];
    out = VkRenderingAttachmentInfo{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (a.view == nullptr) {
      continue;
    }
    encoder->Ref(a.view);
    width = a.view->width;
    height = a.view->height;
    out.imageView = a.view->view;
    out.imageLayout = colorLayout;
    if (a.resolveTarget != nullptr) {
      encoder->Ref(a.resolveTarget);
      out.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
      out.resolveImageView = a.resolveTarget->view;
      out.resolveImageLayout = colorLayout;
    }
    out.loadOp = a.loadOp == WGPULoadOp_Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    out.storeOp = a.storeOp == WGPUStoreOp_Discard ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    const FormatInfo& info = a.view->texture->info;
    if (info.sint) {
      out.clearValue.color.int32[0] = static_cast<int32_t>(a.clearValue.r);
      out.clearValue.color.int32[1] = static_cast<int32_t>(a.clearValue.g);
      out.clearValue.color.int32[2] = static_cast<int32_t>(a.clearValue.b);
      out.clearValue.color.int32[3] = static_cast<int32_t>(a.clearValue.a);
    } else if (info.uint) {
      out.clearValue.color.uint32[0] = static_cast<uint32_t>(a.clearValue.r);
      out.clearValue.color.uint32[1] = static_cast<uint32_t>(a.clearValue.g);
      out.clearValue.color.uint32[2] = static_cast<uint32_t>(a.clearValue.b);
      out.clearValue.color.uint32[3] = static_cast<uint32_t>(a.clearValue.a);
    } else {
      out.clearValue.color.float32[0] = static_cast<float>(a.clearValue.r);
      out.clearValue.color.float32[1] = static_cast<float>(a.clearValue.g);
      out.clearValue.color.float32[2] = static_cast<float>(a.clearValue.b);
      out.clearValue.color.float32[3] = static_cast<float>(a.clearValue.a);
      // AURORA_VK_DEBUG_CLEAR=2: every color clear is green, to see which passes reach the screen.
      static const bool greenClears = [] {
        const char* v = std::getenv("AURORA_VK_DEBUG_CLEAR");
        return v != nullptr && v[0] == '2';
      }();
      if (greenClears) {
        // Magenta on the swapchain: if it stays magenta, even the draw that copies the frame fails.
        out.clearValue.color = a.view->texture->swapchain ? VkClearColorValue{.float32 = {1.f, 0.f, 1.f, 1.f}}
                                                          : VkClearColorValue{.float32 = {0.f, 1.f, 0.f, 1.f}};
      }
    }
  }
  VkRenderingAttachmentInfo depth{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  VkRenderingAttachmentInfo stencil{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  bool hasDepth = false;
  bool hasStencil = false;
  if (const auto* d = descriptor->depthStencilAttachment) {
    encoder->Ref(d->view);
    width = d->view->width;
    height = d->view->height;
    const FormatInfo& info = d->view->texture->info;
    hasDepth = info.depth;
    hasStencil = info.stencil;
    depth.imageView = d->view->view;
    depth.imageLayout = depthLayout;
    depth.loadOp = d->depthLoadOp == WGPULoadOp_Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = d->depthReadOnly || d->depthStoreOp == WGPUStoreOp_Discard ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                                               : VK_ATTACHMENT_STORE_OP_STORE;
    if (d->depthReadOnly && g_device->hasStoreOpNone) {
      depth.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
    }
    depth.clearValue.depthStencil.depth = d->depthClearValue;
    stencil = depth;
    stencil.loadOp = d->stencilLoadOp == WGPULoadOp_Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    stencil.storeOp = d->stencilReadOnly && g_device->hasStoreOpNone ? VK_ATTACHMENT_STORE_OP_NONE
                      : d->stencilStoreOp == WGPUStoreOp_Discard ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                                  : VK_ATTACHMENT_STORE_OP_STORE;
    stencil.clearValue.depthStencil.stencil = d->stencilClearValue;
    // Mali corrupts a packed depth/stencil attachment whose two aspects load differently (ARMSX2,
    // from PPSSPP). Today's depth is Depth32Float with no stencil; keep a future packed one safe.
    if (hasDepth && hasStencil && stencil.loadOp != depth.loadOp) {
      stencil.loadOp = depth.loadOp;
      stencil.storeOp = depth.storeOp;
    }
  }
  const VkRenderingInfo info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, {width, height}},
      .layerCount = 1,
      .colorAttachmentCount = static_cast<uint32_t>(colors.size()),
      .pColorAttachments = colors.data(),
      .pDepthAttachment = hasDepth ? &depth : nullptr,
      .pStencilAttachment = hasStencil ? &stencil : nullptr,
  };
  const uint32_t startQuery = WriteTimestamp(encoder->slot, false, 2);
  if (encoder->dev->legacy) {
    // Tier 0: the same pass as a VkRenderPass, with a cached framebuffer. Attachments are listed
    // the way LegacyRenderPass numbers them: colours, then resolve targets, then depth/stencil.
    std::vector<VkImageView> views;
    std::vector<VkClearValue> clears;
    uint32_t fbWidth = UINT32_MAX;
    uint32_t fbHeight = UINT32_MAX;
    const auto attach = [&](WGPUTextureViewImpl* view, const VkClearValue& clear) {
      views.push_back(view->view);
      clears.push_back(clear);
      fbWidth = std::min(fbWidth, view->width);
      fbHeight = std::min(fbHeight, view->height);
    };
    std::vector<LegacyAttachment> legacyColors(descriptor->colorAttachmentCount);
    std::vector<LegacyAttachment> legacyResolves;
    for (size_t i = 0; i < descriptor->colorAttachmentCount; ++i) {
      const auto& a = descriptor->colorAttachments[i];
      if (a.view == nullptr) {
        continue;
      }
      LegacyAttachment& c = legacyColors[i];
      c.format = a.view->format;
      c.samples = static_cast<VkSampleCountFlagBits>(a.view->texture->samples);
      c.load = colors[i].loadOp;
      c.store = colors[i].storeOp;
      c.layout = colorLayout;
      attach(a.view, colors[i].clearValue);
    }
    for (size_t i = 0; i < descriptor->colorAttachmentCount; ++i) {
      const auto& a = descriptor->colorAttachments[i];
      if (a.view == nullptr || a.resolveTarget == nullptr) {
        continue;
      }
      legacyResolves.resize(descriptor->colorAttachmentCount);
      LegacyAttachment& r = legacyResolves[i];
      r.format = a.resolveTarget->format;
      r.samples = VK_SAMPLE_COUNT_1_BIT;
      r.load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      r.store = VK_ATTACHMENT_STORE_OP_STORE;
      r.layout = colorLayout;
      attach(a.resolveTarget, VkClearValue{});
    }
    LegacyAttachment legacyDepth;
    if (hasDepth || hasStencil) {
      const auto* d = descriptor->depthStencilAttachment;
      legacyDepth.format = d->view->format;
      legacyDepth.samples = static_cast<VkSampleCountFlagBits>(d->view->texture->samples);
      legacyDepth.load = hasDepth ? depth.loadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      legacyDepth.store = hasDepth ? depth.storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE;
      legacyDepth.stencilLoad = hasStencil ? stencil.loadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      legacyDepth.stencilStore = hasStencil ? stencil.storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE;
      legacyDepth.layout = depthLayout;
      VkClearValue clear{};
      clear.depthStencil.depth = depth.clearValue.depthStencil.depth;
      clear.depthStencil.stencil = stencil.clearValue.depthStencil.stencil;
      attach(d->view, clear);
    }
    if (views.empty()) {
      fbWidth = std::max(1u, width);
      fbHeight = std::max(1u, height);
    }
    // PowerVR Rogue (clearByAttachments): CLEAR becomes LOAD here and the clear runs right after
    // the pass begins, so every pass on these attachments has the same load ops.
    std::vector<VkClearAttachment> lateClears;
    if (encoder->dev->clearByAttachments) {
      for (size_t i = 0; i < legacyColors.size(); ++i) {
        LegacyAttachment& c = legacyColors[i];
        if (c.format != VK_FORMAT_UNDEFINED && c.load == VK_ATTACHMENT_LOAD_OP_CLEAR) {
          c.load = VK_ATTACHMENT_LOAD_OP_LOAD;
          lateClears.push_back(VkClearAttachment{VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i),
                                                 colors[i].clearValue});
        }
      }
      VkImageAspectFlags aspects = 0;
      if (legacyDepth.format != VK_FORMAT_UNDEFINED && legacyDepth.load == VK_ATTACHMENT_LOAD_OP_CLEAR) {
        legacyDepth.load = VK_ATTACHMENT_LOAD_OP_LOAD;
        aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
      }
      if (legacyDepth.format != VK_FORMAT_UNDEFINED && legacyDepth.stencilLoad == VK_ATTACHMENT_LOAD_OP_CLEAR) {
        legacyDepth.stencilLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
        aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
      }
      if (aspects != 0) {
        VkClearValue clear{};
        clear.depthStencil.depth = depth.clearValue.depthStencil.depth;
        clear.depthStencil.stencil = stencil.clearValue.depthStencil.stencil;
        lateClears.push_back(VkClearAttachment{aspects, 0, clear});
      }
    }
    const VkRenderPass renderPass = LegacyRenderPass(encoder->dev, legacyColors, legacyResolves, legacyDepth);
    const VkFramebuffer fb = GetCachedFramebuffer(encoder->dev, renderPass, views, fbWidth, fbHeight);
    const VkRenderPassBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass,
        .framebuffer = fb,
        .renderArea = {{0, 0}, {fbWidth, fbHeight}},
        .clearValueCount = static_cast<uint32_t>(clears.size()),
        .pClearValues = clears.empty() ? nullptr : clears.data(),
    };
    vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
    if (!lateClears.empty()) {
      const VkClearRect rect{.rect = {{0, 0}, {fbWidth, fbHeight}}, .baseArrayLayer = 0, .layerCount = 1};
      vkCmdClearAttachments(cb, static_cast<uint32_t>(lateClears.size()), lateClears.data(), 1, &rect);
    }
  } else {
    vkCmdBeginRendering(cb, &info);
  }
  // WebGPU's defaults: the whole target, y up.
  const VkViewport viewport{0.0f, static_cast<float>(height), static_cast<float>(width), -static_cast<float>(height),
                            0.0f, 1.0f};
  vkCmdSetViewport(cb, 0, 1, &viewport);
  const VkRect2D scissor{{0, 0}, {width, height}};
  vkCmdSetScissor(cb, 0, 1, &scissor);
  const float blend[4] = {0, 0, 0, 0};
  vkCmdSetBlendConstants(cb, blend);
  vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, 0);

  auto* pass = new WGPURenderPassEncoderImpl;
  pass->encoder = encoder;
  encoder->AddRef();
  pass->cb = cb;
  pass->timed = {startQuery, 0, width, height};
  return pass;
}

void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder pass) { pass->Release(); }

void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder pass) {
  if (pass->encoder->dev->legacy) {
    vkCmdEndRenderPass(pass->cb);
  } else {
    vkCmdEndRendering(pass->cb);
  }
  if (pass->timed.query != UINT32_MAX) {
    WriteTimestamp(pass->encoder->slot, true);
    pass->encoder->slot.timing->passes.push_back(pass->timed);
  }
  pass->encoder->needBarrier = true;
}

void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline) {
  pass->pipeline = pipeline;
  pass->encoder->Ref(pipeline);
  if (pipeline->pipeline != VK_NULL_HANDLE) {
    vkCmdBindPipeline(pass->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
  }
}

void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder pass, uint32_t groupIndex, WGPUBindGroup group,
                                       size_t dynamicOffsetCount, const uint32_t* dynamicOffsets) {
  pass->encoder->Ref(group);
  pass->binds.Set(groupIndex, group, dynamicOffsetCount, dynamicOffsets);
}

void wgpuRenderPassEncoderSetVertexBuffer(WGPURenderPassEncoder pass, uint32_t slot, WGPUBuffer buffer,
                                          uint64_t offset, uint64_t) {
  if (buffer == nullptr) {
    return;
  }
  pass->encoder->Ref(buffer);
  vkCmdBindVertexBuffers(pass->cb, slot, 1, &buffer->buffer, &offset);
}

void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder pass, WGPUBuffer buffer, WGPUIndexFormat format,
                                         uint64_t offset, uint64_t) {
  pass->encoder->Ref(buffer);
  vkCmdBindIndexBuffer(pass->cb, buffer->buffer, offset,
                       format == WGPUIndexFormat_Uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

void wgpuRenderPassEncoderSetViewport(WGPURenderPassEncoder pass, float x, float y, float width, float height,
                                      float minDepth, float maxDepth) {
  VkViewport viewport{x, y + height, width, -height, minDepth, maxDepth};
  if (width == 0.0f) {
    viewport = VkViewport{0.0f, y, 1.0f, 0.0f, minDepth, maxDepth};
  }
  vkCmdSetViewport(pass->cb, 0, 1, &viewport);
}

void wgpuRenderPassEncoderSetScissorRect(WGPURenderPassEncoder pass, uint32_t x, uint32_t y, uint32_t width,
                                         uint32_t height) {
  const VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height}};
  vkCmdSetScissor(pass->cb, 0, 1, &scissor);
}

void wgpuRenderPassEncoderSetBlendConstant(WGPURenderPassEncoder pass, const WGPUColor* color) {
  const float constants[4] = {static_cast<float>(color->r), static_cast<float>(color->g), static_cast<float>(color->b),
                              static_cast<float>(color->a)};
  vkCmdSetBlendConstants(pass->cb, constants);
}

void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder pass, uint32_t vertexCount, uint32_t instanceCount,
                               uint32_t firstVertex, uint32_t firstInstance) {
  if (pass->pipeline == nullptr || pass->pipeline->pipeline == VK_NULL_HANDLE) {
    return;
  }
  pass->binds.Flush(pass->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->pipeline->layout);
  vkCmdDraw(pass->cb, vertexCount, instanceCount, firstVertex, firstInstance);
  ++pass->timed.draws;
  TimeDraw(pass, vertexCount, instanceCount);
}

void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder pass, uint32_t indexCount, uint32_t instanceCount,
                                      uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) {
  if (pass->pipeline == nullptr || pass->pipeline->pipeline == VK_NULL_HANDLE) {
    return;
  }
  pass->binds.Flush(pass->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->pipeline->layout);
  vkCmdDrawIndexed(pass->cb, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
  ++pass->timed.draws;
  TimeDraw(pass, indexCount, instanceCount);
}

void wgpuRenderPassEncoderPushDebugGroup(WGPURenderPassEncoder, WGPUStringView) {}
void wgpuRenderPassEncoderPopDebugGroup(WGPURenderPassEncoder) {}

WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder encoder, const WGPUComputePassDescriptor*) {
  encoder->Settle();
  auto* pass = new WGPUComputePassEncoderImpl;
  pass->encoder = encoder;
  encoder->AddRef();
  pass->cb = encoder->slot.cb;
  return pass;
}

void wgpuComputePassEncoderRelease(WGPUComputePassEncoder pass) { pass->Release(); }

void wgpuComputePassEncoderSetPipeline(WGPUComputePassEncoder pass, WGPUComputePipeline pipeline) {
  pass->pipeline = pipeline;
  pass->encoder->Ref(pipeline);
  if (pipeline->pipeline != VK_NULL_HANDLE) {
    vkCmdBindPipeline(pass->cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
  }
}

void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder pass, uint32_t groupIndex, WGPUBindGroup group,
                                        size_t dynamicOffsetCount, const uint32_t* dynamicOffsets) {
  pass->encoder->Ref(group);
  pass->binds.Set(groupIndex, group, dynamicOffsetCount, dynamicOffsets);
}

void wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder pass, uint32_t x, uint32_t y, uint32_t z) {
  if (pass->pipeline == nullptr || pass->pipeline->pipeline == VK_NULL_HANDLE) {
    return;
  }
  pass->binds.Flush(pass->cb, VK_PIPELINE_BIND_POINT_COMPUTE, pass->pipeline->layout);
  vkCmdDispatch(pass->cb, x, y, z);
  // Dispatches in one pass may depend on each other's writes.
  GlobalBarrier(pass->cb);
}

void wgpuComputePassEncoderEnd(WGPUComputePassEncoder pass) { pass->encoder->needBarrier = true; }

void wgpuQuerySetRelease(WGPUQuerySet querySet) { querySet->Release(); }

// --- queue ---

void wgpuQueueSubmit(WGPUQueue queue, size_t commandCount, const WGPUCommandBuffer* commands) {
  WGPUDeviceImpl* dev = queue->dev;
  InFlight entry;
  std::vector<VkCommandBufferSubmitInfo> cbs;
  {
    std::lock_guard lock(dev->queueMutex);
    if (dev->uploadOpen) {
      EndSlot(dev->upload);
      cbs.push_back(VkCommandBufferSubmitInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                              .commandBuffer = dev->upload.cb});
      entry.slots.push_back(dev->upload);
      entry.chunks = std::move(dev->uploadChunks);
      entry.refs = std::move(dev->uploadRefs);
      dev->uploadChunks.clear();
      dev->uploadRefs.clear();
      dev->uploadOpen = false;
    }
    for (size_t i = 0; i < commandCount; ++i) {
      WGPUCommandBufferImpl* cmd = commands[i];
      cmd->submitted = true;
      cbs.push_back(VkCommandBufferSubmitInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                              .commandBuffer = cmd->slot.cb});
      entry.slots.push_back(cmd->slot);
      entry.refs.insert(entry.refs.end(), cmd->refs.begin(), cmd->refs.end());
      cmd->refs.clear();
    }
    const uint64_t serial = ++dev->submittedSerial;
    entry.serial = serial;
    bool touchesSwapchain = false;
    for (size_t i = 0; i < commandCount; ++i) {
      touchesSwapchain |= commands[i]->touchesSwapchain;
      for (WGPUBufferImpl* buffer : commands[i]->mapTracked) {
        buffer->lastUseSerial.store(serial, std::memory_order_release);
      }
    }
    const VkSemaphore acquire = TakeAcquire(dev, touchesSwapchain, serial);
    VkSemaphore presentSem = VK_NULL_HANDLE;
    if (touchesSwapchain && dev->pendingPresent != VK_NULL_HANDLE) {
      presentSem = dev->pendingPresent;
      dev->pendingPresent = VK_NULL_HANDLE;
      dev->presentSignaled = true;
    }
    std::vector<VkSemaphoreSubmitInfo> signals;
    signals.reserve(2);
    if (!dev->legacy) {
      signals.push_back(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = dev->timeline,
          .value = serial,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      });
    }
    if (presentSem != VK_NULL_HANDLE) {
      signals.push_back(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = presentSem,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      });
    }
    const VkSemaphoreSubmitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = acquire,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    // Tier 0 has no timeline semaphore: the serial's fence stands in for the signal.
    const VkFence fence = NewSerialFence(dev, serial);
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = acquire != VK_NULL_HANDLE ? 1u : 0u,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = static_cast<uint32_t>(cbs.size()),
        .pCommandBufferInfos = cbs.data(),
        .signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size()),
        .pSignalSemaphoreInfos = signals.empty() ? nullptr : signals.data(),
    };
    VK_CHECK(vkQueueSubmit2(dev->queue, 1, &submit, fence));
    std::lock_guard inflightLock(dev->inflightMutex);
    dev->inflight.push_back(std::move(entry));
  }
  dev->futureCv.notify_all();
}

WGPUFuture wgpuQueueOnSubmittedWorkDone(WGPUQueue queue, WGPUQueueWorkDoneCallbackInfo callbackInfo) {
  WGPUDeviceImpl* dev = queue->dev;
  uint64_t serial;
  {
    std::lock_guard lock(dev->queueMutex);
    serial = dev->submittedSerial;
  }
  const uint64_t id = AddFuture(dev, serial, callbackInfo.mode, [callbackInfo] {
    callbackInfo.callback(WGPUQueueWorkDoneStatus_Success, MakeView(""), callbackInfo.userdata1, callbackInfo.userdata2);
  });
  return WGPUFuture{id};
}

void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t bufferOffset, const void* data, size_t size) {
  if (size == 0) {
    return;
  }
  WGPUDeviceImpl* dev = queue->dev;
  std::lock_guard lock(dev->queueMutex);
  const auto [chunk, offset] = AllocStaging(dev, size, 16);
  std::memcpy(chunk->mapped + offset, data, size);
  const VkBufferCopy region{offset, bufferOffset, size};
  vkCmdCopyBuffer(UploadCb(dev), chunk->buffer, buffer->buffer, 1, &region);
  TransferBarrier(dev->upload.cb);
  buffer->AddRef();
  dev->uploadRefs.push_back(buffer);
}

void wgpuQueueWriteTexture(WGPUQueue queue, const WGPUTexelCopyTextureInfo* destination, const void* data,
                           size_t dataSize, const WGPUTexelCopyBufferLayout* dataLayout, const WGPUExtent3D* writeSize) {
  if (writeSize->width == 0 || writeSize->height == 0 || dataSize == 0) {
    return;
  }
  WGPUDeviceImpl* dev = queue->dev;
  WGPUTextureImpl* tex = destination->texture;
  const FormatInfo& info = tex->info;
  // Copy just the rows the write covers, tightly packed, into staging.
  const uint32_t blocksWide = (writeSize->width + info.blockWidth - 1) / info.blockWidth;
  const uint32_t blocksHigh = (writeSize->height + info.blockHeight - 1) / info.blockHeight;
  const uint32_t layers = std::max(1u, writeSize->depthOrArrayLayers);
  const uint64_t rowBytes = static_cast<uint64_t>(blocksWide) * info.blockBytes;
  const uint64_t srcRowPitch = dataLayout->bytesPerRow == WGPU_COPY_STRIDE_UNDEFINED ? rowBytes : dataLayout->bytesPerRow;
  const uint64_t srcRowsPerImage =
      dataLayout->rowsPerImage == WGPU_COPY_STRIDE_UNDEFINED ? blocksHigh : dataLayout->rowsPerImage;
  const uint64_t total = rowBytes * blocksHigh * layers;
  std::lock_guard lock(dev->queueMutex);
  const auto [chunk, offset] = AllocStaging(dev, total, std::max<uint64_t>(info.blockBytes, 16));
  const auto* src = static_cast<const uint8_t*>(data) + dataLayout->offset;
  uint8_t* dst = chunk->mapped + offset;
  for (uint32_t layer = 0; layer < layers; ++layer) {
    for (uint32_t row = 0; row < blocksHigh; ++row) {
      std::memcpy(dst + (layer * blocksHigh + row) * rowBytes, src + (layer * srcRowsPerImage + row) * srcRowPitch,
                  rowBytes);
    }
  }
  const VkBufferImageCopy region{
      .bufferOffset = offset,
      .imageSubresource = {AspectFor(info, destination->aspect) & (info.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
                           destination->mipLevel, destination->origin.z, layers},
      .imageOffset = {static_cast<int32_t>(destination->origin.x), static_cast<int32_t>(destination->origin.y), 0},
      .imageExtent = {writeSize->width, writeSize->height, 1},
  };
  VkCommandBuffer cb = UploadCb(dev);
  if (tex->home == VK_IMAGE_LAYOUT_GENERAL) {
    vkCmdCopyBufferToImage(cb, chunk->buffer, tex->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    TransferBarrier(cb);
  } else {
    ImageLayoutBarrier2(cb, LayoutTransition(tex->image, tex->aspects, destination->mipLevel, 1, destination->origin.z,
                                             layers, tex->home, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
    vkCmdCopyBufferToImage(cb, chunk->buffer, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    // Back home and visible to anything later, as TransferBarrier does for the GENERAL path.
    VkImageMemoryBarrier2 back = LayoutTransition(tex->image, tex->aspects, destination->mipLevel, 1,
                                                  destination->origin.z, layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                  tex->home);
    back.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    back.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    ImageLayoutBarrier2(cb, back);
  }
  tex->AddRef();
  dev->uploadRefs.push_back(tex);
}

// --- surface ---

WGPUStatus wgpuSurfaceGetCapabilities(WGPUSurface surface, WGPUAdapter, WGPUSurfaceCapabilities* caps) {
  WGPUDeviceImpl* dev = g_device;
  if (dev == nullptr) {
    return WGPUStatus_Error;
  }
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(dev->phys, surface->surface, &count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(dev->phys, surface->surface, &count, formats.data());
  std::vector<WGPUTextureFormat> outFormats;
  for (const auto& f : formats) {
    const WGPUTextureFormat format = FromVkFormat(f.format);
    if (format != WGPUTextureFormat_Undefined && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
        std::find(outFormats.begin(), outFormats.end(), format) == outFormats.end()) {
      outFormats.push_back(format);
    }
  }
  uint32_t modeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(dev->phys, surface->surface, &modeCount, nullptr);
  std::vector<VkPresentModeKHR> modes(modeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(dev->phys, surface->surface, &modeCount, modes.data());
  std::vector<WGPUPresentMode> outModes;
  for (VkPresentModeKHR mode : modes) {
    switch (mode) {
    case VK_PRESENT_MODE_FIFO_KHR:
      outModes.push_back(WGPUPresentMode_Fifo);
      break;
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
      outModes.push_back(WGPUPresentMode_FifoRelaxed);
      break;
    case VK_PRESENT_MODE_MAILBOX_KHR:
      outModes.push_back(WGPUPresentMode_Mailbox);
      break;
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
      outModes.push_back(WGPUPresentMode_Immediate);
      break;
    default:
      break;
    }
  }
  auto* f = new WGPUTextureFormat[outFormats.size()];
  std::copy(outFormats.begin(), outFormats.end(), f);
  auto* m = new WGPUPresentMode[outModes.size()];
  std::copy(outModes.begin(), outModes.end(), m);
  auto* a = new WGPUCompositeAlphaMode[1]{WGPUCompositeAlphaMode_Opaque};
  caps->usages = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
  caps->formatCount = outFormats.size();
  caps->formats = f;
  caps->presentModeCount = outModes.size();
  caps->presentModes = m;
  caps->alphaModeCount = 1;
  caps->alphaModes = a;
  return WGPUStatus_Success;
}

void wgpuSurfaceCapabilitiesFreeMembers(WGPUSurfaceCapabilities caps) {
  delete[] caps.formats;
  delete[] caps.presentModes;
  delete[] caps.alphaModes;
}

void wgpuSurfaceAddRef(WGPUSurface surface) { surface->AddRef(); }
void wgpuSurfaceRelease(WGPUSurface surface) { surface->Release(); }

void wgpuSurfaceConfigure(WGPUSurface surface, const WGPUSurfaceConfiguration* config) {
  WGPUDeviceImpl* dev = config->device;
  surface->dev = dev;
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->phys, surface->surface, &caps);
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  switch (config->presentMode) {
  case WGPUPresentMode_Mailbox:
    mode = VK_PRESENT_MODE_MAILBOX_KHR;
    break;
  case WGPUPresentMode_Immediate:
    mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    break;
  case WGPUPresentMode_FifoRelaxed:
    mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    break;
  default:
    break;
  }
  uint32_t imageCount = std::max(caps.minImageCount + 1, 3u);
  if (caps.maxImageCount != 0) {
    imageCount = std::min(imageCount, caps.maxImageCount);
  }
  VkExtent2D extent{config->width, config->height};
  if (caps.currentExtent.width != UINT32_MAX) {
    extent = caps.currentExtent;
  }
  // Android reports a landscape window on a portrait panel as a 90/270 degree currentTransform.
  // Rendering is never pre-rotated here, so ask for the identity transform and the window's own size
  // (what aurora renders for); the compositor rotates. currentExtent can't be used for that: drivers
  // disagree on whether it is in the panel's orientation or the window's.
  //
  // Pre-rotation (default on): leaving the rotation to the compositor makes many phones compose the
  // game layer on the GPU every frame (Redmi Note 12 Pro: SurfaceFlinger "CLIENT, ROT_90"). Instead
  // the swapchain takes the panel's orientation and aurora's final copy pass draws the picture
  // rotated (aurora_vk_surface_rotation). AURORA_VK_PRE_ROTATE=0 keeps the old behaviour.
  static const bool preRotate = [] {
    const char* env = std::getenv("AURORA_VK_PRE_ROTATE");
    return env == nullptr || env[0] != '0';
  }();
  int rotation = 0;
  switch (caps.currentTransform) {
  case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
    rotation = 90;
    break;
  case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
    rotation = 180;
    break;
  case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
    rotation = 270;
    break;
  default:
    break;
  }
  VkSurfaceTransformFlagBitsKHR transform = caps.currentTransform;
  bool preRotated = false;
  if (preRotate && rotation != 0 && (caps.supportedTransforms & caps.currentTransform) != 0 &&
      config->width != 0 && config->height != 0) {
    preRotated = true;
    extent = rotation == 180 ? VkExtent2D{config->width, config->height}
                             : VkExtent2D{config->height, config->width};
  } else if (caps.currentTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR &&
      (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0) {
    transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (config->width != 0 && config->height != 0) {
      extent = {config->width, config->height};
    }
  }
  surface->rotatedByCompositor = transform != caps.currentTransform;
  g_surfaceRotation.store(preRotated ? rotation : 0, std::memory_order_release);
  // PowerVR driver quirk (ARMSX2 GSGPUDriverProfile.cpp vk-powervr-old-swapchain-width): align
  // swapchain width to 32 pixels to avoid visual corruption on Imagination drivers.
  if (dev->props.vendorID == 0x1010 && extent.width >= 64) {
    extent.width &= ~31u;
  }
  extent.width = std::max(1u, extent.width);
  extent.height = std::max(1u, extent.height);
  VkSwapchainKHR old = surface->swapchain;
  if (old != VK_NULL_HANDLE) {
    {
      std::lock_guard lock(dev->queueMutex);
      vkQueueWaitIdle(dev->queue);
      dev->pendingAcquire = VK_NULL_HANDLE;
      dev->pendingAcquireSerial = nullptr;
      dev->pendingPresent = VK_NULL_HANDLE;
      dev->presentSignaled = false;
    }
    Retire(dev);
    for (auto* image : surface->images) {
      if (image->defaultView != VK_NULL_HANDLE) {
        DestroyViewFramebuffers(dev, image->defaultView);
        vkDestroyImageView(dev->device, image->defaultView, nullptr);
        image->defaultView = VK_NULL_HANDLE;
      }
      image->image = VK_NULL_HANDLE;
      image->Release();
    }
    surface->images.clear();
    for (VkSemaphore sem : surface->acquireSems) {
      vkDestroySemaphore(dev->device, sem, nullptr);
    }
    for (VkSemaphore sem : surface->presentSems) {
      vkDestroySemaphore(dev->device, sem, nullptr);
    }
    surface->acquireSems.clear();
    surface->acquireSemSerial.clear();
    surface->presentSems.clear();
  }
  surface->wgpuFormat = config->format;
  surface->format = GetFormat(config->format).vk;
  VkSwapchainCreateInfoKHR info{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface->surface,
      .minImageCount = imageCount,
      .imageFormat = surface->format,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = transform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = mode,
      .clipped = VK_TRUE,
      .oldSwapchain = old,
  };
  surface->swapchain = VK_NULL_HANDLE;
  VkResult created = vkCreateSwapchainKHR(dev->device, &info, nullptr, &surface->swapchain);
  // The old swapchain is retired even when creation fails.
  if (old != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(dev->device, old, nullptr);
  }
  // Stock Adreno drivers refuse a new swapchain on the same ANativeWindow while the old one is
  // still attached (ARMSX2 #374/#380): with the old one destroyed above, try once more without it.
  if (created == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR && old != VK_NULL_HANDLE) {
    Log("swapchain: native window in use, retrying without the old swapchain");
    info.oldSwapchain = VK_NULL_HANDLE;
    surface->swapchain = VK_NULL_HANDLE;
    created = vkCreateSwapchainKHR(dev->device, &info, nullptr, &surface->swapchain);
  }
  if (created != VK_SUCCESS) {
    // No swapchain: acquire reports Error (or Lost) and aurora reconfigures on a later frame,
    // instead of aborting while the window is being torn down or handed back.
    static int s_failures = 0;
    if (++s_failures <= 8 || s_failures % 120 == 0) {
      Log("swapchain: create failed (VkResult %d, %ux%u), will retry (%d)", static_cast<int>(created), extent.width,
          extent.height, s_failures);
    }
    surface->swapchain = VK_NULL_HANDLE;
    surface->surfaceLost = created == VK_ERROR_SURFACE_LOST_KHR;
    surface->width = 0;
    surface->height = 0;
    return;
  }
  surface->surfaceLost = false;
  surface->width = extent.width;
  surface->height = extent.height;
  uint32_t count = 0;
  vkGetSwapchainImagesKHR(dev->device, surface->swapchain, &count, nullptr);
  std::vector<VkImage> images(count);
  vkGetSwapchainImagesKHR(dev->device, surface->swapchain, &count, images.data());
  const VkSemaphoreCreateInfo semInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (VkImage image : images) {
    auto* tex = new WGPUTextureImpl;
    tex->dev = dev;
    tex->image = image;
    tex->swapchain = true;
    tex->home = OpLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    tex->format = config->format;
    tex->info = GetFormat(config->format);
    tex->width = extent.width;
    tex->height = extent.height;
    surface->images.push_back(tex);
    VkSemaphore present;
    VK_CHECK(vkCreateSemaphore(dev->device, &semInfo, nullptr, &present));
    surface->presentSems.push_back(present);
  }
  for (uint32_t i = 0; i <= count; ++i) {
    VkSemaphore acquire;
    VK_CHECK(vkCreateSemaphore(dev->device, &semInfo, nullptr, &acquire));
    surface->acquireSems.push_back(acquire);
    surface->acquireSemSerial.push_back(0);
  }
  surface->nextAcquire = 0;
  surface->current = UINT32_MAX;
}

void wgpuSurfaceUnconfigure(WGPUSurface surface) {
  if (surface->dev != nullptr) {
    surface->DestroySwapchain();
  }
}

void wgpuSurfaceGetCurrentTexture(WGPUSurface surface, WGPUSurfaceTexture* out) {
  out->texture = nullptr;
  WGPUDeviceImpl* dev = surface->dev;
  if (dev == nullptr || surface->swapchain == VK_NULL_HANDLE) {
    out->status = surface->surfaceLost ? WGPUSurfaceGetCurrentTextureStatus_Lost
                                       : WGPUSurfaceGetCurrentTextureStatus_Error;
    return;
  }
  const uint32_t slot = surface->nextAcquire;
  // The semaphore must not still be waited on by an unfinished submission.
  if (const uint64_t serial = surface->acquireSemSerial[slot];
      serial != UINT64_MAX && serial > dev->completedSerial.load()) {
    WaitSerial(dev, serial, UINT64_MAX);
  }
  VkSemaphore sem = surface->acquireSems[slot];
  uint32_t index = 0;
  const VkResult result = vkAcquireNextImageKHR(dev->device, surface->swapchain, 1'000'000'000, sem, VK_NULL_HANDLE, &index);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    out->status = WGPUSurfaceGetCurrentTextureStatus_Outdated;
    return;
  }
  if (result == VK_TIMEOUT || result == VK_NOT_READY) {
    out->status = WGPUSurfaceGetCurrentTextureStatus_Timeout;
    return;
  }
  if (result == VK_ERROR_SURFACE_LOST_KHR) {
    out->status = WGPUSurfaceGetCurrentTextureStatus_Lost;
    return;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    out->status = WGPUSurfaceGetCurrentTextureStatus_Error;
    return;
  }
  static const bool debugClear = [] {
    const char* v = std::getenv("AURORA_VK_DEBUG_CLEAR");
    return v != nullptr && v[0] == '1';
  }();
  surface->nextAcquire = (slot + 1) % static_cast<uint32_t>(surface->acquireSems.size());
  {
    std::lock_guard lock(dev->queueMutex);
    dev->pendingAcquire = sem;
    // Until a submission waits on it, the semaphore must not be reused.
    surface->acquireSemSerial[slot] = UINT64_MAX;
    dev->pendingAcquireSerial = &surface->acquireSemSerial[slot];
    dev->pendingPresent = debugClear ? VK_NULL_HANDLE : surface->presentSems[index];
    dev->presentSignaled = false;
  }
  surface->current = index;
  WGPUTextureImpl* tex = surface->images[index];
  tex->layout = VK_IMAGE_LAYOUT_UNDEFINED;
  tex->AddRef();
  out->texture = tex;
  out->status = result == VK_SUBOPTIMAL_KHR && !surface->rotatedByCompositor
                    ? WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal
                    : WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
}

WGPUStatus wgpuSurfacePresent(WGPUSurface surface) {
  WGPUDeviceImpl* dev = surface->dev;
  if (dev == nullptr || surface->current == UINT32_MAX) {
    return WGPUStatus_Error;
  }
  const uint32_t index = surface->current;
  WGPUTextureImpl* tex = surface->images[index];
  // AURORA_VK_DEBUG_CLEAR=1 clears the left half of each presented image to magenta: magenta on screen
  // means presentation works and only the frame's content is missing.
  static const bool debugClear = [] {
    const char* v = std::getenv("AURORA_VK_DEBUG_CLEAR");
    return v != nullptr && v[0] == '1';
  }();
  {
    std::lock_guard lock(dev->queueMutex);
    if (dev->presentSignaled && !dev->uploadOpen && !debugClear && tex->layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
      dev->presentSignaled = false;
      const VkPresentInfoKHR present{
          .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &surface->presentSems[index],
          .swapchainCount = 1,
          .pSwapchains = &surface->swapchain,
          .pImageIndices = &index,
      };
      const VkResult result = vkQueuePresentKHR(dev->queue, &present);
      surface->current = UINT32_MAX;
      if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
        return WGPUStatus_Error;
      }
      return WGPUStatus_Success;
    }
    dev->pendingPresent = VK_NULL_HANDLE;
    dev->presentSignaled = false;
  }
  CmdSlot slot = AcquireSlot(dev);
  VkImageLayout from =
      tex->layout == tex->home ? tex->home : VK_IMAGE_LAYOUT_UNDEFINED;
  // One view per swapchain image, never destroyed (debug only).
  static std::unordered_map<VkImage, VkImageView> debugViews;
  VkImageView debugView = VK_NULL_HANDLE;
  if (debugClear && debugViews.count(tex->image) != 0) {
    debugView = debugViews[tex->image];
  } else if (debugClear) {
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = tex->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = surface->format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    if (vkCreateImageView(dev->device, &viewInfo, nullptr, &debugView) == VK_SUCCESS) {
      debugViews[tex->image] = debugView;
    }
  }
  // Dynamic rendering only: tier 0 skips the debug clear.
  if (debugView != VK_NULL_HANDLE && !dev->legacy) {
    {
      ImageLayoutBarrier(slot.cb, tex->image, VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_GENERAL);
      from = VK_IMAGE_LAYOUT_GENERAL;
      const VkRenderingAttachmentInfo color{
          .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
          .imageView = debugView,
          .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .clearValue = {.color = {.float32 = {1.f, 0.f, 1.f, 1.f}}},
      };
      const VkRenderingInfo info{
          .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
          .renderArea = {{0, 0}, {std::max(1u, tex->width / 2), tex->height}},
          .layerCount = 1,
          .colorAttachmentCount = 1,
          .pColorAttachments = &color,
      };
      vkCmdBeginRendering(slot.cb, &info);
      vkCmdEndRendering(slot.cb);
    }
  }
  ImageLayoutBarrier(slot.cb, tex->image, VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  if (slot.timing != nullptr) {
    slot.timing->present = true;
  }
  EndSlot(slot);
  tex->layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkResult result;
  {
    std::lock_guard lock(dev->queueMutex);
    InFlight entry;
    std::vector<VkCommandBufferSubmitInfo> cbs;
    if (dev->uploadOpen) {
      EndSlot(dev->upload);
      cbs.push_back(VkCommandBufferSubmitInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                              .commandBuffer = dev->upload.cb});
      entry.slots.push_back(dev->upload);
      entry.chunks = std::move(dev->uploadChunks);
      entry.refs = std::move(dev->uploadRefs);
      dev->uploadChunks.clear();
      dev->uploadRefs.clear();
      dev->uploadOpen = false;
    }
    cbs.push_back(VkCommandBufferSubmitInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                            .commandBuffer = slot.cb});
    entry.slots.push_back(slot);
    const uint64_t serial = ++dev->submittedSerial;
    entry.serial = serial;
    const std::array<VkSemaphoreSubmitInfo, 2> signals{{
        {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = dev->timeline,
         .value = serial,
         .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT},
        {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = surface->presentSems[index],
         .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT},
    }};
    const VkSemaphore acquire = TakeAcquire(dev, true, serial);
    const VkSemaphoreSubmitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = acquire,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    // Tier 0 has no timeline semaphore: only the present semaphore is signalled, and the serial's
    // fence stands in for the timeline value.
    const VkFence fence = NewSerialFence(dev, serial);
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = acquire != VK_NULL_HANDLE ? 1u : 0u,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = static_cast<uint32_t>(cbs.size()),
        .pCommandBufferInfos = cbs.data(),
        .signalSemaphoreInfoCount = dev->legacy ? 1u : static_cast<uint32_t>(signals.size()),
        .pSignalSemaphoreInfos = dev->legacy ? &signals[1] : signals.data(),
    };
    VK_CHECK(vkQueueSubmit2(dev->queue, 1, &submit, fence));
    {
      std::lock_guard inflightLock(dev->inflightMutex);
      dev->inflight.push_back(std::move(entry));
    }
    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &surface->presentSems[index],
        .swapchainCount = 1,
        .pSwapchains = &surface->swapchain,
        .pImageIndices = &index,
    };
    result = vkQueuePresentKHR(dev->queue, &present);
  }
  dev->futureCv.notify_all();
  surface->current = UINT32_MAX;
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
    return WGPUStatus_Error;
  }
  return WGPUStatus_Success;
}

// Degrees (0/90/180/270) aurora's final copy pass must rotate the picture by: the swapchain is in
// the panel's orientation (see wgpuSurfaceConfigure). 0 when the compositor rotates.
int aurora_vk_surface_rotation() { return g_surfaceRotation.load(std::memory_order_acquire); }

void aurora_vk_flush_buffer_range(WGPUBuffer buffer, uint64_t offset, uint64_t size) {
  if (buffer == nullptr || buffer->dev == nullptr || buffer->alloc == nullptr || size == 0) {
    return;
  }
  vmaFlushAllocation(buffer->dev->vma, buffer->alloc, offset, size);
}

uint64_t aurora_vk_submitted_serial() {
  WGPUDeviceImpl* dev = g_device;
  return dev != nullptr ? dev->submittedSerial.load(std::memory_order_acquire) : 0;
}

uint64_t aurora_vk_poll_completed() {
  WGPUDeviceImpl* dev = g_device;
  return dev != nullptr ? KnownCompleted(dev) : 0;
}

void aurora_vk_wait_serial(uint64_t serial) {
  WGPUDeviceImpl* dev = g_device;
  if (dev == nullptr || serial == 0) {
    return;
  }
  WaitSerial(dev, serial, UINT64_MAX);
  Retire(dev, false);
}

// Adapted from Dolphin VKGfx.cpp ClearRegion (GPL-2.0-or-later): clear color and/or depth attachments
// inside an active render pass without disturbing bound pipeline, descriptor sets, viewport, or scissor.
void aurora_vk_clear_attachments(WGPURenderPassEncoder pass, bool clearColor, WGPUColor color, bool clearDepth,
                                 float depth, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  if (pass == nullptr || pass->cb == VK_NULL_HANDLE || width == 0 || height == 0) {
    return;
  }
  std::array<VkClearAttachment, 2> attachments{};
  uint32_t attachmentCount = 0;
  if (clearColor) {
    VkClearAttachment& a = attachments[attachmentCount++];
    a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    a.colorAttachment = 0;
    a.clearValue.color.float32[0] = static_cast<float>(color.r);
    a.clearValue.color.float32[1] = static_cast<float>(color.g);
    a.clearValue.color.float32[2] = static_cast<float>(color.b);
    a.clearValue.color.float32[3] = static_cast<float>(color.a);
  }
  if (clearDepth) {
    VkClearAttachment& a = attachments[attachmentCount++];
    a.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    a.clearValue.depthStencil.depth = depth;
  }
  if (attachmentCount == 0) {
    return;
  }
  const VkClearRect rect{
      .rect = {{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height}},
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
  vkCmdClearAttachments(pass->cb, attachmentCount, attachments.data(), 1, &rect);
}

} // extern "C"
