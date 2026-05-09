$input v_texcoord0, v_viewDir

#include <bgfx_shader.sh>

uniform vec4 u_sunDir;        // xyz = direction toward sun (normalized)
uniform vec4 u_skyZenith;     // rgb = zenith color (top)
uniform vec4 u_skyHorizon;    // rgb = horizon color
uniform vec4 u_sunColor;      // rgb = sun/corona color, a = cos(sun_radius)

void main() {
    vec3 dir = normalize(v_viewDir);

    // Sky gradient: t=0 at horizon (dir.y=0), t=1 at zenith (dir.y=1)
    float t = clamp(dir.y, 0.0, 1.0);
    vec3 sky = mix(u_skyHorizon.rgb, u_skyZenith.rgb, pow(t, 0.4));

    // Below horizon: darken to ground color
    float below = clamp(-dir.y * 4.0, 0.0, 1.0);
    sky = mix(sky, u_skyHorizon.rgb * 0.3, below);

    // Sun disc
    float sunDot  = dot(dir, u_sunDir.xyz);
    float sunMask = smoothstep(u_sunColor.a - 0.0002, u_sunColor.a + 0.0002, sunDot);

    // Sun corona glow
    float glow = pow(max(sunDot, 0.0), 128.0) * 0.5;

    vec3 finalColor = sky + u_sunColor.rgb * (sunMask + glow);

    gl_FragColor = vec4(finalColor, 1.0);
}
