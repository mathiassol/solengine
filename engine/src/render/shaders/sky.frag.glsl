#version 450

// Physically-based atmospheric sky — Nishita 1993 single-scatter (analytical OD).
// SkyPush (128 bytes):
//   sun_dir.xyz = normalized direction toward sun,  sun_dir.w = sun_intensity
//   atmo.x = turbidity (2=clear, 10=hazy), .y = rayleigh_scale, .z = mie_strength, .w = sky_exposure
//   sun_disk.x = cos(sun angular radius),  .y = sun_bloom_mult, .z = star_density, .w = star_brightness
//   night.x = night_sky_brightness, .yzw = night_sky_color
//   flags.x = 1 → sample HDR cubemap
//   sky_tint.xyz = artistic sky color tint
//   sun_tint.xyz = sun disk color tint
//   ground.xyz = below-horizon ground color, .w = elapsed time (for star twinkling)

const float PI = 3.14159265359;

// Rayleigh scattering coefficients per metre (RGB for ~680, 550, 440 nm wavelengths)
const vec3  BETA_R    = vec3(5.5e-6, 13.0e-6, 22.4e-6);
// Mie base coefficient per metre per turbidity unit
const float BETA_M_B  = 2.1e-6;
// Rayleigh and Mie atmospheric scale heights (metres)
const float H_R = 7994.0;
const float H_M = 1200.0;
// Mie phase asymmetry factor (0.76 = typical for haze)
const float MIE_G = 0.76;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_light_mtx[4];
    vec4 u_cam_pos;
    vec4 u_ambient;
    vec4 u_light_count;
    vec4 u_light_data0[8];
    vec4 u_light_data1[8];
    vec4 u_light_data2[8];
    vec4 u_light_data3[8];
    vec4 u_shadow_config;
    vec4 u_cascade_splits;
    mat4 u_inv_view_proj;
    vec4 u_shadow_params;
    vec4 u_shadow_extra;
    mat4 u_prev_view_proj;
    vec4 u_temporal_params;
    vec4 u_ibl_params;
};

layout(set = 1, binding = 0) uniform samplerCube u_sky_cube;

layout(push_constant) uniform SkyPush {
    vec4  sun_dir;   // xyz = toward sun (normalised), w = sun_intensity
    vec4  atmo;      // x = turbidity, y = rayleigh_scale, z = mie_strength, w = sky_exposure
    vec4  sun_disk;  // x = cos(sun angular radius), y = sun_bloom_mult, z = star_density, w = star_brightness
    vec4  night;     // x = night_sky_brightness, yzw = night_sky_color (rgb)
    ivec4 flags;     // x = 1 → HDR cubemap
    vec4  sky_tint;  // xyz = artistic sky color tint
    vec4  sun_tint;  // xyz = sun disk color tint
    vec4  ground;    // xyz = ground color, w = elapsed time
} pc;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;

float phase_rayleigh(float cos_t) {
    return (3.0 / (16.0 * PI)) * (1.0 + cos_t * cos_t);
}

float phase_mie(float cos_t) {
    float g  = MIE_G;
    float g2 = g * g;
    float d  = max(1.0 + g2 - 2.0 * g * cos_t, 1e-6);
    return (3.0 / (8.0 * PI)) * (1.0 - g2) * (1.0 + cos_t * cos_t)
           / ((2.0 + g2) * pow(d, 1.5));
}

// Simplified atmospheric optical depth from ground level at sin(elevation).
// Capped to avoid singularity at the horizon.
float od_ray(float sin_elev, float scale_height) {
    return scale_height / max(sin_elev + 0.04, 0.04);
}

vec3 atmosphere(vec3 view_dir, vec3 sun_dir,
                float turbidity, float rayleigh_scale, float mie_strength) {
    float cos_t   = dot(view_dir, sun_dir);
    float BETA_M  = BETA_M_B * turbidity * mie_strength;

    // Optical depths for view ray and sun ray
    float sinV = max(view_dir.y, 0.0);
    float sinS = sun_dir.y;  // may be negative (below horizon)

    float odRV = od_ray(sinV, H_R);
    float odMV = od_ray(sinV, H_M);
    float odRS = od_ray(sinS, H_R);  // od_ray clamps at sinS+0.04
    float odMS = od_ray(sinS, H_M);

    // Combined transmittance (view path + sun path)
    vec3  T_R = exp(-BETA_R * rayleigh_scale * (odRV + odRS));
    float T_M = exp(-BETA_M  * (odMV + odMS));

    float pR = phase_rayleigh(cos_t);
    float pM = phase_mie(cos_t);

    // In-scatter: beta * phase * optical_depth * transmittance
    vec3  sR = BETA_R * rayleigh_scale * pR * odRV * T_R;
    float sM = BETA_M  * pM             * odMV * T_M;

    // Below the horizon: sinV is clamped to 0, so the scatter evaluates at the horizon
    // elevation — giving the warm orange/white horizon colour rather than pure black.
    return (sR + sM);
}

// ---- Hash + noise utilities ------------------------------------------------

float hash31(vec3 p) {
    p  = fract(p * vec3(127.1, 311.7, 74.7));
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

// Smooth value noise (trilinear interpolation of hash values)
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash31(i),               hash31(i+vec3(1,0,0)), f.x),
            mix(hash31(i+vec3(0,1,0)),   hash31(i+vec3(1,1,0)), f.x), f.y),
        mix(mix(hash31(i+vec3(0,0,1)),   hash31(i+vec3(1,0,1)), f.x),
            mix(hash31(i+vec3(0,1,1)),   hash31(i+vec3(1,1,1)), f.x), f.y),
        f.z);
}

// ---- Star field (two layers, twinkling via elapsed time) -------------------

vec3 star_field(vec3 dir, float density, float brightness, float night_t, float time) {
    if (night_t < 0.001) return vec3(0.0);
    float hfade = smoothstep(0.0, 0.07, dir.y);
    if (hfade < 0.001) return vec3(0.0);

    vec3 result = vec3(0.0);

    // Layer 1: bright, sparse stars with twinkling
    vec3  c1 = floor(dir * 200.0);
    float h1 = hash31(c1);
    if (h1 > 1.0 - density) {
        float s      = (h1 - (1.0 - density)) / density;
        s            = s * s;
        float col    = hash31(c1 + 5.3);
        // Each star has its own twinkling frequency and phase
        float twinkle = 0.80 + 0.20 * sin(time * (3.0 + h1 * 9.0) + h1 * 47.3);
        vec3 color    = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.90, 0.6), col * col);
        result += color * s * 1.5 * twinkle;
    }

    // Layer 2: dimmer, denser stars (Milky Way feel)
    vec3  c2 = floor(dir * 450.0);
    float h2 = hash31(c2 + vec3(7.3, 2.1, 5.9));
    if (h2 > 1.0 - density * 4.0) {
        float s = (h2 - (1.0 - density * 4.0)) / (density * 4.0);
        result += vec3(0.50, 0.60, 0.80) * s * 0.3;
    }

    return result * brightness * night_t * hfade;
}

// ---- Nebula (multi-octave colored noise, gives Milky Way feel) -------------

vec3 night_nebula(vec3 dir, float night_t) {
    if (night_t < 0.001) return vec3(0.0);
    float hfade = smoothstep(0.0, 0.18, dir.y);
    if (hfade < 0.001) return vec3(0.0);

    // 3-octave FBM
    float n = vnoise(dir * 7.0)  * 0.55
            + vnoise(dir * 14.0) * 0.28
            + vnoise(dir * 28.0) * 0.17;
    n = smoothstep(0.42, 0.78, n);

    // Region color varies by a coarse noise layer
    float region = vnoise(dir * 2.5 + 0.9);
    vec3 col_a   = vec3(0.04, 0.08, 0.30);   // deep blue band
    vec3 col_b   = vec3(0.22, 0.04, 0.18);   // magenta/purple band
    vec3 col_c   = vec3(0.00, 0.13, 0.10);   // teal band
    vec3 col     = mix(mix(col_a, col_b, region), col_c, region * region * 0.6);

    return col * n * night_t * hfade * 0.55;
}

// ---------------------------------------------------------------------------

void main() {
    // Reconstruct view direction using precomputed inv_view_proj
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 wfar  = u_inv_view_proj * vec4(ndc, 1.0, 1.0);
    wfar      /= wfar.w;
    vec3 world_dir = normalize(wfar.xyz - u_cam_pos.xyz);

    if (pc.flags.x == 1) {
        out_color = vec4(texture(u_sky_cube, world_dir).rgb, 1.0);
        return;
    }

    vec3  sun_dir        = normalize(pc.sun_dir.xyz);
    float sun_intensity  = pc.sun_dir.w;
    float turbidity      = pc.atmo.x;
    float rayleigh_scale = pc.atmo.y;
    float mie_strength   = pc.atmo.z;
    float sky_exposure   = pc.atmo.w;
    float sun_cos_r      = pc.sun_disk.x;
    float sun_bloom      = pc.sun_disk.y;
    float star_density   = pc.sun_disk.z;
    float star_bright    = pc.sun_disk.w;
    float night_bright   = pc.night.x;
    vec3  night_color    = pc.night.yzw;
    vec3  sky_tint       = pc.sky_tint.xyz;
    vec3  sun_tint       = pc.sun_tint.xyz;
    vec3  ground_color   = pc.ground.xyz;
    float time           = pc.ground.w;

    // Atmospheric scattering — apply artistic sky tint
    vec3 sky = atmosphere(world_dir, sun_dir, turbidity, rayleigh_scale, mie_strength);
    sky *= sun_intensity * sky_exposure * sky_tint;

    // Atmosphere fades as sun goes below horizon.
    // Wider range (-0.25 → 0.08) keeps the sky orange/warm all the way through golden hour.
    // At exact horizon (y=0): day_fac ≈ 0.85 — sky is still bright and colourful.
    float day_fac = smoothstep(-0.25, 0.08, sun_dir.y);
    sky *= day_fac;

    // Night elements appear ONLY when sun is below horizon — prevents night sky
    // bleeding into golden hour.  Fully night at ~sun elevation -0.25 rad.
    float night_fac = smoothstep(0.0, -0.25, sun_dir.y);

    // Below horizon: blend the scatter horizon colour (which sky already holds,
    // since sinV is clamped to 0) toward the user-controlled ground tone.
    // Cubic ease-in keeps the horizon scatter dominant for a long way down — no band.
    if (world_dir.y < 0.0) {
        float t = clamp(-world_dir.y / 0.4, 0.0, 1.0);
        t = t * t * t;
        sky = mix(sky, ground_color, t);
    }

    // Night sky: user-tinted base gradient + nebula + twinkling stars
    vec3 night_sky = night_color * night_bright * smoothstep(-0.1, 0.3, world_dir.y);
    sky += night_sky * night_fac;
    sky += night_nebula(world_dir, night_fac);
    sky += star_field(world_dir, star_density, star_bright, night_fac, time);

    // Sun disk
    float cos_angle = dot(world_dir, sun_dir);
    if (cos_angle > sun_cos_r * 0.995) {
        float disc = smoothstep(sun_cos_r * 0.9995, sun_cos_r, cos_angle);

        // Limb darkening
        float sin_disc = sqrt(max(1.0 - cos_angle * cos_angle, 0.0));
        float sin_r    = sqrt(max(1.0 - sun_cos_r * sun_cos_r, 0.0));
        float mu       = clamp(1.0 - sin_disc / max(sin_r, 1e-4), 0.0, 1.0);
        float limb     = mix(0.6, 1.0, mu * mu);

        // Sun colour from Rayleigh transmittance, then apply user tint
        float odRS  = od_ray(sun_dir.y, H_R);
        vec3  T_sun = exp(-BETA_R * rayleigh_scale * odRS);
        T_sun      /= max(T_sun.g, 0.01);
        T_sun      *= sun_tint;

        sky += disc * sun_bloom * sun_intensity * sky_exposure * T_sun * limb * day_fac;
    }

    out_color = vec4(sky, 1.0);
}
