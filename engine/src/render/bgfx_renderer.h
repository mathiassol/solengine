#pragma once
#include "sol/render/renderer.h"
#include <glm/glm.hpp>

namespace sol {

class BgfxRenderer final : public Renderer {
public:
    bool init    (Window& window)                          override;
    void shutdown()                                        override;
    void begin_frame()                                     override;
    void end_frame()                                       override;
    void resize  (int w, int h)                            override;
    void submit  (const Mesh& mesh, const Material& mat,
                  const glm::mat4& transform)              override;

private:
    bool init_shadow_pipeline_();
    bool init_pbr_pipeline_();
    void shutdown_pipelines_();

    bool     m_initialized = false;

    // ---- Shadow pass (view 0) ----
    uint16_t m_shadow_fb      = 0xffff; // FrameBufferHandle idx
    uint16_t m_shadow_tex     = 0xffff; // D16 depth texture idx (for binding to PBR pass)
    uint16_t m_shadow_prog    = 0xffff; // ProgramHandle idx

    // ---- PBR pass (view 1) ----
    uint16_t m_pbr_prog       = 0xffff; // ProgramHandle idx

    // ---- Uniforms (PBR) ----
    uint16_t m_u_baseColor    = 0xffff;
    uint16_t m_u_pbr0         = 0xffff; // x=metallic, y=roughness
    uint16_t m_u_emissive     = 0xffff;
    uint16_t m_u_params       = 0xffff; // x=lit, y=hasAlbedo, z=hasNormal, w=hasMR
    uint16_t m_u_camPos       = 0xffff;
    uint16_t m_u_ambient      = 0xffff;
    uint16_t m_u_lightCount   = 0xffff;
    uint16_t m_u_lightMtx     = 0xffff; // mat4 for shadow coord
    uint16_t m_u_shadowConfig = 0xffff; // x=shadow active
    uint16_t m_u_lightData0   = 0xffff; // vec4[8]: xyz=dir/pos, w=type
    uint16_t m_u_lightData1   = 0xffff; // vec4[8]: rgb=color, a=intensity
    uint16_t m_u_lightData2   = 0xffff; // vec4[8]: xyz=spot_dir, w=range
    uint16_t m_u_lightData3   = 0xffff; // vec4[8]: x=inner_cos, y=outer_cos

    // ---- Samplers ----
    uint16_t m_s_albedo       = 0xffff;
    uint16_t m_s_normalMap    = 0xffff;
    uint16_t m_s_mrMap        = 0xffff;
    uint16_t m_s_shadowMap    = 0xffff;

    // ---- Textures (fallbacks) ----
    uint16_t m_white_tex      = 0xffff; // 1x1 white RGBA8
    uint16_t m_flat_normal    = 0xffff; // 1x1 flat normal (128,128,255,255)
    uint16_t m_white_mr       = 0xffff; // 1x1 MR (R=0, G=roughness=255, B=metallic=0)

    // ---- Shadow matrix (computed in begin_frame from previous frame's light) ----
    glm::mat4 m_light_mtx     {1.0f};
    bool      m_has_shadow    = false;  // was there a shadow-casting dir light last frame?
};

} // namespace sol
