#version 450

layout(set = 0, binding = 0) uniform sampler2D s_ssao;

layout(push_constant) uniform BlurPush {
    vec4 texel;  // xy = 1/width, 1/height
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out float out_ao;

void main() {
    vec2 ts = pc.texel.xy;
    float ao = 0.0;
    for (int x = -2; x <= 1; ++x)
        for (int y = -2; y <= 1; ++y)
            ao += texture(s_ssao, v_uv + vec2(float(x), float(y)) * ts).r;
    out_ao = ao / 16.0;
}
