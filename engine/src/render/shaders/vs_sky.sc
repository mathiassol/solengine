$input a_position, a_texcoord0
$output v_texcoord0, v_viewDir

#include <bgfx_shader.sh>

// u_invViewProj is a bgfx built-in, set automatically by setViewTransform()
uniform vec4 u_camPos;

void main() {
    gl_Position = vec4(a_position.xy, 0.9999, 1.0);
    v_texcoord0 = a_texcoord0;

    vec4 worldFar = mul(u_invViewProj, vec4(a_position.x, a_position.y, 1.0, 1.0));
    v_viewDir = normalize(worldFar.xyz / worldFar.w - u_camPos.xyz);
}
