#version 450

// Resolve froxel lighting volume to 2D fog image.
// Set 0: frame_layout  (FrameUBO — for u_cam_pos and u_inv_view_proj)
// Set 1: vol_resolve_layout  (depth sampler + lighting3D sampler + history + density3D)

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

layout(set = 1, binding = 0) uniform sampler2D  s_depth;
layout(set = 1, binding = 1) uniform sampler3D  s_vol_lighting;
layout(set = 1, binding = 2) uniform sampler2D  s_history;
layout(set = 1, binding = 3) uniform sampler3D  s_vol_density;

layout(push_constant) uniform VolResolvePush {
    vec4 params;   // x=vol_near, y=vol_far, z=slice_count, w=temporal_blend
    vec4 params2;  // x=g (phase asymmetry), yzw=unused
} pc;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_fog;

// Convert raw depth + UV to froxel-W (log-linear view distance in [0,1]).
float raw_to_froxel_w(vec2 uv, float raw_d, float near, float far) {
    if (raw_d >= 0.9999) return 1.0;
    vec4 ndc_h = vec4(uv * 2.0 - 1.0, raw_d, 1.0);
    vec4 wpos  = u_inv_view_proj * ndc_h;
    wpos      /= wpos.w;
    float dist = length(wpos.xyz - u_cam_pos.xyz);
    if (dist < near) return 0.0;
    return clamp(log(dist / near) / log(far / near), 0.0, 1.0);
}

// Clamp history fog to a neighbourhood around the current frame to bound ghosting.
vec4 clamp_history(vec4 hist, vec4 curr) {
    vec3 lo = curr.rgb * 0.1;
    vec3 hi = curr.rgb * 2.5 + vec3(0.01);
    return vec4(clamp(hist.rgb, lo, hi),
                clamp(hist.a, curr.a - 0.15, curr.a + 0.15));
}

// Bilateral 4-tap sample of the froxel lighting volume.
vec4 bilateral_sample(float pixel_depth, float w, float near, float far, float slice_count) {
    ivec3 vol_size = textureSize(s_vol_lighting, 0);

    vec2 ftc = v_uv * vec2(vol_size.xy);
    vec2 f0  = floor(ftc - 0.5);
    vec2 fr  = ftc - 0.5 - f0;
    f0       = clamp(f0, vec2(0.0), vec2(vol_size.xy) - 2.0);

    vec2 uv00 = (f0 + vec2(0.5, 0.5)) / vec2(vol_size.xy);
    vec2 uv10 = (f0 + vec2(1.5, 0.5)) / vec2(vol_size.xy);
    vec2 uv01 = (f0 + vec2(0.5, 1.5)) / vec2(vol_size.xy);
    vec2 uv11 = (f0 + vec2(1.5, 1.5)) / vec2(vol_size.xy);

    float d00 = texture(s_depth, uv00).r;
    float d10 = texture(s_depth, uv10).r;
    float d01 = texture(s_depth, uv01).r;
    float d11 = texture(s_depth, uv11).r;

    float fw_px = raw_to_froxel_w(v_uv, pixel_depth, near, far);
    float fw_00 = raw_to_froxel_w(uv00, d00, near, far);
    float fw_10 = raw_to_froxel_w(uv10, d10, near, far);
    float fw_01 = raw_to_froxel_w(uv01, d01, near, far);
    float fw_11 = raw_to_froxel_w(uv11, d11, near, far);

    const float kD = 25.0;
    float bw00 = (1.0-fr.x)*(1.0-fr.y) * exp(-abs(fw_00 - fw_px) * kD);
    float bw10 =      fr.x *(1.0-fr.y) * exp(-abs(fw_10 - fw_px) * kD);
    float bw01 = (1.0-fr.x)*     fr.y  * exp(-abs(fw_01 - fw_px) * kD);
    float bw11 =      fr.x *     fr.y  * exp(-abs(fw_11 - fw_px) * kD);
    float wt   = bw00 + bw10 + bw01 + bw11;

    if (wt < 1e-4) {
        bw00 = (1.0-fr.x)*(1.0-fr.y); bw10 = fr.x*(1.0-fr.y);
        bw01 = (1.0-fr.x)*fr.y;        bw11 = fr.x*fr.y;
        wt   = 1.0;
    }

    return (texture(s_vol_lighting, vec3(uv00, w)) * bw00 +
            texture(s_vol_lighting, vec3(uv10, w)) * bw10 +
            texture(s_vol_lighting, vec3(uv01, w)) * bw01 +
            texture(s_vol_lighting, vec3(uv11, w)) * bw11) / wt;
}

// Henyey-Greenstein phase function.
float hg_phase_resolve(float cos_theta, float g) {
    float g2 = g * g;
    float d  = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-5);
    return (1.0 - g2) / (4.0 * 3.14159265 * d * sqrt(d));
}

// Per-pixel shadow sampling (same cascade logic as scatter pass).
float resolve_shadow(vec3 world_pos) {
    vec4  vp  = u_view * vec4(world_pos, 1.0);
    float vz  = -vp.z;
    int   cas = 3;
    if      (vz < u_cascade_splits.x) cas = 0;
    else if (vz < u_cascade_splits.y) cas = 1;
    else if (vz < u_cascade_splits.z) cas = 2;
    vec4 sc = u_light_mtx[cas] * vec4(world_pos, 1.0);
    sc.xyz /= sc.w;
    sc.xy   = sc.xy * 0.5 + 0.5;
    if (any(lessThan(sc.xy, vec2(0.0))) || any(greaterThan(sc.xy, vec2(1.0))) ||
        sc.z < 0.0 || sc.z > 1.0) return 1.0;
    float bias = u_shadow_params.x + u_shadow_params.y * 0.5;
    return texture(s_shadow_map, vec4(sc.xy, float(cas), sc.z - bias));
}

// Per-pixel directional light accumulation via ray march.
// Samples shadow at per-pixel world positions — no froxel-grid shadow aliasing.
vec3 march_directional(vec2 uv, vec3 view_dir, float w_max, float g,
                        float near, float far) {
    const int STEPS = 10;

    int n_lights = int(u_light_count.x);
    int sun_idx  = -1;
    for (int i = 0; i < n_lights; ++i) {
        if (u_light_data0[i].w < 0.5) { sun_idx = i; break; }
    }
    if (sun_idx < 0) return vec3(0.0);

    vec3  sun_dir  = normalize(-u_light_data0[sun_idx].xyz);
    vec3  sun_col  = u_light_data1[sun_idx].rgb * u_light_data1[sun_idx].a;
    float cos_t    = dot(view_dir, sun_dir);
    float phase    = hg_phase_resolve(cos_t, g);
    bool  shad_on  = u_shadow_config.x > 0.5;
    float shad_str = u_shadow_params.y;

    vec3  accum = vec3(0.0);
    float T     = 1.0;

    for (int s = 0; s < STEPS; ++s) {
        float t  = (float(s) + 0.5) / float(STEPS);
        float w  = t * w_max;

        // Sample density from froxel volume (smooth field)
        vec4  dens    = texture(s_vol_density, vec3(uv, w));
        float scatter = dens.r;
        float extinct = max(dens.a, 0.0);

        if (extinct < 1e-5) continue;

        // Slice thickness (exponential depth distribution)
        float w0 = float(s)     / float(STEPS) * w_max;
        float w1 = float(s + 1) / float(STEPS) * w_max;
        float d0 = near * pow(far / near, w0);
        float d1 = near * pow(far / near, w1);
        float dt = d1 - d0;

        // Per-pixel world position for shadow sampling
        float dist      = near * pow(far / near, w);
        vec3  world_pos = u_cam_pos.xyz + view_dir * dist;

        float shad = shad_on ? mix(1.0, resolve_shadow(world_pos), shad_str) : 1.0;

        accum += sun_col * phase * shad * scatter * dt * T;
        T     *= exp(-extinct * dt);
    }
    return accum;
}

void main() {
    float depth     = texture(s_depth, v_uv).r;
    float near      = pc.params.x;
    float far       = pc.params.y;
    float slice_cnt = pc.params.z;
    float blend     = pc.params.w;
    float g         = pc.params2.x;

    // Compute per-pixel view direction for phase evaluation.
    vec4 ndc_view  = vec4(v_uv * 2.0 - 1.0, clamp(depth, 0.001, 0.999), 1.0);
    vec4 wp_view   = u_inv_view_proj * ndc_view;
    wp_view       /= wp_view.w;
    vec3 pixel_view_dir = normalize(wp_view.xyz - u_cam_pos.xyz);

    if (depth >= 0.9999) {
        // Sky pixel: sample fog up to w_sky depth for horizon haze.
        float w_sky = 0.65;
        out_fog = texture(s_vol_lighting, vec3(v_uv, w_sky));

        // Elevation fade: full fog at horizon, 10% at zenith.
        float elev    = dot(pixel_view_dir, vec3(0.0, 1.0, 0.0));
        float sky_fog = clamp(1.0 - max(elev, 0.0) * 2.0, 0.1, 1.0);

        out_fog.rgb *= sky_fog;
        out_fog.a = mix(1.0, out_fog.a, sky_fog);

        // Per-pixel directional ray march — full-resolution shadow, no froxel blocks.
        out_fog.rgb += march_directional(v_uv, pixel_view_dir, w_sky, g, near, far);

        if (blend > 0.001) {
            vec4 ndc_h  = vec4(v_uv * 2.0 - 1.0, 0.99, 1.0);
            vec4 wpos_h = u_inv_view_proj * ndc_h;
            wpos_h /= wpos_h.w;
            vec3 sky_dir   = normalize(wpos_h.xyz - u_cam_pos.xyz);
            vec3 far_point = u_cam_pos.xyz + sky_dir * far;
            vec4 prev_clip = u_prev_view_proj * vec4(far_point, 1.0);
            if (prev_clip.w > 0.001) {
                vec2 hist_uv = prev_clip.xy / prev_clip.w * 0.5 + 0.5;
                if (all(greaterThan(hist_uv, vec2(0.01))) && all(lessThan(hist_uv, vec2(0.99)))) {
                    vec4 hist = clamp_history(texture(s_history, hist_uv), out_fog);
                    out_fog = mix(out_fog, hist, blend);
                }
            }
        }
        return;
    }

    float w = raw_to_froxel_w(v_uv, depth, near, far);
    out_fog = bilateral_sample(depth, w, near, far, slice_cnt);

    // Per-pixel directional ray march — full-resolution shadow, no froxel blocks.
    out_fog.rgb += march_directional(v_uv, pixel_view_dir, w, g, near, far);

    if (blend > 0.001) {
        vec4 wpos_h = u_inv_view_proj * vec4(v_uv * 2.0 - 1.0, depth, 1.0);
        wpos_h /= wpos_h.w;
        vec4 prev_clip = u_prev_view_proj * vec4(wpos_h.xyz, 1.0);
        if (prev_clip.w > 0.001) {
            vec2 hist_uv = prev_clip.xy / prev_clip.w * 0.5 + 0.5;
            if (all(greaterThan(hist_uv, vec2(0.01))) && all(lessThan(hist_uv, vec2(0.99)))) {
                vec4 hist = clamp_history(texture(s_history, hist_uv), out_fog);
                out_fog = mix(out_fog, hist, blend);
            }
        }
    }
}
