#pragma once

namespace sol {

// Runtime-tweakable render settings.
// Access via engine.renderer().settings().
// All changes take effect on the very next frame — no rebuild required.
struct RenderSettings {
    // ---- Post-processing ----------------------------------------
    float exposure        = 1.0f;   // linear exposure multiplier (0.1 .. 4.0)
    int   tonemap_mode    = 0;      // 0=ACES  1=Reinhard  2=Linear(clamp)

    bool  bloom_enabled   = true;
    float bloom_threshold = 1.2f;   // luminance cutoff before blur (0.3 .. 4.0)
    float bloom_intensity = 0.15f;  // blend weight into tonemap (0.0 .. 1.0)

    // ---- Lighting -----------------------------------------------
    bool  shadows_enabled  = true;
    float ambient_scale    = 1.0f;  // multiplied on top of scene-set ambient (0.0 .. 3.0)

    // ---- Cascaded Shadow Maps (CSM) ---------------------------------
    float csm_far    = 100.0f;  // shadow far plane in world units
    float csm_lambda = 0.75f;   // PSSM blend: 0=uniform splits, 1=logarithmic splits

    // ---- Shadow quality -----------------------------------------
    float shadow_bias_const  = 0.00010f;  // constant depth bias
    float shadow_bias_slope  = 0.0012f;   // slope-scaled depth bias
    float shadow_pcf_radius  = 1.5f;      // PCF kernel half-width in texels
    float shadow_pcss_light  = 3.0f;      // PCSS virtual light source size (world units)
    float vsm_light_bleed    = 0.1f;      // VSM light-bleed reduction threshold [0,0.5]
    float vsm_min_variance   = 0.0f;  // EVSM neg: variance is ~1e-11, keep at 0 by default
    int   shadow_quality     = 1;         // 0=Low(8 samp)  1=Medium(16)  2=High(32)

    // ---- Contact shadows (screen-space) -------------------------
    float contact_shadow_distance  = 0.5f;  // ray max length in world units; 0 = disabled
    float contact_shadow_thickness = 0.5f;  // view-space depth tolerance in world units

    // ---- Temporal shadow filtering -----------------------------
    bool  temporal_shadow_enabled  = true;
    float temporal_shadow_alpha    = 0.1f;   // blend weight (0.05=very smooth, 0.5=responsive)
    float temporal_shadow_max_dist = 20.0f;  // view-space depth cutoff; beyond = no temporal blend

    // ---- SSAO ---------------------------------------------------
    bool  ssao_enabled  = true;
    float ssao_radius   = 0.5f;   // hemisphere radius in view-space units
    float ssao_bias     = 0.025f; // depth bias to prevent self-occlusion
    float ssao_power    = 2.0f;   // contrast exponent (>1 darkens occlusion)
    float ssao_strength = 1.0f;   // blend factor (0=off, 1=full SSAO)

    // ---- IBL (Image-Based Lighting) ----------------------------------------
    // Drives ambient when an environment map is loaded (Phase: IBL).
    // ibl_enabled=false keeps the current constant-colour ambient.
    bool  ibl_enabled        = false; // replace constant ambient with env-map IBL
    float ibl_intensity      = 1.0f;  // overall IBL brightness scale
    float ibl_diffuse_scale  = 1.0f;  // diffuse irradiance contribution scale
    float ibl_specular_scale = 1.0f;  // specular pre-filtered env contribution scale

    // ---- TAA (Temporal Anti-Aliasing) -----------------------------------
    bool  taa_enabled        = true;
    float taa_blend          = 0.1f;    // blend weight toward current frame (0.05=smooth, 0.5=responsive)
    float taa_variance_gamma = 1.25f;   // YCoCg neighbourhood AABB expansion (γσ; tighter is safe in YCoCg)
    float taa_sharpening     = 0.2f;    // unsharp-mask strength applied after accumulation (0=off)

    // ---- Debug views --------------------------------------------
    // 0=Off  1=Albedo  2=Normals  3=Metallic  4=Roughness  5=AO  6=Emissive  7=Cascade Debug
    int debug_view = 0;
};

} // namespace sol
