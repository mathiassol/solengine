#include "vk_swapchain.h"
#include "vk_context.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>

namespace sol::vk {

// ---------------------------------------------------------------------------
SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    SwapchainSupportDetails d;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &d.caps);
    uint32_t n;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, nullptr);
    d.formats.resize(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, d.formats.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &n, nullptr);
    d.present_modes.resize(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &n, d.present_modes.data());
    return d;
}

static VkSurfaceFormatKHR choose_format(const std::vector<VkSurfaceFormatKHR>& fmts) {
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return fmts[0];
}

static VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR)     return m;
    for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR)   return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* w) {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    int fw, fh;
    glfwGetFramebufferSize(w, &fw, &fh);
    VkExtent2D e = { (uint32_t)fw, (uint32_t)fh };
    e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

static VkExtent2D choose_extent_wh(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h) {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    VkExtent2D e = { w > 0 ? w : 1280u, h > 0 ? h : 720u };
    e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

// ---------------------------------------------------------------------------
bool VkSwapchain::init(VkContext& ctx, GLFWwindow* window) {
    auto sup = query_swapchain_support(ctx.gpu(), ctx.surface());
    auto fmt = choose_format(sup.formats);
    m_format = fmt.format;
    m_extent = choose_extent(sup.caps, window);

    uint32_t imgCount = sup.caps.minImageCount + 1;
    if (sup.caps.maxImageCount > 0) imgCount = std::min(imgCount, sup.caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.surface();
    ci.minImageCount    = imgCount;
    ci.imageFormat      = m_format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = m_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    auto& qf = ctx.queue_families();
    uint32_t qfArr[] = { *qf.graphics, *qf.present };
    if (*qf.graphics != *qf.present) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = qfArr;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform   = sup.caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = choose_present_mode(sup.present_modes);
    ci.clipped        = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(ctx.device(), &ci, nullptr, &m_swapchain));

    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, nullptr);
    m_images.resize(imgCount);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, m_images.data());

    create_image_views_(ctx.device());
    create_frame_sync_(ctx);
    return true;
}

void VkSwapchain::create_image_views_(VkDevice device) {
    m_image_views.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image                           = m_images[i];
        vi.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vi.format                          = m_format;
        vi.components                      = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                               VK_COMPONENT_SWIZZLE_IDENTITY,
                                               VK_COMPONENT_SWIZZLE_IDENTITY,
                                               VK_COMPONENT_SWIZZLE_IDENTITY };
        vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel   = 0;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount     = 1;
        VK_CHECK(vkCreateImageView(device, &vi, nullptr, &m_image_views[i]));
    }
}

void VkSwapchain::create_frame_sync_(VkContext& ctx) {
    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so first frame doesn't wait

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = ctx.cmd_pool();
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    for (auto& f : m_frames) {
        VK_CHECK(vkCreateSemaphore(ctx.device(), &si, nullptr, &f.image_available));
        VK_CHECK(vkCreateSemaphore(ctx.device(), &si, nullptr, &f.render_finished));
        VK_CHECK(vkCreateFence(ctx.device(), &fi, nullptr, &f.in_flight_fence));
        VK_CHECK(vkAllocateCommandBuffers(ctx.device(), &ai, &f.cmd));
    }
}

// ---------------------------------------------------------------------------
void VkSwapchain::destroy_(VkContext& ctx) {
    for (auto& f : m_frames) {
        if (f.in_flight_fence)  { vkDestroyFence(ctx.device(), f.in_flight_fence, nullptr);    f.in_flight_fence = VK_NULL_HANDLE; }
        if (f.render_finished)  { vkDestroySemaphore(ctx.device(), f.render_finished, nullptr); f.render_finished = VK_NULL_HANDLE; }
        if (f.image_available)  { vkDestroySemaphore(ctx.device(), f.image_available, nullptr); f.image_available = VK_NULL_HANDLE; }
        if (f.cmd) {
            vkFreeCommandBuffers(ctx.device(), ctx.cmd_pool(), 1, &f.cmd);
            f.cmd = VK_NULL_HANDLE;
        }
    }
    for (auto iv : m_image_views)
        vkDestroyImageView(ctx.device(), iv, nullptr);
    m_image_views.clear();
    if (m_swapchain) { vkDestroySwapchainKHR(ctx.device(), m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
}

void VkSwapchain::shutdown(VkContext& ctx) { destroy_(ctx); }

bool VkSwapchain::recreate(VkContext& ctx, GLFWwindow* window) {
    vkDeviceWaitIdle(ctx.device());
    // Destroy swapchain + image views but keep frame sync objects
    for (auto iv : m_image_views)
        vkDestroyImageView(ctx.device(), iv, nullptr);
    m_image_views.clear();
    VkSwapchainKHR old = m_swapchain;
    m_swapchain = VK_NULL_HANDLE;

    auto sup = query_swapchain_support(ctx.gpu(), ctx.surface());
    auto fmt = choose_format(sup.formats);
    m_format = fmt.format;
    m_extent = choose_extent(sup.caps, window);

    uint32_t imgCount = sup.caps.minImageCount + 1;
    if (sup.caps.maxImageCount > 0) imgCount = std::min(imgCount, sup.caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.surface();
    ci.minImageCount    = imgCount;
    ci.imageFormat      = m_format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = m_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    auto& qf = ctx.queue_families();
    uint32_t qfArr[] = { *qf.graphics, *qf.present };
    if (*qf.graphics != *qf.present) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = qfArr;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform     = sup.caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = choose_present_mode(sup.present_modes);
    ci.clipped          = VK_TRUE;
    ci.oldSwapchain     = old;

    VK_CHECK(vkCreateSwapchainKHR(ctx.device(), &ci, nullptr, &m_swapchain));
    if (old) vkDestroySwapchainKHR(ctx.device(), old, nullptr);

    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, nullptr);
    m_images.resize(imgCount);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, m_images.data());
    create_image_views_(ctx.device());
    return true;
}

// ---------------------------------------------------------------------------
VkResult VkSwapchain::acquire_next(VkContext& ctx, uint32_t frame_idx, uint32_t& out_idx) {
    auto& f = m_frames[frame_idx];
    vkWaitForFences(ctx.device(), 1, &f.in_flight_fence, VK_TRUE, UINT64_MAX);
    VkResult r = vkAcquireNextImageKHR(ctx.device(), m_swapchain, UINT64_MAX,
                                       f.image_available, VK_NULL_HANDLE, &out_idx);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return r;
    vkResetFences(ctx.device(), 1, &f.in_flight_fence);
    return VK_SUCCESS;
}

VkResult VkSwapchain::present(VkContext& ctx, uint32_t frame_idx, uint32_t image_idx) {
    auto& f = m_frames[frame_idx];
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &f.render_finished;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_swapchain;
    pi.pImageIndices      = &image_idx;
    return vkQueuePresentKHR(ctx.present_queue(), &pi);
}

// ---------------------------------------------------------------------------
// No-GLFW overloads (editor / Win32 path)
// ---------------------------------------------------------------------------
bool VkSwapchain::init(VkContext& ctx, uint32_t fallback_w, uint32_t fallback_h) {
    auto sup = query_swapchain_support(ctx.gpu(), ctx.surface());
    auto fmt = choose_format(sup.formats);
    m_format = fmt.format;
    m_extent = choose_extent_wh(sup.caps, fallback_w, fallback_h);

    uint32_t imgCount = sup.caps.minImageCount + 1;
    if (sup.caps.maxImageCount > 0) imgCount = std::min(imgCount, sup.caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.surface();
    ci.minImageCount    = imgCount;
    ci.imageFormat      = m_format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = m_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    auto& qf = ctx.queue_families();
    uint32_t qfArr[] = { *qf.graphics, *qf.present };
    if (*qf.graphics != *qf.present) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = qfArr;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform   = sup.caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = choose_present_mode(sup.present_modes);
    ci.clipped        = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(ctx.device(), &ci, nullptr, &m_swapchain));

    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, nullptr);
    m_images.resize(imgCount);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, m_images.data());

    create_image_views_(ctx.device());
    create_frame_sync_(ctx);
    return true;
}

bool VkSwapchain::recreate(VkContext& ctx, uint32_t w, uint32_t h) {
    vkDeviceWaitIdle(ctx.device());
    for (auto iv : m_image_views)
        vkDestroyImageView(ctx.device(), iv, nullptr);
    m_image_views.clear();
    VkSwapchainKHR old = m_swapchain;
    m_swapchain = VK_NULL_HANDLE;

    auto sup = query_swapchain_support(ctx.gpu(), ctx.surface());
    auto fmt = choose_format(sup.formats);
    m_format = fmt.format;
    m_extent = choose_extent_wh(sup.caps, w, h);

    uint32_t imgCount = sup.caps.minImageCount + 1;
    if (sup.caps.maxImageCount > 0) imgCount = std::min(imgCount, sup.caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.surface();
    ci.minImageCount    = imgCount;
    ci.imageFormat      = m_format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = m_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    auto& qf = ctx.queue_families();
    uint32_t qfArr[] = { *qf.graphics, *qf.present };
    if (*qf.graphics != *qf.present) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = qfArr;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform     = sup.caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = choose_present_mode(sup.present_modes);
    ci.clipped          = VK_TRUE;
    ci.oldSwapchain     = old;

    VK_CHECK(vkCreateSwapchainKHR(ctx.device(), &ci, nullptr, &m_swapchain));
    if (old) vkDestroySwapchainKHR(ctx.device(), old, nullptr);

    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, nullptr);
    m_images.resize(imgCount);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, m_images.data());
    create_image_views_(ctx.device());
    return true;
}

} // namespace sol::vk
