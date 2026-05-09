$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdrTex,   0);
SAMPLER2D(s_bloomTex, 1);

uniform vec4 u_postParams; // x=bloomStrength, y=exposure

vec3 aces_approx(vec3 v) {
    return clamp((v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 hdr   = texture2D(s_hdrTex,   v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloomTex, v_texcoord0).rgb;

    vec3 color = hdr * u_postParams.y + bloom * u_postParams.x;
    color = aces_approx(color);
    color = pow(color, vec3_splat(1.0 / 2.2));

    gl_FragColor = vec4(color, 1.0);
}
