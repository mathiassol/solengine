#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

#include "sol/export.h"
#include "sol/render/camera.h"
#include "sol/render/light.h"

namespace sol {
class Window;
class Mesh;
class Material;

// Abstract renderer interface.
// Default implementation is BgfxRenderer (created by Engine::init()).
// Swap by calling engine.set_renderer(std::make_unique<MyRenderer>()) before init().
class SOL_API Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool init    (Window& window)                                  = 0;
    virtual void shutdown()                                                = 0;
    virtual void begin_frame()                                             = 0;
    virtual void end_frame()                                               = 0;
    virtual void resize  (int w, int h)                                    = 0;

    // Submit a draw call between begin_frame() / end_frame().
    virtual void submit  (const Mesh& mesh, const Material& mat,
                          const glm::mat4& transform)                      = 0;

    // --- Per-frame configuration ---
    void set_camera     (const Camera& cam)    { m_camera    = cam; }
    void set_clear_color(uint32_t rgba)        { m_clear     = rgba; }

    // Convenience: submit a single directional light (backward compat).
    // Games that only need one sun-style light can call this instead of submit_light().
    void set_light_dir(const glm::vec3& dir) {
        Light l;
        l.type      = LightType::Directional;
        l.direction = glm::normalize(dir);
        l.color     = {1.0f, 1.0f, 1.0f};
        l.intensity = 1.0f;
        l.cast_shadow = false;
        submit_light(l);
    }

    // Submit a light for the current frame. Call between begin_frame / end_frame.
    // Lights are cleared automatically at begin_frame().
    void submit_light(const Light& light) {
        if (m_lights.size() < 8) m_lights.push_back(light);
    }

    // Ambient light color (applied to all PBR surfaces).
    void set_ambient(const glm::vec3& color) { m_ambient = color; }

    uint16_t      width ()  const { return m_w; }
    uint16_t      height()  const { return m_h; }
    const Camera& camera()  const { return m_camera; }

protected:
    void clear_lights_() { m_lights.clear(); }

    uint16_t           m_w       = 0;
    uint16_t           m_h       = 0;
    uint32_t           m_clear   = 0x101820ff;
    Camera             m_camera  {};
    glm::vec3          m_ambient {0.05f, 0.05f, 0.08f};
    std::vector<Light> m_lights;
};

} // namespace sol
