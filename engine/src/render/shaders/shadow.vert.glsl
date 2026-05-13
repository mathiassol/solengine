#version 450

layout(location = 0) in vec3 a_pos;

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

layout(push_constant) uniform ShadowPush {
    mat4 model;
    vec4 params;  // x = cascade index (as float, cast to int)
} pc;

void main() {
    int cascade = int(pc.params.x + 0.5);
    gl_Position = u_light_mtx[cascade] * pc.model * vec4(a_pos, 1.0);
}
