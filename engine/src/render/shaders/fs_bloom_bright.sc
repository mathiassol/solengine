$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdrTex, 0);
uniform vec4 u_bloomThresh; // x=threshold, y=knee

void main() {
    vec3 color      = texture2D(s_hdrTex, v_texcoord0).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    float thresh = u_bloomThresh.x;
    float knee   = u_bloomThresh.y;

    // Soft-knee threshold curve
    float rq     = clamp(brightness - thresh + knee, 0.0, 2.0 * knee);
    rq           = (rq * rq) / (4.0 * knee + 0.00001);
    float w      = max(rq, brightness - thresh) / max(brightness, 0.00001);

    gl_FragColor = vec4(color * w, 1.0);
}
