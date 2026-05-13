#include "vk_desc.h"
#include "vk_context.h"

namespace sol::vk {

static VkDescriptorSetLayout make_dsl(VkDevice device,
                                       std::initializer_list<VkDescriptorSetLayoutBinding> bindings)
{
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = (uint32_t)bindings.size();
    ci.pBindings    = bindings.begin();
    VkDescriptorSetLayout dsl;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &ci, nullptr, &dsl));
    return dsl;
}

bool DescriptorManager::init(VkContext& ctx) {
    VkDevice dev = ctx.device();

    // Set 0: UBO binding 0, shadow sampler binding 1, raw shadow sampler binding 2, VSM sampler binding 3
    m_frame_layout = make_dsl(dev, {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    // Set 1: albedo, normal, MR, emissive samplers
    m_mat_layout = make_dsl(dev, {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    // Post: HDR + bloom
    m_post_layout = make_dsl(dev, {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    // Single sampler (bloom passes)
    m_single_samp_layout = make_dsl(dev, {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    // Deferred input: gbuf0-3 + depth + ssao + shadow_accum + ibl_irr + ibl_pref + ibl_lut
    m_deferred_layout = make_dsl(dev, {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    // SSAO input: gbuf1 normals + depth + noise (3 combined image samplers)
    m_ssao_input_layout = make_dsl(dev, {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    // TAA input: current HDR + history + depth (3 combined image samplers)
    m_taa_input_layout = make_dsl(dev, {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    });

    // Per-frame descriptor pools — one per frame slot, reset when that slot's fence signals.
    std::array<VkDescriptorPoolSize, 2> pool_sizes = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1024 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192 }
    }};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets       = 8192;
    pi.poolSizeCount = (uint32_t)pool_sizes.size();
    pi.pPoolSizes    = pool_sizes.data();
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_pools[0]));
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_pools[1]));
    return true;
}

void DescriptorManager::shutdown(VkDevice device) {
    if (m_frame_layout)       { vkDestroyDescriptorSetLayout(device, m_frame_layout,       nullptr); m_frame_layout = VK_NULL_HANDLE; }
    if (m_mat_layout)         { vkDestroyDescriptorSetLayout(device, m_mat_layout,         nullptr); m_mat_layout = VK_NULL_HANDLE; }
    if (m_post_layout)        { vkDestroyDescriptorSetLayout(device, m_post_layout,        nullptr); m_post_layout = VK_NULL_HANDLE; }
    if (m_single_samp_layout) { vkDestroyDescriptorSetLayout(device, m_single_samp_layout, nullptr); m_single_samp_layout = VK_NULL_HANDLE; }
    if (m_deferred_layout)    { vkDestroyDescriptorSetLayout(device, m_deferred_layout,    nullptr); m_deferred_layout = VK_NULL_HANDLE; }
    if (m_ssao_input_layout)  { vkDestroyDescriptorSetLayout(device, m_ssao_input_layout,  nullptr); m_ssao_input_layout = VK_NULL_HANDLE; }
    if (m_taa_input_layout)   { vkDestroyDescriptorSetLayout(device, m_taa_input_layout,   nullptr); m_taa_input_layout = VK_NULL_HANDLE; }
    for (auto& pool : m_pools) {
        if (pool) { vkDestroyDescriptorPool(device, pool, nullptr); pool = VK_NULL_HANDLE; }
    }
}

static VkDescriptorSet alloc_set(VkDevice dev, VkDescriptorPool pool, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;
    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, &set));
    return set;
}

VkDescriptorSet DescriptorManager::alloc_frame_set(VkDevice device,
                                                    VkBuffer ubo, VkDeviceSize ubo_size,
                                                    VkImageView shadow_view,     VkSampler shadow_sampler,
                                                    VkImageView shadow_raw_view, VkSampler shadow_raw_sampler,
                                                    VkImageView vsm_view,        VkSampler vsm_sampler)
{
    auto set = alloc_set(device, m_pools[m_active_pool_idx], m_frame_layout);
    VkDescriptorBufferInfo bi{ ubo, 0, ubo_size };
    VkDescriptorImageInfo  ii0{ shadow_sampler,     shadow_view,     VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo  ii1{ shadow_raw_sampler, shadow_raw_view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo  ii2{ vsm_sampler,        vsm_view,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    std::array<VkWriteDescriptorSet, 4> writes = {{
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bi, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii0, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii1, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii2, nullptr, nullptr }
    }};
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    return set;
}

VkDescriptorSet DescriptorManager::alloc_material_set(VkDevice device,
                                                       VkImageView albedo, VkSampler albedo_samp,
                                                       VkImageView normal, VkSampler normal_samp,
                                                       VkImageView mr,     VkSampler mr_samp,
                                                       VkImageView emissive, VkSampler emissive_samp)
{
    auto set = alloc_set(device, m_pools[m_active_pool_idx], m_mat_layout);
    std::array<VkDescriptorImageInfo, 4> imgs = {{
        { albedo_samp,   albedo,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { normal_samp,   normal,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { mr_samp,       mr,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { emissive_samp, emissive, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
    }};
    std::array<VkWriteDescriptorSet, 4> writes = {{
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[3], nullptr, nullptr }
    }};
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    return set;
}

VkDescriptorSet DescriptorManager::alloc_post_set(VkDevice device,
                                                   VkImageView hdr,   VkSampler hdr_samp,
                                                   VkImageView bloom, VkSampler bloom_samp)
{
    auto set = alloc_set(device, m_pools[m_active_pool_idx], m_post_layout);
    std::array<VkDescriptorImageInfo, 2> imgs = {{
        { hdr_samp,   hdr,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { bloom_samp, bloom, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
    }};
    std::array<VkWriteDescriptorSet, 2> writes = {{
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr }
    }};
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    return set;
}

VkDescriptorSet DescriptorManager::alloc_single_sampler_set(VkDevice device,
                                                              VkImageView view, VkSampler samp)
{
    auto set = alloc_set(device, m_pools[m_active_pool_idx], m_single_samp_layout);
    VkDescriptorImageInfo ii{ samp, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, nullptr, nullptr };
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    return set;
}

void DescriptorManager::reset_pool(VkDevice device, uint32_t frame_idx) {
    m_active_pool_idx = frame_idx;
    vkResetDescriptorPool(device, m_pools[m_active_pool_idx], 0);
}

VkDescriptorSet DescriptorManager::alloc_deferred_input_set(VkDevice device,
                                                              VkImageView gbuf0, VkSampler s0,
                                                              VkImageView gbuf1, VkSampler s1,
                                                              VkImageView gbuf2, VkSampler s2,
                                                              VkImageView gbuf3, VkSampler s3,
                                                              VkImageView depth,        VkSampler s_depth,
                                                              VkImageView ssao,         VkSampler s_ssao,
                                                              VkImageView shadow_accum, VkSampler s_accum,
                                                              VkImageView ibl_irr,      VkSampler s_irr,
                                                              VkImageView ibl_pref,     VkSampler s_pref,
                                                              VkImageView ibl_lut,      VkSampler s_lut)
{
    auto set = alloc_set(device, m_pools[m_active_pool_idx], m_deferred_layout);
    std::array<VkDescriptorImageInfo, 10> imgs = {{
        { s0,      gbuf0,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s1,      gbuf1,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s2,      gbuf2,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s3,      gbuf3,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s_depth, depth,        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
        { s_ssao,  ssao,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s_accum, shadow_accum, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s_irr,   ibl_irr,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s_pref,  ibl_pref,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s_lut,   ibl_lut,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
    }};
    std::array<VkWriteDescriptorSet, 10> writes = {{
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[3], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[4], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 5, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[5], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 6, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[6], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 7, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[7], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 8, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[8], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 9, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[9], nullptr, nullptr }
    }};
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    return set;
}

VkDescriptorSet DescriptorManager::alloc_ssao_input_set(VkDevice device,
                                              VkImageView normals, VkSampler s_normals,
                                              VkImageView depth,   VkSampler s_depth,
                                              VkImageView noise,   VkSampler s_noise)
{
    auto set = alloc_set(device, m_pools[m_active_pool_idx], m_ssao_input_layout);
    std::array<VkDescriptorImageInfo, 3> imgs = {{
        { s_normals, normals, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s_depth,   depth,   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
        { s_noise,   noise,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
    }};
    std::array<VkWriteDescriptorSet, 3> writes = {{
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr }
    }};
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    return set;
}

VkDescriptorSet DescriptorManager::alloc_taa_input_set(VkDevice device,
                                                         VkImageView hdr,     VkSampler s_hdr,
                                                         VkImageView history, VkSampler s_hist,
                                                         VkImageView depth,   VkSampler s_depth)
{
    auto set = alloc_set(device, m_pools[m_active_pool_idx], m_taa_input_layout);
    std::array<VkDescriptorImageInfo, 3> imgs = {{
        { s_hdr,   hdr,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s_hist,  history, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { s_depth, depth,   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }
    }};
    std::array<VkWriteDescriptorSet, 3> writes = {{
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr }
    }};
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    return set;
}

} // namespace sol::vk
