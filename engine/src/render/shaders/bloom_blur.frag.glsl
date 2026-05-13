#version 450

layout(set = 0, binding = 0) uniform sampler2D s_input;

layout(push_constant) uniform BlurPush {
    vec4 dir;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = texture(s_input, v_uv).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = pc.dir.xy * float(i);
        result += texture(s_input, v_uv + offset).rgb * weights[i];
        result += texture(s_input, v_uv - offset).rgb * weights[i];
    }
    out_color = vec4(result, 1.0);
}
