#version 450

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
    mat4 u_inv_view_proj;  // unused in forward pass but needed for UBO alignment
    vec4 u_shadow_params;  // x=bias_const, y=bias_slope, z=pcf_radius, w=pcss_light_size
    vec4 u_shadow_extra;   // x=vsm_light_bleed
};

layout(set = 0, binding = 1) uniform sampler2DArrayShadow s_shadow_map;
layout(set = 0, binding = 2) uniform sampler2DArray        s_shadow_raw; // for PCSS blocker search
layout(set = 0, binding = 3) uniform sampler2DArray s_vsm_fwd; // VSM moments texture
layout(set = 1, binding = 0) uniform sampler2D s_albedo;
layout(set = 1, binding = 1) uniform sampler2D s_normal_map;
layout(set = 1, binding = 2) uniform sampler2D s_mr_map;
layout(set = 1, binding = 3) uniform sampler2D s_emissive_map;

layout(push_constant) uniform PbrPush {
    mat4 model;
    vec4 base_color;
    // pbr.x=metallic  pbr.y=roughness  pbr.z=alpha_cutoff  pbr.w=alpha_mode(0=opaque,1=mask)
    vec4 pbr;
    // emissive.xyz=emissive_color  emissive.w=hasEmissiveTex
    vec4 emissive;
    // flags.x=lit  flags.y=hasAlbedo  flags.z=hasNormal  flags.w=hasMR
    vec4 flags;
} pc;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_color;
layout(location = 4) in vec4 v_world_tangent;

layout(location = 0) out vec4 out_color;

const float PI = 3.14159265359;

// ---- Poisson disk (32 samples) ----
const vec2 POISSON32[32] = vec2[32](
    vec2(-0.613392,  0.617481), vec2( 0.170019, -0.040254),
    vec2(-0.299417,  0.791925), vec2( 0.645680,  0.493210),
    vec2(-0.651784,  0.717887), vec2( 0.421003,  0.027070),
    vec2(-0.817194, -0.271096), vec2(-0.705374, -0.668203),
    vec2( 0.977050, -0.108615), vec2( 0.063326,  0.142369),
    vec2( 0.203528,  0.214331), vec2(-0.667531,  0.326090),
    vec2(-0.098422, -0.295755), vec2(-0.885922,  0.215369),
    vec2( 0.566637,  0.605213), vec2( 0.039766, -0.396100),
    vec2( 0.751946,  0.453352), vec2( 0.078707, -0.715323),
    vec2(-0.075838, -0.529344), vec2( 0.724479, -0.580798),
    vec2( 0.222999, -0.215125), vec2(-0.467574, -0.405438),
    vec2(-0.248268, -0.814753), vec2( 0.354411, -0.887570),
    vec2( 0.175817,  0.382366), vec2( 0.487472, -0.063082),
    vec2(-0.084078,  0.898312), vec2( 0.488876, -0.783441),
    vec2( 0.470016,  0.217933), vec2(-0.696890, -0.549791),
    vec2(-0.149693,  0.605762), vec2( 0.034211,  0.979980)
);

float distribution_ggx(vec3 n, vec3 h, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float n_dot_h = max(dot(n, h), 0.0);
    float n_dot_h2 = n_dot_h * n_dot_h;
    float denom = n_dot_h2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

// Smith-GGX visibility = G/(4*NoL*NoV), combined & optimized (Karis 2013 SIGGRAPH)
float visibility_smith_ggx(float n_dot_l, float n_dot_v, float roughness) {
    float a2 = roughness * roughness; a2 *= a2;
    float G_V = n_dot_v + sqrt((n_dot_v - n_dot_v * a2) * n_dot_v + a2);
    float G_L = n_dot_l + sqrt((n_dot_l - n_dot_l * a2) * n_dot_l + a2);
    return 1.0 / max(G_V * G_L, 1e-6);
}

// Spherical Gaussian approx for Schlick Fresnel (Lagarde 2012)
vec3 fresnel_schlick(float cos_theta, vec3 f0) {
    float fc = exp2((-5.55473 * cos_theta - 6.98316) * cos_theta);
    return f0 + (1.0 - f0) * fc;
}

// IBL variant: roughness reduces the Fresnel rim effect at glancing angles
vec3 fresnel_schlick_roughness(float cos_theta, vec3 f0, float roughness) {
    float fc = exp2((-5.55473 * cos_theta - 6.98316) * cos_theta);
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * fc;
}

// ---- Shadow helpers ----
float ign_noise_f(vec2 c) { return fract(52.9829189 * fract(dot(c, vec2(0.06711056, 0.00583715)))); }
mat2 poisson_rot_f() {
    float a = ign_noise_f(gl_FragCoord.xy) * 6.28318530718;
    return mat2(cos(a), -sin(a), sin(a), cos(a));
}

float compute_bias_fwd(vec3 normal, vec3 light_dir) {
    float n_dot_l = max(dot(normal, light_dir), 0.0);
    return u_shadow_params.x + (1.0 - n_dot_l) * u_shadow_params.y;
}

float pcf_shadow_fwd(vec3 shadow_uvz, int cascade, float bias, float radius, mat2 rot) {
    int n_samp = 8 << clamp(int(u_shadow_config.w + 0.5), 0, 2);
    float sum = 0.0;
    for (int i = 0; i < 32; ++i) {
        if (i >= n_samp) break;
        vec2 off = rot * POISSON32[i] * radius;
        sum += texture(s_shadow_map,
            vec4(shadow_uvz.xy + off, float(cascade), shadow_uvz.z - bias));
    }
    return sum / float(n_samp);
}

float pcss_shadow_fwd(vec3 shadow_uvz, int cascade, float bias) {
    float texel      = u_shadow_config.y;
    float light_size = u_shadow_params.w;
    float receiver_z = shadow_uvz.z - bias;
    float search_r   = light_size * texel * 3.0;
    mat2  rot        = poisson_rot_f();
    int   n_samp     = 8 << clamp(int(u_shadow_config.w + 0.5), 0, 2);

    float blocker_sum = 0.0;
    int   blocker_cnt = 0;
    for (int i = 0; i < 32; ++i) {
        if (i >= n_samp) break;
        vec2  off   = rot * POISSON32[i] * search_r;
        float depth = texture(s_shadow_raw, vec3(shadow_uvz.xy + off, float(cascade))).r;
        if (depth < receiver_z) {
            blocker_sum += depth;
            blocker_cnt++;
        }
    }
    if (blocker_cnt == 0) return 1.0;

    float avg_blocker = blocker_sum / float(blocker_cnt);
    float penumbra = (receiver_z - avg_blocker) / max(avg_blocker, 0.001) * light_size;
    float pcf_r    = clamp(penumbra * texel * 50.0, texel * 0.5, texel * 24.0);
    return pcf_shadow_fwd(shadow_uvz, cascade, bias, pcf_r, rot);
}

const float EVSM_C_FWD = 40.0;
float vsm_shadow_fwd(vec3 shadow_uvz, int cascade) {
    vec2  m = texture(s_vsm_fwd, vec3(shadow_uvz.xy, float(cascade))).rg;
    float t = exp(-EVSM_C_FWD * shadow_uvz.z);
    if (t >= m.x) return 1.0;
    float var = max(m.y - m.x * m.x, u_shadow_extra.y); // y = vsm_min_variance
    float d   = m.x - t;
    float p   = var / (var + d * d);
    float lo  = u_shadow_extra.x; // x = vsm_light_bleed
    return clamp((p - lo) / max(1.0 - lo, 1e-5), 0.0, 1.0);
}

float sample_shadow(vec3 normal, vec3 light_dir, int shadow_mode) {
    if (u_shadow_config.x < 0.5 || shadow_mode == 0) return 1.0;

    float view_z = -(u_view * vec4(v_world_pos, 1.0)).z;
    int cascade = 3;
    if      (view_z < u_cascade_splits.x) cascade = 0;
    else if (view_z < u_cascade_splits.y) cascade = 1;
    else if (view_z < u_cascade_splits.z) cascade = 2;

    vec4 shadow_clip = u_light_mtx[cascade] * vec4(v_world_pos, 1.0);
    vec3 shadow_ndc  = shadow_clip.xyz / max(shadow_clip.w, 1e-5);
    vec3 shadow_uvz  = vec3(shadow_ndc.xy * 0.5 + 0.5, shadow_ndc.z);

    if (shadow_uvz.z <= 0.0 || shadow_uvz.z >= 1.0) return 1.0;
    if (shadow_uvz.x <= 0.0 || shadow_uvz.x >= 1.0 ||
        shadow_uvz.y <= 0.0 || shadow_uvz.y >= 1.0) return 1.0;

    float bias = compute_bias_fwd(normal, light_dir);

    if (shadow_mode == 2) {
        return pcss_shadow_fwd(shadow_uvz, cascade, bias);
    } else if (shadow_mode == 3) {
        return vsm_shadow_fwd(shadow_uvz, cascade);
    } else {
        float radius = u_shadow_params.z * u_shadow_config.y;
        return pcf_shadow_fwd(shadow_uvz, cascade, bias, radius, poisson_rot_f());
    }
}

void main() {
    vec4 albedo_sample = pc.flags.y > 0.5 ? texture(s_albedo, v_uv) : vec4(1.0);
    vec3 base = pc.base_color.rgb * albedo_sample.rgb * v_color.rgb;
    float alpha = pc.base_color.a * albedo_sample.a * v_color.a;

    // Alpha cutout (MASK mode)
    int alpha_mode = int(round(pc.pbr.w));
    float alpha_cutoff = pc.pbr.z;
    if (alpha_mode == 1 && alpha < alpha_cutoff) discard;

    float metallic  = clamp(pc.pbr.x, 0.0, 1.0);
    float roughness = clamp(pc.pbr.y, 0.04, 1.0);

    if (pc.flags.w > 0.5) {
        vec4 mr = texture(s_mr_map, v_uv);
        roughness *= mr.g;
        metallic  *= mr.b;
    }

    vec3 n = normalize(v_world_normal);
    if (pc.flags.z > 0.5) {
        vec3 t = normalize(v_world_tangent.xyz);
        vec3 b = normalize(cross(n, t)) * v_world_tangent.w;
        mat3 tbn = mat3(t, b, n);
        vec3 map_n = texture(s_normal_map, v_uv).xyz * 2.0 - 1.0;
        n = normalize(tbn * map_n);
    }

    vec3 v      = normalize(u_cam_pos.xyz - v_world_pos);
    float n_dot_v = max(dot(n, v), 1e-4);
    vec3 f0     = mix(vec3(0.04), base, metallic);
    vec3 radiance = vec3(0.0);

    int light_count = int(u_light_count.x);
    bool shadow_used = false;
    for (int i = 0; i < light_count; ++i) {
        vec3  l;
        float attenuation = 1.0;
        float type = u_light_data0[i].w;

        if (type < 0.5) {
            l = normalize(-u_light_data0[i].xyz);
        } else {
            vec3  to_light = u_light_data0[i].xyz - v_world_pos;
            float dist     = length(to_light);
            if (dist <= 1e-4) continue;
            l = to_light / dist;
            float range  = max(u_light_data2[i].w, 0.001);
            float ratio  = dist / range;
            float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
            attenuation  = window * window / (dist * dist * 0.1 + 1.0);
            if (type > 1.5) {
                vec3  spot_dir  = normalize(-u_light_data2[i].xyz);
                float spot      = dot(l, spot_dir);
                float inner_cos = u_light_data3[i].x;
                float outer_cos = u_light_data3[i].y;
                float denom     = max(inner_cos - outer_cos, 1e-4);
                attenuation *= clamp((spot - outer_cos) / denom, 0.0, 1.0);
            }
        }

        float n_dot_l = max(dot(n, l), 0.0);
        if (n_dot_l <= 0.0) continue;

        vec3  h           = normalize(v + l);
        vec3  light_color = u_light_data1[i].rgb * u_light_data1[i].a * attenuation;
        float d           = distribution_ggx(n, h, roughness);
        float vis         = visibility_smith_ggx(n_dot_l, n_dot_v, roughness);
        vec3  f           = fresnel_schlick(max(dot(h, v), 0.0), f0);
        vec3  ks          = f;
        vec3  kd          = (vec3(1.0) - ks) * (1.0 - metallic);
        vec3  spec        = d * vis * f;

        float shadow = 1.0;
        if (!shadow_used && type < 0.5 && u_shadow_config.x > 0.5) {
            int smode = int(u_light_data3[i].z + 0.5);
            shadow = sample_shadow(n, l, smode);
            shadow_used = true;
        }

        radiance += ((kd * base / PI) + spec) * light_color * n_dot_l * shadow;
    }

    // Emissive
    vec3 emissive_color = pc.emissive.rgb;
    if (pc.emissive.w > 0.5)
        emissive_color *= texture(s_emissive_map, v_uv).rgb;

    vec3 ambient_diff = u_ambient.rgb * base * (1.0 - metallic);
    vec3 ambient_spec = u_ambient.rgb * f0 * (1.0 - roughness * roughness) * 0.5;
    vec3 ambient      = ambient_diff + ambient_spec;
    vec3 color = pc.flags.x > 0.5 ? ambient + radiance + emissive_color : base + emissive_color;
    out_color = vec4(color, alpha);
}