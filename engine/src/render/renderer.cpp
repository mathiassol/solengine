#include "bgfx_renderer.h"
#include "platform/window.h"
#include "sol/log.h"
#include "sol/render/mesh.h"
#include "sol/render/material.h"
#include "sol/render/texture.h"
#include "sol/render/light.h"

#include <bgfx/bgfx.h>
#define BGFX_PLATFORM_SUPPORTS_WGSL 0
#include <bgfx/embedded_shader.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <cmath>

// Shadow pass shaders
#include "shaders/dxbc/vs_shadow.sc.bin.h"
#include "shaders/dxbc/fs_shadow.sc.bin.h"
#include "shaders/dxil/vs_shadow.sc.bin.h"
#include "shaders/dxil/fs_shadow.sc.bin.h"
#include "shaders/glsl/vs_shadow.sc.bin.h"
#include "shaders/glsl/fs_shadow.sc.bin.h"
#include "shaders/essl/vs_shadow.sc.bin.h"
#include "shaders/essl/fs_shadow.sc.bin.h"
#include "shaders/spirv/vs_shadow.sc.bin.h"
#include "shaders/spirv/fs_shadow.sc.bin.h"

// PBR pass shaders
#include "shaders/dxbc/vs_pbr.sc.bin.h"
#include "shaders/dxbc/fs_pbr.sc.bin.h"
#include "shaders/dxil/vs_pbr.sc.bin.h"
#include "shaders/dxil/fs_pbr.sc.bin.h"
#include "shaders/150/vs_pbr.sc.bin.h"
#include "shaders/150/fs_pbr.sc.bin.h"
#include "shaders/essl/vs_pbr.sc.bin.h"
#include "shaders/essl/fs_pbr.sc.bin.h"
#include "shaders/spirv/vs_pbr.sc.bin.h"
#include "shaders/spirv/fs_pbr.sc.bin.h"
// Profile '150' maps to 'shaders/150/' but BGFX_EMBEDDED_SHADER expects '_glsl' suffix
#define vs_pbr_glsl  vs_pbr_150
#define fs_pbr_glsl  fs_pbr_150

static const bgfx::EmbeddedShader s_shadow_shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_shadow),
    BGFX_EMBEDDED_SHADER(fs_shadow),
    BGFX_EMBEDDED_SHADER_END()
};

static const bgfx::EmbeddedShader s_pbr_shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_pbr),
    BGFX_EMBEDDED_SHADER(fs_pbr),
    BGFX_EMBEDDED_SHADER_END()
};

static constexpr uint8_t VIEW_SHADOW = 0;
static constexpr uint8_t VIEW_MAIN   = 1;
static constexpr int     SHADOW_SIZE = 4096;
static constexpr int     MAX_LIGHTS  = 8;

namespace sol {

bool BgfxRenderer::init(Window& window) {
    bgfx::renderFrame();

    bgfx::Init init;
    init.type     = bgfx::RendererType::Count;
    init.vendorId = BGFX_PCI_ID_NONE;
    init.platformData.nwh  = window.native_handle();
    init.platformData.ndt  = window.native_display();
    init.resolution.width  = (uint32_t)window.width();
    init.resolution.height = (uint32_t)window.height();
    init.resolution.reset  = BGFX_RESET_VSYNC;

    if (!bgfx::init(init)) {
        SOL_ERROR("bgfx::init failed");
        return false;
    }

    m_w = (uint16_t)window.width();
    m_h = (uint16_t)window.height();

    if (!init_shadow_pipeline_() || !init_pbr_pipeline_()) {
        SOL_ERROR("Renderer: pipeline init failed");
        bgfx::shutdown();
        return false;
    }

    m_initialized = true;
    SOL_INFO(std::string("Renderer initialized (bgfx, ") +
             bgfx::getRendererName(bgfx::getRendererType()) + ")");
    return true;
}

bool BgfxRenderer::init_shadow_pipeline_() {
    // Shadow map texture — bilinear + comparison (gives hardware 2×2 PCF base)
    const uint64_t shadowTexFlags =
        BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    bgfx::TextureHandle shadowTex = bgfx::createTexture2D(
        SHADOW_SIZE, SHADOW_SIZE, false, 1,
        bgfx::TextureFormat::D16, shadowTexFlags);
    if (!bgfx::isValid(shadowTex)) return false;
    m_shadow_tex = shadowTex.idx;

    bgfx::FrameBufferHandle shadowFb = bgfx::createFrameBuffer(1, &shadowTex, false);
    if (!bgfx::isValid(shadowFb)) return false;
    m_shadow_fb = shadowFb.idx;

    // Shadow shaders
    bgfx::RendererType::Enum type = bgfx::getRendererType();
    auto vsh = bgfx::createEmbeddedShader(s_shadow_shaders, type, "vs_shadow");
    auto fsh = bgfx::createEmbeddedShader(s_shadow_shaders, type, "fs_shadow");
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        if (bgfx::isValid(vsh)) bgfx::destroy(vsh);
        if (bgfx::isValid(fsh)) bgfx::destroy(fsh);
        return false;
    }
    auto prog = bgfx::createProgram(vsh, fsh, true);
    if (!bgfx::isValid(prog)) return false;
    m_shadow_prog = prog.idx;
    return true;
}

bool BgfxRenderer::init_pbr_pipeline_() {
    bgfx::RendererType::Enum type = bgfx::getRendererType();
    auto vsh = bgfx::createEmbeddedShader(s_pbr_shaders, type, "vs_pbr");
    auto fsh = bgfx::createEmbeddedShader(s_pbr_shaders, type, "fs_pbr");
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        if (bgfx::isValid(vsh)) bgfx::destroy(vsh);
        if (bgfx::isValid(fsh)) bgfx::destroy(fsh);
        return false;
    }
    auto prog = bgfx::createProgram(vsh, fsh, true);
    if (!bgfx::isValid(prog)) return false;
    m_pbr_prog = prog.idx;

    // Uniforms
    m_u_baseColor    = bgfx::createUniform("u_baseColor",    bgfx::UniformType::Vec4).idx;
    m_u_pbr0         = bgfx::createUniform("u_pbr0",         bgfx::UniformType::Vec4).idx;
    m_u_emissive     = bgfx::createUniform("u_emissive",     bgfx::UniformType::Vec4).idx;
    m_u_params       = bgfx::createUniform("u_params",       bgfx::UniformType::Vec4).idx;
    m_u_camPos       = bgfx::createUniform("u_camPos",       bgfx::UniformType::Vec4).idx;
    m_u_ambient      = bgfx::createUniform("u_ambient",      bgfx::UniformType::Vec4).idx;
    m_u_lightCount   = bgfx::createUniform("u_lightCount",   bgfx::UniformType::Vec4).idx;
    m_u_lightMtx     = bgfx::createUniform("u_lightMtx",     bgfx::UniformType::Mat4).idx;
    m_u_shadowConfig = bgfx::createUniform("u_shadowConfig", bgfx::UniformType::Vec4).idx;
    m_u_lightData0   = bgfx::createUniform("u_lightData0",   bgfx::UniformType::Vec4, MAX_LIGHTS).idx;
    m_u_lightData1   = bgfx::createUniform("u_lightData1",   bgfx::UniformType::Vec4, MAX_LIGHTS).idx;
    m_u_lightData2   = bgfx::createUniform("u_lightData2",   bgfx::UniformType::Vec4, MAX_LIGHTS).idx;
    m_u_lightData3   = bgfx::createUniform("u_lightData3",   bgfx::UniformType::Vec4, MAX_LIGHTS).idx;

    // Sampler uniforms
    m_s_albedo    = bgfx::createUniform("s_albedo",    bgfx::UniformType::Sampler).idx;
    m_s_normalMap = bgfx::createUniform("s_normalMap", bgfx::UniformType::Sampler).idx;
    m_s_mrMap     = bgfx::createUniform("s_mrMap",     bgfx::UniformType::Sampler).idx;
    m_s_shadowMap = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler).idx;

    // Fallback textures
    {
        const uint32_t white = 0xffffffff;
        m_white_tex = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,
                                            bgfx::copy(&white,sizeof(white))).idx;
    }
    {
        // Flat normal: R=128, G=128, B=255, A=255 → tangent-space (0,0,1)
        // bgfx RGBA8: stored as R,G,B,A in memory; as uint32_t little-endian: 0xAABBGGRR
        // So R=0x80, G=0x80, B=0xFF, A=0xFF → 0xFF FF 80 80 = 0xFFFF8080
        const uint32_t flat = 0xFFFF8080u;
        m_flat_normal = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,
                                              bgfx::copy(&flat,sizeof(flat))).idx;
    }
    {
        // MR fallback: G=255 (full rough), B=0 (non-metallic), R=0, A=255
        // 0xAABBGGRR → A=255, B=0, G=255, R=0 → 0xFF00FF00
        const uint32_t mr = 0xFF00FF00u;
        m_white_mr = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,
                                           bgfx::copy(&mr,sizeof(mr))).idx;
    }

    return true;
}

void BgfxRenderer::shutdown_pipelines_() {
    auto di = [](uint16_t& idx, auto h) {
        using H = decltype(h);
        if (idx != 0xffff) { H hh{idx}; bgfx::destroy(hh); idx = 0xffff; }
    };
    di(m_white_mr,      bgfx::TextureHandle{});
    di(m_flat_normal,   bgfx::TextureHandle{});
    di(m_white_tex,     bgfx::TextureHandle{});
    di(m_s_shadowMap,   bgfx::UniformHandle{});
    di(m_s_mrMap,       bgfx::UniformHandle{});
    di(m_s_normalMap,   bgfx::UniformHandle{});
    di(m_s_albedo,      bgfx::UniformHandle{});
    di(m_u_lightData3,  bgfx::UniformHandle{});
    di(m_u_lightData2,  bgfx::UniformHandle{});
    di(m_u_lightData1,  bgfx::UniformHandle{});
    di(m_u_lightData0,  bgfx::UniformHandle{});
    di(m_u_shadowConfig,bgfx::UniformHandle{});
    di(m_u_lightMtx,    bgfx::UniformHandle{});
    di(m_u_lightCount,  bgfx::UniformHandle{});
    di(m_u_ambient,     bgfx::UniformHandle{});
    di(m_u_camPos,      bgfx::UniformHandle{});
    di(m_u_params,      bgfx::UniformHandle{});
    di(m_u_emissive,    bgfx::UniformHandle{});
    di(m_u_pbr0,        bgfx::UniformHandle{});
    di(m_u_baseColor,   bgfx::UniformHandle{});
    di(m_pbr_prog,      bgfx::ProgramHandle{});
    di(m_shadow_prog,   bgfx::ProgramHandle{});
    di(m_shadow_fb,     bgfx::FrameBufferHandle{});
    // Note: m_shadow_tex is owned by the framebuffer (created with destroy=false above),
    // so destroy it separately
    di(m_shadow_tex,    bgfx::TextureHandle{});
}

void BgfxRenderer::shutdown() {
    if (m_initialized) {
        shutdown_pipelines_();
        bgfx::shutdown();
        m_initialized = false;
    }
}

void BgfxRenderer::resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    m_w = (uint16_t)w; m_h = (uint16_t)h;
    bgfx::reset((uint32_t)w, (uint32_t)h, BGFX_RESET_VSYNC);
    bgfx::setViewRect(VIEW_MAIN, 0, 0, m_w, m_h);
}

void BgfxRenderer::begin_frame() {
    clear_lights_();

    const bool  homogeneous = bgfx::getCaps()->homogeneousDepth;

    // --- Shadow pass (view 0) ---
    {
        bgfx::FrameBufferHandle fb{m_shadow_fb};
        bgfx::setViewFrameBuffer(VIEW_SHADOW, fb);
        bgfx::setViewClear(VIEW_SHADOW, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
        bgfx::setViewRect(VIEW_SHADOW, 0, 0, SHADOW_SIZE, SHADOW_SIZE);
        bgfx::touch(VIEW_SHADOW);
    }

    // --- Main PBR pass (view 1) ---
    {
        bgfx::setViewClear(VIEW_MAIN, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, m_clear, 1.0f, 0);
        bgfx::setViewRect(VIEW_MAIN,  0, 0, m_w, m_h);

        const float aspect = m_h > 0 ? (float)m_w / (float)m_h : 1.0f;
        glm::mat4 view = m_camera.view();
        glm::mat4 proj = m_camera.proj(aspect, homogeneous);
        bgfx::setViewTransform(VIEW_MAIN, &view[0][0], &proj[0][0]);

        bgfx::touch(VIEW_MAIN);
    }

    bgfx::dbgTextClear();
}

void BgfxRenderer::end_frame() {
    bgfx::frame();
}

// Helper: build the light view-proj + crop-bias matrix for the shadow pass.
static glm::mat4 build_shadow_matrix(const glm::vec3& toLightDir,
                                     const glm::vec3& camPos,
                                     bool homogeneous, bool originBL,
                                     float orthoSize, float farDist)
{
    // Light sits far behind the camera in the toLightDir direction
    glm::vec3 lightPos = camPos - toLightDir * (farDist * 0.5f);
    glm::vec3 up = (std::abs(toLightDir.y) > 0.99f)
                   ? glm::vec3(1,0,0) : glm::vec3(0,1,0);

    // bx ortho + lookAt for correct platform depth convention
    float bxLightView[16], bxLightProj[16], bxLightVP[16];
    bx::mtxLookAt(bxLightView,
                  {lightPos.x, lightPos.y, lightPos.z},
                  {camPos.x,   camPos.y,   camPos.z},
                  {up.x,       up.y,       up.z});
    bx::mtxOrtho(bxLightProj,
                 -orthoSize, orthoSize, -orthoSize, orthoSize,
                 0.0f, farDist, 0.0f, homogeneous);
    bx::mtxMul(bxLightVP, bxLightView, bxLightProj);

    // Crop-bias: NDC → UV [0,1]
    float sy   = originBL ?  0.5f : -0.5f;
    float sz   = homogeneous ? 0.5f : 1.0f;
    float tz   = homogeneous ? 0.5f : 0.0f;
    tz -= 0.0005f; // shadow bias 

    float bxCrop[16] = {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f,   sy, 0.0f, 0.0f,
        0.0f, 0.0f,   sz, 0.0f,
        0.5f, 0.5f,   tz, 1.0f,
    };

    float bxFinal[16];
    bx::mtxMul(bxFinal, bxLightVP, bxCrop);

    glm::mat4 result;
    std::memcpy(&result[0][0], bxFinal, 64);
    return result;
}

void BgfxRenderer::submit(const Mesh& mesh, const Material& mat, const glm::mat4& transform) {
    if (!m_initialized || !mesh.valid() || m_pbr_prog == 0xffff) return;

    const bool  homogeneous = bgfx::getCaps()->homogeneousDepth;
    const bool  originBL    = bgfx::getCaps()->originBottomLeft;

    // --- Locate first shadow-casting directional light ---
    const Light* shadowLight = nullptr;
    for (const auto& lt : m_lights) {
        if (lt.type == LightType::Directional && lt.cast_shadow) {
            shadowLight = &lt;
            break;
        }
    }

    // Rebuild shadow view-proj whenever we have a shadow light
    if (shadowLight) {
        glm::mat4 newLightMtx = build_shadow_matrix(
            glm::normalize(shadowLight->direction),
            m_camera.position,
            homogeneous, originBL, 40.0f, 200.0f);

        // Also set the VIEW_SHADOW transform so depth is written correctly
        float bxLV[16], bxLP[16];
        glm::vec3 ld = glm::normalize(shadowLight->direction);
        glm::vec3 lpos = m_camera.position - ld * 100.0f;
        glm::vec3 up = (std::abs(ld.y) > 0.99f) ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
        bx::mtxLookAt(bxLV, {lpos.x,lpos.y,lpos.z},
                      {m_camera.position.x,m_camera.position.y,m_camera.position.z},
                      {up.x,up.y,up.z});
        bx::mtxOrtho(bxLP, -40.0f,40.0f,-40.0f,40.0f, 0.0f,200.0f,0.0f,homogeneous);
        bgfx::setViewTransform(VIEW_SHADOW, bxLV, bxLP);

        m_light_mtx  = newLightMtx;
        m_has_shadow = true;
    }

    // =====================================================
    // SHADOW PASS (view 0)
    // =====================================================
    if (shadowLight) {
        bgfx::setTransform(&transform[0][0]);
        bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{mesh.vbh_idx()});
        bgfx::setIndexBuffer(     bgfx::IndexBufferHandle{mesh.ibh_idx()});

        uint64_t shadowState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        if (!mat.double_sided) shadowState |= BGFX_STATE_CULL_CCW;
        bgfx::setState(shadowState);
        bgfx::submit(VIEW_SHADOW, bgfx::ProgramHandle{m_shadow_prog});
    }

    // =====================================================
    // PBR MAIN PASS (view 1)
    // =====================================================

    // --- Material uniforms ---
    {
        float bc[4] = { mat.base_color.r, mat.base_color.g, mat.base_color.b, mat.base_color.a };
        bgfx::setUniform(bgfx::UniformHandle{m_u_baseColor}, bc);
    }
    {
        float pbr[4] = { mat.metallic, mat.roughness, 0.0f, 0.0f };
        bgfx::setUniform(bgfx::UniformHandle{m_u_pbr0}, pbr);
    }
    {
        float em[4] = { mat.emissive.r, mat.emissive.g, mat.emissive.b, 0.0f };
        bgfx::setUniform(bgfx::UniformHandle{m_u_emissive}, em);
    }
    {
        float p[4] = {
            mat.lit ? 1.0f : 0.0f,
            (mat.albedo     && mat.albedo->valid())     ? 1.0f : 0.0f,
            (mat.normal_map && mat.normal_map->valid()) ? 1.0f : 0.0f,
            (mat.mr_map     && mat.mr_map->valid())     ? 1.0f : 0.0f,
        };
        bgfx::setUniform(bgfx::UniformHandle{m_u_params}, p);
    }
    {
        float cp[4] = { m_camera.position.x, m_camera.position.y, m_camera.position.z, 0.0f };
        bgfx::setUniform(bgfx::UniformHandle{m_u_camPos}, cp);
    }
    {
        float amb[4] = { m_ambient.r, m_ambient.g, m_ambient.b, 0.0f };
        bgfx::setUniform(bgfx::UniformHandle{m_u_ambient}, amb);
    }

    // --- Light data ---
    {
        float lc[4] = { (float)m_lights.size(), 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(bgfx::UniformHandle{m_u_lightCount}, lc);
    }
    {
        float d0[MAX_LIGHTS][4] = {};
        float d1[MAX_LIGHTS][4] = {};
        float d2[MAX_LIGHTS][4] = {};
        float d3[MAX_LIGHTS][4] = {};
        for (int i = 0; i < (int)m_lights.size() && i < MAX_LIGHTS; ++i) {
            const Light& lt = m_lights[i];
            if (lt.type == LightType::Directional) {
                d0[i][0] = lt.direction.x; d0[i][1] = lt.direction.y; d0[i][2] = lt.direction.z;
                d0[i][3] = 0.0f; // type=directional
            } else {
                d0[i][0] = lt.position.x; d0[i][1] = lt.position.y; d0[i][2] = lt.position.z;
                d0[i][3] = (lt.type == LightType::Point) ? 1.0f : 2.0f;
            }
            d1[i][0] = lt.color.r; d1[i][1] = lt.color.g; d1[i][2] = lt.color.b;
            d1[i][3] = lt.intensity;
            d2[i][0] = lt.direction.x; d2[i][1] = lt.direction.y; d2[i][2] = lt.direction.z;
            d2[i][3] = lt.range;
            float innerRad = glm::radians(lt.inner_angle);
            float outerRad = glm::radians(lt.outer_angle);
            d3[i][0] = std::cos(innerRad);
            d3[i][1] = std::cos(outerRad);
            d3[i][2] = 0.0f; d3[i][3] = 0.0f;
        }
        bgfx::setUniform(bgfx::UniformHandle{m_u_lightData0}, d0, MAX_LIGHTS);
        bgfx::setUniform(bgfx::UniformHandle{m_u_lightData1}, d1, MAX_LIGHTS);
        bgfx::setUniform(bgfx::UniformHandle{m_u_lightData2}, d2, MAX_LIGHTS);
        bgfx::setUniform(bgfx::UniformHandle{m_u_lightData3}, d3, MAX_LIGHTS);
    }

    // --- Shadow uniforms ---
    {
        bgfx::setUniform(bgfx::UniformHandle{m_u_lightMtx}, &m_light_mtx[0][0]);
        float sc[4] = { m_has_shadow ? 1.0f : 0.0f,
                        1.0f / (float)SHADOW_SIZE, // texel size for PCF
                        0.0f, 0.0f };
        bgfx::setUniform(bgfx::UniformHandle{m_u_shadowConfig}, sc);
    }

    // --- Textures ---
    bgfx::TextureHandle albedoTex;
    albedoTex.idx = (mat.albedo && mat.albedo->valid()) ? mat.albedo->handle_idx() : m_white_tex;
    bgfx::setTexture(0, bgfx::UniformHandle{m_s_albedo}, albedoTex);

    bgfx::TextureHandle normalTex;
    normalTex.idx = (mat.normal_map && mat.normal_map->valid()) ? mat.normal_map->handle_idx() : m_flat_normal;
    bgfx::setTexture(1, bgfx::UniformHandle{m_s_normalMap}, normalTex);

    bgfx::TextureHandle mrTex;
    mrTex.idx = (mat.mr_map && mat.mr_map->valid()) ? mat.mr_map->handle_idx() : m_white_mr;
    bgfx::setTexture(2, bgfx::UniformHandle{m_s_mrMap}, mrTex);

    bgfx::TextureHandle shadowTex;
    shadowTex.idx = m_shadow_tex;
    bgfx::setTexture(3, bgfx::UniformHandle{m_s_shadowMap}, shadowTex);

    // --- Geometry ---
    bgfx::setTransform(&transform[0][0]);
    bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{mesh.vbh_idx()});
    bgfx::setIndexBuffer(     bgfx::IndexBufferHandle{mesh.ibh_idx()});

    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
    if (!mat.double_sided) state |= BGFX_STATE_CULL_CCW;
    if (mat.base_color.a < 1.0f)
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);

    bgfx::submit(VIEW_MAIN, bgfx::ProgramHandle{m_pbr_prog});
}

} // namespace sol
