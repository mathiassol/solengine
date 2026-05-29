#version 450

layout(set = 0, binding = 0) uniform sampler2D s_hdr;
layout(set = 0, binding = 1) uniform sampler2D s_bloom;
layout(set = 0, binding = 2) uniform sampler2D s_ssr;

layout(push_constant) uniform TonemapPush {
    vec4 params; // x=exposure  y=bloom_intensity  z=mode(0=ACES,1=Reinhard,2=Linear)  w=ssr_intensity
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

// AgX by Troy Sobotka — wide-gamut, neutral hue handling, filmic rolloff.
// Polynomial approximation of the AgX contrast sigmoid (mean error^2 ≈ 3.67e-6).
vec3 agx_curve(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return  15.5     * x4 * x2
           - 40.14   * x4 * x
           + 31.96   * x4
           - 6.868   * x2 * x
           + 0.4298  * x2
           + 0.1191  * x
           - 0.00232;
}

vec3 agx(vec3 col) {
    // Inset matrix: linear sRGB → AgX log-shaper space
    const mat3 agx_mat = mat3(
        0.842479062253094,  0.0784335999999992, 0.0792237451477643,
        0.0423282422610123, 0.878468636469772,  0.0791661274605434,
        0.0423756549057051, 0.0784336,          0.879142973793104
    );
    col = agx_mat * col;
    // Log2 encode into [min_ev, max_ev], then normalise to [0,1]
    col = clamp(log2(max(col, vec3(1e-10))), -12.47393, 4.026069);
    col = (col + 12.47393) / (4.026069 + 12.47393);
    return agx_curve(col);
}

vec3 agx_eotf(vec3 col) {
    // Outset matrix: AgX display space → linear sRGB (sRGB OETF applied by hardware)
    const mat3 agx_inv = mat3(
         1.19687900512017,  -0.0980208811401368, -0.0990297440797205,
        -0.0528968517574562, 1.15190312990417,   -0.0989611768448433,
        -0.0529716355144438,-0.0980434501171241,  1.15107367264116
    );
    return max(agx_inv * col, vec3(0.0));
}

void main() {
    vec3 hdr     = texture(s_hdr,   v_uv).rgb;
    vec3 bloom   = texture(s_bloom, v_uv).rgb * pc.params.y;
    vec3 ssr     = texture(s_ssr,   v_uv).rgb * pc.params.w;
    vec3 exposed = (hdr + bloom + ssr) * pc.params.x;

    int mode = int(pc.params.z + 0.5);
    vec3 mapped;
    if      (mode == 1) mapped = reinhard(exposed);
    else if (mode == 2) mapped = clamp(exposed, 0.0, 1.0);
    else if (mode == 3) mapped = agx_eotf(agx(exposed));
    else                mapped = aces(exposed);

    out_color = vec4(mapped, 1.0);
}
