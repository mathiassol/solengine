#pragma once

#include "sol/render/mesh.h"
#include "sol/render/renderer.h"
#include "vk_context.h"
#include "vk_desc.h"
#include "vk_swapchain.h"
#include "vk_buffer.h"
#include "vk_image.h"

#include <glm/glm.hpp>
#include <vector>

struct GLFWwindow;

namespace sol {

class VulkanRenderer final : public Renderer {
public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    static VulkanRenderer* get();

    bool init(Window& window) override;
    bool init_win32(void* hwnd, void* hinstance, int w, int h);
    void shutdown() override;
    void begin_frame() override;
    void end_frame() override;
    void resize(int w, int h) override;
    void submit(const Mesh& mesh, const Material& mat, const glm::mat4& transform) override;

    void* alloc_mesh(const Vertex* verts, size_t vertex_count, const uint32_t* indices, size_t index_count);
    void  free_mesh(void* gpu);
    void* alloc_texture(const void* pixels, int width, int height);
    void  free_texture(void* gpu);

    bool init_imgui_backend(GLFWwindow* window);
    void shutdown_imgui_backend();

    vk::VkContext& context() { return m_ctx; }
    const vk::VkContext& context() const { return m_ctx; }

    void request_ibl_rebuild() { m_ibl_dirty = true; }
    bool ibl_ready()    const  { return m_ibl_ready; }

private:
    struct DrawItem;
    struct GpuMesh;
    struct GpuTexture;

    bool create_render_passes_();
    bool create_pipelines_();
    bool create_frame_resources_();
    bool create_shadow_resources_();
    bool create_swapchain_targets_();
    bool recreate_swapchain_();

    void destroy_swapchain_targets_();
    void destroy_shadow_resources_();
    void destroy_pipelines_();
    void destroy_render_passes_();
    void destroy_frame_resources_();

    bool update_frame_ubo_(uint32_t frame_idx, vk::FrameUBO& out_ubo, glm::vec3& out_shadow_dir);
    void record_shadow_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set);
    void record_vsm_shadow_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set);
    void record_gbuf_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set);
    void record_light_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set, uint32_t taa_read = 0, uint32_t taa_write = 1);
    void record_forward_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set);
    void record_bloom_passes_(VkCommandBuffer cmd, const vk::VulkanImage& hdr_src);
    void record_tonemap_pass_(VkCommandBuffer cmd, uint32_t image_idx, const vk::VulkanImage& hdr_src);
    void record_taa_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set, uint32_t taa_read, uint32_t taa_write);
    void record_imgui_pass_(VkCommandBuffer cmd, uint32_t image_idx);
    void record_ssao_passes_(VkCommandBuffer cmd, VkDescriptorSet frame_set);

    Window* m_window = nullptr;
    vk::VkContext m_ctx;
    vk::VkSwapchain m_swapchain;
    vk::DescriptorManager m_desc;

    vk::VulkanBuffer m_frame_ubos[vk::FRAMES_IN_FLIGHT];

    vk::VulkanImage m_shadow;
    VkImageView   m_shadow_layer_views[4] = {};
    VkSampler     m_shadow_raw_sampler = VK_NULL_HANDLE;  // non-comparison sampler for PCSS blocker search

    // VSM (Variance Shadow Maps)
    vk::VulkanImage  m_vsm_moment;
    VkImageView      m_vsm_layer_views[4]  = {};
    VkSampler        m_vsm_sampler         = VK_NULL_HANDLE;
    VkRenderPass     m_vsm_pass            = VK_NULL_HANDLE;
    VkFramebuffer    m_vsm_fbs[4]          = {};
    VkPipelineLayout m_vsm_layout          = VK_NULL_HANDLE;
    VkPipeline       m_vsm_pipe            = VK_NULL_HANDLE;
    VkPipeline       m_vsm_pipe_double     = VK_NULL_HANDLE;
    bool             m_vsm_mode            = false;
    vk::VulkanImage m_hdr_color;
    vk::VulkanImage m_hdr_depth;
    vk::VulkanImage m_gbuf0;   // albedo + ao
    vk::VulkanImage m_gbuf1;   // world normal + geom flag
    vk::VulkanImage m_gbuf2;   // metallic + roughness + lit flag
    vk::VulkanImage m_gbuf3;   // emissive
    VkSampler       m_hdr_depth_sampler = VK_NULL_HANDLE;  // non-compare sampler for depth read in deferred
    vk::VulkanImage m_bloom_a;
    vk::VulkanImage m_bloom_b;
    vk::VulkanImage m_fallback_white;
    vk::VulkanImage m_fallback_normal;
    vk::VulkanImage m_fallback_mr;
    vk::VulkanImage m_fallback_black;  // for emissive fallback (no glow)

    vk::VulkanImage  m_ssao;
    vk::VulkanImage  m_ssao_blur;
    vk::VulkanImage  m_ssao_noise;

    vk::VulkanImage m_shadow_accum[2];           // ping-pong shadow accumulation (R16_SFLOAT)
    VkSampler       m_shadow_accum_sampler = VK_NULL_HANDLE;
    uint32_t        m_taa_idx             = 0;   // incremented each frame for shadow ping-pong
    uint32_t        m_color_taa_idx       = 0;   // incremented each frame for TAA color ping-pong + jitter
    glm::mat4       m_prev_view_proj      = glm::mat4(0.0f); // last frame's proj*view; 0 on first frame

    // TAA (Temporal Anti-Aliasing) color ping-pong
    vk::VulkanImage  m_taa_color[2];
    VkRenderPass     m_taa_pass      = VK_NULL_HANDLE;
    VkFramebuffer    m_taa_fbs[2]    = {};
    VkPipelineLayout m_taa_layout    = VK_NULL_HANDLE;
    VkPipeline       m_taa_pipe      = VK_NULL_HANDLE;

    VkRenderPass     m_ssao_pass      = VK_NULL_HANDLE;
    VkFramebuffer    m_ssao_fb        = VK_NULL_HANDLE;
    VkFramebuffer    m_ssao_blur_fb   = VK_NULL_HANDLE;
    VkPipelineLayout m_ssao_layout     = VK_NULL_HANDLE;
    VkPipelineLayout m_ssao_blur_layout = VK_NULL_HANDLE;
    VkPipeline       m_ssao_pipe       = VK_NULL_HANDLE;
    VkPipeline       m_ssao_blur_pipe  = VK_NULL_HANDLE;

    VkRenderPass m_shadow_pass  = VK_NULL_HANDLE;
    VkRenderPass m_gbuf_pass    = VK_NULL_HANDLE;
    VkRenderPass m_light_pass   = VK_NULL_HANDLE;
    VkRenderPass m_forward_pass = VK_NULL_HANDLE;
    VkRenderPass m_bloom_pass   = VK_NULL_HANDLE;
    VkRenderPass m_tonemap_pass = VK_NULL_HANDLE;
    VkRenderPass m_imgui_pass   = VK_NULL_HANDLE;

    VkFramebuffer m_shadow_fbs[4] = {};
    VkFramebuffer m_gbuf_fb    = VK_NULL_HANDLE;
    VkFramebuffer m_light_fbs[2] = {};    // ping-pong for shadow accumulation
    VkFramebuffer m_forward_fb = VK_NULL_HANDLE;
    VkFramebuffer m_bloom_a_fb = VK_NULL_HANDLE;
    VkFramebuffer m_bloom_b_fb = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_tonemap_fbs;
    std::vector<VkFramebuffer> m_imgui_fbs;

    VkPipelineLayout m_shadow_layout   = VK_NULL_HANDLE;
    VkPipelineLayout m_pbr_layout      = VK_NULL_HANDLE;   // reused for gbuf + forward pipes
    VkPipelineLayout m_deferred_layout = VK_NULL_HANDLE;   // for deferred_light pipe
    VkPipelineLayout m_bright_layout   = VK_NULL_HANDLE;
    VkPipelineLayout m_blur_layout     = VK_NULL_HANDLE;
    VkPipelineLayout m_tonemap_layout  = VK_NULL_HANDLE;

    VkPipeline m_shadow_pipe         = VK_NULL_HANDLE;
    VkPipeline m_shadow_pipe_double  = VK_NULL_HANDLE;
    VkPipeline m_gbuf_pipe           = VK_NULL_HANDLE;     // opaque/mask, back-cull, 4 MRT
    VkPipeline m_gbuf_pipe_double    = VK_NULL_HANDLE;     // opaque/mask, no cull
    VkPipeline m_deferred_pipe       = VK_NULL_HANDLE;     // fullscreen deferred lighting
    VkPipeline m_forward_pipe        = VK_NULL_HANDLE;     // blend, back-cull, depth read-only
    VkPipeline m_forward_pipe_double = VK_NULL_HANDLE;     // blend, no cull
    VkPipeline m_bright_pipe         = VK_NULL_HANDLE;
    VkPipeline m_blur_pipe           = VK_NULL_HANDLE;
    VkPipeline m_tonemap_pipe        = VK_NULL_HANDLE;

    std::vector<DrawItem> m_draws;
    std::vector<Light> m_frame_lights;
    bool m_has_shadow = false;

    uint32_t m_frame_index = 0;
    bool m_initialized = false;
    bool m_imgui_initialized = false;
    bool m_editor_mode = false;

    // IBL resources
    vk::VulkanImage m_ibl_sky;            // 128x128 RGBA16F cubemap (sky capture)
    vk::VulkanImage m_ibl_irradiance;     // 32x32 RGBA16F cubemap (diffuse)
    vk::VulkanImage m_ibl_prefilter;      // 128x128 RGBA16F cubemap, 5 mips (specular)
    vk::VulkanImage m_ibl_brdf_lut;       // 256x256 R16G16F (BRDF split-sum)
    vk::VulkanImage m_ibl_fallback_cube;  // 1x1 black cubemap for when IBL is not ready
    VkSampler       m_ibl_cube_sampler      = VK_NULL_HANDLE; // for sky + irradiance
    VkSampler       m_ibl_prefilter_sampler = VK_NULL_HANDLE; // trilinear + mips for prefilter
    VkSampler       m_ibl_lut_sampler       = VK_NULL_HANDLE; // for BRDF LUT

    VkImageView m_ibl_sky_face_views[6]      = {};
    VkImageView m_ibl_irr_face_views[6]      = {};
    VkImageView m_ibl_pref_face_views[6][5]  = {};

    VkRenderPass     m_ibl_sky_rp    = VK_NULL_HANDLE;
    VkRenderPass     m_ibl_irr_rp    = VK_NULL_HANDLE;
    VkRenderPass     m_ibl_pref_rp   = VK_NULL_HANDLE;
    VkRenderPass     m_ibl_lut_rp    = VK_NULL_HANDLE;
    VkPipelineLayout m_ibl_sky_layout  = VK_NULL_HANDLE;
    VkPipelineLayout m_ibl_irr_layout  = VK_NULL_HANDLE;
    VkPipelineLayout m_ibl_pref_layout = VK_NULL_HANDLE;
    VkPipelineLayout m_ibl_lut_layout  = VK_NULL_HANDLE;
    VkPipeline       m_ibl_sky_pipe  = VK_NULL_HANDLE;
    VkPipeline       m_ibl_irr_pipe  = VK_NULL_HANDLE;
    VkPipeline       m_ibl_pref_pipe = VK_NULL_HANDLE;
    VkPipeline       m_ibl_lut_pipe  = VK_NULL_HANDLE;
    bool             m_ibl_ready     = false;
    bool             m_ibl_dirty     = true;

    bool create_ibl_resources_();
    bool create_ibl_pipelines_();
    void rebuild_ibl_();
    void destroy_ibl_();

}; // class VulkanRenderer

} // namespace sol
