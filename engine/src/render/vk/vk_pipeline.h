#pragma once
#include "vk_common.h"
#include <vector>
#include <cstdint>

namespace sol::vk {
class VkContext;

// Builds a VkPipeline from a SPIR-V vert + frag pair.
struct PipelineDesc {
    // Shader SPIR-V blobs
    const uint32_t* vert_code   = nullptr;
    uint32_t        vert_size   = 0;  // bytes
    const uint32_t* frag_code   = nullptr;
    uint32_t        frag_size   = 0;

    // Fixed-function
    VkPrimitiveTopology   topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool                  depth_test     = true;
    bool                  depth_write    = true;
    VkCompareOp           depth_op       = VK_COMPARE_OP_LESS;
    bool                  blend_enable   = false;
    VkCullModeFlags       cull_mode      = VK_CULL_MODE_BACK_BIT;
    VkFrontFace           front_face     = VK_FRONT_FACE_CLOCKWISE;
    bool                  depth_clamp    = false;
    float                 depth_bias_const = 0.0f;
    float                 depth_bias_slope = 0.0f;
    // Number of color blend attachment states. 0 = auto (0 if depth-only, 1 otherwise).
    uint32_t              color_attachment_count = 0;
    // MSAA sample count for the pipeline (default 1x).
    VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT;

    // Vertex input
    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attribs;

    // Layouts
    VkPipelineLayout layout      = VK_NULL_HANDLE;
    VkRenderPass     render_pass = VK_NULL_HANDLE;
    uint32_t         subpass     = 0;
};

VkPipeline build_pipeline(VkDevice device, const PipelineDesc& desc);

// Simple pipeline layout builder
struct LayoutDesc {
    std::vector<VkDescriptorSetLayout> set_layouts;
    std::vector<VkPushConstantRange>   push_ranges;
};
VkPipelineLayout build_pipeline_layout(VkDevice device, const LayoutDesc& desc);

} // namespace sol::vk
