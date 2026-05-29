#pragma once
#include "sol/scene/node.h"
#include <glm/glm.hpp>
#include <string>

namespace sol {

class SOL_API WorldEnvironment : public Node {
public:
    // Sky
    int         sky_mode     = 0;
    std::string hdr_path;
    glm::vec3   zenith_color  {0.08f, 0.15f, 0.40f};
    glm::vec3   horizon_color {0.50f, 0.60f, 0.70f};
    glm::vec3   sun_color     {3.0f,  2.5f,  2.0f};
    float       sun_disk_size = 1.8f;
    bool        follow_sun    = true;

    // Ambient
    glm::vec3 ambient_color     {0.30f, 0.30f, 0.35f};
    float     ambient_intensity = 1.0f;

    // Tonemapping / exposure
    int   tonemap_mode = 3;
    float exposure     = 1.0f;

    // Bloom
    bool  bloom_enabled   = true;
    float bloom_threshold = 1.2f;
    float bloom_intensity = 0.15f;

    // SSAO
    bool  ssao_enabled  = false;
    float ssao_radius   = 0.5f;
    float ssao_bias     = 0.025f;
    float ssao_power    = 2.0f;
    float ssao_strength = 1.0f;

    // SSR
    bool  ssr_enabled          = false;
    int   ssr_steps            = 64;
    float ssr_thickness        = 0.3f;
    float ssr_max_distance     = 8.0f;
    float ssr_roughness_cutoff = 0.5f;
    float ssr_intensity        = 1.0f;

    // IBL
    bool  ibl_enabled       = true;
    float ibl_intensity     = 1.0f;
    float ibl_diffuse_scale  = 1.0f;
    float ibl_specular_scale = 1.0f;

    // Volumetrics
    bool  vol_enabled    = false;
    float vol_density    = 0.05f;
    float vol_scattering = 0.3f;
    float vol_g          = 0.0f;
    int   vol_march_steps = 32;

    // Anti-aliasing
    int   aa_mode        = 4;
    float taa_blend      = 0.1f;
    float taa_sharpening = 0.2f;

    const char* type_name() const override { return "WorldEnvironment"; }
    void on_render(Engine& engine, const glm::mat4& world_xform) override;
};

} // namespace sol
