#pragma once
#include <imgui.h>

namespace sol {

// UE5 dark editor palette
namespace UE5Colors {
    inline constexpr ImVec4 BgDeep        { 0.102f, 0.102f, 0.102f, 1.0f };
    inline constexpr ImVec4 BgPanel       { 0.145f, 0.145f, 0.145f, 1.0f };
    inline constexpr ImVec4 BgChild       { 0.118f, 0.118f, 0.118f, 1.0f };
    inline constexpr ImVec4 BgPopup       { 0.176f, 0.176f, 0.176f, 1.0f };
    inline constexpr ImVec4 BgTitlebar    { 0.086f, 0.086f, 0.086f, 1.0f };
    inline constexpr ImVec4 BgTabBar      { 0.102f, 0.102f, 0.102f, 1.0f };
    inline constexpr ImVec4 BgBottomBar   { 0.067f, 0.067f, 0.067f, 1.0f };
    inline constexpr ImVec4 BgFrame       { 0.102f, 0.102f, 0.102f, 1.0f };
    inline constexpr ImVec4 BgFrameHov    { 0.176f, 0.176f, 0.176f, 1.0f };
    inline constexpr ImVec4 BgFrameAct    { 0.220f, 0.220f, 0.220f, 1.0f };
    inline constexpr ImVec4 Border        { 0.239f, 0.239f, 0.239f, 1.0f };
    inline constexpr ImVec4 Text          { 0.800f, 0.800f, 0.800f, 1.0f };
    inline constexpr ImVec4 TextDim       { 0.400f, 0.400f, 0.400f, 1.0f };
    inline constexpr ImVec4 Accent        { 0.106f, 0.431f, 0.761f, 1.0f };
    inline constexpr ImVec4 AccentHov     { 0.180f, 0.510f, 0.839f, 1.0f };
    inline constexpr ImVec4 BtnNormal     { 0.176f, 0.176f, 0.176f, 1.0f };
    inline constexpr ImVec4 BtnHov        { 0.239f, 0.239f, 0.239f, 1.0f };
    inline constexpr ImVec4 Header        { 0.157f, 0.157f, 0.157f, 1.0f };
    inline constexpr ImVec4 HeaderHov     { 0.239f, 0.239f, 0.239f, 1.0f };
    inline constexpr ImVec4 Separator     { 0.239f, 0.239f, 0.239f, 1.0f };
    inline constexpr ImVec4 Tab           { 0.118f, 0.118f, 0.118f, 1.0f };
    inline constexpr ImVec4 TabActive     { 0.145f, 0.145f, 0.145f, 1.0f };
    inline constexpr ImVec4 TabHov        { 0.176f, 0.176f, 0.176f, 1.0f };
    inline constexpr ImVec4 Green         { 0.180f, 0.490f, 0.180f, 1.0f };
    inline constexpr ImVec4 Red           { 0.600f, 0.160f, 0.160f, 1.0f };
}

void ApplyUE5Theme(ImGuiStyle& style);

} // namespace sol
