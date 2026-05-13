#version 450

layout(set = 0, binding = 0) uniform samplerCube s_env;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform IBLFacePush {
    int face;
    int pad0; int pad1; int pad2;
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

float radical_inverse_vdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radical_inverse_vdC(i));
}

void main() {
    vec3 n = uv_to_dir(v_uv, pc.face);
    vec3 up    = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, n));
    up         = normalize(cross(n, right));

    vec3 irradiance = vec3(0.0);
    const uint N = 512u;
    for (uint i = 0u; i < N; i++) {
        vec2 xi   = hammersley(i, N);
        // Malley's method: cosine-weighted hemisphere
        float phi    = 2.0 * PI * xi.x;
        float sin_t  = sqrt(xi.y);
        float cos_t  = sqrt(1.0 - xi.y);
        vec3 sample_local = vec3(sin_t * cos(phi), sin_t * sin(phi), cos_t);
        vec3 world_dir = sample_local.x * right + sample_local.y * up + sample_local.z * n;
        irradiance += texture(s_env, world_dir).rgb;
    }
    out_color = vec4(irradiance / float(N), 1.0);
}
