#include "ui/imgui_layer.h"
#include "sol/log.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

namespace sol {

bool ImGuiLayer::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Bgfx is the renderer; install a no-op "Other" backend so ImGui::Render() works.
    // A full bgfx render backend can be added later — this keeps the pipeline live.
    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui::StyleColorsDark();
    // No render backend yet -> manually build the font atlas so NewFrame asserts pass.
    unsigned char* px = nullptr; int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
    m_initialized = true;
    SOL_INFO("ImGui initialized");
    return true;
}

void ImGuiLayer::shutdown() {
    if (!m_initialized) return;
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void ImGuiLayer::begin_frame() {
    if (!m_initialized) return;
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::end_frame() {
    if (!m_initialized) return;
    ImGui::Render();
    // TODO: render ImGui::GetDrawData() through bgfx. For now ImGui state still
    // ticks correctly (input, layout, IDs); drawing is added with a render backend.
}

} // namespace sol
