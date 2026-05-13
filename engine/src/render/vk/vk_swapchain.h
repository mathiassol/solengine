#pragma once
#include "vk_common.h"
#include <vector>

struct GLFWwindow;

namespace sol::vk {
class VkContext;

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR        caps{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;
};

SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice gpu, VkSurfaceKHR surface);

// FRAMES_IN_FLIGHT: number of simultaneously-in-flight frames (double buffer).
static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

struct FrameSync {
    VkSemaphore image_available  = VK_NULL_HANDLE;
    VkSemaphore render_finished  = VK_NULL_HANDLE;
    VkFence     in_flight_fence  = VK_NULL_HANDLE;
    VkCommandBuffer cmd          = VK_NULL_HANDLE;
};

class VkSwapchain {
public:
    bool init(VkContext& ctx, GLFWwindow* window);
    bool init(VkContext& ctx, uint32_t fallback_w, uint32_t fallback_h);
    void shutdown(VkContext& ctx);

    // Returns VK_ERROR_OUT_OF_DATE_KHR if resize needed
    VkResult acquire_next(VkContext& ctx, uint32_t frame_idx, uint32_t& out_image_idx);
    VkResult present(VkContext& ctx, uint32_t frame_idx, uint32_t image_idx);

    bool recreate(VkContext& ctx, GLFWwindow* window);
    bool recreate(VkContext& ctx, uint32_t w, uint32_t h);

    VkSwapchainKHR         handle()              const { return m_swapchain; }
    VkFormat               format()              const { return m_format; }
    VkExtent2D             extent()              const { return m_extent; }
    uint32_t               image_count()         const { return (uint32_t)m_images.size(); }
    VkImageView            image_view(uint32_t i) const { return m_image_views[i]; }
    const FrameSync&       frame(uint32_t i)     const { return m_frames[i]; }
    FrameSync&             frame(uint32_t i)           { return m_frames[i]; }

private:
    void create_image_views_(VkDevice device);
    void create_frame_sync_(VkContext& ctx);
    void destroy_(VkContext& ctx);

    VkSwapchainKHR            m_swapchain = VK_NULL_HANDLE;
    VkFormat                  m_format    = VK_FORMAT_UNDEFINED;
    VkExtent2D                m_extent    {};
    std::vector<VkImage>      m_images;
    std::vector<VkImageView>  m_image_views;
    FrameSync                 m_frames[FRAMES_IN_FLIGHT];
};

} // namespace sol::vk
