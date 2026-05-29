#version 450
// SSR ray march shader — linear DDA in view space
// Set 0: scene color, depth, world normals (gbuf1), roughness (gbuf_roughness)
// Set 1: FrameUBO (binding 0 only — u_view, u_proj used)

layout(set = 0, binding = 0) uniform sampler2D s_scene;     // hdr_color
layout(set = 0, binding = 1) uniform sampler2D s_depth;     // hdr_depth (D32, ZO)
layout(set = 0, binding = 2) uniform sampler2D s_normals;   // gbuf1 (world normal xyz, w=geom flag)
layout(set = 0, binding = 3) uniform sampler2D s_roughness; // gbuf_roughness (R8_UNORM)

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

layout(push_constant) uniform SSRPush {
    // x=steps  y=thickness(view-space)  z=max_distance  w=roughness_cutoff
    vec4 params;
} pc;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color; // rgb=reflected_color*blend, a=blend_weight

// Reconstruct view-space position from UV and linear ZO depth
vec3 view_pos(vec2 uv, float depth) {
    vec2 ndc  = uv * 2.0 - 1.0;
    float z   = -u_proj[3][2] / (depth + u_proj[2][2]);
    float x   = -z * ndc.x / u_proj[0][0];
    float y   = -z * ndc.y / u_proj[1][1];
    return vec3(x, y, z);
}

void main() {
    float depth = texture(s_depth, v_uv).r;
    if (depth >= 0.9999) { out_color = vec4(0.0); return; }

    vec4 normal_sample = texture(s_normals, v_uv);
    if (normal_sample.w < 0.5) { out_color = vec4(0.0); return; } // sky / unlit

    float roughness = texture(s_roughness, v_uv).r;
    if (roughness > pc.params.w) { out_color = vec4(0.0); return; }

    // Surface position and normal in view space
    vec3 pos_vs    = view_pos(v_uv, depth);
    vec3 normal_vs = normalize((u_view * vec4(normalize(normal_sample.xyz), 0.0)).xyz);

    // View direction and reflection
    vec3 v_dir   = normalize(-pos_vs);                 // from surface to camera
    vec3 refl_vs = reflect(-v_dir, normal_vs);         // reflection of incoming ray

    // Fresnel + roughness blend weight
    float n_dot_v        = max(dot(normal_vs, v_dir), 0.0);
    float fresnel        = 0.04 + 0.96 * pow(1.0 - n_dot_v, 5.0);
    float roughness_norm = roughness / pc.params.w;
    float rough_fade     = (1.0 - roughness_norm) * (1.0 - roughness_norm);

    int   steps     = max(int(pc.params.x), 1);
    float thickness = pc.params.y;
    float max_dist  = pc.params.z;
    float step_sz   = max_dist / float(steps);

    bool  hit     = false;
    float hit_t   = 0.0;
    vec2  hit_uv  = vec2(0.0);
    float prev_t  = 0.0;

    for (int i = 1; i <= steps; i++) {
        float t        = float(i) * step_sz;
        vec3  samp_vs  = pos_vs + t * refl_vs;

        vec4  clip = u_proj * vec4(samp_vs, 1.0);
        if (clip.w <= 0.0) break;
        vec3  ndc  = clip.xyz / clip.w;
        if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0) break;
        if (ndc.z < 0.0 || ndc.z > 1.0) { prev_t = t; continue; }

        vec2  suv      = ndc.xy * 0.5 + 0.5;
        float sc_depth = texture(s_depth, suv).r;
        if (sc_depth >= 0.9999) { prev_t = t; continue; }

        float sc_z = -u_proj[3][2] / (sc_depth + u_proj[2][2]);

        if (samp_vs.z < sc_z && (sc_z - samp_vs.z) < thickness) {
            hit    = true;
            hit_t  = t;
            hit_uv = suv;
            break;
        }
        prev_t = t;
    }

    if (!hit) { out_color = vec4(0.0); return; }

    // Binary search refinement (8 steps)
    {
        float t_lo = prev_t, t_hi = hit_t;
        for (int i = 0; i < 8; i++) {
            float t_mid  = (t_lo + t_hi) * 0.5;
            vec3  sv     = pos_vs + t_mid * refl_vs;
            vec4  c      = u_proj * vec4(sv, 1.0);
            if (c.w <= 0.0) break;
            vec3  n      = c.xyz / c.w;
            vec2  uv_mid = n.xy * 0.5 + 0.5;
            float sd     = texture(s_depth, uv_mid).r;
            if (sd < 0.9999) {
                float sz = -u_proj[3][2] / (sd + u_proj[2][2]);
                if (sv.z < sz) { t_hi = t_mid; hit_uv = uv_mid; }
                else           { t_lo = t_mid; }
            }
        }
    }

    // Confidence fades
    vec2  fade_v    = 1.0 - smoothstep(0.8, 1.0, abs(hit_uv * 2.0 - 1.0));
    float edge_fade = fade_v.x * fade_v.y;
    float dist_fade = 1.0 - clamp(hit_t / max_dist, 0.0, 1.0);
    float blend     = fresnel * rough_fade * edge_fade * dist_fade;

    vec3 refl_color = texture(s_scene, hit_uv).rgb;
    out_color = vec4(refl_color * blend, blend);
}
