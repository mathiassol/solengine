#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform SkyCapturePush {
    vec4 sun_dir;
    vec4 zenith;
    vec4 horizon;
    vec4 sun_color;
    int  face;
    float pad0; float pad1; float pad2;
} pc;

const float PI = 3.14159265359;

vec3 uv_to_dir(vec2 uv, int face) {
    vec2 n = uv * 2.0 - 1.0;
    if      (face == 0) return normalize(vec3( 1.0, -n.y, -n.x));
    else if (face == 1) return normalize(vec3(-1.0, -n.y,  n.x));
    else if (face == 2) return normalize(vec3( n.x,  1.0,  n.y));
    else if (face == 3) return normalize(vec3( n.x, -1.0, -n.y));
    else if (face == 4) return normalize(vec3( n.x, -n.y,  1.0));
    else                return normalize(vec3(-n.x, -n.y, -1.0));
}

vec3 sample_sky(vec3 dir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(pc.horizon.rgb, pc.zenith.rgb, t * t);
    // Sun disk
    float sun_dot = dot(normalize(pc.sun_dir.xyz), dir);
    float sun_mask = smoothstep(pc.sun_color.a, pc.sun_color.a + 0.0002, sun_dot);
    sky += pc.sun_color.rgb * sun_mask;
    return sky;
}

void main() {
    vec3 dir = uv_to_dir(v_uv, pc.face);
    out_color = vec4(sample_sky(dir), 1.0);
}
