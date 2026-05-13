#include "sol/render/texture.h"
#include "sol/log.h"
#include "render/vk/vk_renderer.h"

namespace sol {

Texture::~Texture() { destroy_(); }

Texture::Texture(Texture&& o) noexcept
    : m_gpu(o.m_gpu), m_w(o.m_w), m_h(o.m_h) {
    o.m_gpu = nullptr;
    o.m_w = 0;
    o.m_h = 0;
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        destroy_();
        m_gpu = o.m_gpu;
        m_w = o.m_w;
        m_h = o.m_h;
        o.m_gpu = nullptr;
        o.m_w = 0;
        o.m_h = 0;
    }
    return *this;
}

void Texture::destroy_() {
    if (m_gpu) {
        if (auto* renderer = VulkanRenderer::get()) renderer->free_texture(m_gpu);
        m_gpu = nullptr;
    }
    m_w = 0;
    m_h = 0;
}

Texture Texture::from_rgba8(const void* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) {
        SOL_ERROR("Texture::from_rgba8: invalid args");
        return {};
    }

    auto* renderer = VulkanRenderer::get();
    if (!renderer) {
        SOL_ERROR("Texture::from_rgba8: Vulkan renderer not initialized");
        return {};
    }

    Texture t;
    t.m_gpu = renderer->alloc_texture(pixels, w, h);
    if (!t.m_gpu) {
        SOL_ERROR("Texture::from_rgba8: Vulkan texture allocation failed");
        return {};
    }

    t.m_w = w;
    t.m_h = h;
    return t;
}

} // namespace sol
