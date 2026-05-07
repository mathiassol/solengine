$input a_position, a_normal, a_tangent, a_texcoord0, a_color0
$output v_worldPos, v_normalWS, v_tangentWS, v_bitangentWS, v_texcoord0, v_color0, v_shadowCoord

#include <bgfx_shader.sh>

uniform mat4 u_lightMtx;

void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position   = mul(u_viewProj, worldPos);

    v_worldPos = worldPos.xyz;

    // Transform normal and tangent (assumes uniform scale)
    vec3 N = normalize(mul(u_model[0], vec4(a_normal,       0.0)).xyz);
    vec3 T = normalize(mul(u_model[0], vec4(a_tangent.xyz,  0.0)).xyz);
    T = normalize(T - dot(T, N) * N); // re-orthogonalise
    vec3 B = cross(N, T) * a_tangent.w;

    v_normalWS    = N;
    v_tangentWS   = T;
    v_bitangentWS = B;
    v_texcoord0   = a_texcoord0;
    v_color0      = a_color0;

    // Shadow coordinate (world → light clip → UV)
    v_shadowCoord = mul(u_lightMtx, worldPos);
}
