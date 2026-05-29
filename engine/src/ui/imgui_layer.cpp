#include "ui/imgui_layer.h"
#include "ui/fa_font_embedded.h"
#include "sol/log.h"
#include "render/vk/vk_renderer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <string>

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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.BackendPlatformName = "sol_editor_qt";
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.FramePadding      = ImVec2(6.0f, 3.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]             = ImVec4(0.118f, 0.118f, 0.176f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.118f, 0.118f, 0.176f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.094f, 0.094f, 0.141f, 1.00f);
    colors[ImGuiCol_Border]               = ImVec4(0.271f, 0.282f, 0.408f, 1.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.141f, 0.149f, 0.216f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.200f, 0.212f, 0.310f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.267f, 0.282f, 0.412f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.094f, 0.094f, 0.141f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.118f, 0.118f, 0.176f, 1.00f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.094f, 0.094f, 0.141f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.094f, 0.094f, 0.141f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.271f, 0.282f, 0.408f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.361f, 0.376f, 0.545f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.537f, 0.706f, 0.980f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.537f, 0.706f, 0.980f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.537f, 0.706f, 0.980f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.647f, 0.780f, 0.988f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.200f, 0.212f, 0.310f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.271f, 0.286f, 0.416f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.537f, 0.706f, 0.980f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.200f, 0.212f, 0.310f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.271f, 0.286f, 0.416f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.537f, 0.706f, 0.980f, 1.00f);
    colors[ImGuiCol_Separator]            = ImVec4(0.271f, 0.282f, 0.408f, 1.00f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.141f, 0.149f, 0.216f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.537f, 0.706f, 0.980f, 0.80f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.200f, 0.212f, 0.310f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.094f, 0.094f, 0.141f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.141f, 0.149f, 0.216f, 1.00f);
    colors[ImGuiCol_DockingPreview]       = ImVec4(0.537f, 0.706f, 0.980f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]       = ImVec4(0.118f, 0.118f, 0.176f, 1.00f);
    colors[ImGuiCol_Text]                 = ImVec4(0.808f, 0.839f, 0.957f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.451f, 0.478f, 0.643f, 1.00f);

    // ── Font setup (must happen before init_imgui_backend builds the atlas) ──
    {
        const float sz = 14.0f;

        // Segoe UI — clean, modern look on Windows. Falls back to built-in.
        ImFont* mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", sz);
        if (!mainFont) {
            SOL_WARN("Segoe UI not found, using built-in font");
            io.Fonts->AddFontDefault();
        }

        // Merge FontAwesome 6 Solid icons from the embedded subset (25 icons, ~6 KB).
        // FontDataOwnedByAtlas=false because kFaFontData is a static array.
        ImFontConfig cfg;
        cfg.MergeMode            = true;
        cfg.PixelSnapH           = true;
        cfg.GlyphMinAdvanceX     = sz;
        cfg.FontDataOwnedByAtlas = false;
        static const ImWchar fa_ranges[] = { 0xF000, 0xF8FF, 0 };
        ImFont* fa = io.Fonts->AddFontFromMemoryTTF(
            kFaFontData, kFaFontSize, sz, &cfg, fa_ranges);
        SOL_INFO(std::string("FA icon font: ") + (fa ? "loaded OK" : "FAILED"));
    }

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

void ImGuiLayer::begin_frame_absorb(int w, int h, float dt) {
    if (!m_initialized || !m_editor_mode) return;
    // Set up per-frame Vulkan resources exactly once per GPU frame.
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    io.DeltaTime = (dt > 0.0f) ? dt : (1.0f / 60.0f);
    ImGui_ImplVulkan_NewFrame();

    // Save the input event queue, then clear it so the throwaway frame gets
    // zero input events. Game nodes can still call ImGui safely (the frame is
    // active) but all mouse/keyboard events are preserved for the editor frame.
    ImGuiContext& g = *ImGui::GetCurrentContext();
    m_saved_input_events.resize(g.InputEventsQueue.Size);
    for (int i = 0; i < g.InputEventsQueue.Size; ++i)
        m_saved_input_events[i] = g.InputEventsQueue[i];
    g.InputEventsQueue.resize(0);

    ImGui::NewFrame(); // throwaway frame — absorbs game UI calls with no input
}

void ImGuiLayer::begin_frame_editor_after_absorb(int w, int h, float dt) {
    if (!m_initialized || !m_editor_mode) return;
    // Discard the throwaway frame (EndFrame without Render = no GPU submission).
    ImGui::EndFrame();

    // Restore the saved input events so the editor frame sees correct input.
    ImGuiContext& g = *ImGui::GetCurrentContext();
    g.InputEventsQueue.resize(0);
    for (const auto& ev : m_saved_input_events)
        g.InputEventsQueue.push_back(ev);
    m_saved_input_events.clear();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    io.DeltaTime = (dt > 0.0f) ? dt : (1.0f / 60.0f);
    ImGui::NewFrame(); // clean editor frame with full input
}

void ImGuiLayer::discard_frame() {
    if (!m_initialized) return;
    if (ImGui::GetCurrentContext() && ImGui::GetFrameCount() > 0) {
        ImGui::EndFrame();
    }
}

void ImGuiLayer::end_frame() {
    if (!m_initialized) return;
    ImGui::Render();
}

} // namespace sol
