#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec2 out_brdf; // R = scale, G = bias

const float PI = 3.14159265359;

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

// IBL geometry: k = roughness^2 / 2  (different from direct light k = (r+1)^2 / 8)
float geometry_schlick_ggx_ibl(float ndv, float roughness) {
    float k = (roughness * roughness) / 2.0;
    return ndv / (ndv * (1.0 - k) + k);
}

float geometry_smith_ibl(vec3 n, vec3 v, vec3 l, float roughness) {
    return geometry_schlick_ggx_ibl(max(dot(n, v), 0.0), roughness) *
           geometry_schlick_ggx_ibl(max(dot(n, l), 0.0), roughness);
}

void main() {
    float n_dot_v  = v_uv.x;
    float roughness = v_uv.y;

    vec3 v = vec3(sqrt(1.0 - n_dot_v * n_dot_v), 0.0, n_dot_v);
    vec3 n = vec3(0.0, 0.0, 1.0);

    float A = 0.0, B = 0.0;
    const uint N = 1024u;
    for (uint i = 0u; i < N; i++) {
        vec2 xi  = hammersley(i, N);
        vec3 h   = importance_sample_ggx(xi, n, roughness);
        vec3 l   = normalize(2.0 * dot(v, h) * h - v);

        float n_dot_l = max(l.z, 0.0);
        float n_dot_h = max(h.z, 0.0);
        float v_dot_h = max(dot(v, h), 0.0);

        if (n_dot_l > 0.0) {
            float G     = geometry_smith_ibl(n, v, l, roughness);
            float G_vis = (G * v_dot_h) / max(n_dot_h * n_dot_v, 0.0001);
            float Fc    = pow(1.0 - v_dot_h, 5.0);
            A += (1.0 - Fc) * G_vis;
            B += Fc * G_vis;
        }
    }
    out_brdf = vec2(A, B) / float(N);
}
