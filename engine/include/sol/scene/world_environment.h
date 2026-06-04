#pragma once
#include "sol/scene/node.h"
#include <glm/glm.hpp>
#include <string>

namespace sol {

class SOL_API WorldEnvironment : public Node {
public:
    // Sky  (sky_mode 0 = Procedural Nishita, 1 = HDR Panorama)
    int         sky_mode  = 0;
    std::string hdr_path;

    // Procedural sky / time-of-day atmosphere
    float time_of_day       = 14.5f;
    bool  auto_sun          = true;
    float latitude          = 45.0f;
    float turbidity         = 2.5f;
    float sun_intensity     = 20.0f;
    float rayleigh_scale    = 1.0f;
    float mie_strength      = 1.0f;
    float sun_bloom_mult    = 50.0f;
    float night_brightness  = 1.0f;
    float sky_exposure      = 1.0f;
    float sun_disk_size_deg = 0.53f;

    // Sky color controls — mix with physics to create artistic looks
    glm::vec3 sky_tint        {1.0f,   1.0f,  1.0f};    // multiply scattered sky
    glm::vec3 sun_color_tint  {1.0f,   1.0f,  1.0f};    // tint the sun disk
    glm::vec3 night_sky_color {0.001f, 0.002f, 0.008f}; // base night sky color
    glm::vec3 ground_color    {0.03f,  0.025f, 0.02f};  // below-horizon ground tone
    float     star_density    = 0.003f;  // fraction of sky that has stars
    float     star_brightness = 2.5f;   // star brightness multiplier

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
    bool      vol_enabled          = false;
    float     vol_near             = 0.5f;
    float     vol_far              = 64.0f;

    bool      fog_enabled          = false;
    float     fog_density          = 0.05f;
    float     fog_scattering       = 0.6f;
    glm::vec3 fog_albedo           = {1.0f, 1.0f, 1.0f};
    float     fog_g                = 0.3f;

    bool      height_fog_enabled   = false;
    float     height_fog_density   = 0.1f;
    float     height_fog_scattering= 0.6f;
    float     height_fog_base      = 0.0f;
    float     height_fog_falloff   = 10.0f;
    glm::vec3 height_fog_albedo    = {0.8f, 0.87f, 1.0f};

    float     vol_sun_intensity    = 1.0f;
    float     vol_shadow_strength  = 1.0f;

    // Fog noise (procedural FBM, computed in density shader)
    bool  fog_noise_enabled    = false;
    float fog_noise_scale      = 0.05f;   // world-space frequency
    float fog_noise_speed      = 0.02f;   // animation speed (units/sec)
    float fog_noise_strength   = 0.8f;    // amplitude 0..1 (0=no noise, 1=full modulation)
    int   fog_noise_octaves    = 4;       // FBM octaves
    float fog_noise_lacunarity = 2.0f;    // octave frequency multiplier
    float fog_noise_gain       = 0.5f;    // octave amplitude falloff

    // Anti-aliasing
    int   aa_mode        = 4;
    float taa_blend      = 0.1f;
    float taa_sharpening = 0.2f;

    const char* type_name() const override { return "WorldEnvironment"; }
    void on_render(Engine& engine, const glm::mat4& world_xform) override;
};

} // namespace sol
