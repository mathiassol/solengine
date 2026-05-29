#version 450
// SSR temporal accumulation shader
// Set 0: ssr_raw, ssr_history, depth
// Set 1: FrameUBO

layout(set = 0, binding = 0) uniform sampler2D s_ssr_raw;
layout(set = 0, binding = 1) uniform sampler2D s_ssr_history;
layout(set = 0, binding = 2) uniform sampler2D s_depth;

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

layout(push_constant) uniform SSRTemporalPush {
    vec4 params; // x=blend_alpha
} pc;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 current = texture(s_ssr_raw, v_uv).rgb;

    // Reprojection: reconstruct world pos, project to previous frame UV
    float depth = texture(s_depth, v_uv).r;
    if (depth >= 0.9999) {
        out_color = vec4(current, 1.0);
        return;
    }

    vec2  ndc_xy  = v_uv * 2.0 - 1.0;
    vec4  clip_h  = vec4(ndc_xy, depth, 1.0);
    vec4  world_h = u_inv_view_proj * clip_h;
    vec3  world   = world_h.xyz / world_h.w;

    vec4  prev_c  = u_prev_view_proj * vec4(world, 1.0);
    if (prev_c.w <= 0.0) { out_color = vec4(current, 1.0); return; }
    vec2  prev_uv = (prev_c.xy / prev_c.w) * 0.5 + 0.5;

    if (any(lessThan(prev_uv, vec2(0.01))) || any(greaterThan(prev_uv, vec2(0.99)))) {
        out_color = vec4(current, 1.0);
        return;
    }

    // 3x3 neighbourhood AABB clamp in RGB
    vec3 aabb_min = current, aabb_max = current;
    vec2 ts = 1.0 / vec2(textureSize(s_ssr_raw, 0));
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            vec3 s = texture(s_ssr_raw, v_uv + vec2(x, y) * ts).rgb;
            aabb_min = min(aabb_min, s);
            aabb_max = max(aabb_max, s);
        }
    }

    vec3 history = texture(s_ssr_history, prev_uv).rgb;
    vec3 clamped = clamp(history, aabb_min, aabb_max);
    out_color    = vec4(mix(clamped, current, pc.params.x), 1.0);
}
