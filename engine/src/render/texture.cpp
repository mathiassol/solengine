#include "sol/render/texture.h"
#include "sol/log.h"

#include <bgfx/bgfx.h>

namespace sol {

Texture::~Texture() { destroy_(); }

Texture::Texture(Texture&& o) noexcept
    : m_handle_idx(o.m_handle_idx), m_w(o.m_w), m_h(o.m_h) {
    o.m_handle_idx = 0xffff;
    o.m_w = o.m_h = 0;
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        destroy_();
        m_handle_idx = o.m_handle_idx;
        m_w = o.m_w; m_h = o.m_h;
        o.m_handle_idx = 0xffff;
        o.m_w = o.m_h = 0;
    }
    return *this;
}

void Texture::destroy_() {
    if (m_handle_idx != 0xffff) {
        bgfx::TextureHandle h{m_handle_idx};
        bgfx::destroy(h);
        m_handle_idx = 0xffff;
    }
    m_w = m_h = 0;
}

Texture Texture::from_rgba8(const void* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) {
        SOL_ERROR("Texture::from_rgba8: invalid args");
        return {};
    }
    const bgfx::Memory* mem = bgfx::copy(pixels, uint32_t(w * h * 4));
    bgfx::TextureHandle th = bgfx::createTexture2D(
        (uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8, 0, mem);
    if (!bgfx::isValid(th)) {
        SOL_ERROR("Texture::from_rgba8: bgfx createTexture2D failed");
        return {};
    }
    Texture t;
    t.m_handle_idx = th.idx;
    t.m_w = w; t.m_h = h;
    return t;
}

} // namespace sol
