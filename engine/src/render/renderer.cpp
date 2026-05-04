#include "render/renderer.h"
#include "platform/window.h"
#include "sol/log.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

namespace sol {

bool Renderer::init(Window& window) {
    bgfx::Init init;
    init.type     = bgfx::RendererType::Count; // auto-pick best
    init.vendorId = BGFX_PCI_ID_NONE;
    init.platformData.nwh  = window.native_handle();
    init.platformData.ndt  = window.native_display();
    init.resolution.width  = (uint32_t)window.width();
    init.resolution.height = (uint32_t)window.height();
    init.resolution.reset  = BGFX_RESET_VSYNC;

    if (!bgfx::init(init)) {
        SOL_ERROR("bgfx::init failed");
        return false;
    }

    m_w = (uint16_t)window.width();
    m_h = (uint16_t)window.height();

    bgfx::setViewClear(0,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        0x101820ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, m_w, m_h);

    m_initialized = true;
    SOL_INFO("Renderer initialized (bgfx)");
    return true;
}

void Renderer::shutdown() {
    if (m_initialized) {
        bgfx::shutdown();
        m_initialized = false;
    }
}

void Renderer::resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    m_w = (uint16_t)w; m_h = (uint16_t)h;
    bgfx::reset((uint32_t)w, (uint32_t)h, BGFX_RESET_VSYNC);
    bgfx::setViewRect(0, 0, 0, m_w, m_h);
}

void Renderer::begin_frame() {
    bgfx::touch(0);
    bgfx::dbgTextClear();
}

void Renderer::end_frame() {
    bgfx::frame();
}

} // namespace sol
