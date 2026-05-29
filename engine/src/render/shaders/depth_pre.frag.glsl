#version 450
// Depth pre-pass fragment shader.
// Outputs world normals (for SSAO) and per-pixel roughness, and discards alpha-masked fragments.

layout(set = 1, binding = 0) uniform sampler2D s_albedo;
layout(set = 1, binding = 2) uniform sampler2D s_mr_map;

layout(push_constant) uniform PbrPush {
    mat4 model;
    vec4 base_color;
    // pbr.x=metallic  pbr.y=roughness  pbr.z=alpha_cutoff  pbr.w=alpha_mode(0=opaque,1=mask)
    vec4 pbr;
    vec4 emissive;
    // flags.x=lit  flags.y=hasAlbedo  flags.z=hasNormal  flags.w=hasMR
    vec4 flags;
} pc;

layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_color;

layout(location = 0) out vec4  out_normal;    // world normal + w=1.0 geometry flag
layout(location = 1) out float out_roughness; // per-pixel roughness for SSR

void main() {
    // Alpha mask discard
    int alpha_mode = int(round(pc.pbr.w));
    if (alpha_mode == 1) {
        float alpha = pc.base_color.a * v_color.a;
        if (pc.flags.y > 0.5) alpha *= texture(s_albedo, v_uv).a;
        if (alpha < pc.pbr.z) discard;
    }

    out_normal = vec4(normalize(v_world_normal), 1.0);

    // Roughness: material roughness * texture roughness (green channel of MR map)
    float tex_roughness = (pc.flags.w > 0.5) ? texture(s_mr_map, v_uv).g : 1.0;
    out_roughness = clamp(pc.pbr.y * tex_roughness, 0.04, 1.0);
}
