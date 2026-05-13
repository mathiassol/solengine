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

layout(push_constant) uniform SkyPush {
    vec4 sun_dir;
    vec4 zenith;
    vec4 horizon;
    vec4 sun_color;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    mat4 inv_proj = inverse(u_proj);
    mat4 inv_view = inverse(u_view);
    vec4 view_ray = inv_proj * vec4(ndc, 1.0, 1.0);
    vec3 world_dir = normalize((inv_view * vec4(normalize(view_ray.xyz), 0.0)).xyz);

    float t = clamp(world_dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(pc.horizon.rgb, pc.zenith.rgb, t);
    float sun = smoothstep(pc.sun_color.a, 1.0, dot(normalize(pc.sun_dir.xyz), world_dir));
    sky += pc.sun_color.rgb * sun;
    out_color = vec4(sky, 1.0);
}
