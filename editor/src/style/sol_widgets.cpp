#include "sol_widgets.h"
#include "ue5_theme.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cctype>
#include <cstring>
#include <string>

namespace sol {

// ── Internal helpers ─────────────────────────────────────────────────────────

static ImU32 sectionAccentColor(const char* name) {
    std::string n(name);
    for (char& c : n)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (n.find("transform")   != std::string::npos ||
        n.find("position")    != std::string::npos ||
        n.find("rotation")    != std::string::npos ||
        n.find("scale")       != std::string::npos)  return IM_COL32(220, 130,  50, 255);
    if (n.find("mesh")        != std::string::npos ||
        n.find("model")       != std::string::npos ||
        n.find("material")    != std::string::npos)   return IM_COL32( 60, 130, 200, 255);
    if (n.find("physics")     != std::string::npos ||
        n.find("collider")    != std::string::npos ||
        n.find("body")        != std::string::npos)   return IM_COL32( 80, 190,  80, 255);
    if (n.find("light")       != std::string::npos)   return IM_COL32(220, 200,  50, 255);
    if (n.find("script")      != std::string::npos ||
        n.find("lua")         != std::string::npos)   return IM_COL32(160,  80, 200, 255);
    if (n.find("camera")      != std::string::npos)   return IM_COL32( 80, 200, 200, 255);
    if (n.find("world")       != std::string::npos ||
        n.find("environment") != std::string::npos)   return IM_COL32( 80, 180, 120, 255);
    if (n.find("audio")       != std::string::npos)   return IM_COL32(200, 120,  80, 255);
    return IM_COL32(100, 130, 180, 255);
}

// ── SolInputFloat ─────────────────────────────────────────────────────────────

bool SolInputFloat(const char* id, float* v, float speed, ImU32 border_color, float width) {
    ImGui::SetNextItemWidth(width < 0.0f ? ImGui::GetContentRegionAvail().x : width);
    const bool changed = ImGui::DragFloat(id, v, speed);
    if (border_color != 0) {
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(mn, {mn.x + 3.0f, mx.y}, border_color);
    }
    return changed;
}

// ── SolVec3 ──────────────────────────────────────────────────────────────────

bool SolVec3(const char* id, glm::vec3* v, float speed,
             bool* any_activated, bool* any_deactivated)
{
    constexpr ImU32 kColors[3] = {
        IM_COL32(200, 60,  60,  255),   // X = red
        IM_COL32( 80, 180, 80,  255),   // Y = green
        IM_COL32( 60, 100, 200, 255),   // Z = blue
    };

    const float avail   = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float fieldW  = (avail - spacing * 2.0f) / 3.0f;

    bool changed     = false;
    bool activated   = false;
    bool deactivated = false;

    ImGui::PushID(id);
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(i);

        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::DragFloat("##v", &(*v)[i], speed)) changed = true;
        if (ImGui::IsItemActivated())            activated   = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) deactivated = true;

        // Coloured left-edge strip
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(mn, {mn.x + 3.0f, mx.y}, kColors[i]);

        ImGui::PopID();
    }
    ImGui::PopID();

    if (any_activated)   *any_activated   = activated;
    if (any_deactivated) *any_deactivated = deactivated;
    return changed;
}

// ── SolSection ───────────────────────────────────────────────────────────────

bool SolSection(const char* label, ImGuiTreeNodeFlags extra_flags, bool default_open) {
    ImGuiTreeNodeFlags flags = extra_flags;
    if (default_open) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    const bool open = ImGui::CollapsingHeader(label, flags);
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(
        mn, {mn.x + 3.0f, mx.y}, sectionAccentColor(label));
    return open;
}

// ── SolAssetPicker ────────────────────────────────────────────────────────────

bool SolAssetPicker(const char* id, std::string& path,
                    bool* browse_clicked, float btn_width)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float txtW  = std::max(1.0f, avail - btn_width - ImGui::GetStyle().ItemSpacing.x);

    ImGui::PushID(id);

    ImGui::SetNextItemWidth(txtW);
    char buf[512];
    std::strncpy(buf, path.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    bool changed = ImGui::InputText("##txt", buf, sizeof(buf));
    if (changed) path = buf;

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        UE5Colors::BtnNormal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UE5Colors::BtnHov);
    const bool browse = ImGui::Button("...##browse", ImVec2(btn_width, 0.0f));
    ImGui::PopStyleColor(2);

    if (browse_clicked) *browse_clicked = browse;

    ImGui::PopID();
    return changed;
}

// ── SolIconButton ─────────────────────────────────────────────────────────────

bool SolIconButton(const char* icon, const char* tooltip, ImVec4 color) {
    const bool ghost = (color.w <= 0.0f);
    if (ghost) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UE5Colors::BtnHov);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(color.x * 0.85f, color.y * 0.85f, color.z * 0.85f, color.w));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(color.x, color.y, color.z, color.w));
    }

    const float sz      = ImGui::GetFrameHeight();
    const bool  clicked = ImGui::Button(icon, ImVec2(sz, sz));
    ImGui::PopStyleColor(2);

    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

} // namespace sol
