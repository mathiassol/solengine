#version 450

layout(set = 0, binding = 0) uniform samplerCube s_env;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PrefilterPush {
    int   face;
    float roughness;
    int   pad0; int pad1;
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

vec3 importance_sample_ggx(vec2 xi, vec3 n, float roughness) {
    float a  = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    vec3 h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

    vec3 up    = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tan   = normalize(cross(up, n));
    vec3 bitan = cross(n, tan);
    return normalize(tan * h.x + bitan * h.y + n * h.z);
}

void main() {
    vec3 n = uv_to_dir(v_uv, pc.face);
    vec3 v = n; // n = v assumption (isotropic)

    vec3  prefiltered  = vec3(0.0);
    float total_weight = 0.0;
    const uint N = 128u;

    for (uint i = 0u; i < N; i++) {
        vec2 xi  = hammersley(i, N);
        vec3 h   = importance_sample_ggx(xi, n, pc.roughness);
        vec3 l   = normalize(2.0 * dot(v, h) * h - v);
        float nl = max(dot(n, l), 0.0);
        if (nl > 0.0) {
            prefiltered  += texture(s_env, l).rgb * nl;
            total_weight += nl;
        }
    }

    out_color = vec4(prefiltered / max(total_weight, 0.001), 1.0);
}
