#include "vk_renderer.h"

#include "platform/window.h"
#include "sol/log.h"
#include "sol/render/material.h"
#include "sol/render/mesh.h"
#include "sol/render/texture.h"

using AlphaMode = sol::AlphaMode;
#include "vk_pipeline.h"

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "shaders/fullscreen.vert.glsl.h"
#include "shaders/shadow.vert.glsl.h"
#include "shaders/shadow.frag.glsl.h"
#include "shaders/shadow_vsm.frag.glsl.h"
#include "shaders/pbr.vert.glsl.h"
#include "shaders/pbr.frag.glsl.h"
#include "shaders/gbuf.frag.glsl.h"
#include "shaders/deferred_light.frag.glsl.h"
#include "shaders/bloom_bright.frag.glsl.h"
#include "shaders/bloom_blur.frag.glsl.h"
#include "shaders/tonemap.frag.glsl.h"
#include "shaders/ssao.frag.glsl.h"
#include "shaders/ssao_blur.frag.glsl.h"
#include "shaders/ibl_sky_capture.frag.glsl.h"
#include "shaders/ibl_irradiance.frag.glsl.h"
#include "shaders/ibl_prefilter.frag.glsl.h"
#include "shaders/ibl_brdf_lut.frag.glsl.h"
#include "shaders/taa.frag.glsl.h"

namespace sol {
namespace {

constexpr uint32_t SHADOW_SIZE = 4096;
constexpr VkFormat HDR_FORMAT   = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat GBUF0_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;        // albedo + ao
constexpr VkFormat GBUF1_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;   // world normal + geom flag
constexpr VkFormat GBUF2_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;        // metallic + roughness + lit_flag
constexpr VkFormat GBUF3_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;   // emissive
constexpr VkFormat SSAO_FORMAT  = VK_FORMAT_R8_UNORM;
constexpr VkFormat SHADOW_ACCUM_FORMAT = VK_FORMAT_R16_SFLOAT;

struct alignas(16) ShadowPush {
    glm::mat4 model {1.0f};
    glm::vec4 params {0.0f};  // x = float(cascade_idx)
};
static_assert(sizeof(ShadowPush) == 80);

struct alignas(16) PbrPush {
    glm::mat4 model {1.0f};
    glm::vec4 base_color {1.0f};
    glm::vec4 pbr {0.0f, 0.5f, 0.0f, 0.0f};
    glm::vec4 emissive {0.0f};
    glm::vec4 flags {1.0f, 0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(PbrPush) == 128);

struct alignas(16) SkyPush {
    glm::vec4 sun_dir {0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 zenith {0.08f, 0.15f, 0.4f, 0.0f};
    glm::vec4 horizon {0.5f, 0.6f, 0.7f, 0.0f};
    glm::vec4 sun_color {3.0f, 2.5f, 2.0f, 0.9997f};
};

struct alignas(16) Vec4Push {
    glm::vec4 value {0.0f};
};

struct alignas(16) SSAOPush {
    glm::vec4 params;  // x=radius, y=bias, z=power, w=strength
    glm::vec4 screen;  // z=noise_tile_x, w=noise_tile_y
};
static_assert(sizeof(SSAOPush) == 32);

struct alignas(16) IBLSkyPush {
    glm::vec4 sun_dir;
    glm::vec4 zenith;
    glm::vec4 horizon;
    glm::vec4 sun_color;
    int32_t   face;
    float     pad0, pad1, pad2;
};
static_assert(sizeof(IBLSkyPush) == 80);

struct alignas(4) IBLCubePush {
    int32_t face;
    float   roughness;
    int32_t pad0, pad1;
};
static_assert(sizeof(IBLCubePush) == 16);

struct alignas(16) TaaPush {
    glm::vec4 params;     // x=blend_alpha, y=enabled(1.0), z=variance_gamma, w=sharpening
    glm::vec4 resolution; // x=width, y=height, z=1/width, w=1/height
};
static_assert(sizeof(TaaPush) == 32);

inline void store_mat4(float dst[16], const glm::mat4& m) {
    std::memcpy(dst, glm::value_ptr(m), sizeof(float) * 16);
}

inline void store_vec4(float dst[4], const glm::vec4& v) {
    std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 4);
}

inline VkViewport make_viewport(float w, float h) {
    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = w;
    vp.height = h;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    return vp;
}

inline VkRect2D make_scissor(uint32_t w, uint32_t h) {
    VkRect2D sc{};
    sc.extent = {w, h};
    return sc;
}

inline glm::vec4 unpack_rgba(uint32_t rgba) {
    return {
        float((rgba >> 24) & 0xff) / 255.0f,
        float((rgba >> 16) & 0xff) / 255.0f,
        float((rgba >> 8) & 0xff) / 255.0f,
        float(rgba & 0xff) / 255.0f
    };
}

inline PFN_vkVoidFunction imgui_loader(const char* name, void* user_data) {
    return vkGetInstanceProcAddr(reinterpret_cast<VkInstance>(user_data), name);
}

static void compute_csm_matrices(
    const glm::mat4& cam_view,
    float tan_half_fov, float aspect,
    const glm::vec3& light_dir,
    float lambda, float far_plane,
    glm::mat4 out_matrices[4],
    float out_splits[4])
{
    constexpr float near_plane = 0.1f;

    // PSSM split distances (view-space, positive = in front of camera)
    for (int i = 0; i < 4; ++i) {
        float p     = float(i + 1) / 4.0f;
        float log_d = near_plane * std::pow(far_plane / near_plane, p);
        float uni_d = near_plane + (far_plane - near_plane) * p;
        out_splits[i] = lambda * log_d + (1.0f - lambda) * uni_d;
    }

    glm::mat4 inv_cam_view = glm::inverse(cam_view);

    for (int c = 0; c < 4; ++c) {
        float zn = (c == 0) ? near_plane : out_splits[c - 1];
        float zf = out_splits[c];

        float h_near = tan_half_fov * zn;
        float w_near = h_near * aspect;
        float h_far  = tan_half_fov * zf;
        float w_far  = h_far * aspect;

        // 8 frustum corners in world space (view-space: +X right, +Y up, -Z forward)
        std::array<glm::vec3, 8> ws;
        for (int i = 0; i < 4; ++i) {
            float sx = (i & 1) ? 1.0f : -1.0f;
            float sy = (i & 2) ? 1.0f : -1.0f;
            ws[i]   = glm::vec3(inv_cam_view * glm::vec4(sx * w_near, sy * h_near, -zn, 1.0f));
            ws[i+4] = glm::vec3(inv_cam_view * glm::vec4(sx * w_far,  sy * h_far,  -zf, 1.0f));
        }

        // Frustum center
        glm::vec3 center(0.0f);
        for (auto& wc : ws) center += wc;
        center /= 8.0f;

        // Light view matrix
        glm::vec3 ld = glm::normalize(light_dir);
        glm::vec3 up = std::abs(ld.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::mat4 lv = glm::lookAt(center - ld * 300.0f, center, up);

        // Tight AABB in light-view space
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (auto& wc : ws) {
            glm::vec3 ls = glm::vec3(lv * glm::vec4(wc, 1.0f));
            mn = glm::min(mn, ls);
            mx = glm::max(mx, ls);
        }

        // Pad slightly and build ortho projection
        float pad   = std::max(1.0f, (mx.x - mn.x) * 0.02f);
        // In right-handed light view space, objects in front have negative z.
        // orthoRH_ZO(l,r,b,t,zNear,zFar): zNear/zFar are positive clip distances.
        float z_n   = std::max(0.1f, -mx.z - 50.0f);
        float z_f   = -mn.z + 50.0f;
        glm::mat4 lp = glm::orthoRH_ZO(
            mn.x - pad, mx.x + pad,
            mn.y - pad, mx.y + pad,
            z_n, z_f);

        out_matrices[c] = lp * lv;
    }
}

VkFramebuffer make_framebuffer(VkDevice device, VkRenderPass pass,
                               std::initializer_list<VkImageView> views,
                               uint32_t w, uint32_t h) {
    std::vector<VkImageView> attachments(views);
    VkFramebufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass = pass;
    ci.attachmentCount = static_cast<uint32_t>(attachments.size());
    ci.pAttachments = attachments.data();
    ci.width = w;
    ci.height = h;
    ci.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VK_CHECK(vkCreateFramebuffer(device, &ci, nullptr, &fb));
    return fb;
}

VkRenderPass make_render_pass(VkDevice device,
                              const std::vector<VkAttachmentDescription>& attachments,
                              const std::vector<VkAttachmentReference>& color_refs,
                              const VkAttachmentReference* depth_ref) {
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(color_refs.size());
    subpass.pColorAttachments = color_refs.empty() ? nullptr : color_refs.data();
    subpass.pDepthStencilAttachment = depth_ref;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = deps[0].srcStageMask;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = static_cast<uint32_t>(attachments.size());
    ci.pAttachments = attachments.data();
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = static_cast<uint32_t>(deps.size());
    ci.pDependencies = deps.data();

    VkRenderPass pass = VK_NULL_HANDLE;
    VK_CHECK(vkCreateRenderPass(device, &ci, nullptr, &pass));
    return pass;
}

std::vector<VkVertexInputBindingDescription> vertex_bindings() {
    return {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
}

std::vector<VkVertexInputAttributeDescription> vertex_attribs() {
    return {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT,   12},
        {2, 0, VK_FORMAT_R32G32_SFLOAT,      24},
        {3, 0, VK_FORMAT_R8G8B8A8_UNORM,     32},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT,36},
    };
}

} // namespace

struct VulkanRenderer::GpuMesh {
    vk::VulkanBuffer vb;
    vk::VulkanBuffer ib;
    uint32_t index_count = 0;
};

struct VulkanRenderer::GpuTexture {
    vk::VulkanImage image;
};

struct VulkanRenderer::DrawItem {
    const Mesh* mesh = nullptr;
    Material material {};
    glm::mat4 transform {1.0f};
};

static VulkanRenderer* s_renderer = nullptr;

VulkanRenderer::VulkanRenderer() = default;
VulkanRenderer::~VulkanRenderer() { shutdown(); }

VulkanRenderer* VulkanRenderer::get() { return s_renderer; }

bool VulkanRenderer::init(Window& window) {
    try {
        m_window = &window;
        m_w = static_cast<uint16_t>(window.width());
        m_h = static_cast<uint16_t>(window.height());

        s_renderer = this;

#ifndef NDEBUG
        const bool enable_validation = true;
#else
        const bool enable_validation = false;
#endif

        if (!m_ctx.init(window.handle(), enable_validation)) return false;
        if (!m_swapchain.init(m_ctx, window.handle())) return false;
        if (!m_desc.init(m_ctx)) return false;
        if (!create_frame_resources_()) return false;
        if (!create_shadow_resources_()) return false;
        if (!create_render_passes_()) return false;
        if (!create_swapchain_targets_()) return false;
        if (!create_pipelines_()) return false;

        m_fallback_white = vk::create_texture2d_1x1(m_ctx, 255, 255, 255, 255);
        m_fallback_normal = vk::create_texture2d_1x1(m_ctx, 128, 128, 255, 255);
        m_fallback_mr = vk::create_texture2d_1x1(m_ctx, 0, 255, 0, 255);
        m_fallback_black = vk::create_texture2d_1x1(m_ctx, 0, 0, 0, 255);

        if (!create_ibl_resources_()) return false;
        if (!create_ibl_pipelines_()) return false;

        m_initialized = true;
        SOL_INFO("Renderer initialized (Vulkan)");
        return true;
    } catch (const std::exception& e) {
        SOL_ERROR(std::string("VulkanRenderer::init failed: ") + e.what());
        shutdown();
        return false;
    }
}

bool VulkanRenderer::init_win32(void* hwnd, void* hinstance, int w, int h) {
    try {
        m_window = nullptr;
        m_editor_mode = true;
        m_w = static_cast<uint16_t>(w);
        m_h = static_cast<uint16_t>(h);

        s_renderer = this;

        if (!m_ctx.init_win32(hwnd, hinstance, false)) return false;
        volkLoadDevice(m_ctx.device());
        if (!m_swapchain.init(m_ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h))) return false;
        if (!m_desc.init(m_ctx)) return false;
        if (!create_frame_resources_()) return false;
        if (!create_shadow_resources_()) return false;
        if (!create_render_passes_()) return false;
        if (!create_swapchain_targets_()) return false;
        if (!create_pipelines_()) return false;

        m_fallback_white  = vk::create_texture2d_1x1(m_ctx, 255, 255, 255, 255);
        m_fallback_normal = vk::create_texture2d_1x1(m_ctx, 128, 128, 255, 255);
        m_fallback_mr     = vk::create_texture2d_1x1(m_ctx, 0, 255, 0, 255);
        m_fallback_black  = vk::create_texture2d_1x1(m_ctx, 0, 0, 0, 255);

        if (!create_ibl_resources_()) return false;
        if (!create_ibl_pipelines_()) return false;

        m_initialized = true;
        SOL_INFO("Renderer initialized (Vulkan, editor/Win32 mode)");
        return true;
    } catch (const std::exception& e) {
        SOL_ERROR(std::string("VulkanRenderer::init_win32 failed: ") + e.what());
        shutdown();
        return false;
    }
}


void VulkanRenderer::shutdown() {
    if (m_ctx.device()) vkDeviceWaitIdle(m_ctx.device());

    shutdown_imgui_backend();
    destroy_swapchain_targets_();
    destroy_pipelines_();
    destroy_ibl_();
    destroy_render_passes_();
    destroy_shadow_resources_();
    destroy_frame_resources_();

    m_fallback_mr.destroy(m_ctx.device(), m_ctx.allocator());
    m_fallback_normal.destroy(m_ctx.device(), m_ctx.allocator());
    m_fallback_white.destroy(m_ctx.device(), m_ctx.allocator());
    m_fallback_black.destroy(m_ctx.device(), m_ctx.allocator());
    m_ssao_noise.destroy(m_ctx.device(), m_ctx.allocator());

    if (m_ctx.device()) m_desc.shutdown(m_ctx.device());
    if (m_ctx.device()) m_swapchain.shutdown(m_ctx);
    m_ctx.shutdown();

    m_draws.clear();
    m_frame_lights.clear();
    m_initialized = false;
    m_window = nullptr;
    if (s_renderer == this) s_renderer = nullptr;
}

void VulkanRenderer::begin_frame() {
    clear_lights_();
    clear_sky_();
    m_draws.clear();
    m_frame_lights.clear();

    if (m_ibl_dirty && m_ibl_sky_pipe != VK_NULL_HANDLE) {
        rebuild_ibl_();
        m_ibl_dirty = false;
    }
}

void VulkanRenderer::end_frame() {
    if (!m_initialized || (!m_window && !m_editor_mode) || m_swapchain.extent().width == 0 || m_swapchain.extent().height == 0) return;

    try {
        uint32_t image_idx = 0;
        VkResult acquire = m_swapchain.acquire_next(m_ctx, m_frame_index, image_idx);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain_();
            return;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            SOL_ERROR("vkAcquireNextImageKHR failed");
            return;
        }

        auto& frame = m_swapchain.frame(m_frame_index);
        m_desc.reset_pool(m_ctx.device(), m_frame_index);

        vk::FrameUBO ubo{};
        glm::vec3 shadow_dir{0.0f, -1.0f, 0.0f};
        m_has_shadow = update_frame_ubo_(m_frame_index, ubo, shadow_dir);

        VkDescriptorSet frame_set = m_desc.alloc_frame_set(
            m_ctx.device(),
            m_frame_ubos[m_frame_index].handle,
            sizeof(vk::FrameUBO),
            m_shadow.view,       m_shadow.sampler,
            m_shadow.view,       m_shadow_raw_sampler,
            m_vsm_moment.view,   m_vsm_sampler);

        VK_CHECK(vkResetCommandBuffer(frame.cmd, 0));
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(frame.cmd, &bi));

        if (m_has_shadow) {
            if (m_vsm_mode) {
                record_vsm_shadow_pass_(frame.cmd, frame_set);
            } else {
                record_shadow_pass_(frame.cmd, frame_set);
            }
        }

        // Transition gbuf0-3 and hdr_depth for writing in the G-buffer pass
        for (auto* img : {&m_gbuf0, &m_gbuf1, &m_gbuf2, &m_gbuf3}) {
            vk::transition_image_layout(frame.cmd, img->image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        vk::transition_image_layout(frame.cmd, m_hdr_depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        record_gbuf_pass_(frame.cmd, frame_set);

        // SSAO pass runs after GBuffer (reads normals+depth), before deferred lighting
        if (m_settings.ssao_enabled) {
            record_ssao_passes_(frame.cmd, frame_set);
        }

        // Transition hdr_color for the deferred lighting output
        vk::transition_image_layout(frame.cmd, m_hdr_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        // Ping-pong: write to accum[write], read history from accum[read]
        uint32_t taa_write = m_taa_idx % 2;
        uint32_t taa_read  = 1 - taa_write;
        vk::transition_image_layout(frame.cmd, m_shadow_accum[taa_write].image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        record_light_pass_(frame.cmd, frame_set, taa_read, taa_write);
        ++m_taa_idx;

        // Forward pass only runs if there are blend draws
        bool has_blend = false;
        for (const auto& draw : m_draws) {
            if (draw.material.alpha_mode == AlphaMode::Blend) { has_blend = true; break; }
        }
        if (has_blend) {
            // hdr_color is in SHADER_READ_ONLY after light_pass; transition to COLOR_ATTACHMENT for LOAD
            vk::transition_image_layout(frame.cmd, m_hdr_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            record_forward_pass_(frame.cmd, frame_set);
        }

        // --- TAA resolve pass ---
        // Ping-pong: write to taa_color[color_write], read history from taa_color[color_read]
        uint32_t color_write = m_color_taa_idx % 2;
        uint32_t color_read  = 1 - color_write;
        vk::transition_image_layout(frame.cmd, m_taa_color[color_write].image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        record_taa_pass_(frame.cmd, frame_set, color_read, color_write);
        ++m_color_taa_idx;

        // Bloom and tonemap now read from the TAA output instead of raw HDR
        const vk::VulkanImage& post_hdr = m_taa_color[color_write];

        record_bloom_passes_(frame.cmd, post_hdr);
        record_tonemap_pass_(frame.cmd, image_idx, post_hdr);
        record_imgui_pass_(frame.cmd, image_idx);

        VK_CHECK(vkEndCommandBuffer(frame.cmd));

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &frame.image_available;
        si.pWaitDstStageMask = &wait_stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &frame.cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &frame.render_finished;
        VK_CHECK(vkQueueSubmit(m_ctx.gfx_queue(), 1, &si, frame.in_flight_fence));

        VkResult present = m_swapchain.present(m_ctx, m_frame_index, image_idx);
        if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) {
            recreate_swapchain_();
        } else if (present != VK_SUCCESS) {
            SOL_ERROR("vkQueuePresentKHR failed");
        }

        m_frame_index = (m_frame_index + 1) % vk::FRAMES_IN_FLIGHT;
    } catch (const std::exception& e) {
        SOL_ERROR(std::string("VulkanRenderer::end_frame failed: ") + e.what());
    }
}

void VulkanRenderer::resize(int w, int h) {
    if (!m_initialized || w <= 0 || h <= 0) return;
    m_w = static_cast<uint16_t>(w);
    m_h = static_cast<uint16_t>(h);
    recreate_swapchain_();
}

void VulkanRenderer::submit(const Mesh& mesh, const Material& mat, const glm::mat4& transform) {
    if (!mesh.valid()) return;
    m_draws.push_back({&mesh, mat, transform});
}

void* VulkanRenderer::alloc_mesh(const Vertex* verts, size_t vertex_count, const uint32_t* indices, size_t index_count) {
    if (!m_ctx.device()) return nullptr;
    auto* mesh = new GpuMesh();
    try {
        mesh->vb = vk::create_gpu_buffer(
            m_ctx,
            static_cast<VkDeviceSize>(vertex_count * sizeof(Vertex)),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            verts);
        mesh->ib = vk::create_gpu_buffer(
            m_ctx,
            static_cast<VkDeviceSize>(index_count * sizeof(uint32_t)),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            indices);
        mesh->index_count = static_cast<uint32_t>(index_count);
        return mesh;
    } catch (...) {
        mesh->vb.destroy(m_ctx.allocator());
        mesh->ib.destroy(m_ctx.allocator());
        delete mesh;
        return nullptr;
    }
}

void VulkanRenderer::free_mesh(void* gpu) {
    if (!gpu || !m_ctx.allocator()) return;
    auto* mesh = static_cast<GpuMesh*>(gpu);
    mesh->vb.destroy(m_ctx.allocator());
    mesh->ib.destroy(m_ctx.allocator());
    delete mesh;
}

void* VulkanRenderer::alloc_texture(const void* pixels, int width, int height) {
    if (!m_ctx.device()) return nullptr;
    auto* tex = new GpuTexture();
    try {
        tex->image = vk::create_texture2d(m_ctx, pixels, width, height);
        return tex;
    } catch (...) {
        tex->image.destroy(m_ctx.device(), m_ctx.allocator());
        delete tex;
        return nullptr;
    }
}

void VulkanRenderer::free_texture(void* gpu) {
    if (!gpu || !m_ctx.device()) return;
    auto* tex = static_cast<GpuTexture*>(gpu);
    tex->image.destroy(m_ctx.device(), m_ctx.allocator());
    delete tex;
}

bool VulkanRenderer::init_imgui_backend(GLFWwindow*) {
    if (m_imgui_initialized) return true;
    if (!m_ctx.create_imgui_pool()) return false;

    ImGui_ImplVulkan_LoadFunctions(imgui_loader, reinterpret_cast<void*>(m_ctx.instance()));

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = m_ctx.instance();
    info.PhysicalDevice = m_ctx.gpu();
    info.Device = m_ctx.device();
    info.QueueFamily = *m_ctx.queue_families().graphics;
    info.Queue = m_ctx.gfx_queue();
    info.DescriptorPool = m_ctx.imgui_pool();
    info.RenderPass = m_imgui_pass;
    info.MinImageCount = std::max<uint32_t>(2, m_swapchain.image_count());
    info.ImageCount = m_swapchain.image_count();
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS) SOL_ERROR("ImGui Vulkan error");
    };

    if (!ImGui_ImplVulkan_Init(&info)) return false;

    ImGui_ImplVulkan_CreateFontsTexture();

    m_imgui_initialized = true;
    return true;
}

void VulkanRenderer::shutdown_imgui_backend() {
    if (!m_imgui_initialized) return;
    ImGui_ImplVulkan_Shutdown();
    m_imgui_initialized = false;
}

bool VulkanRenderer::create_frame_resources_() {
    for (auto& ubo : m_frame_ubos) {
        ubo = vk::create_host_buffer(m_ctx.allocator(), sizeof(vk::FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    // SSAO noise: 4×4 RGBA8 random rotation vectors
    {
        std::array<uint8_t, 4 * 4 * 4> noise_data{};
        for (int i = 0; i < 16; ++i) {
            float angle = static_cast<float>((i * 6791) & 0xFFFF) / 65536.0f * 2.0f * 3.14159265f;
            noise_data[i*4+0] = static_cast<uint8_t>(std::clamp((std::cos(angle) + 1.0f) * 127.5f, 0.0f, 255.0f));
            noise_data[i*4+1] = static_cast<uint8_t>(std::clamp((std::sin(angle) + 1.0f) * 127.5f, 0.0f, 255.0f));
            noise_data[i*4+2] = 128;
            noise_data[i*4+3] = 255;
        }
        m_ssao_noise = vk::create_texture2d(m_ctx, noise_data.data(), 4, 4);
    }
    return true;
}

void VulkanRenderer::destroy_frame_resources_() {
    for (auto& ubo : m_frame_ubos) ubo.destroy(m_ctx.allocator());
    m_ssao_noise.destroy(m_ctx.device(), m_ctx.allocator());
}

bool VulkanRenderer::create_shadow_resources_() {
    constexpr uint32_t LAYERS = 4;

    // Create 4-layer depth array image
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.extent        = { SHADOW_SIZE, SHADOW_SIZE, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = LAYERS;
    ici.format        = DEPTH_FORMAT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(m_ctx.allocator(), &ici, &ai,
                            &m_shadow.image, &m_shadow.alloc, nullptr));
    m_shadow.format = DEPTH_FORMAT;
    m_shadow.width  = SHADOW_SIZE;
    m_shadow.height = SHADOW_SIZE;

    // Full-array 2D_ARRAY view (for shadow sampler binding in frame set)
    {
        VkImageViewCreateInfo vci{};
        vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                           = m_shadow.image;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vci.format                          = DEPTH_FORMAT;
        vci.subresourceRange                = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, LAYERS };
        VK_CHECK(vkCreateImageView(m_ctx.device(), &vci, nullptr, &m_shadow.view));
    }

    // Per-layer 2D views (for per-cascade framebuffers)
    for (uint32_t i = 0; i < LAYERS; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                           = m_shadow.image;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = DEPTH_FORMAT;
        vci.subresourceRange                = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, i, 1 };
        VK_CHECK(vkCreateImageView(m_ctx.device(), &vci, nullptr, &m_shadow_layer_views[i]));
    }

    // Comparison shadow sampler (matches sampler2DArrayShadow in GLSL)
    {
        VkSamplerCreateInfo si{};
        si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter     = VK_FILTER_LINEAR;
        si.minFilter     = VK_FILTER_LINEAR;
        si.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        si.compareEnable = VK_TRUE;
        si.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &si, nullptr, &m_shadow.sampler));
    }

    // Non-comparison shadow sampler for PCSS blocker depth reads
    {
        VkSamplerCreateInfo si{};
        si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter     = VK_FILTER_LINEAR;
        si.minFilter     = VK_FILTER_LINEAR;
        si.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        si.compareEnable = VK_FALSE;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &si, nullptr, &m_shadow_raw_sampler));
    }

    // Transition all 4 layers: UNDEFINED → SHADER_READ_ONLY_OPTIMAL (initial resting state)
    {
        auto cmd = m_ctx.begin_single_cmd();
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_shadow.image;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, LAYERS };
        barrier.srcAccessMask       = 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        m_ctx.end_single_cmd(cmd);
    }

    // --- VSM moment image (RG32F, 4 layers) ---
    constexpr VkFormat VSM_FORMAT = VK_FORMAT_R32G32_SFLOAT;
    {
        VkImageCreateInfo vci{};
        vci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        vci.imageType     = VK_IMAGE_TYPE_2D;
        vci.extent        = { SHADOW_SIZE, SHADOW_SIZE, 1 };
        vci.mipLevels     = 1;
        vci.arrayLayers   = LAYERS;
        vci.format        = VSM_FORMAT;
        vci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        vci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        vci.samples       = VK_SAMPLE_COUNT_1_BIT;
        vci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(m_ctx.allocator(), &vci, &ai,
                                &m_vsm_moment.image, &m_vsm_moment.alloc, nullptr));
        m_vsm_moment.format = VSM_FORMAT;
        m_vsm_moment.width  = SHADOW_SIZE;
        m_vsm_moment.height = SHADOW_SIZE;

        // Full 2D_ARRAY view for sampling in lighting shaders
        VkImageViewCreateInfo iv{};
        iv.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image            = m_vsm_moment.image;
        iv.viewType         = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        iv.format           = VSM_FORMAT;
        iv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, LAYERS };
        VK_CHECK(vkCreateImageView(m_ctx.device(), &iv, nullptr, &m_vsm_moment.view));

        // Per-layer 2D views for VSM framebuffers
        for (uint32_t i = 0; i < LAYERS; ++i) {
            iv.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            iv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, i, 1 };
            VK_CHECK(vkCreateImageView(m_ctx.device(), &iv, nullptr, &m_vsm_layer_views[i]));
        }
    }

    // VSM linear sampler (clamped, no comparison)
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        si.compareEnable = VK_FALSE;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &si, nullptr, &m_vsm_sampler));
    }

    // Transition VSM moment image to SHADER_READ_ONLY (initial resting state)
    {
        auto cmd = m_ctx.begin_single_cmd();
        VkImageMemoryBarrier bar{};
        bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image               = m_vsm_moment.image;
        bar.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, LAYERS };
        bar.srcAccessMask       = 0;
        bar.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &bar);
        m_ctx.end_single_cmd(cmd);
    }

    return true;
}

void VulkanRenderer::destroy_shadow_resources_() {
    for (uint32_t i = 0; i < 4; ++i) {
        if (m_shadow_layer_views[i]) {
            vkDestroyImageView(m_ctx.device(), m_shadow_layer_views[i], nullptr);
            m_shadow_layer_views[i] = VK_NULL_HANDLE;
        }
    }
    if (m_shadow_raw_sampler) {
        vkDestroySampler(m_ctx.device(), m_shadow_raw_sampler, nullptr);
        m_shadow_raw_sampler = VK_NULL_HANDLE;
    }
    m_shadow.destroy(m_ctx.device(), m_ctx.allocator());

    for (uint32_t i = 0; i < 4; ++i) {
        if (m_vsm_layer_views[i]) {
            vkDestroyImageView(m_ctx.device(), m_vsm_layer_views[i], nullptr);
            m_vsm_layer_views[i] = VK_NULL_HANDLE;
        }
    }
    if (m_vsm_sampler) {
        vkDestroySampler(m_ctx.device(), m_vsm_sampler, nullptr);
        m_vsm_sampler = VK_NULL_HANDLE;
    }
    m_vsm_moment.destroy(m_ctx.device(), m_ctx.allocator());
}

bool VulkanRenderer::create_render_passes_() {
    // --- Shadow pass (unchanged) ---
    const VkAttachmentDescription shadow_att{
        0, DEPTH_FORMAT, VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkAttachmentReference shadow_ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    m_shadow_pass = make_render_pass(m_ctx.device(), {shadow_att}, {}, &shadow_ref);

    // --- VSM shadow pass: 1 color (RG32F moments) + 1 depth (scratch) ---
    {
        constexpr VkFormat VSM_FORMAT = VK_FORMAT_R32G32_SFLOAT;
        const VkAttachmentDescription vsm_color_att{
            0, VSM_FORMAT, VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        const VkAttachmentDescription vsm_depth_att{
            0, DEPTH_FORMAT, VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        VkAttachmentReference vsm_color_ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference vsm_depth_ref{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        m_vsm_pass = make_render_pass(m_ctx.device(),
            {vsm_color_att, vsm_depth_att},
            {vsm_color_ref}, &vsm_depth_ref);
    }

    // --- G-buffer pass: 4 color MRT + depth ---
    // Attachments: gbuf0, gbuf1, gbuf2, gbuf3 (indices 0-3), depth (index 4)
    const std::vector<VkAttachmentDescription> gbuf_atts = {
        {0, GBUF0_FORMAT, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {0, GBUF1_FORMAT, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {0, GBUF2_FORMAT, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {0, GBUF3_FORMAT, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {0, DEPTH_FORMAT, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
    };
    const std::vector<VkAttachmentReference> gbuf_color_refs = {
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };
    VkAttachmentReference gbuf_depth_ref{4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    m_gbuf_pass = make_render_pass(m_ctx.device(), gbuf_atts, gbuf_color_refs, &gbuf_depth_ref);

    // --- Deferred lighting pass: HDR color (att 0) + shadow accum (att 1), no depth ---
    const std::array<VkAttachmentDescription, 2> light_atts = {{
        {0, HDR_FORMAT, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {0, SHADOW_ACCUM_FORMAT, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    }};
    const std::array<VkAttachmentReference, 2> light_color_refs = {{
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    }};
    m_light_pass = make_render_pass(m_ctx.device(),
        {light_atts[0], light_atts[1]},
        {light_color_refs[0], light_color_refs[1]},
        nullptr);

    // --- Forward transparent pass: HDR LOAD + depth read-only ---
    // Custom pass (not using make_render_pass) for read-only depth dependency.
    {
        const std::array<VkAttachmentDescription, 2> fw_atts = {{
            {0, HDR_FORMAT, VK_SAMPLE_COUNT_1_BIT,
             VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
             VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {0, DEPTH_FORMAT, VK_SAMPLE_COUNT_1_BIT,
             VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE,
             VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
        }};
        VkAttachmentReference fw_color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference fw_depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &fw_color;
        subpass.pDepthStencilAttachment = &fw_depth;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = (uint32_t)fw_atts.size();
        ci.pAttachments    = fw_atts.data();
        ci.subpassCount    = 1;
        ci.pSubpasses      = &subpass;
        ci.dependencyCount = (uint32_t)deps.size();
        ci.pDependencies   = deps.data();
        VK_CHECK(vkCreateRenderPass(m_ctx.device(), &ci, nullptr, &m_forward_pass));
    }

    // --- Bloom pass (unchanged) ---
    const VkAttachmentDescription bloom_att{
        0, HDR_FORMAT, VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkAttachmentReference bloom_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    m_bloom_pass = make_render_pass(m_ctx.device(), {bloom_att}, {bloom_ref}, nullptr);

    const VkAttachmentDescription tonemap_att{
        0, m_swapchain.format(), VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkAttachmentReference tonemap_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    m_tonemap_pass = make_render_pass(m_ctx.device(), {tonemap_att}, {tonemap_ref}, nullptr);

    const VkAttachmentDescription imgui_att{
        0, m_swapchain.format(), VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference imgui_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    m_imgui_pass = make_render_pass(m_ctx.device(), {imgui_att}, {imgui_ref}, nullptr);

    // --- SSAO pass: single R8_UNORM color, UNDEFINED→SHADER_READ_ONLY ---
    const VkAttachmentDescription ssao_att{
        0, SSAO_FORMAT, VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkAttachmentReference ssao_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    m_ssao_pass = make_render_pass(m_ctx.device(), {ssao_att}, {ssao_ref}, nullptr);

    // --- TAA resolve pass: HDR color attachment OPTIMAL→SHADER_READ_ONLY ---
    // initialLayout is COLOR_ATTACHMENT_OPTIMAL because we do an explicit barrier before starting it.
    const VkAttachmentDescription taa_att{
        0, HDR_FORMAT, VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkAttachmentReference taa_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    m_taa_pass = make_render_pass(m_ctx.device(), {taa_att}, {taa_ref}, nullptr);

    return true;
}

void VulkanRenderer::destroy_render_passes_() {
    auto destroy = [&](VkRenderPass& pass) {
        if (pass) {
            vkDestroyRenderPass(m_ctx.device(), pass, nullptr);
            pass = VK_NULL_HANDLE;
        }
    };
    destroy(m_imgui_pass);
    destroy(m_tonemap_pass);
    destroy(m_bloom_pass);
    destroy(m_forward_pass);
    destroy(m_light_pass);
    destroy(m_gbuf_pass);
    destroy(m_shadow_pass);
    destroy(m_vsm_pass);
    destroy(m_ssao_pass);
    destroy(m_taa_pass);
}

bool VulkanRenderer::create_swapchain_targets_() {
    const auto extent = m_swapchain.extent();
    m_w = static_cast<uint16_t>(extent.width);
    m_h = static_cast<uint16_t>(extent.height);

    m_hdr_color = vk::create_attachment(
        m_ctx, extent.width, extent.height,
        HDR_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true);
    m_hdr_depth = vk::create_attachment(
        m_ctx, extent.width, extent.height,
        DEPTH_FORMAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        false);

    // G-buffer attachments
    m_gbuf0 = vk::create_attachment(m_ctx, extent.width, extent.height,
        GBUF0_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, true);
    m_gbuf1 = vk::create_attachment(m_ctx, extent.width, extent.height,
        GBUF1_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, true);
    m_gbuf2 = vk::create_attachment(m_ctx, extent.width, extent.height,
        GBUF2_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, true);
    m_gbuf3 = vk::create_attachment(m_ctx, extent.width, extent.height,
        GBUF3_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, true);

    // Depth sampler for deferred lighting (non-compare, nearest, raw depth read)
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_NEAREST;
        si.minFilter    = VK_FILTER_NEAREST;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &si, nullptr, &m_hdr_depth_sampler));
    }

    const uint32_t bloom_w = std::max(1u, extent.width / 2);
    const uint32_t bloom_h = std::max(1u, extent.height / 2);
    m_bloom_a = vk::create_attachment(
        m_ctx, bloom_w, bloom_h,
        HDR_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true);
    m_bloom_b = vk::create_attachment(
        m_ctx, bloom_w, bloom_h,
        HDR_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true);

    // SSAO textures (full-resolution R8_UNORM)
    m_ssao = vk::create_attachment(
        m_ctx, extent.width, extent.height,
        SSAO_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, true);
    m_ssao_blur = vk::create_attachment(
        m_ctx, extent.width, extent.height,
        SSAO_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, true);

    // Shadow accumulation ping-pong textures (R16_SFLOAT, screen resolution)
    for (int i = 0; i < 2; ++i) {
        m_shadow_accum[i] = vk::create_attachment(
            m_ctx, extent.width, extent.height,
            SHADOW_ACCUM_FORMAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, true);
    }
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &si, nullptr, &m_shadow_accum_sampler));
    }

    // TAA color ping-pong textures (HDR_FORMAT, screen resolution)
    for (int i = 0; i < 2; ++i) {
        m_taa_color[i] = vk::create_attachment(
            m_ctx, extent.width, extent.height,
            HDR_FORMAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, true);
    }

    // Transition all images to their expected initial layouts
    auto cmd = m_ctx.begin_single_cmd();
    // Color attachments → SHADER_READ_ONLY (resting state between frames)
    for (auto* img : {&m_hdr_color, &m_gbuf0, &m_gbuf1, &m_gbuf2, &m_gbuf3, &m_bloom_a, &m_bloom_b, &m_ssao, &m_ssao_blur, &m_shadow_accum[0], &m_shadow_accum[1], &m_taa_color[0], &m_taa_color[1]}) {
        vk::transition_image_layout(cmd, img->image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    // Depth → DEPTH_STENCIL_READ_ONLY (resting state; gbuf pass transitions to DS_ATT before writing)
    vk::transition_image_layout(cmd, m_hdr_depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    m_ctx.end_single_cmd(cmd);

    for (uint32_t i = 0; i < 4; ++i) {
        m_shadow_fbs[i] = make_framebuffer(m_ctx.device(), m_shadow_pass,
                                            {m_shadow_layer_views[i]}, SHADOW_SIZE, SHADOW_SIZE);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        m_vsm_fbs[i] = make_framebuffer(m_ctx.device(), m_vsm_pass,
                                         {m_vsm_layer_views[i], m_shadow_layer_views[i]},
                                         SHADOW_SIZE, SHADOW_SIZE);
    }
    m_gbuf_fb= make_framebuffer(m_ctx.device(), m_gbuf_pass,
                                    {m_gbuf0.view, m_gbuf1.view, m_gbuf2.view, m_gbuf3.view, m_hdr_depth.view},
                                    extent.width, extent.height);
    m_light_fbs[0] = make_framebuffer(m_ctx.device(), m_light_pass,
                                       {m_hdr_color.view, m_shadow_accum[0].view}, extent.width, extent.height);
    m_light_fbs[1] = make_framebuffer(m_ctx.device(), m_light_pass,
                                       {m_hdr_color.view, m_shadow_accum[1].view}, extent.width, extent.height);
    m_forward_fb = make_framebuffer(m_ctx.device(), m_forward_pass,
                                    {m_hdr_color.view, m_hdr_depth.view}, extent.width, extent.height);
    m_bloom_a_fb = make_framebuffer(m_ctx.device(), m_bloom_pass, {m_bloom_a.view}, bloom_w, bloom_h);
    m_bloom_b_fb = make_framebuffer(m_ctx.device(), m_bloom_pass, {m_bloom_b.view}, bloom_w, bloom_h);
    m_ssao_fb      = make_framebuffer(m_ctx.device(), m_ssao_pass, {m_ssao.view},      extent.width, extent.height);
    m_ssao_blur_fb = make_framebuffer(m_ctx.device(), m_ssao_pass, {m_ssao_blur.view}, extent.width, extent.height);
    for (int i = 0; i < 2; ++i) {
        m_taa_fbs[i] = make_framebuffer(m_ctx.device(), m_taa_pass, {m_taa_color[i].view}, extent.width, extent.height);
    }

    m_tonemap_fbs.resize(m_swapchain.image_count());
    m_imgui_fbs.resize(m_swapchain.image_count());
    for (uint32_t i = 0; i < m_swapchain.image_count(); ++i) {
        m_tonemap_fbs[i] = make_framebuffer(m_ctx.device(), m_tonemap_pass, {m_swapchain.image_view(i)}, extent.width, extent.height);
        m_imgui_fbs[i]   = make_framebuffer(m_ctx.device(), m_imgui_pass,   {m_swapchain.image_view(i)}, extent.width, extent.height);
    }
    return true;
}

void VulkanRenderer::destroy_swapchain_targets_() {
    for (auto& fb : m_imgui_fbs) {
        if (fb) vkDestroyFramebuffer(m_ctx.device(), fb, nullptr);
    }
    for (auto& fb : m_tonemap_fbs) {
        if (fb) vkDestroyFramebuffer(m_ctx.device(), fb, nullptr);
    }
    m_imgui_fbs.clear();
    m_tonemap_fbs.clear();

    if (m_bloom_b_fb)  { vkDestroyFramebuffer(m_ctx.device(), m_bloom_b_fb,  nullptr); m_bloom_b_fb  = VK_NULL_HANDLE; }
    if (m_bloom_a_fb)  { vkDestroyFramebuffer(m_ctx.device(), m_bloom_a_fb,  nullptr); m_bloom_a_fb  = VK_NULL_HANDLE; }
    if (m_ssao_blur_fb) { vkDestroyFramebuffer(m_ctx.device(), m_ssao_blur_fb, nullptr); m_ssao_blur_fb = VK_NULL_HANDLE; }
    if (m_ssao_fb)      { vkDestroyFramebuffer(m_ctx.device(), m_ssao_fb,      nullptr); m_ssao_fb      = VK_NULL_HANDLE; }
    if (m_forward_fb)  { vkDestroyFramebuffer(m_ctx.device(), m_forward_fb,  nullptr); m_forward_fb  = VK_NULL_HANDLE; }
    for (int i = 0; i < 2; ++i) {
        if (m_light_fbs[i]) { vkDestroyFramebuffer(m_ctx.device(), m_light_fbs[i], nullptr); m_light_fbs[i] = VK_NULL_HANDLE; }
    }
    if (m_shadow_accum_sampler) { vkDestroySampler(m_ctx.device(), m_shadow_accum_sampler, nullptr); m_shadow_accum_sampler = VK_NULL_HANDLE; }
    for (int i = 0; i < 2; ++i) m_shadow_accum[i].destroy(m_ctx.device(), m_ctx.allocator());
    for (int i = 0; i < 2; ++i) {
        if (m_taa_fbs[i]) { vkDestroyFramebuffer(m_ctx.device(), m_taa_fbs[i], nullptr); m_taa_fbs[i] = VK_NULL_HANDLE; }
    }
    for (int i = 0; i < 2; ++i) m_taa_color[i].destroy(m_ctx.device(), m_ctx.allocator());
    if (m_gbuf_fb)     { vkDestroyFramebuffer(m_ctx.device(), m_gbuf_fb,     nullptr); m_gbuf_fb     = VK_NULL_HANDLE; }
    for (uint32_t i = 0; i < 4; ++i) {
        if (m_shadow_fbs[i]) { vkDestroyFramebuffer(m_ctx.device(), m_shadow_fbs[i], nullptr); m_shadow_fbs[i] = VK_NULL_HANDLE; }
    }
    for (uint32_t i = 0; i < 4; ++i) {
        if (m_vsm_fbs[i]) { vkDestroyFramebuffer(m_ctx.device(), m_vsm_fbs[i], nullptr); m_vsm_fbs[i] = VK_NULL_HANDLE; }
    }

    if (m_hdr_depth_sampler){ vkDestroySampler(m_ctx.device(), m_hdr_depth_sampler, nullptr); m_hdr_depth_sampler = VK_NULL_HANDLE; }

    m_bloom_b.destroy(m_ctx.device(), m_ctx.allocator());
    m_bloom_a.destroy(m_ctx.device(), m_ctx.allocator());
    m_ssao_blur.destroy(m_ctx.device(), m_ctx.allocator());
    m_ssao.destroy(m_ctx.device(), m_ctx.allocator());
    m_gbuf3.destroy(m_ctx.device(), m_ctx.allocator());
    m_gbuf2.destroy(m_ctx.device(), m_ctx.allocator());
    m_gbuf1.destroy(m_ctx.device(), m_ctx.allocator());
    m_gbuf0.destroy(m_ctx.device(), m_ctx.allocator());
    m_hdr_depth.destroy(m_ctx.device(), m_ctx.allocator());
    m_hdr_color.destroy(m_ctx.device(), m_ctx.allocator());
}

bool VulkanRenderer::recreate_swapchain_() {
    if (!m_ctx.device()) return false;

    int w = 0, h = 0;
    if (m_window) {
        glfwGetFramebufferSize(m_window->handle(), &w, &h);
    } else {
        w = m_w;
        h = m_h;
    }
    if (w <= 0 || h <= 0) return false;

    vkDeviceWaitIdle(m_ctx.device());
    destroy_swapchain_targets_();
    if (m_window) {
        if (!m_swapchain.recreate(m_ctx, m_window->handle())) return false;
    } else {
        if (!m_swapchain.recreate(m_ctx, (uint32_t)w, (uint32_t)h)) return false;
    }
    if (!create_swapchain_targets_()) return false;
    if (m_imgui_initialized) ImGui_ImplVulkan_SetMinImageCount(std::max<uint32_t>(2, m_swapchain.image_count()));
    return true;
}

bool VulkanRenderer::create_pipelines_() {
    std::vector<VkVertexInputBindingDescription> bindings = vertex_bindings();
    std::vector<VkVertexInputAttributeDescription> attribs = vertex_attribs();

    // --- Shadow pipelines (unchanged) ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.frame_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush)}};
        m_shadow_layout = vk::build_pipeline_layout(m_ctx.device(), layout);

        vk::PipelineDesc desc{};
        desc.vert_code = shadow_vert_glsl;
        desc.vert_size = sizeof(shadow_vert_glsl);
        desc.frag_code = shadow_frag_glsl;
        desc.frag_size = sizeof(shadow_frag_glsl);
        desc.vertex_bindings = bindings;
        desc.vertex_attribs = attribs;
        desc.layout = m_shadow_layout;
        desc.render_pass = m_shadow_pass;
        desc.depth_bias_const = 0.0f;
        desc.depth_bias_slope = 0.0f;
        m_shadow_pipe = vk::build_pipeline(m_ctx.device(), desc);
        desc.cull_mode = VK_CULL_MODE_NONE;
        m_shadow_pipe_double = vk::build_pipeline(m_ctx.device(), desc);
    }

    // --- VSM shadow pipeline (same vertex shader, moments fragment output) ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.frame_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush)}};
        m_vsm_layout = vk::build_pipeline_layout(m_ctx.device(), layout);

        vk::PipelineDesc desc{};
        desc.vert_code             = shadow_vert_glsl;
        desc.vert_size             = sizeof(shadow_vert_glsl);
        desc.frag_code             = shadow_vsm_frag_glsl;
        desc.frag_size             = sizeof(shadow_vsm_frag_glsl);
        desc.vertex_bindings       = bindings;
        desc.vertex_attribs        = attribs;
        desc.layout                = m_vsm_layout;
        desc.render_pass           = m_vsm_pass;
        desc.depth_bias_const      = 0.0f;
        desc.depth_bias_slope      = 0.0f;
        desc.color_attachment_count = 1;
        m_vsm_pipe = vk::build_pipeline(m_ctx.device(), desc);
        desc.cull_mode = VK_CULL_MODE_NONE;
        m_vsm_pipe_double = vk::build_pipeline(m_ctx.device(), desc);
    }

    // --- PBR layout: shared by gbuf + forward passes ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.frame_layout(), m_desc.material_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PbrPush)}};
        m_pbr_layout = vk::build_pipeline_layout(m_ctx.device(), layout);
    }

    // --- G-buffer pipelines: pbr.vert + gbuf.frag, 4 MRT, opaque/mask only ---
    {
        vk::PipelineDesc desc{};
        desc.vert_code = pbr_vert_glsl;
        desc.vert_size = sizeof(pbr_vert_glsl);
        desc.frag_code = gbuf_frag_glsl;
        desc.frag_size = sizeof(gbuf_frag_glsl);
        desc.vertex_bindings = bindings;
        desc.vertex_attribs = attribs;
        desc.layout = m_pbr_layout;
        desc.render_pass = m_gbuf_pass;
        desc.color_attachment_count = 4;
        desc.blend_enable = false;
        m_gbuf_pipe = vk::build_pipeline(m_ctx.device(), desc);
        desc.cull_mode = VK_CULL_MODE_NONE;
        m_gbuf_pipe_double = vk::build_pipeline(m_ctx.device(), desc);
    }

    // --- Deferred lighting pipeline: fullscreen, reads gbuf + depth ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.frame_layout(), m_desc.deferred_input_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPush)}};
        m_deferred_layout = vk::build_pipeline_layout(m_ctx.device(), layout);

        vk::PipelineDesc desc{};
        desc.vert_code = fullscreen_vert_glsl;
        desc.vert_size = sizeof(fullscreen_vert_glsl);
        desc.frag_code = deferred_light_frag_glsl;
        desc.frag_size = sizeof(deferred_light_frag_glsl);
        desc.layout = m_deferred_layout;
        desc.render_pass = m_light_pass;
        desc.depth_test = false;
        desc.depth_write = false;
        desc.cull_mode = VK_CULL_MODE_NONE;
        desc.color_attachment_count = 2;
        m_deferred_pipe = vk::build_pipeline(m_ctx.device(), desc);
    }

    // --- Forward transparent pipelines: pbr.vert + pbr.frag, blend, depth read-only ---
    {
        vk::PipelineDesc desc{};
        desc.vert_code = pbr_vert_glsl;
        desc.vert_size = sizeof(pbr_vert_glsl);
        desc.frag_code = pbr_frag_glsl;
        desc.frag_size = sizeof(pbr_frag_glsl);
        desc.vertex_bindings = bindings;
        desc.vertex_attribs = attribs;
        desc.layout = m_pbr_layout;
        desc.render_pass = m_forward_pass;
        desc.blend_enable = true;
        desc.depth_write = false;
        desc.depth_test = true;
        m_forward_pipe = vk::build_pipeline(m_ctx.device(), desc);
        desc.cull_mode = VK_CULL_MODE_NONE;
        m_forward_pipe_double = vk::build_pipeline(m_ctx.device(), desc);
    }

    // --- Bloom pipelines (unchanged) ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.single_sampler_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4Push)}};
        m_bright_layout = vk::build_pipeline_layout(m_ctx.device(), layout);
        m_blur_layout = vk::build_pipeline_layout(m_ctx.device(), layout);

        vk::PipelineDesc bright{};
        bright.vert_code = fullscreen_vert_glsl;
        bright.vert_size = sizeof(fullscreen_vert_glsl);
        bright.frag_code = bloom_bright_frag_glsl;
        bright.frag_size = sizeof(bloom_bright_frag_glsl);
        bright.layout = m_bright_layout;
        bright.render_pass = m_bloom_pass;
        bright.depth_test = false;
        bright.depth_write = false;
        bright.cull_mode = VK_CULL_MODE_NONE;
        m_bright_pipe = vk::build_pipeline(m_ctx.device(), bright);

        bright.frag_code = bloom_blur_frag_glsl;
        bright.frag_size = sizeof(bloom_blur_frag_glsl);
        bright.layout = m_blur_layout;
        m_blur_pipe = vk::build_pipeline(m_ctx.device(), bright);
    }

    // --- Tonemap pipeline (unchanged) ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.post_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4Push)}};
        m_tonemap_layout = vk::build_pipeline_layout(m_ctx.device(), layout);

        vk::PipelineDesc desc{};
        desc.vert_code = fullscreen_vert_glsl;
        desc.vert_size = sizeof(fullscreen_vert_glsl);
        desc.frag_code = tonemap_frag_glsl;
        desc.frag_size = sizeof(tonemap_frag_glsl);
        desc.layout = m_tonemap_layout;
        desc.render_pass = m_tonemap_pass;
        desc.depth_test = false;
        desc.depth_write = false;
        desc.cull_mode = VK_CULL_MODE_NONE;
        m_tonemap_pipe = vk::build_pipeline(m_ctx.device(), desc);
    }

    // --- SSAO pipeline ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.frame_layout(), m_desc.ssao_input_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOPush)}};
        m_ssao_layout = vk::build_pipeline_layout(m_ctx.device(), layout);

        vk::PipelineDesc desc{};
        desc.vert_code    = fullscreen_vert_glsl;
        desc.vert_size    = sizeof(fullscreen_vert_glsl);
        desc.frag_code    = ssao_frag_glsl;
        desc.frag_size    = sizeof(ssao_frag_glsl);
        desc.layout       = m_ssao_layout;
        desc.render_pass  = m_ssao_pass;
        desc.depth_test   = false;
        desc.depth_write  = false;
        desc.cull_mode    = VK_CULL_MODE_NONE;
        m_ssao_pipe = vk::build_pipeline(m_ctx.device(), desc);
    }

    // --- SSAO blur pipeline ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.single_sampler_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4Push)}};
        m_ssao_blur_layout = vk::build_pipeline_layout(m_ctx.device(), layout);

        vk::PipelineDesc desc{};
        desc.vert_code    = fullscreen_vert_glsl;
        desc.vert_size    = sizeof(fullscreen_vert_glsl);
        desc.frag_code    = ssao_blur_frag_glsl;
        desc.frag_size    = sizeof(ssao_blur_frag_glsl);
        desc.layout       = m_ssao_blur_layout;
        desc.render_pass  = m_ssao_pass;
        desc.depth_test   = false;
        desc.depth_write  = false;
        desc.cull_mode    = VK_CULL_MODE_NONE;
        m_ssao_blur_pipe = vk::build_pipeline(m_ctx.device(), desc);
    }

    // --- TAA resolve pipeline ---
    {
        vk::LayoutDesc layout{};
        layout.set_layouts = {m_desc.taa_input_layout(), m_desc.frame_layout()};
        layout.push_ranges = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TaaPush)}};
        m_taa_layout = vk::build_pipeline_layout(m_ctx.device(), layout);

        vk::PipelineDesc desc{};
        desc.vert_code   = fullscreen_vert_glsl;
        desc.vert_size   = sizeof(fullscreen_vert_glsl);
        desc.frag_code   = taa_frag_glsl;
        desc.frag_size   = sizeof(taa_frag_glsl);
        desc.layout      = m_taa_layout;
        desc.render_pass = m_taa_pass;
        desc.depth_test  = false;
        desc.depth_write = false;
        desc.cull_mode   = VK_CULL_MODE_NONE;
        m_taa_pipe = vk::build_pipeline(m_ctx.device(), desc);
    }

    return true;
}

void VulkanRenderer::destroy_pipelines_() {
    auto destroy_pipe = [&](VkPipeline& pipe) {
        if (pipe) {
            vkDestroyPipeline(m_ctx.device(), pipe, nullptr);
            pipe = VK_NULL_HANDLE;
        }
    };
    auto destroy_layout = [&](VkPipelineLayout& layout) {
        if (layout) {
            vkDestroyPipelineLayout(m_ctx.device(), layout, nullptr);
            layout = VK_NULL_HANDLE;
        }
    };

    destroy_pipe(m_tonemap_pipe);
    destroy_pipe(m_blur_pipe);
    destroy_pipe(m_bright_pipe);
    destroy_pipe(m_ssao_blur_pipe);
    destroy_pipe(m_ssao_pipe);
    destroy_pipe(m_taa_pipe);
    destroy_pipe(m_forward_pipe_double);
    destroy_pipe(m_forward_pipe);
    destroy_pipe(m_deferred_pipe);
    destroy_pipe(m_gbuf_pipe_double);
    destroy_pipe(m_gbuf_pipe);
    destroy_pipe(m_shadow_pipe_double);
    destroy_pipe(m_shadow_pipe);
    destroy_pipe(m_vsm_pipe_double);
    destroy_pipe(m_vsm_pipe);

    destroy_layout(m_tonemap_layout);
    destroy_layout(m_blur_layout);
    destroy_layout(m_bright_layout);
    destroy_layout(m_ssao_blur_layout);
    destroy_layout(m_ssao_layout);
    destroy_layout(m_taa_layout);
    destroy_layout(m_deferred_layout);
    destroy_layout(m_pbr_layout);
    destroy_layout(m_shadow_layout);
    destroy_layout(m_vsm_layout);
}

bool VulkanRenderer::update_frame_ubo_(uint32_t frame_idx, vk::FrameUBO& out_ubo, glm::vec3&) {
    const float aspect = m_h > 0 ? float(m_w) / float(m_h) : 1.0f;
    glm::mat4 view          = camera().view();
    glm::mat4 proj_unflipped = camera().proj(aspect, false);
    glm::mat4 proj           = proj_unflipped;
    proj[1][1] *= -1.0f;

    // --- Halton jitter for TAA (applied to proj before uploading to GPU) ---
    static auto halton = [](int index, int base) -> float {
        float f = 1.0f, r = 0.0f;
        while (index > 0) {
            f /= float(base);
            r += f * float(index % base);
            index /= base;
        }
        return r;
    };
    if (m_settings.taa_enabled && m_w > 0 && m_h > 0) {
        int jidx = int(m_color_taa_idx) % 8;
        float jx = (halton(jidx, 2) - 0.5f) * 2.0f / float(m_w);
        float jy = (halton(jidx, 3) - 0.5f) * 2.0f / float(m_h);
        // Apply sub-pixel jitter to clip-space (column-major GLM: proj[col][row])
        proj[2][0] -= jx;
        proj[2][1] -= jy;
    }

    std::vector<Light> ordered = m_lights;
    auto it = std::find_if(ordered.begin(), ordered.end(), [](const Light& l) {
        return l.type == LightType::Directional && l.cast_shadow;
    });
    if (it != ordered.end() && it != ordered.begin()) std::iter_swap(ordered.begin(), it);
    m_has_shadow = !ordered.empty() && ordered.front().type == LightType::Directional && ordered.front().cast_shadow;
    m_vsm_mode = m_has_shadow && (ordered.front().shadow_mode == 3);

    // Compute CSM cascade matrices from camera frustum
    glm::mat4 csm_matrices[4];
    float     csm_splits[4] = {};
    if (m_has_shadow) {
        // Extract tan(fov_y/2) from unflipped projection matrix
        float tan_half_fov = 1.0f / proj_unflipped[1][1];
        compute_csm_matrices(
            view, tan_half_fov, aspect,
            ordered.front().direction,
            m_settings.csm_lambda,
            m_settings.csm_far,
            csm_matrices, csm_splits);
    }

    store_mat4(out_ubo.view, view);
    store_mat4(out_ubo.proj, proj);
    for (int i = 0; i < 4; ++i)
        store_mat4(out_ubo.lightMtx[i], m_has_shadow ? csm_matrices[i] : glm::mat4(1.0f));
    store_vec4(out_ubo.camPos,   glm::vec4(m_camera.position, 1.0f));
    store_vec4(out_ubo.ambient,  glm::vec4(m_ambient * m_settings.ambient_scale, 1.0f));
    out_ubo.lightCount[0]   = float(std::min<size_t>(ordered.size(), 8));
    out_ubo.shadowConfig[0] = (m_has_shadow && m_settings.shadows_enabled) ? 1.0f : 0.0f;
    out_ubo.shadowConfig[1] = 1.0f / float(SHADOW_SIZE);
    out_ubo.shadowConfig[2] = float(m_settings.debug_view);
    out_ubo.shadowConfig[3] = float(m_settings.shadow_quality);
    for (int i = 0; i < 4; ++i)
        out_ubo.cascadeSplits[i] = csm_splits[i];
    store_mat4(out_ubo.invViewProj, glm::inverse(proj * view));
    out_ubo.shadowParams[0] = m_settings.shadow_bias_const;
    out_ubo.shadowParams[1] = m_settings.shadow_bias_slope;
    out_ubo.shadowParams[2] = m_settings.shadow_pcf_radius;
    out_ubo.shadowParams[3] = m_settings.shadow_pcss_light;
    out_ubo.shadowExtra[0] = m_settings.vsm_light_bleed;
    out_ubo.shadowExtra[1] = m_settings.vsm_min_variance;
    out_ubo.shadowExtra[2] = m_settings.contact_shadow_distance;
    out_ubo.shadowExtra[3] = m_settings.contact_shadow_thickness;

    // Temporal shadow: upload last frame's view-proj for reprojection, then store current
    store_mat4(out_ubo.prevViewProj, m_prev_view_proj);
    out_ubo.temporalParams[0] = m_settings.temporal_shadow_alpha;
    out_ubo.temporalParams[1] = m_settings.temporal_shadow_enabled ? 1.0f : 0.0f;
    out_ubo.temporalParams[2] = m_settings.temporal_shadow_max_dist;
    out_ubo.temporalParams[3] = 0.0f;
    out_ubo.iblParams[0] = m_settings.ibl_enabled ? 1.0f : 0.0f;
    out_ubo.iblParams[1] = m_settings.ibl_intensity;
    out_ubo.iblParams[2] = m_settings.ibl_diffuse_scale;
    out_ubo.iblParams[3] = m_settings.ibl_specular_scale;
    m_prev_view_proj = proj * view;  // store current for next frame

    for (size_t i = 0; i < std::min<size_t>(ordered.size(), 8); ++i) {
        const Light& l = ordered[i];
        glm::vec4 d0{}, d1{}, d2{}, d3{};
        if (l.type == LightType::Directional) {
            d0 = glm::vec4(glm::normalize(l.direction), 0.0f);
        } else {
            d0 = glm::vec4(l.position, l.type == LightType::Point ? 1.0f : 2.0f);
        }
        d1 = glm::vec4(l.color, l.intensity);
        d2 = glm::vec4(glm::normalize(l.direction), l.range);
        float shadow_mode_z = (l.type == LightType::Directional) ? float(l.shadow_mode) : 0.0f;
        d3 = glm::vec4(std::cos(glm::radians(l.inner_angle)), std::cos(glm::radians(l.outer_angle)), shadow_mode_z, 0.0f);
        store_vec4(out_ubo.lightData0[i], d0);
        store_vec4(out_ubo.lightData1[i], d1);
        store_vec4(out_ubo.lightData2[i], d2);
        store_vec4(out_ubo.lightData3[i], d3);
    }

    std::memcpy(m_frame_ubos[frame_idx].mapped, &out_ubo, sizeof(out_ubo));
    return m_has_shadow;
}

void VulkanRenderer::record_shadow_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set) {
    // Transition all 4 cascade layers: SHADER_READ_ONLY → DEPTH_STENCIL_ATTACHMENT
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_shadow.image;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 4 };
        barrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    for (int cascade = 0; cascade < 4; ++cascade) {
        VkClearValue clear{};
        clear.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_shadow_pass;
        rp.framebuffer       = m_shadow_fbs[cascade];
        rp.renderArea.extent = { SHADOW_SIZE, SHADOW_SIZE };
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp = make_viewport(float(SHADOW_SIZE), float(SHADOW_SIZE));
        VkRect2D   sc = make_scissor(SHADOW_SIZE, SHADOW_SIZE);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd,  0, 1, &sc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_shadow_layout, 0, 1, &frame_set, 0, nullptr);

        for (const auto& draw : m_draws) {
            auto* mesh = static_cast<GpuMesh*>(draw.mesh->gpu_data());
            if (!mesh) continue;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              draw.material.double_sided ? m_shadow_pipe_double : m_shadow_pipe);

            VkBuffer     vb   = mesh->vb.handle;
            VkDeviceSize offs = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offs);
            vkCmdBindIndexBuffer(cmd, mesh->ib.handle, 0, VK_INDEX_TYPE_UINT32);

            ShadowPush push{};
            push.model    = draw.transform;
            push.params.x = float(cascade);
            vkCmdPushConstants(cmd, m_shadow_layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(push), &push);
            vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
        // Each render pass's finalLayout = SHADER_READ_ONLY_OPTIMAL,
        // so each layer is left in SHADER_READ_ONLY after its pass.
    }
    // All 4 cascade layers are now in SHADER_READ_ONLY_OPTIMAL — ready for sampling.
}

void VulkanRenderer::record_vsm_shadow_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set) {
    // Transition shadow depth layers: SHADER_READ_ONLY → DEPTH_STENCIL_ATTACHMENT (scratch depth)
    {
        VkImageMemoryBarrier bar{};
        bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image               = m_shadow.image;
        bar.subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 4 };
        bar.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        bar.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &bar);
    }
    // Transition VSM moments: SHADER_READ_ONLY → COLOR_ATTACHMENT
    {
        VkImageMemoryBarrier bar{};
        bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image               = m_vsm_moment.image;
        bar.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 4 };
        bar.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        bar.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &bar);
    }

    for (int cascade = 0; cascade < 4; ++cascade) {
        std::array<VkClearValue, 2> clears{};
        clears[0].color        = {{ 0.0f, 0.0f, 0.0f, 0.0f }}; // EVSM neg: background = exp(-inf) ≈ 0
        clears[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_vsm_pass;
        rp.framebuffer       = m_vsm_fbs[cascade];
        rp.renderArea.extent = { SHADOW_SIZE, SHADOW_SIZE };
        rp.clearValueCount   = 2;
        rp.pClearValues      = clears.data();

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp = make_viewport(float(SHADOW_SIZE), float(SHADOW_SIZE));
        VkRect2D   sc = make_scissor(SHADOW_SIZE, SHADOW_SIZE);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd,  0, 1, &sc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_vsm_layout, 0, 1, &frame_set, 0, nullptr);

        for (const auto& draw : m_draws) {
            auto* mesh = static_cast<GpuMesh*>(draw.mesh->gpu_data());
            if (!mesh) continue;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              draw.material.double_sided ? m_vsm_pipe_double : m_vsm_pipe);

            VkBuffer     vb   = mesh->vb.handle;
            VkDeviceSize offs = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offs);
            vkCmdBindIndexBuffer(cmd, mesh->ib.handle, 0, VK_INDEX_TYPE_UINT32);

            ShadowPush push{};
            push.model    = draw.transform;
            push.params.x = float(cascade);
            vkCmdPushConstants(cmd, m_vsm_layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(push), &push);
            vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
    }
    // All 4 VSM cascade layers are in SHADER_READ_ONLY_OPTIMAL (per finalLayout).
    // Shadow depth layers are in SHADER_READ_ONLY_OPTIMAL (per finalLayout).
}

void VulkanRenderer::record_gbuf_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set) {
    std::array<VkClearValue, 5> clears{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clears[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // gbuf1: no geometry written
    clears[2].color = {{0.0f, 0.5f, 0.0f, 0.0f}};
    clears[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clears[4].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = m_gbuf_pass;
    rp.framebuffer = m_gbuf_fb;
    rp.renderArea.extent = m_swapchain.extent();
    rp.clearValueCount = (uint32_t)clears.size();
    rp.pClearValues = clears.data();

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = make_viewport(float(m_swapchain.extent().width), float(m_swapchain.extent().height));
    VkRect2D sc   = make_scissor(m_swapchain.extent().width, m_swapchain.extent().height);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    auto pick_tex = [&](const Texture* tex, const vk::VulkanImage& fallback) -> const vk::VulkanImage& {
        if (tex && tex->valid()) {
            auto* gpu = static_cast<const GpuTexture*>(tex->gpu_data());
            if (gpu) return gpu->image;
        }
        return fallback;
    };

    for (const auto& draw : m_draws) {
        if (draw.material.alpha_mode == AlphaMode::Blend) continue;

        auto* mesh = static_cast<GpuMesh*>(draw.mesh->gpu_data());
        if (!mesh) continue;

        const vk::VulkanImage& albedo   = pick_tex(draw.material.albedo,       m_fallback_white);
        const vk::VulkanImage& normal   = pick_tex(draw.material.normal_map,    m_fallback_normal);
        const vk::VulkanImage& mr       = pick_tex(draw.material.mr_map,        m_fallback_mr);
        const vk::VulkanImage& emissive = pick_tex(draw.material.emissive_tex,  m_fallback_black);

        VkDescriptorSet mat_set = m_desc.alloc_material_set(
            m_ctx.device(),
            albedo.view, albedo.sampler,
            normal.view, normal.sampler,
            mr.view,     mr.sampler,
            emissive.view, emissive.sampler);

        std::array<VkDescriptorSet, 2> sets = {frame_set, mat_set};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbr_layout, 0,
                                (uint32_t)sets.size(), sets.data(), 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          draw.material.double_sided ? m_gbuf_pipe_double : m_gbuf_pipe);

        bool has_emissive_tex = draw.material.emissive_tex && draw.material.emissive_tex->valid();
        PbrPush push{};
        push.model      = draw.transform;
        push.base_color = draw.material.base_color;
        push.pbr = glm::vec4(draw.material.metallic, draw.material.roughness,
                             draw.material.alpha_cutoff,
                             draw.material.alpha_mode == AlphaMode::Mask ? 1.0f : 0.0f);
        push.emissive = glm::vec4(draw.material.emissive, has_emissive_tex ? 1.0f : 0.0f);
        push.flags = glm::vec4(
            draw.material.lit ? 1.0f : 0.0f,
            draw.material.albedo && draw.material.albedo->valid() ? 1.0f : 0.0f,
            draw.material.normal_map && draw.material.normal_map->valid() ? 1.0f : 0.0f,
            draw.material.mr_map && draw.material.mr_map->valid() ? 1.0f : 0.0f);
        vkCmdPushConstants(cmd, m_pbr_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        VkBuffer vb = mesh->vb.handle;
        VkDeviceSize offs = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offs);
        vkCmdBindIndexBuffer(cmd, mesh->ib.handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::record_light_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set, uint32_t taa_read, uint32_t taa_write) {
    // gbuf0-3 are in SHADER_READ_ONLY_OPTIMAL (finalLayout of gbuf_pass)
    // hdr_depth is in DEPTH_STENCIL_READ_ONLY_OPTIMAL (finalLayout of gbuf_pass depth)
    VkImageView  ssao_view    = m_settings.ssao_enabled ? m_ssao_blur.view    : m_fallback_white.view;
    VkSampler    ssao_sampler = m_settings.ssao_enabled ? m_ssao_blur.sampler : m_fallback_white.sampler;
    // IBL fallbacks: use real textures if IBL is ready, else fallback black (cube/2D)
    VkImageView  ibl_irr_view  = m_ibl_ready ? m_ibl_irradiance.view  : m_ibl_fallback_cube.view;
    VkSampler    ibl_irr_samp  = m_ibl_ready ? m_ibl_cube_sampler     : m_ibl_cube_sampler;
    VkImageView  ibl_pref_view = m_ibl_ready ? m_ibl_prefilter.view   : m_ibl_fallback_cube.view;
    VkSampler    ibl_pref_samp = m_ibl_ready ? m_ibl_prefilter_sampler: m_ibl_cube_sampler;
    VkImageView  ibl_lut_view  = m_ibl_ready ? m_ibl_brdf_lut.view    : m_fallback_black.view;
    VkSampler    ibl_lut_samp  = m_ibl_ready ? m_ibl_lut_sampler      : m_fallback_black.sampler;

    VkDescriptorSet deferred_set = m_desc.alloc_deferred_input_set(
        m_ctx.device(),
        m_gbuf0.view, m_gbuf0.sampler,
        m_gbuf1.view, m_gbuf1.sampler,
        m_gbuf2.view, m_gbuf2.sampler,
        m_gbuf3.view, m_gbuf3.sampler,
        m_hdr_depth.view, m_hdr_depth_sampler,
        ssao_view, ssao_sampler,
        m_shadow_accum[taa_read].view, m_shadow_accum_sampler,
        ibl_irr_view, ibl_irr_samp,
        ibl_pref_view, ibl_pref_samp,
        ibl_lut_view, ibl_lut_samp);

    glm::vec4 clear_rgb = unpack_rgba(m_clear);
    VkClearValue clears[2]{};
    clears[0].color = {{clear_rgb.r, clear_rgb.g, clear_rgb.b, 1.0f}};
    clears[1].color = {{1.0f, 1.0f, 1.0f, 1.0f}};  // shadow accum: default fully lit

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = m_light_pass;
    rp.framebuffer = m_light_fbs[taa_write];
    rp.renderArea.extent = m_swapchain.extent();
    rp.clearValueCount = 2;
    rp.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = make_viewport(float(m_swapchain.extent().width), float(m_swapchain.extent().height));
    VkRect2D sc   = make_scissor(m_swapchain.extent().width, m_swapchain.extent().height);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferred_pipe);

    std::array<VkDescriptorSet, 2> sets = {frame_set, deferred_set};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferred_layout, 0,
                            (uint32_t)sets.size(), sets.data(), 0, nullptr);

    SkyPush sky{};
    if (m_has_sky) {
        sky.sun_dir   = glm::vec4(glm::normalize(m_sky_sun_dir), 0.0f);
        sky.zenith    = glm::vec4(m_sky_zenith, 0.0f);
        sky.horizon   = glm::vec4(m_sky_horizon, 0.0f);
        sky.sun_color = glm::vec4(m_sky_sun_color, m_sky_sun_cos_r);
    } else {
        // No sky: flat clear color, no sun
        sky.zenith    = glm::vec4(clear_rgb.r, clear_rgb.g, clear_rgb.b, 0.0f);
        sky.horizon   = glm::vec4(clear_rgb.r, clear_rgb.g, clear_rgb.b, 0.0f);
        sky.sun_color = glm::vec4(0.0f, 0.0f, 0.0f, 2.0f);  // alpha >= 1 = no sun
    }
    vkCmdPushConstants(cmd, m_deferred_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sky), &sky);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::record_forward_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set) {
    // hdr_color is in COLOR_ATTACHMENT_OPTIMAL (caller transitioned it before calling us)
    // hdr_depth is in DEPTH_STENCIL_READ_ONLY_OPTIMAL
    std::array<VkClearValue, 2> clears{};
    // LOAD_OP_LOAD for color — no clear needed (value ignored)
    clears[1].depthStencil = {1.0f, 0};  // also LOAD for depth — ignored

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = m_forward_pass;
    rp.framebuffer = m_forward_fb;
    rp.renderArea.extent = m_swapchain.extent();
    rp.clearValueCount = (uint32_t)clears.size();
    rp.pClearValues = clears.data();

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = make_viewport(float(m_swapchain.extent().width), float(m_swapchain.extent().height));
    VkRect2D sc   = make_scissor(m_swapchain.extent().width, m_swapchain.extent().height);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    auto pick_tex = [&](const Texture* tex, const vk::VulkanImage& fallback) -> const vk::VulkanImage& {
        if (tex && tex->valid()) {
            auto* gpu = static_cast<const GpuTexture*>(tex->gpu_data());
            if (gpu) return gpu->image;
        }
        return fallback;
    };

    // Collect and sort blend draws back-to-front
    std::vector<const DrawItem*> blend_draws;
    for (const auto& draw : m_draws) {
        if (draw.material.alpha_mode == AlphaMode::Blend)
            blend_draws.push_back(&draw);
    }
    const glm::vec3 cam_pos = m_camera.position;
    std::sort(blend_draws.begin(), blend_draws.end(), [&](const DrawItem* a, const DrawItem* b) {
        glm::vec3 da = glm::vec3(a->transform[3]) - cam_pos;
        glm::vec3 db = glm::vec3(b->transform[3]) - cam_pos;
        return glm::dot(da, da) > glm::dot(db, db);
    });

    for (const auto* draw : blend_draws) {
        auto* mesh = static_cast<GpuMesh*>(draw->mesh->gpu_data());
        if (!mesh) continue;

        const vk::VulkanImage& albedo   = pick_tex(draw->material.albedo,       m_fallback_white);
        const vk::VulkanImage& normal   = pick_tex(draw->material.normal_map,    m_fallback_normal);
        const vk::VulkanImage& mr       = pick_tex(draw->material.mr_map,        m_fallback_mr);
        const vk::VulkanImage& emissive = pick_tex(draw->material.emissive_tex,  m_fallback_black);

        VkDescriptorSet mat_set = m_desc.alloc_material_set(
            m_ctx.device(),
            albedo.view, albedo.sampler,
            normal.view, normal.sampler,
            mr.view,     mr.sampler,
            emissive.view, emissive.sampler);

        std::array<VkDescriptorSet, 2> sets = {frame_set, mat_set};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbr_layout, 0,
                                (uint32_t)sets.size(), sets.data(), 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          draw->material.double_sided ? m_forward_pipe_double : m_forward_pipe);

        bool has_emissive_tex = draw->material.emissive_tex && draw->material.emissive_tex->valid();
        PbrPush push{};
        push.model      = draw->transform;
        push.base_color = draw->material.base_color;
        push.pbr = glm::vec4(draw->material.metallic, draw->material.roughness,
                             draw->material.alpha_cutoff, 0.0f);
        push.emissive = glm::vec4(draw->material.emissive, has_emissive_tex ? 1.0f : 0.0f);
        push.flags = glm::vec4(
            draw->material.lit ? 1.0f : 0.0f,
            draw->material.albedo && draw->material.albedo->valid() ? 1.0f : 0.0f,
            draw->material.normal_map && draw->material.normal_map->valid() ? 1.0f : 0.0f,
            draw->material.mr_map && draw->material.mr_map->valid() ? 1.0f : 0.0f);
        vkCmdPushConstants(cmd, m_pbr_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        VkBuffer vb = mesh->vb.handle;
        VkDeviceSize offs = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offs);
        vkCmdBindIndexBuffer(cmd, mesh->ib.handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::record_taa_pass_(VkCommandBuffer cmd, VkDescriptorSet frame_set,
                                       uint32_t taa_read, uint32_t taa_write) {
    const uint32_t w = m_swapchain.extent().width;
    const uint32_t h = m_swapchain.extent().height;

    VkDescriptorSet taa_set = m_desc.alloc_taa_input_set(
        m_ctx.device(),
        m_hdr_color.view,         m_hdr_color.sampler,
        m_taa_color[taa_read].view, m_taa_color[taa_read].sampler,
        m_hdr_depth.view,         m_hdr_depth_sampler);

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp{};
    rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass      = m_taa_pass;
    rp.framebuffer     = m_taa_fbs[taa_write];
    rp.renderArea.extent = {w, h};
    rp.clearValueCount = 1;
    rp.pClearValues    = &clear;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = make_viewport(float(w), float(h));
    VkRect2D sc = make_scissor(w, h);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_taa_pipe);

    std::array<VkDescriptorSet, 2> sets = {taa_set, frame_set};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_taa_layout,
                             0, (uint32_t)sets.size(), sets.data(), 0, nullptr);

    TaaPush push{};
    push.params     = glm::vec4(
        m_settings.taa_blend,
        m_settings.taa_enabled ? 1.0f : 0.0f,
        m_settings.taa_variance_gamma,
        m_settings.taa_sharpening);
    push.resolution = glm::vec4(float(w), float(h), 1.0f / float(w), 1.0f / float(h));
    vkCmdPushConstants(cmd, m_taa_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::record_bloom_passes_(VkCommandBuffer cmd, const vk::VulkanImage& hdr_src) {
    const uint32_t w = std::max(1u, m_swapchain.extent().width / 2);
    const uint32_t h = std::max(1u, m_swapchain.extent().height / 2);
    VkViewport vp = make_viewport(float(w), float(h));
    VkRect2D sc = make_scissor(w, h);
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    auto run_pass = [&](VkFramebuffer fb, const vk::VulkanImage& input, const VkPipeline pipe, VkPipelineLayout layout, const glm::vec4& push_value) {
        VkDescriptorSet set = m_desc.alloc_single_sampler_set(m_ctx.device(), input.view, input.sampler);
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_bloom_pass;
        rp.framebuffer = fb;
        rp.renderArea.extent = {w, h};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
        Vec4Push push{};
        push.value = push_value;
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    };

    vk::transition_image_layout(cmd, m_bloom_a.image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    run_pass(m_bloom_a_fb, hdr_src, m_bright_pipe, m_bright_layout, {m_settings.bloom_threshold, 0.0f, 0.0f, 0.0f});

    vk::transition_image_layout(cmd, m_bloom_b.image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    run_pass(m_bloom_b_fb, m_bloom_a, m_blur_pipe, m_blur_layout, {1.0f / float(w), 0.0f, 0.0f, 0.0f});

    vk::transition_image_layout(cmd, m_bloom_a.image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    run_pass(m_bloom_a_fb, m_bloom_b, m_blur_pipe, m_blur_layout, {0.0f, 1.0f / float(h), 0.0f, 0.0f});
}

void VulkanRenderer::record_tonemap_pass_(VkCommandBuffer cmd, uint32_t image_idx, const vk::VulkanImage& hdr_src) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = m_tonemap_pass;
    rp.framebuffer = m_tonemap_fbs[image_idx];
    rp.renderArea.extent = m_swapchain.extent();
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    VkDescriptorSet set = m_desc.alloc_post_set(
        m_ctx.device(),
        hdr_src.view, hdr_src.sampler,
        m_bloom_a.view, m_bloom_a.sampler);

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = make_viewport(float(m_swapchain.extent().width), float(m_swapchain.extent().height));
    VkRect2D sc = make_scissor(m_swapchain.extent().width, m_swapchain.extent().height);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemap_pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemap_layout, 0, 1, &set, 0, nullptr);
    Vec4Push push{};
    float bloom_blend = m_settings.bloom_enabled ? m_settings.bloom_intensity : 0.0f;
    push.value = {m_settings.exposure, bloom_blend, float(m_settings.tonemap_mode), 0.0f};
    vkCmdPushConstants(cmd, m_tonemap_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::record_imgui_pass_(VkCommandBuffer cmd, uint32_t image_idx) {
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = m_imgui_pass;
    rp.framebuffer = m_imgui_fbs[image_idx];
    rp.renderArea.extent = m_swapchain.extent();

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (m_imgui_initialized && ImGui::GetCurrentContext()) {
        ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data && draw_data->Valid) {
            ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
        }
    }
    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::record_ssao_passes_(VkCommandBuffer cmd, VkDescriptorSet frame_set) {
    const auto& s = m_settings;
    const float w = float(m_swapchain.extent().width);
    const float h = float(m_swapchain.extent().height);

    // --- Pass 1: SSAO (gbuf1 normals + depth + noise → m_ssao) ---
    {
        VkDescriptorSet ssao_input = m_desc.alloc_ssao_input_set(
            m_ctx.device(),
            m_gbuf1.view, m_gbuf1.sampler,
            m_hdr_depth.view, m_hdr_depth_sampler,
            m_ssao_noise.view, m_ssao_noise.sampler);

        VkClearValue clear{};
        clear.color = {{1.0f, 1.0f, 1.0f, 1.0f}};

        VkRenderPassBeginInfo rp{};
        rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass      = m_ssao_pass;
        rp.framebuffer     = m_ssao_fb;
        rp.renderArea.extent = m_swapchain.extent();
        rp.clearValueCount = 1;
        rp.pClearValues    = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp = make_viewport(w, h);
        VkRect2D   sc = make_scissor(m_swapchain.extent().width, m_swapchain.extent().height);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd,  0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssao_pipe);

        std::array<VkDescriptorSet, 2> sets = {frame_set, ssao_input};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssao_layout, 0,
                                (uint32_t)sets.size(), sets.data(), 0, nullptr);

        SSAOPush push{};
        push.params = {s.ssao_radius, s.ssao_bias, s.ssao_power, s.ssao_strength};
        push.screen = {w, h, w / 4.0f, h / 4.0f};
        vkCmdPushConstants(cmd, m_ssao_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        // m_ssao is now SHADER_READ_ONLY_OPTIMAL (render pass finalLayout)
    }

    // --- Pass 2: Blur (m_ssao → m_ssao_blur) ---
    {
        VkDescriptorSet blur_set = m_desc.alloc_single_sampler_set(
            m_ctx.device(), m_ssao.view, m_ssao.sampler);

        VkClearValue clear{};
        clear.color = {{1.0f, 1.0f, 1.0f, 1.0f}};

        VkRenderPassBeginInfo rp{};
        rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass      = m_ssao_pass;
        rp.framebuffer     = m_ssao_blur_fb;
        rp.renderArea.extent = m_swapchain.extent();
        rp.clearValueCount = 1;
        rp.pClearValues    = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp = make_viewport(w, h);
        VkRect2D   sc = make_scissor(m_swapchain.extent().width, m_swapchain.extent().height);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd,  0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssao_blur_pipe);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssao_blur_layout, 0,
                                1, &blur_set, 0, nullptr);

        Vec4Push blur_push{};
        blur_push.value = {1.0f / w, 1.0f / h, 0.0f, 0.0f};
        vkCmdPushConstants(cmd, m_ssao_blur_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blur_push), &blur_push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        // m_ssao_blur is now SHADER_READ_ONLY_OPTIMAL (render pass finalLayout)
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IBL helpers
// ─────────────────────────────────────────────────────────────────────────────

bool VulkanRenderer::create_ibl_resources_() {
    VkDevice dev = m_ctx.device();

    constexpr uint32_t SKY_SIZE  = 512;
    constexpr uint32_t IRR_SIZE  = 32;
    constexpr uint32_t PREF_SIZE = 128;
    constexpr uint32_t LUT_SIZE  = 512;
    constexpr uint32_t PREF_MIPS = 5;

    // Cubemaps: create_cubemap_image(ctx, face_size, mips, format)
    m_ibl_sky        = vk::create_cubemap_image(m_ctx, SKY_SIZE,  1,         VK_FORMAT_R16G16B16A16_SFLOAT);
    m_ibl_irradiance = vk::create_cubemap_image(m_ctx, IRR_SIZE,  1,         VK_FORMAT_R16G16B16A16_SFLOAT);
    m_ibl_prefilter  = vk::create_cubemap_image(m_ctx, PREF_SIZE, PREF_MIPS, VK_FORMAT_R16G16B16A16_SFLOAT);

    // BRDF LUT: 2D colour attachment (ctx, w, h, format, usage, aspect)
    m_ibl_brdf_lut = vk::create_attachment(m_ctx, LUT_SIZE, LUT_SIZE,
        VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    // Fallback 1x1 black cubemap
    m_ibl_fallback_cube = vk::create_cubemap_image(m_ctx, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT);

    // Samplers
    m_ibl_cube_sampler      = vk::create_ibl_sampler(dev, 1);
    m_ibl_prefilter_sampler = vk::create_ibl_sampler(dev, PREF_MIPS);
    m_ibl_lut_sampler       = vk::create_ibl_sampler(dev, 1);

    // Per-face views: create_cubemap_face_view(device, image, format, layer_index, mip_level)
    for (int f = 0; f < 6; ++f) {
        m_ibl_sky_face_views[f] = vk::create_cubemap_face_view(dev, m_ibl_sky.image,
            VK_FORMAT_R16G16B16A16_SFLOAT, f, 0);
        m_ibl_irr_face_views[f] = vk::create_cubemap_face_view(dev, m_ibl_irradiance.image,
            VK_FORMAT_R16G16B16A16_SFLOAT, f, 0);
        for (uint32_t m = 0; m < PREF_MIPS; ++m) {
            m_ibl_pref_face_views[f][m] = vk::create_cubemap_face_view(dev, m_ibl_prefilter.image,
                VK_FORMAT_R16G16B16A16_SFLOAT, f, m);
        }
    }

    // Render passes
    m_ibl_sky_rp  = vk::create_ibl_render_pass(dev, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_ibl_irr_rp  = vk::create_ibl_render_pass(dev, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_ibl_pref_rp = vk::create_ibl_render_pass(dev, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_ibl_lut_rp  = vk::create_ibl_render_pass(dev, VK_FORMAT_R16G16_SFLOAT);

    // Transition fallback cubemap to SHADER_READ_ONLY_OPTIMAL
    {
        VkCommandBuffer cmd = m_ctx.begin_single_cmd();
        vk::transition_cubemap_layout(cmd, m_ibl_fallback_cube.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_ctx.end_single_cmd(cmd);
    }

    // Assign shared samplers for descriptor writes (no ownership transfer)
    m_ibl_irradiance.sampler    = m_ibl_cube_sampler;
    m_ibl_prefilter.sampler     = m_ibl_prefilter_sampler;
    m_ibl_brdf_lut.sampler      = m_ibl_lut_sampler;
    m_ibl_fallback_cube.sampler = m_ibl_cube_sampler;

    return true;
}

bool VulkanRenderer::create_ibl_pipelines_() {
    VkDevice dev = m_ctx.device();

    // Helper: build pipeline layout + pipeline for a fullscreen IBL pass
    auto make_pipe = [&](
        const uint32_t* frag_spv, uint32_t frag_bytes,
        uint32_t push_size,
        VkRenderPass rp,
        bool has_input_sampler,   // true = set 0 = single_sampler_layout
        VkPipelineLayout& out_layout,
        VkPipeline& out_pipe)
    {
        // Build layout
        vk::LayoutDesc ld;
        if (has_input_sampler)
            ld.set_layouts.push_back(m_desc.single_sampler_layout());
        if (push_size > 0) {
            VkPushConstantRange pc{};
            pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pc.size = push_size;
            ld.push_ranges.push_back(pc);
        }
        out_layout = vk::build_pipeline_layout(dev, ld);

        // Build pipeline (fullscreen triangle, no vertex input, no depth)
        vk::PipelineDesc pd;
        pd.vert_code  = fullscreen_vert_glsl;
        pd.vert_size  = sizeof(fullscreen_vert_glsl);
        pd.frag_code  = frag_spv;
        pd.frag_size  = frag_bytes;
        pd.depth_test = false;
        pd.depth_write = false;
        pd.cull_mode  = VK_CULL_MODE_NONE;
        pd.layout     = out_layout;
        pd.render_pass = rp;
        out_pipe = vk::build_pipeline(dev, pd);
    };

    make_pipe(ibl_sky_capture_frag_glsl, sizeof(ibl_sky_capture_frag_glsl),
              sizeof(IBLSkyPush), m_ibl_sky_rp, false,
              m_ibl_sky_layout, m_ibl_sky_pipe);

    make_pipe(ibl_irradiance_frag_glsl, sizeof(ibl_irradiance_frag_glsl),
              sizeof(IBLCubePush), m_ibl_irr_rp, true,
              m_ibl_irr_layout, m_ibl_irr_pipe);

    make_pipe(ibl_prefilter_frag_glsl, sizeof(ibl_prefilter_frag_glsl),
              sizeof(IBLCubePush), m_ibl_pref_rp, true,
              m_ibl_pref_layout, m_ibl_pref_pipe);

    make_pipe(ibl_brdf_lut_frag_glsl, sizeof(ibl_brdf_lut_frag_glsl),
              0, m_ibl_lut_rp, false,
              m_ibl_lut_layout, m_ibl_lut_pipe);

    return true;
}

void VulkanRenderer::rebuild_ibl_() {
    VkDevice dev = m_ctx.device();
    constexpr uint32_t SKY_SIZE  = 512;
    constexpr uint32_t IRR_SIZE  = 32;
    constexpr uint32_t PREF_SIZE = 128;
    constexpr uint32_t LUT_SIZE  = 512;
    constexpr uint32_t PREF_MIPS = 5;

    std::vector<VkFramebuffer> temp_fbs;
    VkCommandBuffer cmd = m_ctx.begin_single_cmd();

    // ── Transition sky faces to COLOR_ATTACHMENT_OPTIMAL ──────────────────
    {
        VkImageLayout src_layout = m_ibl_ready
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
        vk::transition_cubemap_layout(cmd, m_ibl_sky.image, src_layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    // ── Render sky per face ────────────────────────────────────────────────
    for (int f = 0; f < 6; ++f) {
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = m_ibl_sky_rp;
        fci.attachmentCount = 1;
        fci.pAttachments = &m_ibl_sky_face_views[f];
        fci.width = SKY_SIZE; fci.height = SKY_SIZE; fci.layers = 1;
        VkFramebuffer fb; vkCreateFramebuffer(dev, &fci, nullptr, &fb);
        temp_fbs.push_back(fb);

        VkClearValue cv{}; cv.color = {{0,0,0,1}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = m_ibl_sky_rp; rp.framebuffer = fb;
        rp.renderArea.extent = {SKY_SIZE, SKY_SIZE};
        rp.clearValueCount = 1; rp.pClearValues = &cv;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp = make_viewport(float(SKY_SIZE), float(SKY_SIZE));
        VkRect2D sc   = make_scissor(SKY_SIZE, SKY_SIZE);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd,  0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ibl_sky_pipe);

        IBLSkyPush push{};
        push.sun_dir   = glm::vec4(m_sky_sun_dir, 0.0f);
        push.zenith    = glm::vec4(m_sky_zenith, 0.0f);
        push.horizon   = glm::vec4(m_sky_horizon, 0.0f);
        push.sun_color = glm::vec4(m_sky_sun_color, m_sky_sun_cos_r);
        push.face      = f;
        vkCmdPushConstants(cmd, m_ibl_sky_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
    // Transition sky to SHADER_READ_ONLY for irradiance/prefilter input
    vk::transition_cubemap_layout(cmd, m_ibl_sky.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // ── Sky descriptor set for irradiance/prefilter ────────────────────────
    VkDescriptorSet sky_set = m_desc.alloc_single_sampler_set(
        dev, m_ibl_sky.view, m_ibl_cube_sampler);

    // ── Irradiance ─────────────────────────────────────────────────────────
    {
        VkImageLayout src_layout = m_ibl_ready
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
        vk::transition_cubemap_layout(cmd, m_ibl_irradiance.image, src_layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        for (int f = 0; f < 6; ++f) {
            VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fci.renderPass = m_ibl_irr_rp;
            fci.attachmentCount = 1;
            fci.pAttachments = &m_ibl_irr_face_views[f];
            fci.width = IRR_SIZE; fci.height = IRR_SIZE; fci.layers = 1;
            VkFramebuffer fb; vkCreateFramebuffer(dev, &fci, nullptr, &fb);
            temp_fbs.push_back(fb);

            VkClearValue cv{}; cv.color = {{0,0,0,1}};
            VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rp.renderPass = m_ibl_irr_rp; rp.framebuffer = fb;
            rp.renderArea.extent = {IRR_SIZE, IRR_SIZE};
            rp.clearValueCount = 1; rp.pClearValues = &cv;
            vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport vp = make_viewport(float(IRR_SIZE), float(IRR_SIZE));
            VkRect2D sc   = make_scissor(IRR_SIZE, IRR_SIZE);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd,  0, 1, &sc);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ibl_irr_pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_ibl_irr_layout, 0, 1, &sky_set, 0, nullptr);

            IBLCubePush push{}; push.face = f; push.roughness = 0.0f;
            vkCmdPushConstants(cmd, m_ibl_irr_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(push), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
        }
        vk::transition_cubemap_layout(cmd, m_ibl_irradiance.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // ── Prefiltered specular ───────────────────────────────────────────────
    {
        VkImageLayout src_layout = m_ibl_ready
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
        vk::transition_cubemap_layout(cmd, m_ibl_prefilter.image, src_layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, PREF_MIPS);

        for (uint32_t mip = 0; mip < PREF_MIPS; ++mip) {
            float roughness = float(mip) / float(PREF_MIPS - 1);
            uint32_t mip_size = std::max(1u, PREF_SIZE >> mip);
            for (int f = 0; f < 6; ++f) {
                VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                fci.renderPass = m_ibl_pref_rp;
                fci.attachmentCount = 1;
                fci.pAttachments = &m_ibl_pref_face_views[f][mip];
                fci.width = mip_size; fci.height = mip_size; fci.layers = 1;
                VkFramebuffer fb; vkCreateFramebuffer(dev, &fci, nullptr, &fb);
                temp_fbs.push_back(fb);

                VkClearValue cv{}; cv.color = {{0,0,0,1}};
                VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                rp.renderPass = m_ibl_pref_rp; rp.framebuffer = fb;
                rp.renderArea.extent = {mip_size, mip_size};
                rp.clearValueCount = 1; rp.pClearValues = &cv;
                vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport vp = make_viewport(float(mip_size), float(mip_size));
                VkRect2D sc   = make_scissor(mip_size, mip_size);
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd,  0, 1, &sc);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ibl_pref_pipe);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_ibl_pref_layout, 0, 1, &sky_set, 0, nullptr);

                IBLCubePush push{}; push.face = f; push.roughness = roughness;
                vkCmdPushConstants(cmd, m_ibl_pref_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(push), &push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd);
            }
        }
        vk::transition_cubemap_layout(cmd, m_ibl_prefilter.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, PREF_MIPS);
    }

    // ── BRDF LUT ───────────────────────────────────────────────────────────
    {
        VkImageLayout lut_src = m_ibl_ready
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // create_attachment leaves it here

        if (m_ibl_ready) {
            // Transition back to COLOR_ATTACHMENT for re-render
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.image = m_ibl_brdf_lut.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);
        }

        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = m_ibl_lut_rp;
        fci.attachmentCount = 1;
        fci.pAttachments = &m_ibl_brdf_lut.view;
        fci.width = LUT_SIZE; fci.height = LUT_SIZE; fci.layers = 1;
        VkFramebuffer fb; vkCreateFramebuffer(dev, &fci, nullptr, &fb);
        temp_fbs.push_back(fb);

        VkClearValue cv{}; cv.color = {{0,0,0,1}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = m_ibl_lut_rp; rp.framebuffer = fb;
        rp.renderArea.extent = {LUT_SIZE, LUT_SIZE};
        rp.clearValueCount = 1; rp.pClearValues = &cv;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp = make_viewport(float(LUT_SIZE), float(LUT_SIZE));
        VkRect2D sc   = make_scissor(LUT_SIZE, LUT_SIZE);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd,  0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ibl_lut_pipe);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        // Transition LUT to SHADER_READ_ONLY
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.image = m_ibl_brdf_lut.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    }

    m_ctx.end_single_cmd(cmd); // implicit queue wait

    // Destroy temp framebuffers
    for (auto fb : temp_fbs) vkDestroyFramebuffer(dev, fb, nullptr);

    m_ibl_ready = true;
    SOL_INFO("IBL cubemaps rebuilt");
}

void VulkanRenderer::destroy_ibl_() {
    VkDevice dev = m_ctx.device();
    if (!dev) return;

    vkDeviceWaitIdle(dev);

    // Destroy per-face views
    for (int f = 0; f < 6; ++f) {
        if (m_ibl_sky_face_views[f])  vkDestroyImageView(dev, m_ibl_sky_face_views[f], nullptr);
        if (m_ibl_irr_face_views[f])  vkDestroyImageView(dev, m_ibl_irr_face_views[f], nullptr);
        for (int m = 0; m < 5; ++m) {
            if (m_ibl_pref_face_views[f][m]) vkDestroyImageView(dev, m_ibl_pref_face_views[f][m], nullptr);
        }
    }
    std::memset(m_ibl_sky_face_views,  0, sizeof(m_ibl_sky_face_views));
    std::memset(m_ibl_irr_face_views,  0, sizeof(m_ibl_irr_face_views));
    std::memset(m_ibl_pref_face_views, 0, sizeof(m_ibl_pref_face_views));

    // Null out shared sampler pointers BEFORE destroy() to avoid double-free
    m_ibl_irradiance.sampler    = VK_NULL_HANDLE;
    m_ibl_prefilter.sampler     = VK_NULL_HANDLE;
    m_ibl_brdf_lut.sampler      = VK_NULL_HANDLE;
    m_ibl_fallback_cube.sampler = VK_NULL_HANDLE;

    m_ibl_sky.destroy(dev, m_ctx.allocator());
    m_ibl_irradiance.destroy(dev, m_ctx.allocator());
    m_ibl_prefilter.destroy(dev, m_ctx.allocator());
    m_ibl_brdf_lut.destroy(dev, m_ctx.allocator());
    m_ibl_fallback_cube.destroy(dev, m_ctx.allocator());

    if (m_ibl_cube_sampler)      { vkDestroySampler(dev, m_ibl_cube_sampler, nullptr);      m_ibl_cube_sampler = VK_NULL_HANDLE; }
    if (m_ibl_prefilter_sampler) { vkDestroySampler(dev, m_ibl_prefilter_sampler, nullptr); m_ibl_prefilter_sampler = VK_NULL_HANDLE; }
    if (m_ibl_lut_sampler)       { vkDestroySampler(dev, m_ibl_lut_sampler, nullptr);       m_ibl_lut_sampler = VK_NULL_HANDLE; }

    if (m_ibl_sky_rp)  { vkDestroyRenderPass(dev, m_ibl_sky_rp,  nullptr); m_ibl_sky_rp  = VK_NULL_HANDLE; }
    if (m_ibl_irr_rp)  { vkDestroyRenderPass(dev, m_ibl_irr_rp,  nullptr); m_ibl_irr_rp  = VK_NULL_HANDLE; }
    if (m_ibl_pref_rp) { vkDestroyRenderPass(dev, m_ibl_pref_rp, nullptr); m_ibl_pref_rp = VK_NULL_HANDLE; }
    if (m_ibl_lut_rp)  { vkDestroyRenderPass(dev, m_ibl_lut_rp,  nullptr); m_ibl_lut_rp  = VK_NULL_HANDLE; }

    if (m_ibl_sky_pipe)  { vkDestroyPipeline(dev, m_ibl_sky_pipe,  nullptr); m_ibl_sky_pipe  = VK_NULL_HANDLE; }
    if (m_ibl_irr_pipe)  { vkDestroyPipeline(dev, m_ibl_irr_pipe,  nullptr); m_ibl_irr_pipe  = VK_NULL_HANDLE; }
    if (m_ibl_pref_pipe) { vkDestroyPipeline(dev, m_ibl_pref_pipe, nullptr); m_ibl_pref_pipe = VK_NULL_HANDLE; }
    if (m_ibl_lut_pipe)  { vkDestroyPipeline(dev, m_ibl_lut_pipe,  nullptr); m_ibl_lut_pipe  = VK_NULL_HANDLE; }

    if (m_ibl_sky_layout)  { vkDestroyPipelineLayout(dev, m_ibl_sky_layout,  nullptr); m_ibl_sky_layout  = VK_NULL_HANDLE; }
    if (m_ibl_irr_layout)  { vkDestroyPipelineLayout(dev, m_ibl_irr_layout,  nullptr); m_ibl_irr_layout  = VK_NULL_HANDLE; }
    if (m_ibl_pref_layout) { vkDestroyPipelineLayout(dev, m_ibl_pref_layout, nullptr); m_ibl_pref_layout = VK_NULL_HANDLE; }
    if (m_ibl_lut_layout)  { vkDestroyPipelineLayout(dev, m_ibl_lut_layout,  nullptr); m_ibl_lut_layout  = VK_NULL_HANDLE; }

    m_ibl_ready = false;
}

} // namespace sol
