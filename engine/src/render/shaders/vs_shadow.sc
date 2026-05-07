$input a_position
$output v_dummy

#include <bgfx_shader.sh>

void main()
{
    v_dummy     = vec4_splat(0.0);
    gl_Position = mul(u_viewProj, mul(u_model[0], vec4(a_position, 1.0)));
}
