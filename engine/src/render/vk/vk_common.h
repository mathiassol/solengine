#pragma once
// Common Vulkan includes for all vk/ translation units.
// VK_NO_PROTOTYPES is set globally so we use Volk's dynamic dispatch.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <volk.h>
#include <vk_mem_alloc.h>

#include <sol/log.h>
#include <stdexcept>
#include <string>

namespace sol::vk {

// Hard-abort on a failed Vulkan call.  Use VK_CHECK for internal operations.
inline void vk_check(VkResult r, const char* expr, const char* file, int line) {
    if (r == VK_SUCCESS) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "VkResult %d at %s:%d  %s", (int)r, file, line, expr);
    SOL_ERROR(buf);
    throw std::runtime_error(buf);
}

#define VK_CHECK(expr) ::sol::vk::vk_check((expr), #expr, __FILE__, __LINE__)

// Simple helper: set a VkDebugUtilsObjectNameInfoEXT if validation layers are on
#ifndef NDEBUG
#  define VK_NAME(device, type, handle, name) \
    do { if (vkSetDebugUtilsObjectNameEXT) { \
        VkDebugUtilsObjectNameInfoEXT ni{}; \
        ni.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT; \
        ni.objectType = (type); ni.objectHandle = (uint64_t)(handle); \
        ni.pObjectName = (name); \
        vkSetDebugUtilsObjectNameEXT((device), &ni); } } while(0)
#else
#  define VK_NAME(device, type, handle, name) do {} while(0)
#endif

} // namespace sol::vk
