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
    vec4 u_shadow_params; // x=bias_const, y=bias_slope, z=pcf_radius, w=pcss_light_size (alignment only)
};

layout(set = 1, binding = 0) uniform sampler2D s_normals;
layout(set = 1, binding = 1) uniform sampler2D s_depth;
layout(set = 1, binding = 2) uniform sampler2D s_noise;

layout(push_constant) uniform SSAOPush {
    vec4 params;   // x=radius, y=bias, z=power, w=strength
    vec4 screen;   // z=noise_tile_x, w=noise_tile_y
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out float out_ao;

const vec3 KERNEL[16] = vec3[16](
    vec3( 0.053505,  0.015115,  0.083119),
    vec3( 0.036581,  0.096510,  0.012791),
    vec3( 0.110090, -0.034789,  0.011225),
    vec3( 0.084222, -0.064220,  0.085315),
    vec3( 0.131258,  0.037287,  0.090978),
    vec3(-0.123149, -0.156601,  0.017623),
    vec3( 0.019826,  0.092474,  0.224925),
    vec3( 0.092769,  0.136040,  0.245973),
    vec3(-0.247969, -0.176025,  0.185102),
    vec3( 0.327540,  0.122845,  0.239589),
    vec3( 0.298065,  0.050914,  0.398202),
    vec3( 0.071597,  0.188337,  0.548141),
    vec3( 0.069193,  0.669647,  0.061334),
    vec3(-0.012624,  0.559068,  0.538015),
    vec3( 0.581322,  0.527588,  0.406413),
    vec3( 0.371349,  0.438747,  0.818291));

vec3 uv_to_view(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 wp   = u_inv_view_proj * clip;
    wp /= wp.w;
    return (u_view * wp).xyz;
}

void main() {
    float depth = texture(s_depth, v_uv).r;
    vec4  g1    = texture(s_normals, v_uv);

    if (g1.w < 0.5 || depth >= 0.9999) { out_ao = 1.0; return; }

    vec3 frag_view = uv_to_view(v_uv, depth);
    vec3 world_n   = normalize(g1.rgb);
    vec3 view_n    = normalize(mat3(u_view) * world_n);

    vec2 noise_uv = v_uv * pc.screen.zw;
    vec2 rand_xy  = texture(s_noise, noise_uv).rg * 2.0 - 1.0;
    vec3 rand_vec = vec3(rand_xy, 0.0);

    vec3 tangent   = normalize(rand_vec - view_n * dot(rand_vec, view_n));
    vec3 bitangent = cross(view_n, tangent);
    mat3 TBN       = mat3(tangent, bitangent, view_n);

    float radius    = pc.params.x;
    float bias      = pc.params.y;
    float occlusion = 0.0;

    for (int i = 0; i < 16; ++i) {
        vec3 s_view_pos = frag_view + (TBN * KERNEL[i]) * radius;

        vec4 clip = u_proj * vec4(s_view_pos, 1.0);
        clip.xy  /= clip.w;
        vec2 s_uv = clip.xy * 0.5 + 0.5;

        if (s_uv.x < 0.0 || s_uv.x > 1.0 || s_uv.y < 0.0 || s_uv.y > 1.0)
            continue;

        float s_depth = texture(s_depth, s_uv).r;
        if (s_depth >= 0.9999) continue;

        vec3 s_real_view = uv_to_view(s_uv, s_depth);

        float range_check = smoothstep(0.0, 1.0,
            radius / max(abs(frag_view.z - s_real_view.z), 0.001));

        occlusion += (s_real_view.z >= s_view_pos.z + bias ? 1.0 : 0.0) * range_check;
    }

    float ao = 1.0 - (occlusion / 16.0);
    ao = pow(max(ao, 0.0), pc.params.z);
    out_ao = mix(1.0, ao, pc.params.w);
}
