#include "ue5_theme.h"

namespace sol {

void ApplyUE5Theme(ImGuiStyle& s) {
    using namespace UE5Colors;
    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 3.0f;
    s.FrameRounding     = 3.0f;
    s.PopupRounding     = 4.0f;
    s.ScrollbarRounding = 3.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 3.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
    s.ItemSpacing       = { 6.0f, 4.0f };
    s.ItemInnerSpacing  = { 4.0f, 4.0f };
    s.FramePadding      = { 6.0f, 3.0f };
    s.WindowPadding     = { 8.0f, 6.0f };
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 10.0f;
    s.GrabMinSize       = 8.0f;
    s.TabBarBorderSize  = 0.0f;
    s.DockingSeparatorSize = 2.0f;

    auto& c = s.Colors;
    c[ImGuiCol_WindowBg]             = BgPanel;
    c[ImGuiCol_ChildBg]              = BgChild;
    c[ImGuiCol_PopupBg]              = BgPopup;
    c[ImGuiCol_Border]               = Border;
    c[ImGuiCol_BorderShadow]         = {0,0,0,0};
    c[ImGuiCol_FrameBg]              = BgFrame;
    c[ImGuiCol_FrameBgHovered]       = BgFrameHov;
    c[ImGuiCol_FrameBgActive]        = BgFrameAct;
    c[ImGuiCol_TitleBg]              = BgTitlebar;
    c[ImGuiCol_TitleBgActive]        = BgTitlebar;
    c[ImGuiCol_TitleBgCollapsed]     = BgTitlebar;
    c[ImGuiCol_MenuBarBg]            = BgTitlebar;
    c[ImGuiCol_ScrollbarBg]          = BgDeep;
    c[ImGuiCol_ScrollbarGrab]        = Border;
    c[ImGuiCol_ScrollbarGrabHovered] = BtnHov;
    c[ImGuiCol_ScrollbarGrabActive]  = Accent;
    c[ImGuiCol_CheckMark]            = Accent;
    c[ImGuiCol_SliderGrab]           = Accent;
    c[ImGuiCol_SliderGrabActive]     = AccentHov;
    c[ImGuiCol_Button]               = BtnNormal;
    c[ImGuiCol_ButtonHovered]        = BtnHov;
    c[ImGuiCol_ButtonActive]         = Accent;
    c[ImGuiCol_Header]               = Header;
    c[ImGuiCol_HeaderHovered]        = HeaderHov;
    c[ImGuiCol_HeaderActive]         = Accent;
    c[ImGuiCol_Separator]            = Separator;
    c[ImGuiCol_SeparatorHovered]     = AccentHov;
    c[ImGuiCol_SeparatorActive]      = Accent;
    c[ImGuiCol_ResizeGrip]           = {0,0,0,0};
    c[ImGuiCol_ResizeGripHovered]    = Accent;
    c[ImGuiCol_ResizeGripActive]     = AccentHov;
    c[ImGuiCol_Tab]                  = Tab;
    c[ImGuiCol_TabHovered]           = TabHov;
    c[ImGuiCol_TabSelected]          = TabActive;
    c[ImGuiCol_TabSelectedOverline]  = Accent;
    c[ImGuiCol_TabDimmed]            = Tab;
    c[ImGuiCol_TabDimmedSelected]    = TabActive;
    c[ImGuiCol_TabDimmedSelectedOverline] = {0,0,0,0};
    c[ImGuiCol_DockingPreview]       = Accent;
    c[ImGuiCol_DockingEmptyBg]       = BgDeep;
    c[ImGuiCol_PlotLines]            = Accent;
    c[ImGuiCol_PlotLinesHovered]     = AccentHov;
    c[ImGuiCol_PlotHistogram]        = Accent;
    c[ImGuiCol_PlotHistogramHovered] = AccentHov;
    c[ImGuiCol_TableHeaderBg]        = Header;
    c[ImGuiCol_TableBorderStrong]    = Border;
    c[ImGuiCol_TableBorderLight]     = Separator;
    c[ImGuiCol_TableRowBg]           = {0,0,0,0};
    c[ImGuiCol_TableRowBgAlt]        = {1,1,1,0.02f};
    c[ImGuiCol_TextSelectedBg]       = Accent;
    c[ImGuiCol_DragDropTarget]       = AccentHov;
    c[ImGuiCol_NavHighlight]         = Accent;
    c[ImGuiCol_Text]                 = Text;
    c[ImGuiCol_TextDisabled]         = TextDim;
    c[ImGuiCol_NavWindowingHighlight]= {1,1,1,0.7f};
    c[ImGuiCol_NavWindowingDimBg]    = {0.8f,0.8f,0.8f,0.2f};
    c[ImGuiCol_ModalWindowDimBg]     = {0.1f,0.1f,0.1f,0.5f};
}

} // namespace sol
