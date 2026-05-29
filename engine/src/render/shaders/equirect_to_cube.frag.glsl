#version 450

layout(set = 0, binding = 0) uniform sampler2D u_equirect;

layout(push_constant) uniform EquirectPush {
    int   face;
    float pad0, pad1, pad2;
} pc;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;

const float PI = 3.14159265359;

// Same face convention as ibl_sky_capture.frag.glsl
vec3 uv_to_dir(vec2 uv, int face) {
    vec2 n = uv * 2.0 - 1.0;
    if      (face == 0) return normalize(vec3( 1.0, -n.y, -n.x));
    else if (face == 1) return normalize(vec3(-1.0, -n.y,  n.x));
    else if (face == 2) return normalize(vec3( n.x,  1.0,  n.y));
    else if (face == 3) return normalize(vec3( n.x, -1.0, -n.y));
    else if (face == 4) return normalize(vec3( n.x, -n.y,  1.0));
    else                return normalize(vec3(-n.x, -n.y, -1.0));
}

void main() {
    vec3 dir = uv_to_dir(v_uv, pc.face);

    float phi   = atan(dir.z, dir.x);                    // [-PI, PI]
    float theta = asin(clamp(dir.y, -1.0, 1.0));         // [-PI/2, PI/2]

    vec2 eq_uv = vec2(
        phi   / (2.0 * PI) + 0.5,
        0.5 - theta / PI
    );

    out_color = vec4(texture(u_equirect, eq_uv).rgb, 1.0);
}
