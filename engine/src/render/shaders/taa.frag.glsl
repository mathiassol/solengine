#version 450

layout(location = 0) in vec2 v_uv;

// set 0: TAA inputs
layout(set = 0, binding = 0) uniform sampler2D s_hdr;
layout(set = 0, binding = 1) uniform sampler2D s_history;
layout(set = 0, binding = 2) uniform sampler2D s_depth;

// set 1: frame UBO — only the matrices we need for reprojection
layout(set = 1, binding = 0) uniform FrameUBO {
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

// push constant
layout(push_constant) uniform TaaPush {
    vec4 params;     // x=blend_alpha, y=enabled, z=variance_gamma, w=sharpening
    vec4 resolution; // x=width, y=height, z=1/width, w=1/height
} pc;

layout(location = 0) out vec4 out_color;

// ---- Reinhard tonemap (paper: "tonemap before TAA, inv-tonemap after") -----
// Compresses HDR fireflies so they don't blow up the neighbourhood AABB.
vec3 tm(vec3 c)     { return c / (1.0 + c); }
vec3 inv_tm(vec3 c) { return c / max(1.0 - c, vec3(1e-4)); }

// ---- RGB <-> YCoCg ----------------------------------------------------------
// Paper (Karis/Kar14): "YCoCg decorrelates luma and chroma → tighter AABB"
vec3 to_ycocg(vec3 c) {
    float co  = c.r - c.b;
    float tmp = c.b + co * 0.5;
    float cg  = c.g - tmp;
    return vec3(tmp + cg * 0.5, co, cg);
}
vec3 from_ycocg(vec3 c) {
    float tmp = c.x - c.z * 0.5;
    float g   = c.z + tmp;
    float b   = tmp - c.y * 0.5;
    return max(vec3(0.0), vec3(b + c.y, g, b));
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// ---- 5-tap Catmull-Rom history resampler (Karis/UE4, ~5 bilinear fetches) --
// Paper: "avoid bilinear blur accumulation over many frames"
vec3 sample_catmull_rom(vec2 uv) {
    vec2 px  = pc.resolution.xy;
    vec2 ipx = pc.resolution.zw;

    vec2 pos    = uv * px;
    vec2 center = floor(pos - 0.5) + 0.5;
    vec2 f      = pos - center;

    // Catmull-Rom basis weights
    vec2 w0  = f * (-0.5 + f * ( 1.0 - 0.5 * f));
    vec2 w1  =     ( 1.0 + f * f * (-2.5 + 1.5 * f));
    vec2 w2  = f * ( 0.5 + f * ( 2.0 - 1.5 * f));
    vec2 w3  = f * f * (-0.5 + 0.5 * f);

    // Merge inner taps into one bilinear fetch per axis
    vec2 w12 = w1 + w2;
    vec2 tc0  = (center - 1.0)        * ipx;
    vec2 tc12 = (center + w2 / w12)   * ipx;
    vec2 tc3  = (center + 2.0)        * ipx;

    // 5 bilinear taps (cross pattern + centre)
    vec3 s0 = texture(s_history, vec2(tc12.x, tc0.y )).rgb;
    vec3 s1 = texture(s_history, vec2(tc0.x,  tc12.y)).rgb;
    vec3 s2 = texture(s_history, vec2(tc12.x, tc12.y)).rgb;
    vec3 s3 = texture(s_history, vec2(tc3.x,  tc12.y)).rgb;
    vec3 s4 = texture(s_history, vec2(tc12.x, tc3.y )).rgb;

    float wa = w12.x * w0.y;
    float wb = w0.x  * w12.y;
    float wc = w12.x * w12.y;
    float wd = w3.x  * w12.y;
    float we = w12.x * w3.y;
    float wt = max(wa + wb + wc + wd + we, 1e-5);

    return (s0*wa + s1*wb + s2*wc + s3*wd + s4*we) / wt;
}

// ---- Variance-clip in AABB (Salvi / Lottes) ---------------------------------
// Clips q toward anchor p (guaranteed inside AABB) using ray-box intersection.
vec3 clip_aabb(vec3 amin, vec3 amax, vec3 p, vec3 q) {
    vec3 r    = q - p;
    vec3 rmax = amax - p;
    vec3 rmin = amin - p;
    const float eps = 1e-5;
    float s = 1.0;
    if (r.x >  eps) s = min(s, rmax.x / r.x); else if (r.x < -eps) s = min(s, rmin.x / r.x);
    if (r.y >  eps) s = min(s, rmax.y / r.y); else if (r.y < -eps) s = min(s, rmin.y / r.y);
    if (r.z >  eps) s = min(s, rmax.z / r.z); else if (r.z < -eps) s = min(s, rmin.z / r.z);
    return p + r * s;
}

void main() {
    vec2 uv  = v_uv;
    vec2 ipx = pc.resolution.zw;

    // Pass-through when TAA disabled
    if (pc.params.y < 0.5) {
        out_color = vec4(texture(s_hdr, uv).rgb, 1.0);
        return;
    }

    // 1. Current frame — Reinhard-compressed to kill HDR firefly AABB inflation
    vec3 curr_hdr = max(texture(s_hdr, uv).rgb, vec3(0.0));
    vec3 curr_tm  = tm(curr_hdr);

    // 2. Build 3×3 neighbourhood moments in tonemapped YCoCg space
    //    Paper: variance clipping with μ ± γσ is more accurate than min/max AABB
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec3 s = to_ycocg(tm(max(texture(s_hdr, uv + vec2(x, y) * ipx).rgb, vec3(0.0))));
            m1 += s;
            m2 += s * s;
        }
    }
    m1 /= 9.0;
    m2 /= 9.0;

    float gamma  = pc.params.z;
    vec3 sigma   = sqrt(max(m2 - m1 * m1, vec3(0.0)));
    vec3 aabb_min = m1 - gamma * sigma;
    vec3 aabb_max = m1 + gamma * sigma;

    // 3. Depth reprojection → history UV
    float depth     = texture(s_depth, uv).r;
    vec4  world_h   = u_inv_view_proj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec3  world     = world_h.xyz / world_h.w;
    vec4  prev_clip = u_prev_view_proj * vec4(world, 1.0);
    vec2  hist_uv   = prev_clip.xy / prev_clip.w * 0.5 + 0.5;

    bool valid = all(greaterThanEqual(hist_uv, vec2(0.002))) &&
                 all(lessThanEqual   (hist_uv, vec2(0.998))) &&
                 (u_prev_view_proj[0][0] != 0.0);

    if (!valid) {
        out_color = vec4(curr_hdr, 1.0);
        return;
    }

    // 4. History — 5-tap Catmull-Rom (avoids bilinear blur accumulation)
    //    Also Reinhard-compressed so it lives in the same space as the AABB
    vec3 hist_hdr = max(sample_catmull_rom(hist_uv), vec3(0.0));
    vec3 hist_tm  = tm(hist_hdr);

    // 5. Clip history to neighbourhood AABB in YCoCg space
    vec3 hist_ycocg = to_ycocg(hist_tm);
    vec3 clipped_ycocg = clip_aabb(aabb_min, aabb_max, m1, hist_ycocg);
    vec3 clipped_tm    = from_ycocg(clipped_ycocg);

    // 6. Luminance-adaptive blend weight (Karis/Kar14)
    //    Prevents HDR-bright samples from desaturating history during averaging
    float w_c = 1.0 / (1.0 + luma(curr_tm));
    float w_h = 1.0 / (1.0 + luma(clipped_tm));
    float alpha = pc.params.x;  // base weight toward current frame

    float denom  = max(w_c * alpha + w_h * (1.0 - alpha), 1e-5);
    vec3  blended = (curr_tm * w_c * alpha + clipped_tm * w_h * (1.0 - alpha)) / denom;

    // 7. Optional sharpening — high-frequency from current frame re-added
    //    Applied in tonemapped space before inverse-tonemap
    float sharp = pc.params.w;
    if (sharp > 0.001) {
        // Low-frequency cross average of current frame (4 cardinal neighbours)
        vec3 lf =  tm(max(texture(s_hdr, uv + vec2( 1, 0) * ipx).rgb, vec3(0.0)));
             lf += tm(max(texture(s_hdr, uv + vec2(-1, 0) * ipx).rgb, vec3(0.0)));
             lf += tm(max(texture(s_hdr, uv + vec2( 0, 1) * ipx).rgb, vec3(0.0)));
             lf += tm(max(texture(s_hdr, uv + vec2( 0,-1) * ipx).rgb, vec3(0.0)));
             lf /= 4.0;
        blended += (curr_tm - lf) * sharp;
    }

    // 8. Inverse tonemap back to linear HDR for the downstream tonemap pass
    vec3 result = inv_tm(clamp(blended, vec3(0.0), vec3(0.9999)));

    out_color = vec4(clamp(result, vec3(0.0), vec3(65504.0)), 1.0);
}
