#version 450

layout(set = 0, binding = 0) uniform sampler2D s_input;

layout(push_constant) uniform BrightPush {
    vec4 params;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 c = texture(s_input, v_uv).rgb;
    float threshold = pc.params.x;
    vec3 bright = max(c - threshold, vec3(0.0));
    out_color = vec4(bright, 1.0);
}
