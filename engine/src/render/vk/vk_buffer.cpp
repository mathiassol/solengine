#include "vk_buffer.h"
#include "vk_context.h"
#include <cstring>

namespace sol::vk {

VulkanBuffer create_gpu_buffer(VkContext& ctx,
                               VkDeviceSize size,
                               VkBufferUsageFlags usage,
                               const void* initial_data)
{
    VulkanBuffer buf;
    buf.size = size;

    if (initial_data) {
        // Upload path: staging → device-local
        VulkanBuffer staging = create_staging_buffer(ctx.allocator(), size);
        std::memcpy(staging.mapped, initial_data, (size_t)size);

        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size  = size;
        bci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateBuffer(ctx.allocator(), &bci, &ai, &buf.handle, &buf.alloc, nullptr));

        // Copy staging → device
        auto cmd = ctx.begin_single_cmd();
        VkBufferCopy region{ 0, 0, size };
        vkCmdCopyBuffer(cmd, staging.handle, buf.handle, 1, &region);
        ctx.end_single_cmd(cmd);

        staging.destroy(ctx.allocator());
    } else {
        // Device-local, no initial data
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size  = size;
        bci.usage = usage;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateBuffer(ctx.allocator(), &bci, &ai, &buf.handle, &buf.alloc, nullptr));
    }
    return buf;
}

VulkanBuffer create_staging_buffer(VmaAllocator allocator, VkDeviceSize size) {
    return create_host_buffer(allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
}

VulkanBuffer create_host_buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage) {
    VulkanBuffer buf;
    buf.size = size;

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo info{};
    VK_CHECK(vmaCreateBuffer(allocator, &bci, &ai, &buf.handle, &buf.alloc, &info));
    buf.mapped = info.pMappedData;
    return buf;
}

} // namespace sol::vk
