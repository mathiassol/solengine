#include "vk_image.h"
#include "vk_context.h"
#include "vk_buffer.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

namespace sol::vk {

// ---------------------------------------------------------------------------
void transition_image_layout(VkCommandBuffer cmd, VkImage image,
                              VkImageAspectFlags aspect,
                              VkImageLayout from, VkImageLayout to,
                              uint32_t mip_levels)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = { aspect, 0, mip_levels, 0, 1 };

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (from == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && to == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (from == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && to == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && to == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ---------------------------------------------------------------------------
static VulkanImage make_image_and_view(VkContext& ctx,
                                       uint32_t w, uint32_t h,
                                       uint32_t mips,
                                       VkFormat format,
                                       VkImageUsageFlags usage,
                                       VkImageAspectFlags aspect,
                                       VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT)
{
    VulkanImage img;
    img.format     = format;
    img.width      = w;
    img.height     = h;
    img.mip_levels = mips;

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = mips;
    ici.arrayLayers   = 1;
    ici.format        = format;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage         = usage;
    ici.samples       = samples;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(ctx.allocator(), &ici, &ai, &img.image, &img.alloc, nullptr));

    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = img.image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = format;
    vci.subresourceRange.aspectMask     = aspect;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = mips;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;
    VK_CHECK(vkCreateImageView(ctx.device(), &vci, nullptr, &img.view));

    return img;
}

static VkSampler make_aniso_sampler(VkContext& ctx, uint32_t mip_levels) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx.gpu(), &props);

    VkSamplerCreateInfo si{};
    si.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter        = VK_FILTER_LINEAR;
    si.minFilter        = VK_FILTER_LINEAR;
    si.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = VK_TRUE;
    si.maxAnisotropy    = props.limits.maxSamplerAnisotropy;
    si.minLod           = 0.0f;
    si.maxLod           = (float)mip_levels;
    si.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    VkSampler s;
    VK_CHECK(vkCreateSampler(ctx.device(), &si, nullptr, &s));
    return s;
}

// ---------------------------------------------------------------------------
VulkanImage create_texture2d(VkContext& ctx, const void* pixels, int width, int height) {
    // Compute mip levels
    uint32_t mips = 1 + (uint32_t)std::floor(std::log2((double)std::max(width, height)));

    // Build full mip chain in a staging buffer
    VkDeviceSize total = 0;
    {
        int mw = width, mh = height;
        while (true) {
            total += (VkDeviceSize)(mw * mh * 4);
            if (mw == 1 && mh == 1) break;
            mw = std::max(1, mw / 2);
            mh = std::max(1, mh / 2);
        }
    }

    auto staging = create_staging_buffer(ctx.allocator(), total);
    // Copy mip 0
    std::memcpy(staging.mapped, pixels, (size_t)width * height * 4);
    // Generate mips
    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    uint8_t*       dst = static_cast<uint8_t*>(staging.mapped) + (size_t)width * height * 4;
    int sw = width, sh = height;
    while (sw > 1 || sh > 1) {
        int dw = std::max(1, sw / 2), dh = std::max(1, sh / 2);
        stbir_resize_uint8_linear(src, sw, sh, 0, dst, dw, dh, 0, STBIR_RGBA);
        src = dst;
        dst += (size_t)dw * dh * 4;
        sw = dw; sh = dh;
    }

    VulkanImage img = make_image_and_view(ctx, (uint32_t)width, (uint32_t)height, mips,
                                          VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                          VK_IMAGE_ASPECT_COLOR_BIT);

    // Copy staging to image, mip by mip
    auto cmd = ctx.begin_single_cmd();
    transition_image_layout(cmd, img.image, VK_IMAGE_ASPECT_COLOR_BIT,
                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mips);

    VkDeviceSize offset = 0;
    int mw = width, mh = height;
    for (uint32_t m = 0; m < mips; ++m) {
        VkBufferImageCopy region{};
        region.bufferOffset     = offset;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1 };
        region.imageExtent      = { (uint32_t)mw, (uint32_t)mh, 1 };
        vkCmdCopyBufferToImage(cmd, staging.handle, img.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        offset += (VkDeviceSize)(mw * mh * 4);
        if (mw == 1 && mh == 1) break;
        mw = std::max(1, mw / 2);
        mh = std::max(1, mh / 2);
    }

    transition_image_layout(cmd, img.image, VK_IMAGE_ASPECT_COLOR_BIT,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mips);
    ctx.end_single_cmd(cmd);
    staging.destroy(ctx.allocator());

    img.sampler = make_aniso_sampler(ctx, mips);
    return img;
}

// ---------------------------------------------------------------------------
VulkanImage create_texture2d_hdr(VkContext& ctx, const float* pixels_rgba, int width, int height) {
    VkDeviceSize size = (VkDeviceSize)width * height * 4 * sizeof(float);

    auto staging = create_staging_buffer(ctx.allocator(), size);
    std::memcpy(staging.mapped, pixels_rgba, (size_t)size);

    VulkanImage img = make_image_and_view(ctx, (uint32_t)width, (uint32_t)height, 1,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    auto cmd = ctx.begin_single_cmd();
    transition_image_layout(cmd, img.image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};
    vkCmdCopyBufferToImage(cmd, staging.handle, img.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transition_image_layout(cmd, img.image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.end_single_cmd(cmd);
    staging.destroy(ctx.allocator());

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod       = 0.25f;
    VK_CHECK(vkCreateSampler(ctx.device(), &si, nullptr, &img.sampler));

    return img;
}

// ---------------------------------------------------------------------------
VulkanImage create_texture2d_1x1(VkContext& ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t px[4] = { r, g, b, a };
    return create_texture2d(ctx, px, 1, 1);
}

// ---------------------------------------------------------------------------
VulkanImage create_attachment(VkContext& ctx, uint32_t w, uint32_t h,
                              VkFormat format, VkImageUsageFlags usage,
                              VkImageAspectFlags aspect,
                              bool make_sampler,
                              VkSampleCountFlagBits samples)
{
    VulkanImage img = make_image_and_view(ctx, w, h, 1, format, usage, aspect, samples);

    if (make_sampler) {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW =
            (aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
                ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        si.compareEnable = (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_TRUE : VK_FALSE;
        si.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
        VK_CHECK(vkCreateSampler(ctx.device(), &si, nullptr, &img.sampler));
    }

    // Transition to the expected initial layout
    auto cmd = ctx.begin_single_cmd();
    if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
        transition_image_layout(cmd, img.image, aspect,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    } else {
        transition_image_layout(cmd, img.image, aspect,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    ctx.end_single_cmd(cmd);

    return img;
}

// ---------------------------------------------------------------------------
// IBL helpers
// ---------------------------------------------------------------------------

VkImageView create_cubemap_face_view(VkDevice device, VkImage image, VkFormat format,
                                      uint32_t layer_index, uint32_t mip_level)
{
    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = format;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.baseMipLevel   = mip_level;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = layer_index;
    vci.subresourceRange.layerCount     = 1;
    VkImageView view;
    VK_CHECK(vkCreateImageView(device, &vci, nullptr, &view));
    return view;
}

void transition_cubemap_layout(VkCommandBuffer cmd, VkImage image,
                                VkImageLayout from, VkImageLayout to,
                                uint32_t base_mip, uint32_t mip_count)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, base_mip, mip_count, 0, 6 };

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (from == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkRenderPass create_ibl_render_pass(VkDevice device, VkFormat format)
{
    VkAttachmentDescription att{};
    att.format         = format;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // we transition manually

    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;

    VkRenderPassCreateInfo rci{};
    rci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rci.attachmentCount = 1;
    rci.pAttachments    = &att;
    rci.subpassCount    = 1;
    rci.pSubpasses      = &sub;

    VkRenderPass pass;
    VK_CHECK(vkCreateRenderPass(device, &rci, nullptr, &pass));
    return pass;
}

VkSampler create_ibl_sampler(VkDevice device, uint32_t mip_levels)
{
    VkSamplerCreateInfo si{};
    si.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter        = VK_FILTER_LINEAR;
    si.minFilter        = VK_FILTER_LINEAR;
    si.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod           = 0.0f;
    si.maxLod           = float(mip_levels);
    si.borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    VkSampler s;
    VK_CHECK(vkCreateSampler(device, &si, nullptr, &s));
    return s;
}

VulkanImage create_cubemap_image(VkContext& ctx, uint32_t face_size, uint32_t mips, VkFormat format)
{
    VulkanImage img;
    img.format     = format;
    img.width      = face_size;
    img.height     = face_size;
    img.mip_levels = mips;

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.extent        = { face_size, face_size, 1 };
    ici.mipLevels     = mips;
    ici.arrayLayers   = 6;
    ici.format        = format;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(ctx.allocator(), &ici, &ai, &img.image, &img.alloc, nullptr));

    // Cube view (for sampling as samplerCube)
    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = img.image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_CUBE;
    vci.format                          = format;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = mips;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 6;
    VK_CHECK(vkCreateImageView(ctx.device(), &vci, nullptr, &img.view));

    return img;
}

} // namespace sol::vk