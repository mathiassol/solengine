#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Set 0: FrameUBO + shadow sampler
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
layout(set = 0, binding = 1) uniform sampler2DArrayShadow s_shadow_map;

// Set 1: density (read) + lighting (write)
layout(set = 1, binding = 0, rgba16f) readonly  uniform image3D vbuf_density;
layout(set = 1, binding = 1, rgba16f) writeonly uniform image3D vbuf_lighting;

layout(push_constant) uniform VolScatterPush {
    vec4 grid_params;   // x=vol_near, y=vol_far, z=slice_count, w=frame_num
    vec4 light_params;  // x=sun_intensity, y=shadow_strength, z=g(phase), w=0
} pc;

const float PI = 3.14159265359;

float hg_phase(float cos_theta, float g) {
    float g2 = g * g;
    float d  = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-5);
    return (1.0 - g2) / (4.0 * PI * d * sqrt(d));
}

vec3 froxel_to_world(uvec3 coord, ivec3 grid_size, vec2 jitter) {
    vec2  uv   = (vec2(coord.xy) + 0.5 + jitter) / vec2(grid_size.xy);
    float near = pc.grid_params.x;
    float far  = pc.grid_params.y;
    float e    = (float(coord.z) + 1.0) / pc.grid_params.z;
    float dist = near * pow(far / near, e);
    vec4 ndc_h  = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, 0.0, 1.0);
    vec4 wh     = u_inv_view_proj * ndc_h;
    wh         /= wh.w;
    vec3 rdir   = normalize(wh.xyz - u_cam_pos.xyz);
    return u_cam_pos.xyz + rdir * dist;
}

float slice_thickness(int slice) {
    float near = pc.grid_params.x, far = pc.grid_params.y, cnt = pc.grid_params.z;
    float t0 = near * pow(far / near, float(slice)     / cnt);
    float t1 = near * pow(far / near, float(slice + 1) / cnt);
    return t1 - t0;
}

void main() {
    ivec3 grid_size = imageSize(vbuf_density);
    uvec3 coord     = gl_GlobalInvocationID;
    if (coord.x >= uint(grid_size.x) || coord.y >= uint(grid_size.y)) return;

    int   light_count  = int(u_light_count.x);
    float g            = pc.light_params.z;

    // Halton jitter: 8-point sequence (base-2 × base-3) for temporal sub-voxel detail
    const vec2 halton8[8] = vec2[8](
        vec2(0.0,   0.0  ), vec2(0.5,   0.333),
        vec2(0.25,  0.667), vec2(0.75,  0.111),
        vec2(0.125, 0.444), vec2(0.625, 0.778),
        vec2(0.375, 0.222), vec2(0.875, 0.556)
    );
    // Combine temporal Halton with spatial IGN so adjacent tiles differ within a frame.
    float ign_x = fract(52.9829189 * fract(dot(vec2(coord.xy),              vec2(0.06711056, 0.00583715))));
    float ign_y = fract(52.9829189 * fract(dot(vec2(coord.xy + uvec2(17u, 31u)), vec2(0.00583715, 0.06711056))));
    vec2 tile_jitter = (halton8[int(pc.grid_params.w) % 8] + vec2(ign_x, ign_y)) * 0.5 - 0.5;

    // View ray direction for this tile (phase function, constant per tile)
    vec3 view_pos  = froxel_to_world(uvec3(coord.xy, 32u), grid_size, tile_jitter);
    vec3 view_dir  = normalize(view_pos - u_cam_pos.xyz);

    // Ambient contributes isotropic in-scattering throughout the medium
    vec3 ambient_scatter = u_ambient.rgb;

    vec3  total_scatter = vec3(0.0);
    float transmittance = 1.0;

    for (int z = 0; z < int(pc.grid_params.z); z++) {
        uvec3 voxel     = uvec3(coord.xy, uint(z));
        vec4  density   = imageLoad(vbuf_density, ivec3(voxel));
        vec3  scatter_c = density.rgb;   // scattering coefficient (albedo-weighted)
        float extinct   = density.a;     // extinction coefficient

        if (extinct < 1e-5) {
            imageStore(vbuf_lighting, ivec3(voxel), vec4(total_scatter, transmittance));
            continue;
        }

        float dt  = slice_thickness(z);
        vec3  pos = froxel_to_world(voxel, grid_size, tile_jitter);

        // Accumulate in-scattering from ambient + point lights only.
        // Directional light is fully handled per-pixel in vol_resolve (shadow + phase).
        vec3 in_light = ambient_scatter;

        for (int li = 0; li < light_count; li++) {
            float type        = u_light_data0[li].w;
            vec3  light_color = u_light_data1[li].rgb * u_light_data1[li].a;
            vec3  l_dir;

            if (type < 0.5) {
                // Directional: skip — handled per-pixel in resolve pass.
                continue;
            } else if (type < 1.5) {
                // Point light — phase is fine at froxel resolution.
                vec3  to_l = u_light_data0[li].xyz - pos;
                float d    = length(to_l);
                if (d < 0.001) continue;
                l_dir = to_l / d;
                float range  = max(u_light_data2[li].w, 0.001);
                float ratio  = d / range;
                float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
                float atten  = (window * window) / (d * d * 0.1 + 1.0);
                float cos_t  = dot(view_dir, l_dir);
                float phase  = hg_phase(cos_t, g);
                in_light += light_color * pc.light_params.x * phase * atten;
            } else {
                continue; // skip spot lights
            }
        }

        vec3 in_s = in_light * scatter_c * dt;
        total_scatter += in_s * transmittance;
        transmittance *= exp(-extinct * dt);

        imageStore(vbuf_lighting, ivec3(voxel), vec4(total_scatter, transmittance));
    }
}
