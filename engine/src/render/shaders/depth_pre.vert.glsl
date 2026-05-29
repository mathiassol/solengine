#version 450

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;
layout(location = 4) in vec4 a_tangent;

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

layout(push_constant) uniform PbrPush {
    mat4 model;
    vec4 base_color;
    vec4 pbr;
    vec4 emissive;
    vec4 flags;
} pc;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_color;
layout(location = 4) out vec4 v_world_tangent;

void main() {
    vec4 world = pc.model * vec4(a_pos, 1.0);
    mat3 normal_mtx = transpose(inverse(mat3(pc.model)));
    vec3 n = normalize(normal_mtx * a_normal);
    vec3 t = normalize(mat3(pc.model) * a_tangent.xyz);

    v_world_pos = world.xyz;
    v_world_normal = n;
    v_uv = a_uv;
    v_color = a_color;
    v_world_tangent = vec4(t, a_tangent.w);
    gl_Position = u_proj * u_view * world;
}
