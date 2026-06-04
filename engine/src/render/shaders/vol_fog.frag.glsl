#version 450

// Volumetric fog / atmospheric scattering pass.
// Runs at half resolution.
// Output: inscatter.rgb + transmittance.a
//   rgb – accumulated in-scattered sunlight along the ray
//   a   – Beer-Lambert transmittance [0,1]; 1 = no extinction, 0 = fully opaque fog
//
// Set 0: frame_layout  (FrameUBO + shadow samplers)
// Set 1: s_depth       (single_sampler_layout — m_hdr_depth, non-comparison)
// Push:  VolPush

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_light_mtx[4];
    vec4 u_cam_pos;
    vec4 u_ambient;
    vec4 u_light_count;
    vec4 u_light_data0[8];
    vec4 u_light_data1[8];
    vec4 u_light_data2[8];
    vec4 u_light_data3[8];
    vec4 u_shadow_config;
    vec4 u_cascade_splits;
    mat4 u_inv_view_proj;
    vec4 u_shadow_params;
    vec4 u_shadow_extra;
    mat4 u_prev_view_proj;
    vec4 u_temporal_params;
    vec4 u_ibl_params;
};

layout(set = 0, binding = 1) uniform sampler2DArrayShadow s_shadow_map;

layout(set = 1, binding = 0) uniform sampler2D s_depth;

layout(push_constant) uniform VolPush {
    vec4 params;   // x=density  y=scattering_albedo  z=g(HG phase [-1,1])  w=float(march_steps)
    vec4 params2;  // x=near  y=fog_far  z=sun_intensity  w=shadow_strength
} pc;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_fog;

const float PI               = 3.14159265359;
const float MIN_TRANSMITTANCE = 0.02;   // early-exit threshold

// ---- helpers ----------------------------------------------------------------

// Reconstruct world-space position from screen UV and hardware NDC depth.
vec3 reconstruct_world(vec2 uv, float depth) {
    vec4 ndc   = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = u_inv_view_proj * ndc;
    return world.xyz / world.w;
}

// Henyey-Greenstein phase function.
// g = 0  → isotropic, g > 0 → forward scattering, g < 0 → backward.
float hg_phase(float cos_theta, float g) {
    float g2    = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-5);
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

// Sample the cascaded shadow map at a world-space position.
// Returns 1.0 = fully lit, 0.0 = fully shadowed.
float sample_shadow_csm(vec3 world_pos) {
    // Determine cascade from view-space depth
    vec4 vpos      = u_view * vec4(world_pos, 1.0);
    float depth_vs = -vpos.z;

    int cascade = 3;
    if      (depth_vs < u_cascade_splits.x) cascade = 0;
    else if (depth_vs < u_cascade_splits.y) cascade = 1;
    else if (depth_vs < u_cascade_splits.z) cascade = 2;

    vec4 sc = u_light_mtx[cascade] * vec4(world_pos, 1.0);
    sc.xyz  /= sc.w;
    sc.xy    = sc.xy * 0.5 + 0.5;

    // Out of shadow map coverage → assume lit
    if (any(lessThan(sc.xy,    vec2(0.0))) ||
        any(greaterThan(sc.xy, vec2(1.0))) ||
        sc.z < 0.0 || sc.z > 1.0)
        return 1.0;

    float bias = u_shadow_params.x + u_shadow_params.y * 0.5;
    return texture(s_shadow_map, vec4(sc.xy, float(cascade), sc.z - bias));
}

// ---- main -------------------------------------------------------------------

void main() {
    float depth   = texture(s_depth, v_uv).r;
    vec3  cam_pos = u_cam_pos.xyz;

    // Reconstruct ray endpoint (surface or fog far-plane for sky pixels).
    float fog_far    = pc.params2.y;
    vec3  surface    = reconstruct_world(v_uv, min(depth, 0.9999));
    vec3  ray        = surface - cam_pos;
    float ray_len    = length(ray);
    ray_len          = min(ray_len, fog_far);   // clamp sky rays to fog_far
    vec3  ray_dir    = ray / max(ray_len, 1e-6);

    float density    = pc.params.x;
    float scatter_al = pc.params.y;            // scattering albedo
    float g          = pc.params.z;
    float steps      = max(pc.params.w, 1.0);
    float sun_int    = pc.params2.z;
    float shad_str   = pc.params2.w;

    float step_size  = ray_len / steps;

    // Sun (first directional light, slot 0)
    bool  has_sun   = (u_light_count.x > 0.5 && u_light_data0[0].w < 0.5);
    vec3  sun_dir   = has_sun ? normalize(u_light_data0[0].xyz)          : vec3(0.0, 1.0, 0.0);
    vec3  sun_color = has_sun ? u_light_data1[0].rgb * u_light_data1[0].a : vec3(1.0);

    float cos_theta = dot(ray_dir, sun_dir);
    float phase     = hg_phase(cos_theta, g);

    // Small ambient fill so shadowed fog is not pitch black
    vec3 ambient_fill = u_ambient.rgb * 0.15;

    bool shadows_active = (u_shadow_config.x > 0.5) && (shad_str > 0.001);

    vec3  inscatter     = vec3(0.0);
    float transmittance = 1.0;

    for (float i = 0.5; i < steps; i += 1.0) {
        if (transmittance < MIN_TRANSMITTANCE) break;

        vec3  pos         = cam_pos + ray_dir * (i / steps) * ray_len;

        // Uniform density fog coefficients
        float sigma_e    = density;
        float sigma_s    = sigma_e * scatter_al;

        // Per-step extinction
        float extinction = exp(-sigma_e * step_size);

        // Shadow visibility at this step
        float shadow = 1.0;
        if (shadows_active)
            shadow = mix(1.0, sample_shadow_csm(pos), shad_str);

        // In-scattered energy (sun contribution + ambient fill)
        vec3 in_s = (sun_color * sun_int * phase * shadow + ambient_fill) * sigma_s * step_size;

        inscatter     += in_s * transmittance;
        transmittance *= extinction;
    }

    out_fog = vec4(inscatter, transmittance);
}
