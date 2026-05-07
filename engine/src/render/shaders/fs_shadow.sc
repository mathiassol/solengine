$input v_dummy

#include <bgfx_shader.sh>

void main()
{
    // depth-only pass — no color output needed
    gl_FragColor = vec4_splat(0.0);
}
