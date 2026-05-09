$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_blurTex, 0);
uniform vec4 u_blurDir; // xy = direction * texel_size

void main() {
    vec2 uv  = v_texcoord0;
    vec2 dir = u_blurDir.xy;

    vec3 c = texture2D(s_blurTex, uv).rgb * 0.2270270270;
    c += texture2D(s_blurTex, uv + dir * 1.0).rgb * 0.1945945946;
    c += texture2D(s_blurTex, uv - dir * 1.0).rgb * 0.1945945946;
    c += texture2D(s_blurTex, uv + dir * 2.0).rgb * 0.1216216216;
    c += texture2D(s_blurTex, uv - dir * 2.0).rgb * 0.1216216216;
    c += texture2D(s_blurTex, uv + dir * 3.0).rgb * 0.0540540541;
    c += texture2D(s_blurTex, uv - dir * 3.0).rgb * 0.0540540541;
    c += texture2D(s_blurTex, uv + dir * 4.0).rgb * 0.0162162162;
    c += texture2D(s_blurTex, uv - dir * 4.0).rgb * 0.0162162162;

    gl_FragColor = vec4(c, 1.0);
}
