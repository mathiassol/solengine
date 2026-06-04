#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Set 0: FrameUBO (binding 0) — for u_cam_pos, u_inv_view_proj
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

// Set 1: output density volume  (scattering.rgb, extinction.a)
layout(set = 1, binding = 0, rgba16f) writeonly uniform image3D vbuf_density;

layout(push_constant) uniform VolDensityPush {
    vec4 grid_params;   // x=vol_near, y=vol_far, z=slice_count(64), w=0
    vec4 fog;           // x=extinction, y=scattering_albedo, z=g, w=enabled(1=on)
    vec4 fog_color;     // rgb=fog albedo, a=0
    vec4 height_fog;    // x=base_height, y=inv_falloff(1/H), z=extinction, w=scattering_albedo
    vec4 height_color;  // rgb=height fog color, a=enabled(1=on)
    vec4 noise_params;  // x=scale, y=time_offset, z=strength, w=enabled(1/0)
    vec4 noise_params2; // x=octaves, y=lacunarity, z=gain, w=0
} pc;

// Hash-based FBM noise for fog density variation
float hash3(vec3 p) {
    p = fract(p * vec3(443.8975, 397.2973, 491.1871));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float value_noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // smoothstep
    return mix(
        mix(mix(hash3(i),               hash3(i + vec3(1,0,0)), f.x),
            mix(hash3(i + vec3(0,1,0)), hash3(i + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash3(i + vec3(0,0,1)), hash3(i + vec3(1,0,1)), f.x),
            mix(hash3(i + vec3(0,1,1)), hash3(i + vec3(1,1,1)), f.x), f.y), f.z);
}

float fbm_noise(vec3 p, int octaves, float lacunarity, float gain) {
    float val = 0.0, amp = 0.5, freq = 1.0;
    for (int i = 0; i < octaves; ++i) {
        val  += amp * value_noise(p * freq);
        freq *= lacunarity;
        amp  *= gain;
    }
    return val;
}

// Froxel index → world-space position at voxel centre
vec3 froxel_to_world(uvec3 coord, ivec3 grid_size) {
    vec2  uv   = (vec2(coord.xy) + 0.5) / vec2(grid_size.xy);
    float near = pc.grid_params.x;
    float far  = pc.grid_params.y;
    float e    = (float(coord.z) + 1.0) / pc.grid_params.z;
    float dist = near * pow(far / near, e);

    vec4 ndc_h  = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, 0.0, 1.0);
    vec4 world_h = u_inv_view_proj * ndc_h;
    world_h     /= world_h.w;
    vec3 ray_dir = normalize(world_h.xyz - u_cam_pos.xyz);
    return u_cam_pos.xyz + ray_dir * dist;
}

void main() {
    ivec3 grid_size = imageSize(vbuf_density);
    uvec3 coord     = gl_GlobalInvocationID;
    if (coord.x >= uint(grid_size.x) || coord.y >= uint(grid_size.y)) return;

    for (uint z = 0; z < uint(pc.grid_params.z); z++) {
        uvec3 voxel  = uvec3(coord.xy, z);
        vec3  world_p = froxel_to_world(voxel, grid_size);

        float extinction = 0.0;
        float scattering = 0.0;
        vec3  color      = vec3(1.0);

        // Uniform fog
        if (pc.fog.w > 0.5) {
            extinction += pc.fog.x;
            scattering += pc.fog.x * pc.fog.y;
            color       = pc.fog_color.rgb;
        }

        // Exponential height fog
        if (pc.height_color.w > 0.5 && pc.height_fog.z > 1e-5) {
            float h   = max(world_p.y - pc.height_fog.x, 0.0);
            float hf  = exp(-h * pc.height_fog.y);
            float he  = pc.height_fog.z * hf;
            float hs  = he * pc.height_fog.w;
            float total = extinction + he + 1e-5;
            color      = (color * extinction + pc.height_color.rgb * he) / total;
            extinction += he;
            scattering += hs;
        }

        // Apply FBM noise to fog density for realistic variation
        if (pc.noise_params.w > 0.5) {
            vec3 noise_pos = world_p * pc.noise_params.x + vec3(0.0, 0.0, pc.noise_params.y);
            float n = fbm_noise(noise_pos,
                                int(pc.noise_params2.x),
                                pc.noise_params2.y,
                                pc.noise_params2.z);
            float noise_mod = mix(1.0, n * 2.0, pc.noise_params.z);
            extinction *= max(0.0, noise_mod);
            scattering *= max(0.0, noise_mod);
        }

        imageStore(vbuf_density, ivec3(voxel), vec4(color * scattering, extinction));
    }
}
