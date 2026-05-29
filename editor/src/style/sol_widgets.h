#pragma once
#include <imgui.h>
#include <glm/glm.hpp>
#include <string>

namespace sol {

/// Single DragFloat with a solid 3-px coloured border strip on the left edge.
/// border_color = 0 means no strip. width = -1 means fill available width.
bool SolInputFloat(const char* id, float* v, float speed = 0.01f,
                   ImU32 border_color = 0, float width = -1.0f);

/// Three-axis XYZ drag row with X=red, Y=green, Z=blue left-border strips.
/// any_activated / any_deactivated aggregate IsItemActivated /
/// IsItemDeactivatedAfterEdit across all three sub-fields so callers can
/// drive undo logic without breaking on a single-item assumption.
bool SolVec3(const char* id, glm::vec3* v, float speed = 0.01f,
             bool* any_activated = nullptr, bool* any_deactivated = nullptr);

/// Collapsible section header with a keyword-matched coloured left accent.
/// Starts open by default (DefaultOpen). Pass extra ImGui flags if needed.
/// Set default_open = false for sections that should start collapsed.
bool SolSection(const char* label, ImGuiTreeNodeFlags extra_flags = 0,
                bool default_open = true);

/// Inline asset-path text input + "..." browse button on one row.
/// Returns true when the text field is edited.
/// Sets *browse_clicked when the browse button is pressed.
bool SolAssetPicker(const char* id, std::string& path,
                    bool* browse_clicked = nullptr, float btn_width = 28.0f);

/// Square icon/text button with an optional tooltip.
/// color = {0,0,0,0} → transparent (ghost) background.
bool SolIconButton(const char* icon, const char* tooltip = nullptr,
                   ImVec4 color = {0.f, 0.f, 0.f, 0.f});

} // namespace sol
