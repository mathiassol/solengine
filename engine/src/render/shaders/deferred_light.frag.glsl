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
    mat4 u_inv_view_proj;
    vec4 u_shadow_params; // x=bias_const, y=bias_slope, z=pcf_radius, w=pcss_light_size
    vec4 u_shadow_extra;  // x=vsm_light_bleed, y=vsm_min_variance, z=cs_distance, w=cs_thickness
    mat4 u_prev_view_proj;    // last frame's proj*view for temporal reprojection
    vec4 u_temporal_params;   // x=alpha, y=enabled(1=yes), z=max_dist, w=unused
    vec4 u_ibl_params;        // x=enabled(1=yes), y=intensity, z=diffuse_scale, w=specular_scale
};

layout(set = 0, binding = 1) uniform sampler2DArrayShadow s_shadow_map;
layout(set = 0, binding = 2) uniform sampler2DArray        s_shadow_raw; // for PCSS blocker search
layout(set = 0, binding = 3) uniform sampler2DArray s_vsm; // VSM moments texture

layout(set = 1, binding = 0) uniform sampler2D s_gbuf0;
layout(set = 1, binding = 1) uniform sampler2D s_gbuf1;
layout(set = 1, binding = 2) uniform sampler2D s_gbuf2;
layout(set = 1, binding = 3) uniform sampler2D s_gbuf3;
layout(set = 1, binding = 4) uniform sampler2D s_depth;
layout(set = 1, binding = 5) uniform sampler2D s_ssao;
layout(set = 1, binding = 6) uniform sampler2D s_shadow_accum; // temporal shadow history
layout(set = 1, binding = 7) uniform samplerCube s_ibl_irradiance;
layout(set = 1, binding = 8) uniform samplerCube s_ibl_prefilter;
layout(set = 1, binding = 9) uniform sampler2D   s_ibl_brdf_lut;

layout(push_constant) uniform SkyPush {
    vec4 sun_dir;
    vec4 zenith;
    vec4 horizon;
    vec4 sun_color;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(location = 1) out float out_shadow_term; // temporal shadow accumulation output

const float PI = 3.14159265359;

// ---- Poisson disk (32 samples, used for PCF and PCSS blocker search) ----
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

// ---- PBR helpers ----
float distribution_ggx(vec3 n, vec3 h, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndh = max(dot(n, h), 0.0);
    float denom = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

// Smith-GGX visibility = G/(4*NoL*NoV), combined & optimized (Karis 2013 SIGGRAPH)
// Avoids a separate division; a2 = roughness^4
float visibility_smith_ggx(float n_dot_l, float n_dot_v, float roughness) {
    float a2 = roughness * roughness; a2 *= a2;
    float G_V = n_dot_v + sqrt((n_dot_v - n_dot_v * a2) * n_dot_v + a2);
    float G_L = n_dot_l + sqrt((n_dot_l - n_dot_l * a2) * n_dot_l + a2);
    return 1.0 / max(G_V * G_L, 1e-6);
}

// Spherical Gaussian approx for Schlick Fresnel (Lagarde 2012) — matches pow(1-x,5)
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

// Interleaved gradient noise — unique angle per fragment for Poisson disk rotation
float ign_noise(vec2 c) { return fract(52.9829189 * fract(dot(c, vec2(0.06711056, 0.00583715)))); }
mat2 poisson_rot() {
    float a = ign_noise(gl_FragCoord.xy) * 6.28318530718;
    return mat2(cos(a), -sin(a), sin(a), cos(a));
}

bool get_shadow_coord(vec3 world_pos, out vec3 shadow_uvz, out int cascade) {
    if (u_shadow_config.x < 0.5) return false;

    float view_z = -(u_view * vec4(world_pos, 1.0)).z;
    cascade = 3;
    if      (view_z < u_cascade_splits.x) cascade = 0;
    else if (view_z < u_cascade_splits.y) cascade = 1;
    else if (view_z < u_cascade_splits.z) cascade = 2;

    vec4 sc  = u_light_mtx[cascade] * vec4(world_pos, 1.0);
    vec3 ndc = sc.xyz / max(sc.w, 1e-5);
    shadow_uvz = vec3(ndc.xy * 0.5 + 0.5, ndc.z);

    if (shadow_uvz.z <= 0.0 || shadow_uvz.z >= 1.0) return false;
    if (any(lessThan(shadow_uvz.xy, vec2(0.0))) || any(greaterThan(shadow_uvz.xy, vec2(1.0)))) return false;
    return true;
}

float compute_bias(vec3 normal, vec3 light_dir) {
    float n_dot_l = max(dot(normal, light_dir), 0.0);
    return u_shadow_params.x + (1.0 - n_dot_l) * u_shadow_params.y;
}

// 8/16/32-tap rotated Poisson PCF (n_samp driven by shadow quality setting)
float pcf_shadow(vec3 shadow_uvz, int cascade, float bias, float radius, mat2 rot) {
    int n_samp = 8 << clamp(int(u_shadow_config.w + 0.5), 0, 2); // 8, 16, or 32
    float sum = 0.0;
    for (int i = 0; i < 32; ++i) {
        if (i >= n_samp) break;
        vec2 off = rot * POISSON32[i] * radius;
        sum += texture(s_shadow_map,
            vec4(shadow_uvz.xy + off, float(cascade), shadow_uvz.z - bias));
    }
    return sum / float(n_samp);
}

// PCSS: blocker search then variable-radius PCF
float pcss_shadow(vec3 shadow_uvz, int cascade, float bias) {
    float texel      = u_shadow_config.y;
    float light_size = u_shadow_params.w;
    float receiver_z = shadow_uvz.z - bias;

    float search_r = light_size * texel * 3.0;
    mat2  rot      = poisson_rot();  // one rotation per fragment shared across both passes
    int   n_samp   = 8 << clamp(int(u_shadow_config.w + 0.5), 0, 2);

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

    return pcf_shadow(shadow_uvz, cascade, bias, pcf_r, rot);
}

// VSM: Chebyshev upper bound. No traditional bias needed — the slope-corrected
// second moment (dFdx/dFdy terms in shadow_vsm.frag) already encodes surface slope.
const float EVSM_C = 40.0;
float vsm_shadow(vec3 shadow_uvz, int cascade) {
    vec2  m = texture(s_vsm, vec3(shadow_uvz.xy, float(cascade))).rg;
    // Map receiver depth to same EVSM space: larger depth → smaller value
    float t = exp(-EVSM_C * shadow_uvz.z);
    // t >= m.x means receiver is closer to light (or equal) → fully lit
    if (t >= m.x) return 1.0;
    // Chebyshev upper bound — variance clamped to avoid fp cancellation
    float var = max(m.y - m.x * m.x, u_shadow_extra.y); // y = vsm_min_variance
    float d   = m.x - t; // positive: receiver is deeper (in shadow)
    float p   = var / (var + d * d);
    // Light-bleed reduction via linstep
    float lo  = u_shadow_extra.x; // x = vsm_light_bleed in [0, 1]
    return clamp((p - lo) / max(1.0 - lo, 1e-5), 0.0, 1.0);
}

float sample_shadow(vec3 world_pos, vec3 normal, vec3 light_dir, int shadow_mode) {
    if (u_shadow_config.x < 0.5 || shadow_mode == 0) return 1.0;

    vec3 shadow_uvz;
    int  cascade;
    if (!get_shadow_coord(world_pos, shadow_uvz, cascade)) return 1.0;

    float bias = compute_bias(normal, light_dir);

    if (shadow_mode == 2) {
        return pcss_shadow(shadow_uvz, cascade, bias);
    } else if (shadow_mode == 3) {
        return vsm_shadow(shadow_uvz, cascade);
    } else {
        float radius = u_shadow_params.z * u_shadow_config.y;
        return pcf_shadow(shadow_uvz, cascade, bias, radius, poisson_rot());
    }
}

// Screen-space contact shadows: ray march from surface toward the light in view space.
// n_dot_l drives the adaptive bias — grazing surfaces get more offset to prevent self-shadow.
float contact_shadow(vec3 world_pos, vec3 normal, vec3 light_dir, float n_dot_l) {
    float max_dist  = u_shadow_extra.z;
    if (max_dist < 0.001) return 1.0;  // disabled when distance == 0

    // Skip near-grazing surfaces — they'll always self-shadow regardless of bias
    if (n_dot_l < 0.05) return 1.0;

    float base_thickness = max(u_shadow_extra.w, 0.0001);
    int   steps          = 8 << clamp(int(u_shadow_config.w + 0.5), 0, 2); // 8/16/32

    // Adaptive normal bias: 3% of max_dist base, scaled up on grazing angles.
    // Prevents self-intersection where the surface nearly faces away from the light.
    float normal_bias = max_dist * 0.03 * (1.0 + 3.0 * (1.0 - n_dot_l));
    normal_bias = clamp(normal_bias, 0.002, max_dist * 0.25);

    // Per-fragment jitter: offsets the start of the march by a sub-step amount,
    // breaking the regular sample pattern — temporal filter smooths the noise out.
    float jitter   = ign_noise(gl_FragCoord.xy);
    float step_sz  = max_dist / float(steps);

    vec4  vs_origin = u_view * vec4(world_pos + normal * normal_bias, 1.0);
    vec3  vs_dir    = mat3(u_view) * light_dir;

    for (int i = 0; i < steps; i++) {
        float t      = (float(i) + jitter) * step_sz;
        vec3  vs_step = vs_origin.xyz + vs_dir * t;

        vec4 clip = u_proj * vec4(vs_step, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0) break; // left screen
        if (ndc.z < 0.0 || ndc.z > 1.0) continue;

        vec2  uv      = ndc.xy * 0.5 + 0.5;
        float scene_d = texture(s_depth, uv).r;
        if (scene_d >= 1.0) continue; // sky

        // Reconstruct view-space Z from NDC depth (column-major GLM)
        float z_scene = -u_proj[3][2] / (scene_d + u_proj[2][2]);
        float z_ray   = vs_step.z;

        // Depth-proportional thickness: depth-buffer precision degrades at distance,
        // so allow a wider tolerance in view-space depth the further we are.
        float local_thickness = base_thickness * (1.0 + abs(z_ray) * 0.04);

        if (z_scene > z_ray && (z_scene - z_ray) < local_thickness) {
            // Distance-based soft fade: full shadow at contact, fades out to nothing
            // at max_dist. Gives a natural penumbra without extra samples.
            return clamp(t / max_dist, 0.0, 1.0);
        }
    }
    return 1.0;
}

void main() {
    out_shadow_term = 1.0;
    vec4 g1 = texture(s_gbuf1, v_uv);

    if (g1.w < 0.5) {
        vec2 ndc = v_uv * 2.0 - 1.0;
        vec4 clip_far = vec4(ndc, 1.0, 1.0);
        vec4 world_far4 = u_inv_view_proj * clip_far;
        world_far4 /= world_far4.w;
        vec3 world_dir = normalize(world_far4.xyz - u_cam_pos.xyz);

        float t = clamp(world_dir.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 sky = mix(pc.horizon.rgb, pc.zenith.rgb, t);
        float sun = smoothstep(pc.sun_color.a, 1.0, dot(normalize(pc.sun_dir.xyz), world_dir));
        sky += pc.sun_color.rgb * sun;
        out_color = vec4(sky, 1.0);
        return;
    }

    vec4 g0 = texture(s_gbuf0, v_uv);
    vec4 g2 = texture(s_gbuf2, v_uv);
    vec4 g3 = texture(s_gbuf3, v_uv);

    vec3 base     = g0.rgb;
    float ao      = g0.a * texture(s_ssao, v_uv).r;
    vec3 n        = normalize(g1.rgb);
    float metallic  = g2.r;
    float roughness = g2.g;
    float lit_flag  = g2.a;
    vec3 emissive   = g3.rgb;

    int dbg = int(u_shadow_config.z + 0.5);
    if (dbg > 0) {
        if      (dbg == 1) out_color = vec4(base,            1.0);
        else if (dbg == 2) out_color = vec4(n * 0.5 + 0.5,  1.0);
        else if (dbg == 3) out_color = vec4(vec3(metallic),  1.0);
        else if (dbg == 4) out_color = vec4(vec3(roughness), 1.0);
        else if (dbg == 5) out_color = vec4(vec3(ao),        1.0);
        else if (dbg == 6) out_color = vec4(emissive,        1.0);
        else if (dbg == 7) {
            float depth2 = texture(s_depth, v_uv).r;
            vec4 wp4b = u_inv_view_proj * vec4(v_uv * 2.0 - 1.0, depth2, 1.0);
            vec3 wp2  = wp4b.xyz / wp4b.w;
            float vz2 = -(u_view * vec4(wp2, 1.0)).z;
            int cc = 3;
            if      (vz2 < u_cascade_splits.x) cc = 0;
            else if (vz2 < u_cascade_splits.y) cc = 1;
            else if (vz2 < u_cascade_splits.z) cc = 2;
            vec3 col;
            if      (cc == 0) col = vec3(1.0, 0.2, 0.2);
            else if (cc == 1) col = vec3(0.2, 1.0, 0.2);
            else if (cc == 2) col = vec3(0.2, 0.4, 1.0);
            else              col = vec3(1.0, 1.0, 0.2);
            out_color = vec4(base * col, 1.0);
        }
        return;
    }

    float depth   = texture(s_depth, v_uv).r;
    vec4 wp4      = u_inv_view_proj * vec4(v_uv * 2.0 - 1.0, depth, 1.0);
    vec3 world_pos = wp4.xyz / wp4.w;

    vec3 v      = normalize(u_cam_pos.xyz - world_pos);
    float n_dot_v = max(dot(n, v), 1e-4);   // precompute once; clamped for visibility fn
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
            vec3  to_light = u_light_data0[i].xyz - world_pos;
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
            shadow = sample_shadow(world_pos, n, l, smode);
            shadow_used = true;
            shadow *= contact_shadow(world_pos, n, l, n_dot_l);

            // Temporal accumulation: blend with history for smoother shadows
            // Only applied within max_dist to avoid ghosting in far cascades
            float view_z = -(u_view * vec4(world_pos, 1.0)).z;
            if (u_temporal_params.y > 0.5 && view_z < u_temporal_params.z) {
                vec4 prev_clip = u_prev_view_proj * vec4(world_pos, 1.0);
                if (prev_clip.w > 0.001) {
                    vec2 prev_uv = (prev_clip.xy / prev_clip.w) * 0.5 + 0.5;
                    if (all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)))) {
                        float history = texture(s_shadow_accum, prev_uv).r;
                        shadow = mix(history, shadow, u_temporal_params.x);
                    }
                }
            }
            shadow = clamp(shadow, 0.0, 1.0);
            out_shadow_term = shadow;
        } else if (type < 0.5) {
            shadow *= contact_shadow(world_pos, n, l, n_dot_l);
        }

        radiance += ((kd * base / PI) + spec) * light_color * n_dot_l * shadow;
    }

    vec3 f0_surf  = mix(vec3(0.04), base, metallic);
    vec3 kS_amb   = fresnel_schlick_roughness(n_dot_v, f0_surf, roughness);
    vec3 kD_amb   = (1.0 - kS_amb) * (1.0 - metallic);

    vec3 ambient;
    if (u_ibl_params.x > 0.5) {
        const float MAX_REFLECTION_LOD = 4.0;
        vec3 R           = reflect(-v, n);
        vec3 irradiance  = texture(s_ibl_irradiance, n).rgb;
        vec3 prefiltered = textureLod(s_ibl_prefilter, R, roughness * MAX_REFLECTION_LOD).rgb;
        vec2 brdf_lut    = texture(s_ibl_brdf_lut, vec2(n_dot_v, roughness)).rg;
        vec3 diffuse_ibl  = kD_amb * irradiance * base * u_ibl_params.z;
        vec3 specular_ibl = prefiltered * (kS_amb * brdf_lut.x + brdf_lut.y) * u_ibl_params.w;
        ambient = (diffuse_ibl + specular_ibl) * ao * u_ibl_params.y;
    } else {
        vec3 ambient_diff = u_ambient.rgb * base * (1.0 - metallic) * ao;
        vec3 ambient_spec = u_ambient.rgb * f0_surf * (1.0 - roughness * roughness) * ao * 0.5;
        ambient = ambient_diff + ambient_spec;
    }

    vec3 color   = lit_flag > 0.5 ? ambient + radiance + emissive : base + emissive;
    out_color    = vec4(color, 1.0);
}