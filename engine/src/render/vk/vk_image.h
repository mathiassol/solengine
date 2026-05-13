#pragma once
#include "vk_common.h"

namespace sol::vk {
class VkContext;

struct VulkanImage {
    VkImage       image      = VK_NULL_HANDLE;
    VkImageView   view       = VK_NULL_HANDLE;
    VkSampler     sampler    = VK_NULL_HANDLE;
    VmaAllocation alloc      = VK_NULL_HANDLE;
    VkFormat      format     = VK_FORMAT_UNDEFINED;
    uint32_t      width      = 0;
    uint32_t      height     = 0;
    uint32_t      mip_levels = 1;

    bool valid() const { return image != VK_NULL_HANDLE; }

    void destroy(VkDevice device, VmaAllocator allocator) {
        if (sampler) { vkDestroySampler(device, sampler, nullptr);    sampler = VK_NULL_HANDLE; }
        if (view)    { vkDestroyImageView(device, view, nullptr);     view    = VK_NULL_HANDLE; }
        if (image)   { vmaDestroyImage(allocator, image, alloc);      image   = VK_NULL_HANDLE; alloc = VK_NULL_HANDLE; }
    }
};

// 2D texture from RGBA8 CPU data.  Generates full mip chain.
VulkanImage create_texture2d(VkContext& ctx, const void* pixels, int width, int height);

// 1×1 fallback texture (e.g., white, flat-normal, white-MR)
VulkanImage create_texture2d_1x1(VkContext& ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

// Offscreen attachment (color or depth)
VulkanImage create_attachment(VkContext& ctx, uint32_t w, uint32_t h,
                              VkFormat format, VkImageUsageFlags usage,
                              VkImageAspectFlags aspect,
                              bool create_sampler = false);

// Transition image layout (one-shot cmd)
void transition_image_layout(VkCommandBuffer cmd, VkImage image,
                              VkImageAspectFlags aspect,
                              VkImageLayout from, VkImageLayout to,
                              uint32_t mip_levels = 1);

// Create a Vulkan image view for a single face/mip of a cubemap (used for rendering into faces)
VkImageView create_cubemap_face_view(VkDevice device, VkImage image, VkFormat format,
                                      uint32_t layer_index, uint32_t mip_level);

// Transition a cubemap image (all 6 layers) between layouts
void transition_cubemap_layout(VkCommandBuffer cmd, VkImage image,
                                VkImageLayout from, VkImageLayout to,
                                uint32_t base_mip = 0, uint32_t mip_count = 1);

// Create a VkRenderPass with a single RGBA/RG float color attachment (no depth)
// Store: STORE_OP_STORE, Load: LOAD_OP_CLEAR, final layout: COLOR_ATTACHMENT_OPTIMAL (manual transition)
VkRenderPass create_ibl_render_pass(VkDevice device, VkFormat format);

// Create a sampler suitable for IBL cubemaps (linear, clamp-to-edge, optionally with mips)
VkSampler create_ibl_sampler(VkDevice device, uint32_t mip_levels);

// Create the cubemap VkImage + a cube-view VkImageView
// does NOT create a sampler — use create_ibl_sampler separately
VulkanImage create_cubemap_image(VkContext& ctx, uint32_t face_size, uint32_t mips, VkFormat format);

} // namespace sol::vk
