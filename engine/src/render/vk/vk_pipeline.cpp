#include "vk_pipeline.h"

#include <vector>

namespace sol::vk {

static VkShaderModule create_shader_module(VkDevice device, const uint32_t* code, uint32_t bytes) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode    = code;
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}

VkPipeline build_pipeline(VkDevice device, const PipelineDesc& d) {
    auto vert_mod = create_shader_module(device, d.vert_code, d.vert_size);
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    if (d.frag_code) frag_mod = create_shader_module(device, d.frag_code, d.frag_size);

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    {
        VkPipelineShaderStageCreateInfo s{};
        s.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        s.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        s.module = vert_mod;
        s.pName  = "main";
        stages.push_back(s);
    }
    if (frag_mod) {
        VkPipelineShaderStageCreateInfo s{};
        s.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        s.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        s.module = frag_mod;
        s.pName  = "main";
        stages.push_back(s);
    }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = (uint32_t)d.vertex_bindings.size();
    vi.pVertexBindingDescriptions      = d.vertex_bindings.empty() ? nullptr : d.vertex_bindings.data();
    vi.vertexAttributeDescriptionCount = (uint32_t)d.vertex_attribs.size();
    vi.pVertexAttributeDescriptions    = d.vertex_attribs.empty() ? nullptr : d.vertex_attribs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = d.topology;

    // Dynamic viewport/scissor
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode             = VK_POLYGON_MODE_FILL;
    rs.cullMode                = d.cull_mode;
    rs.frontFace               = d.front_face;
    rs.depthClampEnable        = d.depth_clamp ? VK_TRUE : VK_FALSE;
    rs.depthBiasEnable         = (d.depth_bias_const != 0.0f || d.depth_bias_slope != 0.0f);
    rs.depthBiasConstantFactor = d.depth_bias_const;
    rs.depthBiasSlopeFactor    = d.depth_bias_slope;
    rs.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = d.sample_count;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = d.depth_test  ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = d.depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp   = d.depth_op;

    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (d.blend_enable) {
        ba.blendEnable         = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.colorBlendOp        = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        ba.alphaBlendOp        = VK_BLEND_OP_ADD;
    }

    uint32_t n_color = d.frag_code
        ? (d.color_attachment_count ? d.color_attachment_count : 1)
        : 0;
    std::vector<VkPipelineColorBlendAttachmentState> blend_states(n_color, ba);

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount   = n_color;
    cb.pAttachments      = blend_states.empty() ? nullptr : blend_states.data();

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn_ci{};
    dyn_ci.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn_ci.dynamicStateCount = 2;
    dyn_ci.pDynamicStates    = dyn;

    VkGraphicsPipelineCreateInfo gci{};
    gci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gci.stageCount          = (uint32_t)stages.size();
    gci.pStages             = stages.data();
    gci.pVertexInputState   = &vi;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState      = &vp;
    gci.pRasterizationState = &rs;
    gci.pMultisampleState   = &ms;
    gci.pDepthStencilState  = &ds;
    gci.pColorBlendState    = &cb;
    gci.pDynamicState       = &dyn_ci;
    gci.layout              = d.layout;
    gci.renderPass          = d.render_pass;
    gci.subpass             = d.subpass;

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gci, nullptr, &pipeline));

    vkDestroyShaderModule(device, vert_mod, nullptr);
    if (frag_mod) vkDestroyShaderModule(device, frag_mod, nullptr);
    return pipeline;
}

VkPipelineLayout build_pipeline_layout(VkDevice device, const LayoutDesc& d) {
    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = (uint32_t)d.set_layouts.size();
    ci.pSetLayouts            = d.set_layouts.empty() ? nullptr : d.set_layouts.data();
    ci.pushConstantRangeCount = (uint32_t)d.push_ranges.size();
    ci.pPushConstantRanges    = d.push_ranges.empty() ? nullptr : d.push_ranges.data();
    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &ci, nullptr, &layout));
    return layout;
}

} // namespace sol::vk
