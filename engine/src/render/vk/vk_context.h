#pragma once
#include "vk_common.h"
#include <vector>
#include <optional>
#include <string>

struct GLFWwindow;

namespace sol::vk {

struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};

// Central Vulkan context: instance, device, queues, command pools, VMA allocator.
// One VkContext lives for the lifetime of the engine.
class VkContext {
public:
    VkContext()  = default;
    ~VkContext() = default;

    bool init(GLFWwindow* window, bool enable_validation);
    bool init_win32(void* hwnd, void* hinstance, bool enable_validation);
    void shutdown();

    // ---- accessors ----
    VkInstance          instance()      const { return m_instance; }
    VkPhysicalDevice    gpu()           const { return m_gpu; }
    VkDevice            device()        const { return m_device; }
    VkSurfaceKHR        surface()       const { return m_surface; }
    VkQueue             gfx_queue()     const { return m_gfx_queue; }
    VkQueue             present_queue() const { return m_present_queue; }
    VmaAllocator        allocator()     const { return m_allocator; }
    VkCommandPool       cmd_pool()      const { return m_cmd_pool; }
    const QueueFamilies& queue_families() const { return m_qf; }

    VkPhysicalDeviceProperties gpu_props() const;

    // One-shot command buffer helpers
    VkCommandBuffer begin_single_cmd();
    void            end_single_cmd(VkCommandBuffer cmd);

    // Descriptor pool for ImGui (created on demand)
    VkDescriptorPool imgui_pool() const { return m_imgui_pool; }
    bool             create_imgui_pool();

private:
    bool create_instance_(bool validation);
    bool create_instance_win32_(bool validation);
    bool pick_gpu_();
    bool create_device_();
    bool create_allocator_();
    bool create_cmd_pool_();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb_(
        VkDebugUtilsMessageSeverityFlagBitsEXT sev,
        VkDebugUtilsMessageTypeFlagsEXT        type,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void*);

    VkInstance               m_instance    = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_dbg_msgr    = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface     = VK_NULL_HANDLE;
    VkPhysicalDevice         m_gpu         = VK_NULL_HANDLE;
    VkDevice                 m_device      = VK_NULL_HANDLE;
    QueueFamilies            m_qf;
    VkQueue                  m_gfx_queue     = VK_NULL_HANDLE;
    VkQueue                  m_present_queue = VK_NULL_HANDLE;
    VmaAllocator             m_allocator   = VK_NULL_HANDLE;
    VkCommandPool            m_cmd_pool    = VK_NULL_HANDLE;
    VkDescriptorPool         m_imgui_pool  = VK_NULL_HANDLE;
};

} // namespace sol::vk
