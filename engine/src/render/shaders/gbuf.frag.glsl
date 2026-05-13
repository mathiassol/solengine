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
};

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

layout(location = 0) out vec4 out_gbuf0; // albedo.rgb + ao=1.0
layout(location = 1) out vec4 out_gbuf1; // world_normal.xyz (encoded) + w=1.0 (geometry)
layout(location = 2) out vec4 out_gbuf2; // metallic, roughness, unused, lit_flag
layout(location = 3) out vec4 out_gbuf3; // emissive.rgb, unused

void main() {
    vec4 albedo_sample = pc.flags.y > 0.5 ? texture(s_albedo, v_uv) : vec4(1.0);
    vec3 base = pc.base_color.rgb * albedo_sample.rgb * v_color.rgb;
    float alpha = pc.base_color.a * albedo_sample.a * v_color.a;

    // Alpha cutout (MASK mode)
    int alpha_mode = int(round(pc.pbr.w));
    if (alpha_mode == 1 && alpha < pc.pbr.z) discard;

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

    vec3 emissive_color = pc.emissive.rgb;
    if (pc.emissive.w > 0.5)
        emissive_color *= texture(s_emissive_map, v_uv).rgb;

    // Pack into G-buffer
    out_gbuf0 = vec4(base, 1.0);                            // albedo + ao=1
    out_gbuf1 = vec4(n, 1.0);                               // world normal + w=1 (geometry)
    out_gbuf2 = vec4(metallic, roughness, 0.0, pc.flags.x); // metallic, roughness, unused, lit_flag
    out_gbuf3 = vec4(emissive_color, 0.0);                  // emissive
}
