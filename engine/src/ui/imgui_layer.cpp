#include "ui/imgui_layer.h"
#include "sol/log.h"
#include "render/vk/vk_renderer.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace sol {

bool ImGuiLayer::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui::StyleColorsDark();

    auto* renderer = VulkanRenderer::get();
    if (!renderer || !renderer->init_imgui_backend(window)) {
        SOL_ERROR("ImGui Vulkan backend init failed");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    m_initialized = true;
    m_editor_mode = false;
    SOL_INFO("ImGui initialized");
    return true;
}

bool ImGuiLayer::init_editor() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "sol_editor_qt";
    ImGui::StyleColorsDark();

    auto* renderer = VulkanRenderer::get();
    if (!renderer || !renderer->init_imgui_backend(nullptr)) {
        SOL_ERROR("ImGui Vulkan backend init failed");
        ImGui::DestroyContext();
        return false;
    }

    m_initialized = true;
    m_editor_mode = true;
    SOL_INFO("ImGui initialized (editor mode)");
    return true;
}

void ImGuiLayer::shutdown() {
    if (!m_initialized) return;
    if (auto* renderer = VulkanRenderer::get()) renderer->shutdown_imgui_backend();
    if (!m_editor_mode) ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
    m_editor_mode = false;
}

void ImGuiLayer::begin_frame() {
    if (!m_initialized) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::begin_frame_editor(int w, int h, float dt) {
    if (!m_initialized || !m_editor_mode) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    io.DeltaTime = (dt > 0.0f) ? dt : (1.0f / 60.0f);
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::end_frame() {
    if (!m_initialized) return;
    ImGui::Render();
}

} // namespace sol
