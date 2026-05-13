#version 450

layout(set = 0, binding = 0) uniform sampler2D s_hdr;
layout(set = 0, binding = 1) uniform sampler2D s_bloom;

layout(push_constant) uniform TonemapPush {
    vec4 params; // x=exposure  y=bloom_intensity  z=mode(0=ACES,1=Reinhard,2=Linear)
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 reinhard(vec3 x) {
    return x / (x + vec3(1.0));
}

void main() {
    vec3 hdr     = texture(s_hdr,   v_uv).rgb;
    vec3 bloom   = texture(s_bloom, v_uv).rgb * pc.params.y;
    vec3 exposed = (hdr + bloom) * pc.params.x;

    int mode = int(pc.params.z + 0.5);
    vec3 mapped;
    if      (mode == 1) mapped = reinhard(exposed);
    else if (mode == 2) mapped = clamp(exposed, 0.0, 1.0);
    else                mapped = aces(exposed);   // mode 0: ACES (default)

    out_color = vec4(mapped, 1.0);
}
