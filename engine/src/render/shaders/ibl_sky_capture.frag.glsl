#version 450

// IBL sky capture — same Nishita atmosphere as sky.frag.glsl
// Push constant layout MUST match IBLSkyPush in vk_renderer.cpp (112 bytes)

const float PI = 3.14159265359;
const vec3  BETA_R   = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float BETA_M_B = 2.1e-6;
const float H_R = 7994.0;
const float H_M = 1200.0;
const float MIE_G = 0.76;

layout(push_constant) uniform SkyCapturePush {
    vec4  sun_dir;
    vec4  atmo;
    vec4  sun_disk;
    vec4  night;
    int   face;
    int   pad0; int pad1; int pad2;
    vec4  sky_tint;
    vec4  sun_tint;
    vec4  ground;    // xyz = ground color, w = unused
} pc;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec3 uv_to_dir(vec2 uv, int face) {
    vec2 n = uv * 2.0 - 1.0;
    if      (face == 0) return normalize(vec3( 1.0, -n.y, -n.x));
    else if (face == 1) return normalize(vec3(-1.0, -n.y,  n.x));
    else if (face == 2) return normalize(vec3( n.x,  1.0,  n.y));
    else if (face == 3) return normalize(vec3( n.x, -1.0, -n.y));
    else if (face == 4) return normalize(vec3( n.x, -n.y,  1.0));
    else                return normalize(vec3(-n.x, -n.y, -1.0));
}

float phase_rayleigh(float cos_t) {
    return (3.0 / (16.0 * PI)) * (1.0 + cos_t * cos_t);
}
float phase_mie(float cos_t) {
    float g = MIE_G, g2 = g*g;
    float d = max(1.0 + g2 - 2.0*g*cos_t, 1e-6);
    return (3.0/(8.0*PI)) * (1.0-g2)*(1.0+cos_t*cos_t) / ((2.0+g2)*pow(d,1.5));
}
float od_ray(float sin_elev, float H) { return H / max(sin_elev + 0.04, 0.04); }

float hash31_ibl(vec3 p) {
    p  = fract(p * vec3(127.1, 311.7, 74.7));
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec3 sky_color(vec3 view_dir) {
    vec3  sun_dir       = normalize(pc.sun_dir.xyz);
    float sun_intensity = pc.sun_dir.w;
    float turbidity     = pc.atmo.x;
    float rayleigh_sc   = pc.atmo.y;
    float mie_str       = pc.atmo.z;
    float sky_exp       = pc.atmo.w;
    float sun_cos_r     = pc.sun_disk.x;
    float sun_bloom     = pc.sun_disk.y;
    float star_density  = pc.sun_disk.z;
    float star_bright   = pc.sun_disk.w;
    float night_bright  = pc.night.x;
    vec3  night_color   = pc.night.yzw;
    vec3  sky_tint      = pc.sky_tint.xyz;
    vec3  sun_tint      = pc.sun_tint.xyz;
    vec3  ground_color  = pc.ground.xyz;

    float BETA_M = BETA_M_B * turbidity * mie_str;
    float cos_t  = dot(view_dir, sun_dir);
    float sinV   = max(view_dir.y, 0.0);
    float sinS   = sun_dir.y;

    vec3  T_R = exp(-BETA_R * rayleigh_sc * (od_ray(sinV,H_R) + od_ray(sinS,H_R)));
    float T_M = exp(-BETA_M  * (od_ray(sinV,H_M) + od_ray(sinS,H_M)));

    vec3  sR = BETA_R * rayleigh_sc * phase_rayleigh(cos_t) * od_ray(sinV,H_R) * T_R;
    float sM = BETA_M  * phase_mie(cos_t) * od_ray(sinV,H_M) * T_M;

    float day_fac  = smoothstep(-0.25, 0.08, sinS);
    float night_fac = smoothstep(0.0, -0.25, sinS);  // night only below horizon
    vec3  sky     = (sR + sM) * sun_intensity * sky_exp * sky_tint * day_fac;

    // Below horizon: cubic ease-in to ground colour — no bright band at horizon
    if (view_dir.y < 0.0) {
        float t = clamp(-view_dir.y / 0.4, 0.0, 1.0);
        t = t * t * t;
        sky = mix(sky, ground_color, t);
    }

    sky += night_color * night_bright * smoothstep(-0.1, 0.3, view_dir.y) * night_fac;

    // Stars in IBL capture (no twinkling — time=0)
    if (night_fac > 0.001 && view_dir.y > 0.0) {
        float hfade = smoothstep(0.0, 0.07, view_dir.y);
        vec3  c1    = floor(view_dir * 200.0);
        float h1    = hash31_ibl(c1);
        if (h1 > 1.0 - star_density) {
            float s  = pow((h1 - (1.0 - star_density)) / star_density, 2.0);
            float cv = hash31_ibl(c1 + 5.3);
            sky += mix(vec3(0.75,0.85,1.0), vec3(1.0,0.9,0.6), cv*cv)
                   * s * 1.5 * star_bright * night_fac * hfade;
        }
        vec3  c2 = floor(view_dir * 450.0);
        float h2 = hash31_ibl(c2 + vec3(7.3, 2.1, 5.9));
        if (h2 > 1.0 - star_density * 4.0) {
            float s = (h2 - (1.0 - star_density * 4.0)) / (star_density * 4.0);
            sky += vec3(0.5, 0.6, 0.8) * s * 0.3 * star_bright * night_fac * hfade;
        }
    }

    // Sun disk for IBL
    if (cos_t > sun_cos_r * 0.995) {
        float disc = smoothstep(sun_cos_r * 0.9995, sun_cos_r, cos_t);
        float odRS = od_ray(sinS, H_R);
        vec3  Ts   = exp(-BETA_R * rayleigh_sc * odRS);
        Ts /= max(Ts.g, 0.01);
        Ts *= sun_tint;
        sky += disc * sun_bloom * sun_intensity * sky_exp * Ts * day_fac;
    }
    return sky;
}

void main() {
    vec3 dir = uv_to_dir(v_uv, pc.face);
    out_color = vec4(sky_color(dir), 1.0);
}
