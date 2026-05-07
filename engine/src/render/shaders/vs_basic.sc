$input  a_position, a_normal, a_texcoord0, a_color0
$output v_normalWS, v_texcoord0, v_color0

#include <bgfx_shader.sh>

void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position   = mul(u_viewProj, worldPos);

    // Transform normal by model matrix (uniform-scale assumed; good enough for default mat).
    v_normalWS  = normalize(mul(u_model[0], vec4(a_normal, 0.0)).xyz);
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
}
