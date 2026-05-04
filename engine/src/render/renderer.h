#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace sol {
class Window;

class Renderer {
public:
    bool init(Window& window);
    void shutdown();

    void begin_frame();
    void end_frame();

    void resize(int w, int h);

    uint16_t width()  const { return m_w; }
    uint16_t height() const { return m_h; }

private:
    uint16_t m_w = 0, m_h = 0;
    bool     m_initialized = false;
};
} // namespace sol
