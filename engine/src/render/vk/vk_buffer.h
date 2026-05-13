#pragma once
#include "vk_common.h"

namespace sol::vk {
class VkContext;

struct VulkanBuffer {
    VkBuffer      handle    = VK_NULL_HANDLE;
    VmaAllocation alloc     = VK_NULL_HANDLE;
    VkDeviceSize  size      = 0;

    bool valid() const { return handle != VK_NULL_HANDLE; }

    // Directly mapped (host-visible) persistent pointer, or nullptr for device-local.
    void* mapped = nullptr;

    void destroy(VmaAllocator alloc_) {
        if (handle) { vmaDestroyBuffer(alloc_, handle, alloc); handle = VK_NULL_HANDLE; alloc = VK_NULL_HANDLE; }
    }
};

// Allocate a device-local GPU buffer (vertex, index, UBO) and optionally fill via staging.
VulkanBuffer create_gpu_buffer(VkContext& ctx,
                               VkDeviceSize size,
                               VkBufferUsageFlags usage,
                               const void* initial_data = nullptr);

// Allocate a persistently-mapped host-visible buffer (for per-frame uniform uploads).
VulkanBuffer create_staging_buffer(VmaAllocator allocator, VkDeviceSize size);

// Allocate a persistently-mapped host-visible buffer with custom usage.
VulkanBuffer create_host_buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage);

} // namespace sol::vk
