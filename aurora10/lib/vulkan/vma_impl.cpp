#define VMA_IMPLEMENTATION
#if defined(__ANDROID__)
// Android's libvulkan exports only Vulkan 1.0/1.1 entry points; look the rest up at runtime.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#else
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#endif
#include <vk_mem_alloc.h>
