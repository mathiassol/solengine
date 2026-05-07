$input v_normalWS, v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo, 0);

// u_baseColor : material base color (rgba)
// u_params.x  : 1.0 if lit, 0.0 if unlit/emissive
// u_params.y  : 1.0 if texture supplied, 0.0 if pure base_color
// u_lightDir.xyz : world-space direction TO the light (normalized)
uniform vec4 u_baseColor;
uniform vec4 u_params;
uniform vec4 u_lightDir;

void main()
{
    vec4 tex = mix(vec4(1.0, 1.0, 1.0, 1.0),
                   texture2D(s_albedo, v_texcoord0),
                   u_params.y);

    vec4 albedo = u_baseColor * v_color0 * tex;

    // Half-Lambert wrap with a small ambient floor; cheap and forgiving.
    vec3  n     = normalize(v_normalWS);
    vec3  l     = normalize(u_lightDir.xyz);
    float ndl   = max(dot(n, l), 0.0);
    float wrap  = ndl * 0.5 + 0.5;
    float shade = mix(1.0, wrap * 0.85 + 0.15, u_params.x);

    gl_FragColor = vec4(albedo.rgb * shade, albedo.a);
}
