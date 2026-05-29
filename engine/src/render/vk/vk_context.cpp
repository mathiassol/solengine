#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "vk_context.h"
#include "sol/log.h"

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstring>
#include <set>
#include <stdexcept>

namespace sol::vk {

// ---------------------------------------------------------------------------
static const char* s_validation_layer = "VK_LAYER_KHRONOS_validation";

static bool check_validation_support() {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (auto& l : layers)
        if (strcmp(l.layerName, s_validation_layer) == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
bool VkContext::init(GLFWwindow* window, bool enable_validation) {
    // Initialise Volk (loads vulkan-1.dll)
    if (volkInitialize() != VK_SUCCESS) {
        SOL_ERROR("Volk: failed to load vulkan-1.dll. Ensure GPU drivers with Vulkan support are installed.");
        return false;
    }

    if (!create_instance_(enable_validation)) return false;
    volkLoadInstance(m_instance);

    // Create GLFW Vulkan surface
    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS) {
        SOL_ERROR("glfwCreateWindowSurface failed");
        return false;
    }

    if (!pick_gpu_())    return false;
    if (!create_device_()) return false;
    volkLoadDevice(m_device);

    if (!create_allocator_()) return false;
    if (!create_cmd_pool_())  return false;

    SOL_INFO(std::string("Vulkan context initialised (") + gpu_props().deviceName + ")");
    return true;
}

// ---------------------------------------------------------------------------
void VkContext::shutdown() {
    if (m_device) vkDeviceWaitIdle(m_device);

    if (m_imgui_pool) { vkDestroyDescriptorPool(m_device, m_imgui_pool, nullptr); m_imgui_pool = VK_NULL_HANDLE; }
    if (m_cmd_pool)   { vkDestroyCommandPool(m_device, m_cmd_pool, nullptr);      m_cmd_pool   = VK_NULL_HANDLE; }
    if (m_allocator)  { vmaDestroyAllocator(m_allocator);                         m_allocator  = VK_NULL_HANDLE; }
    if (m_device)     { vkDestroyDevice(m_device, nullptr);                       m_device     = VK_NULL_HANDLE; }
    if (m_surface)    { vkDestroySurfaceKHR(m_instance, m_surface, nullptr);      m_surface    = VK_NULL_HANDLE; }
    if (m_dbg_msgr)   {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_dbg_msgr, nullptr);
        m_dbg_msgr = VK_NULL_HANDLE;
    }
    if (m_instance)   { vkDestroyInstance(m_instance, nullptr);                   m_instance   = VK_NULL_HANDLE; }
}

// ---------------------------------------------------------------------------
bool VkContext::create_instance_(bool validation) {
    VkApplicationInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "SolEngine";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.pEngineName        = "Sol";
    ai.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    ai.apiVersion         = VK_API_VERSION_1_2;

    // Required instance extensions from GLFW + optional debug
    uint32_t glfwCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwCount);
    if (validation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    if (validation && check_validation_support()) {
        layers.push_back(s_validation_layer);
    } else if (validation) {
        SOL_WARN("Validation layers requested but not available");
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount       = (uint32_t)layers.size();
    ci.ppEnabledLayerNames     = layers.data();

    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));

    // Set up debug messenger
    if (validation && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT dbg{};
        dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbg.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = debug_cb_;
        // vkCreateDebugUtilsMessengerEXT loaded by volk after volkLoadInstance
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, &dbg, nullptr, &m_dbg_msgr);
    }
    return true;
}

// ---------------------------------------------------------------------------
static QueueFamilies find_queue_families(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    QueueFamilies qf;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) qf.graphics = i;
        VkBool32 pres = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &pres);
        if (pres) qf.present = i;
        if (qf.complete()) break;
    }
    return qf;
}

static bool check_device_extensions(VkPhysicalDevice gpu) {
    const char* required[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> avail(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, avail.data());
    for (auto req : required) {
        bool found = false;
        for (auto& a : avail) if (strcmp(a.extensionName, req) == 0) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

bool VkContext::pick_gpu_() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) { SOL_ERROR("No Vulkan-capable GPU found"); return false; }
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(m_instance, &count, gpus.data());

    // Prefer discrete GPU; fall back to first suitable
    VkPhysicalDevice fallback = VK_NULL_HANDLE;
    for (auto g : gpus) {
        if (!find_queue_families(g, m_surface).complete()) continue;
        if (!check_device_extensions(g)) continue;
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(g, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { m_gpu = g; break; }
        if (!fallback) fallback = g;
    }
    if (m_gpu == VK_NULL_HANDLE) m_gpu = fallback;
    if (m_gpu == VK_NULL_HANDLE) { SOL_ERROR("No suitable GPU found"); return false; }

    m_qf = find_queue_families(m_gpu, m_surface);
    return true;
}

// ---------------------------------------------------------------------------
bool VkContext::create_device_() {
    std::set<uint32_t> unique_fams = { *m_qf.graphics, *m_qf.present };
    float prio = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    for (uint32_t fam : unique_fams) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = fam;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &prio;
        qcis.push_back(qci);
    }

    VkPhysicalDeviceFeatures feat{};
    feat.samplerAnisotropy = VK_TRUE;
    feat.depthClamp        = VK_TRUE;

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = (uint32_t)qcis.size();
    dci.pQueueCreateInfos       = qcis.data();
    dci.pEnabledFeatures        = &feat;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = devExts;

    VK_CHECK(vkCreateDevice(m_gpu, &dci, nullptr, &m_device));
    vkGetDeviceQueue(m_device, *m_qf.graphics, 0, &m_gfx_queue);
    vkGetDeviceQueue(m_device, *m_qf.present,  0, &m_present_queue);
    return true;
}

// ---------------------------------------------------------------------------
bool VkContext::create_allocator_() {
    VmaVulkanFunctions vf{};
    vf.vkGetInstanceProcAddr               = vkGetInstanceProcAddr;
    vf.vkGetDeviceProcAddr                 = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo ai{};
    ai.physicalDevice   = m_gpu;
    ai.device           = m_device;
    ai.instance         = m_instance;
    ai.pVulkanFunctions = &vf;
    ai.vulkanApiVersion = VK_API_VERSION_1_2;
    VK_CHECK(vmaCreateAllocator(&ai, &m_allocator));
    return true;
}

bool VkContext::create_cmd_pool_() {
    VkCommandPoolCreateInfo pi{};
    pi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.queueFamilyIndex = *m_qf.graphics;
    pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(m_device, &pi, nullptr, &m_cmd_pool));
    return true;
}

// ---------------------------------------------------------------------------
VkPhysicalDeviceProperties VkContext::gpu_props() const {
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(m_gpu, &p);
    return p;
}

VkCommandBuffer VkContext::begin_single_cmd() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_cmd_pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    return cmd;
}

void VkContext::end_single_cmd(VkCommandBuffer cmd) {
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(m_gfx_queue, 1, &si, VK_NULL_HANDLE));
    vkQueueWaitIdle(m_gfx_queue);
    vkFreeCommandBuffers(m_device, m_cmd_pool, 1, &cmd);
}

// ---------------------------------------------------------------------------
bool VkContext::create_imgui_pool() {
    if (m_imgui_pool) return true;
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128 },
    };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets       = 128;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = sizes;
    VK_CHECK(vkCreateDescriptorPool(m_device, &pi, nullptr, &m_imgui_pool));
    return true;
}

// ---------------------------------------------------------------------------
VKAPI_ATTR VkBool32 VKAPI_CALL VkContext::debug_cb_(
    VkDebugUtilsMessageSeverityFlagBitsEXT sev,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        SOL_ERROR(std::string("[Vulkan] ") + data->pMessage);
    else if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        SOL_WARN(std::string("[Vulkan] ") + data->pMessage);
    return VK_FALSE;
}

// ---------------------------------------------------------------------------
bool VkContext::init_win32(void* hwnd, void* hinstance, bool enable_validation) {
    if (volkInitialize() != VK_SUCCESS) {
        SOL_ERROR("Volk: failed to load vulkan-1.dll. Ensure GPU drivers with Vulkan support are installed.");
        return false;
    }

    if (!create_instance_win32_(enable_validation)) return false;
    volkLoadInstance(m_instance);

    VkWin32SurfaceCreateInfoKHR sci{};
    sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = static_cast<HINSTANCE>(hinstance);
    sci.hwnd      = static_cast<HWND>(hwnd);
    if (vkCreateWin32SurfaceKHR(m_instance, &sci, nullptr, &m_surface) != VK_SUCCESS) {
        SOL_ERROR("vkCreateWin32SurfaceKHR failed");
        return false;
    }

    if (!pick_gpu_())      return false;
    if (!create_device_()) return false;
    volkLoadDevice(m_device);

    if (!create_allocator_()) return false;
    if (!create_cmd_pool_())  return false;

    SOL_INFO(std::string("Vulkan context initialised (editor - ") + gpu_props().deviceName + ")");
    return true;
}

// ---------------------------------------------------------------------------
bool VkContext::create_instance_win32_(bool validation) {
    VkApplicationInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "SolEngine";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.pEngineName        = "Sol";
    ai.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    ai.apiVersion         = VK_API_VERSION_1_2;

    std::vector<const char*> exts = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };
    if (validation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    if (validation && check_validation_support()) {
        layers.push_back(s_validation_layer);
    } else if (validation) {
        SOL_WARN("Validation layers requested but not available");
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount       = (uint32_t)layers.size();
    ci.ppEnabledLayerNames     = layers.data();

    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));

    if (validation && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT dbg{};
        dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbg.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = debug_cb_;
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, &dbg, nullptr, &m_dbg_msgr);
    }
    return true;
}

} // namespace sol::vk
