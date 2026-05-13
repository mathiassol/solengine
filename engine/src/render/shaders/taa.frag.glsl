#version 450

layout(location = 0) in vec2 v_uv;

// set 0: TAA inputs
layout(set = 0, binding = 0) uniform sampler2D s_hdr;
layout(set = 0, binding = 1) uniform sampler2D s_history;
layout(set = 0, binding = 2) uniform sampler2D s_depth;

// set 1: frame UBO — only the matrices we need for reprojection
layout(set = 1, binding = 0) uniform FrameUBO {
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

// push constant
layout(push_constant) uniform TaaPush {
    vec4 params;     // x=blend_alpha, y=enabled, z=variance_gamma, w=sharpening
    vec4 resolution; // x=width, y=height, z=1/width, w=1/height
} pc;

layout(location = 0) out vec4 out_color;

// ------- YCoCg helpers ---------------------------------------------------
vec3 rgb_to_ycocg(vec3 c) {
    return vec3(
         0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
         0.5  * c.r              - 0.5  * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b
    );
}

vec3 ycocg_to_rgb(vec3 c) {
    return vec3(
        c.x + c.y - c.z,   // R = Y + Co - Cg
        c.x       + c.z,   // G = Y + Cg
        c.x - c.y - c.z    // B = Y - Co - Cg
    );
}

// Clip history point q toward mean p and into AABB [aabb_min, aabb_max]
vec3 clip_to_aabb(vec3 aabb_min, vec3 aabb_max, vec3 p, vec3 q) {
    vec3 r = q - p;
    vec3 rmax = aabb_max - p;
    vec3 rmin = aabb_min - p;
    const float eps = 1e-5;
    float scale = 1.0;
    if (r.x > eps)       scale = min(scale, rmax.x / r.x);
    else if (r.x < -eps) scale = min(scale, rmin.x / r.x);
    if (r.y > eps)       scale = min(scale, rmax.y / r.y);
    else if (r.y < -eps) scale = min(scale, rmin.y / r.y);
    if (r.z > eps)       scale = min(scale, rmax.z / r.z);
    else if (r.z < -eps) scale = min(scale, rmin.z / r.z);
    return p + r * scale;
}

void main() {
    vec2 uv = v_uv;

    // Pass-through when TAA disabled
    if (pc.params.y < 0.5) {
        out_color = vec4(texture(s_hdr, uv).rgb, 1.0);
        return;
    }

    // --- Depth-based reprojection to find history UV ---
    float depth = texture(s_depth, uv).r;
    vec2  ndc_xy = uv * 2.0 - 1.0;

    vec4 world_h = u_inv_view_proj * vec4(ndc_xy, depth, 1.0);
    vec3 world   = world_h.xyz / world_h.w;

    vec4 prev_clip = u_prev_view_proj * vec4(world, 1.0);
    vec2 history_uv = prev_clip.xy / prev_clip.w * 0.5 + 0.5;

    // Validity: within screen, and prev matrix is initialised (not all-zero)
    bool valid = all(greaterThanEqual(history_uv, vec2(0.01))) &&
                 all(lessThanEqual   (history_uv, vec2(0.99))) &&
                 (u_prev_view_proj[0][0] != 0.0);

    // --- 3×3 neighbourhood of current frame in YCoCg ---
    vec2 tx = pc.resolution.zw; // texel size
    vec3 sum  = vec3(0.0);
    vec3 sum2 = vec3(0.0);
    vec3 nbr_min = vec3( 1e10);
    vec3 nbr_max = vec3(-1e10);
    vec3 cur_ycocg = vec3(0.0);

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec3 s = rgb_to_ycocg(max(texture(s_hdr, uv + vec2(x, y) * tx).rgb, vec3(0.0)));
            sum  += s;
            sum2 += s * s;
            nbr_min = min(nbr_min, s);
            nbr_max = max(nbr_max, s);
            if (x == 0 && y == 0) cur_ycocg = s;
        }
    }

    vec3 mean     = sum / 9.0;
    vec3 variance = max(sum2 / 9.0 - mean * mean, vec3(0.0));
    vec3 sigma    = sqrt(variance);
    float gamma   = pc.params.z;

    // Variance-clamped AABB — expand to always cover neighbour range
    vec3 aabb_min = min(mean - gamma * sigma, nbr_min);
    vec3 aabb_max = max(mean + gamma * sigma, nbr_max);
    // Safety: guarantee min <= max
    aabb_min = min(aabb_min, aabb_max);

    // No history available — output current frame directly
    if (!valid) {
        out_color = vec4(clamp(ycocg_to_rgb(cur_ycocg), vec3(0.0), vec3(65504.0)), 1.0);
        return;
    }

    // --- Sample and clip history ---
    vec3 hist_ycocg = rgb_to_ycocg(max(texture(s_history, history_uv).rgb, vec3(0.0)));

    // Clip history toward mean to suppress ghosting
    vec3 clamped_mean  = clamp(mean, aabb_min, aabb_max);
    vec3 clipped_hist  = clip_to_aabb(aabb_min, aabb_max, clamped_mean, hist_ycocg);

    // --- Exponential blend (default: 10% current, 90% history) ---
    float alpha  = pc.params.x;
    vec3  blended = mix(clipped_hist, cur_ycocg, alpha);

    // --- Unsharp-mask sharpening (applied before converting back to RGB) ---
    float sharp = pc.params.w;
    if (sharp > 0.001) {
        blended += (cur_ycocg - mean) * sharp;
    }

    // Convert back to RGB, THEN clamp — YCoCg Co/Cg channels are signed and must not be clamped to 0
    out_color = vec4(clamp(ycocg_to_rgb(blended), vec3(0.0), vec3(65504.0)), 1.0);
}
