#include "ui/imgui_layer.h"
#include "sol/log.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/math.h>

// Pre-compiled shader binaries from bgfx examples (covers DX11, GL, Vulkan, Metal, etc.)
#include "ui/vs_ocornut_imgui.bin.h"
#include "ui/fs_ocornut_imgui.bin.h"

static const bgfx::EmbeddedShader s_shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END()
};

namespace sol {

struct BgfxImGui {
    bgfx::VertexLayout  layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fontTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sTex    = BGFX_INVALID_HANDLE;
    static constexpr bgfx::ViewId VIEW = 255;
};
static BgfxImGui g_bgfx;

bool ImGuiLayer::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui::StyleColorsDark();

    // Create bgfx program from embedded shader binaries
    bgfx::RendererType::Enum type = bgfx::getRendererType();
    g_bgfx.program = bgfx::createProgram(
        bgfx::createEmbeddedShader(s_shaders, type, "vs_ocornut_imgui"),
        bgfx::createEmbeddedShader(s_shaders, type, "fs_ocornut_imgui"),
        true);

    // Vertex layout must match ImDrawVert exactly (pos.xy + uv.xy + col.rgba)
    g_bgfx.layout
        .begin()
        .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    g_bgfx.sTex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

    // Upload font atlas to GPU
    unsigned char* pixels = nullptr;
    int fw = 0, fh = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
    g_bgfx.fontTex = bgfx::createTexture2D(
        (uint16_t)fw, (uint16_t)fh, false, 1,
        bgfx::TextureFormat::RGBA8, 0,
        bgfx::copy(pixels, fw * fh * 4));
    io.Fonts->SetTexID((ImTextureID)(uintptr_t)g_bgfx.fontTex.idx);

    m_initialized = true;
    SOL_INFO("ImGui initialized");
    return true;
}

void ImGuiLayer::shutdown() {
    if (!m_initialized) return;
    if (bgfx::isValid(g_bgfx.fontTex)) bgfx::destroy(g_bgfx.fontTex);
    if (bgfx::isValid(g_bgfx.sTex))    bgfx::destroy(g_bgfx.sTex);
    if (bgfx::isValid(g_bgfx.program)) bgfx::destroy(g_bgfx.program);
    g_bgfx = {};
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

    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdListsCount == 0) return;

    int32_t fbW = (int32_t)(dd->DisplaySize.x * dd->FramebufferScale.x);
    int32_t fbH = (int32_t)(dd->DisplaySize.y * dd->FramebufferScale.y);
    if (fbW <= 0 || fbH <= 0) return;

    // Set up an orthographic projection for 2D UI on view 255 (renders last, on top)
    bgfx::setViewName(BgfxImGui::VIEW, "ImGui");
    bgfx::setViewMode(BgfxImGui::VIEW, bgfx::ViewMode::Sequential);

    float ortho[16];
    bx::mtxOrtho(ortho,
        dd->DisplayPos.x, dd->DisplayPos.x + dd->DisplaySize.x,
        dd->DisplayPos.y + dd->DisplaySize.y, dd->DisplayPos.y,
        0.0f, 1000.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(BgfxImGui::VIEW, nullptr, ortho);
    bgfx::setViewRect(BgfxImGui::VIEW, 0, 0, (uint16_t)fbW, (uint16_t)fbH);

    const ImVec2 clipOff   = dd->DisplayPos;
    const ImVec2 clipScale = dd->FramebufferScale;

    bgfx::Encoder* enc = bgfx::begin();
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* cl = dd->CmdLists[n];
        uint32_t numV = (uint32_t)cl->VtxBuffer.Size;
        uint32_t numI = (uint32_t)cl->IdxBuffer.Size;

        if (bgfx::getAvailTransientVertexBuffer(numV, g_bgfx.layout) < numV) break;
        if (bgfx::getAvailTransientIndexBuffer(numI) < numI) break;

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer  tib;
        bgfx::allocTransientVertexBuffer(&tvb, numV, g_bgfx.layout);
        bgfx::allocTransientIndexBuffer (&tib, numI, sizeof(ImDrawIdx) == 4);
        memcpy(tvb.data, cl->VtxBuffer.Data, numV * sizeof(ImDrawVert));
        memcpy(tib.data, cl->IdxBuffer.Data, numI  * sizeof(ImDrawIdx));

        for (const ImDrawCmd& cmd : cl->CmdBuffer) {
            if (cmd.UserCallback) { cmd.UserCallback(cl, &cmd); continue; }
            if (!cmd.ElemCount) continue;

            bgfx::TextureHandle th;
            th.idx = (uint16_t)(uintptr_t)cmd.TextureId;
            if (!bgfx::isValid(th)) th = g_bgfx.fontTex;

            float cx = (cmd.ClipRect.x - clipOff.x) * clipScale.x;
            float cy = (cmd.ClipRect.y - clipOff.y) * clipScale.y;
            float cz = (cmd.ClipRect.z - clipOff.x) * clipScale.x;
            float cw = (cmd.ClipRect.w - clipOff.y) * clipScale.y;

            if (cx < fbW && cy < fbH && cz >= 0.0f && cw >= 0.0f) {
                auto sx = (uint16_t)bx::max(cx, 0.0f);
                auto sy = (uint16_t)bx::max(cy, 0.0f);
                enc->setScissor(sx, sy,
                    (uint16_t)(bx::min(cz, 65535.0f) - sx),
                    (uint16_t)(bx::min(cw, 65535.0f) - sy));
                enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
                    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
                enc->setTexture(0, g_bgfx.sTex, th);
                enc->setVertexBuffer(0, &tvb, cmd.VtxOffset, numV);
                enc->setIndexBuffer(&tib, cmd.IdxOffset, cmd.ElemCount);
                enc->submit(BgfxImGui::VIEW, g_bgfx.program);
            }
        }
    }
    bgfx::end(enc);
}

} // namespace sol
