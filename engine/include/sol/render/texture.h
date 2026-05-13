#pragma once

#include "sol/export.h"

#include <cstddef>
#include <cstdint>

namespace sol {

// 2D texture. RAII; move-only.
class SOL_API Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;

    // Create from raw RGBA8 pixels (row-major, no row padding).
    static Texture from_rgba8(const void* pixels, int width, int height);

    bool     valid()  const { return m_gpu != nullptr; }
    void*    gpu_data() const { return m_gpu; }
    int      width()  const { return m_w; }
    int      height() const { return m_h; }

private:
    void*    m_gpu = nullptr;
    int      m_w = 0, m_h = 0;

    void destroy_();
};

} // namespace sol
