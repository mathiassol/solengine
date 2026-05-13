#pragma once
#include "vk_common.h"
#include "vk_image.h"
#include "vk_buffer.h"
#include <array>

namespace sol::vk {
class VkContext;

// ---- per-frame uniform data uploaded to GPU each frame ---------------------
// IMPORTANT: layout must match the GLSL FrameUBO declaration exactly (SoA).
// Shader sees: u_light_data0[8], u_light_data1[8], u_light_data2[8], u_light_data3[8]
// — four flat arrays of 8 vec4s, NOT interleaved per-light structs.
struct alignas(16) FrameUBO {
    float view[16];
    float proj[16];
    float lightMtx[4][16];   // 4 cascade matrices
    float camPos[4];
    float ambient[4];
    float lightCount[4];    // x = count

    // SoA: all d0 for 8 lights, then all d1, etc.
    float lightData0[8][4]; // xyz=dir|pos,  w=type (0=dir, 1=point, 2=spot)
    float lightData1[8][4]; // rgb=color,    a=intensity
    float lightData2[8][4]; // xyz=spot_dir, w=range
    float lightData3[8][4]; // x=inner_cos,  y=outer_cos

    float shadowConfig[4];  // x=active, y=texel_size
    float cascadeSplits[4]; // view-space split distances for 4 cascades
    float invViewProj[16];  // inverse(proj * view)
    float shadowParams[4];  // x=bias_const, y=bias_slope, z=pcf_radius(texels), w=pcss_light_size
    float shadowExtra[4];   // x=vsm_light_bleed, y=vsm_min_variance, z=cs_distance, w=cs_thickness
    float prevViewProj[16]; // u_prev_view_proj: last frame's (proj * view) for reprojection
    float temporalParams[4]; // x=alpha, y=enabled(1.0f=yes), z=max_dist, w=unused
    float iblParams[4];      // x=enabled(1.0f=yes), y=intensity, z=diffuse_scale, w=specular_scale
};

// ---- descriptor management ------------------------------------------------
// Set 0: per-frame UBO + shadow sampler
// Set 1: per-material (albedo, normal, MR)

class DescriptorManager {
public:
    bool init(VkContext& ctx);
    void shutdown(VkDevice device);

    // Set 0 layout
    VkDescriptorSetLayout frame_layout()    const { return m_frame_layout; }
    // Set 1 layout
    VkDescriptorSetLayout material_layout() const { return m_mat_layout; }
    // Fullscreen/post layout (no material, just 2 samplers)
    VkDescriptorSetLayout post_layout()     const { return m_post_layout; }
    // Deferred lighting input layout (gbuf0-3 + depth + ssao)
    VkDescriptorSetLayout deferred_input_layout() const { return m_deferred_layout; }

    // Allocate + write set 0 for a frame (UBO + shadow map + raw shadow map + VSM)
    VkDescriptorSet alloc_frame_set(VkDevice device,
                                    VkBuffer ubo, VkDeviceSize ubo_size,
                                    VkImageView shadow_view,     VkSampler shadow_sampler,
                                    VkImageView shadow_raw_view, VkSampler shadow_raw_sampler,
                                    VkImageView vsm_view,        VkSampler vsm_sampler);

    // Allocate + write set 1 for a material (albedo, normal, MR, emissive)
    VkDescriptorSet alloc_material_set(VkDevice device,
                                       VkImageView albedo, VkSampler albedo_samp,
                                       VkImageView normal, VkSampler normal_samp,
                                       VkImageView mr,     VkSampler mr_samp,
                                       VkImageView emissive, VkSampler emissive_samp);

    // Allocate + write set for fullscreen post (1 HDR + 1 bloom)
    VkDescriptorSet alloc_post_set(VkDevice device,
                                   VkImageView hdr,   VkSampler hdr_samp,
                                   VkImageView bloom, VkSampler bloom_samp);

    // Allocate + write set for a single-sampler pass (bloom bright extract, blur)
    VkDescriptorSet alloc_single_sampler_set(VkDevice device,
                                              VkImageView view, VkSampler samp);

    // Allocate + write deferred input set (gbuf0-3 + depth + ssao + shadow_accum + ibl_irr + ibl_pref + ibl_lut)
    VkDescriptorSet alloc_deferred_input_set(VkDevice device,
                                              VkImageView gbuf0, VkSampler s0,
                                              VkImageView gbuf1, VkSampler s1,
                                              VkImageView gbuf2, VkSampler s2,
                                              VkImageView gbuf3, VkSampler s3,
                                              VkImageView depth,        VkSampler s_depth,
                                              VkImageView ssao,         VkSampler s_ssao,
                                              VkImageView shadow_accum, VkSampler s_accum,
                                              VkImageView ibl_irr,      VkSampler s_irr,
                                              VkImageView ibl_pref,     VkSampler s_pref,
                                              VkImageView ibl_lut,      VkSampler s_lut);

    VkDescriptorSetLayout single_sampler_layout() const { return m_single_samp_layout; }

    // SSAO input layout (gbuf1 normals, depth, noise)
    VkDescriptorSetLayout ssao_input_layout() const { return m_ssao_input_layout; }

    // TAA input layout (hdr current, history, depth)
    VkDescriptorSetLayout taa_input_layout() const { return m_taa_input_layout; }

    // Allocate + write SSAO input set (normals + depth + noise)
    VkDescriptorSet alloc_ssao_input_set(VkDevice device,
                                          VkImageView normals, VkSampler s_normals,
                                          VkImageView depth,   VkSampler s_depth,
                                          VkImageView noise,   VkSampler s_noise);

    // Allocate + write TAA input set (hdr current + history + depth)
    VkDescriptorSet alloc_taa_input_set(VkDevice device,
                                         VkImageView hdr,     VkSampler s_hdr,
                                         VkImageView history, VkSampler s_hist,
                                         VkImageView depth,   VkSampler s_depth);

    void reset_pool(VkDevice device, uint32_t frame_idx); // free all transient sets for the given frame slot

private:
    VkDescriptorSetLayout m_frame_layout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mat_layout        = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_post_layout       = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_single_samp_layout= VK_NULL_HANDLE;
    VkDescriptorSetLayout m_deferred_layout   = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_ssao_input_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_taa_input_layout  = VK_NULL_HANDLE;
    std::array<VkDescriptorPool, 2> m_pools   = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    uint32_t              m_active_pool_idx   = 0u;
};

} // namespace sol::vk
