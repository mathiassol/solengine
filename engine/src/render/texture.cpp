#include "sol/render/texture.h"
#include "sol/log.h"

#include <bgfx/bgfx.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>

#include <vector>
#include <algorithm>
#include <cstring>

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

    // Count mip levels and total byte size
    int mipCount = 0;
    uint32_t totalBytes = 0;
    {
        int mw = w, mh = h;
        while (true) {
            totalBytes += (uint32_t)(mw * mh * 4);
            ++mipCount;
            if (mw == 1 && mh == 1) break;
            mw = std::max(1, mw / 2);
            mh = std::max(1, mh / 2);
        }
    }

    const bgfx::Memory* mem = bgfx::alloc(totalBytes);

    // Copy mip 0
    std::memcpy(mem->data, pixels, (size_t)w * h * 4);

    // Generate successive mip levels using box filter
    const uint8_t* src     = static_cast<const uint8_t*>(pixels);
    uint8_t*       dst     = mem->data + (size_t)w * h * 4;
    int sw = w, sh = h;
    while (sw > 1 || sh > 1) {
        int dw = std::max(1, sw / 2);
        int dh = std::max(1, sh / 2);
        stbir_resize_uint8(src, sw, sh, 0, dst, dw, dh, 0, 4);
        src = dst;
        dst += (size_t)dw * dh * 4;
        sw = dw; sh = dh;
    }

    bgfx::TextureHandle th = bgfx::createTexture2D(
        (uint16_t)w, (uint16_t)h,
        /*hasMips=*/mipCount > 1,
        /*numLayers=*/1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC,
        mem);
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
