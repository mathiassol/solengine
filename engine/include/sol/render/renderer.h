#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "sol/export.h"
#include "sol/render/camera.h"
#include "sol/render/light.h"
#include "sol/render/render_settings.h"

namespace sol {
class Window;
class Mesh;
struct Material;

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

    // Set sky/atmosphere parameters. sun_direction = normalized vector toward the sun.
    void set_sky(const glm::vec3& sun_dir,
                 const glm::vec3& zenith        = {0.08f, 0.15f, 0.40f},
                 const glm::vec3& horizon       = {0.50f, 0.60f, 0.70f},
                 const glm::vec3& sun_color     = {3.0f,  2.5f,  2.0f},
                 float            sun_cos_radius = 0.9997f) {
        m_sky_sun_dir   = glm::normalize(sun_dir);
        m_sky_zenith    = zenith;
        m_sky_horizon   = horizon;
        m_sky_sun_color = sun_color;
        m_sky_sun_cos_r = sun_cos_radius;
        m_has_sky       = true;
    }

    // Set full procedural atmospheric sky parameters.
    // sun_dir = normalised direction toward the sun.
    void set_sky_atmo(const glm::vec3& sun_dir,
                      float turbidity        = 2.5f,
                      float sun_intensity    = 20.0f,
                      float rayleigh_scale   = 1.0f,
                      float mie_strength     = 1.0f,
                      float sun_bloom        = 50.0f,
                      float night_brightness = 1.0f,
                      float sky_exposure     = 1.0f,
                      float sun_disk_deg     = 0.53f,
                      const glm::vec3& sky_tint       = {1.0f, 1.0f, 1.0f},
                      const glm::vec3& sun_color_tint = {1.0f, 1.0f, 1.0f},
                      const glm::vec3& night_color    = {0.001f, 0.002f, 0.008f},
                      float star_density     = 0.003f,
                      float star_brightness  = 2.5f,
                      const glm::vec3& ground_color   = {0.03f, 0.025f, 0.02f},
                      float time             = 0.0f) {
        m_sky_sun_dir          = glm::normalize(sun_dir);
        m_sky_turbidity        = turbidity;
        m_sky_sun_intensity    = sun_intensity;
        m_sky_rayleigh_scale   = rayleigh_scale;
        m_sky_mie_strength     = mie_strength;
        m_sky_sun_bloom        = sun_bloom;
        m_sky_night_brightness = night_brightness;
        m_sky_sky_exposure     = sky_exposure;
        m_sky_sun_cos_r        = std::cos(glm::radians(sun_disk_deg));
        m_sky_tint             = sky_tint;
        m_sky_sun_tint         = sun_color_tint;
        m_sky_night_color      = night_color;
        m_sky_star_density     = star_density;
        m_sky_star_brightness  = star_brightness;
        m_sky_ground_color     = ground_color;
        m_sky_time             = time;
        m_has_sky              = true;
    }

    // Set the HDR environment map for sky background and IBL source.
    // path is relative to the working directory. Empty string disables HDR sky.
    virtual void set_hdr_sky(const std::string& /*path*/) {}
    virtual int draw_call_count() const { return 0; }

    // Block until all in-flight GPU work is complete.
    // Call before destroying GPU resources (e.g. on scene swap) to prevent hazards.
    virtual void wait_idle() {}

    uint16_t      width ()  const { return m_w; }
    uint16_t      height()  const { return m_h; }
    const Camera& camera()  const { return m_camera; }

    RenderSettings&       settings()       { return m_settings; }
    const RenderSettings& settings() const { return m_settings; }

protected:
    void clear_lights_() { m_lights.clear(); }
    void clear_sky_()    { m_has_sky = false; }

    uint16_t           m_w       = 0;
    uint16_t           m_h       = 0;
    uint32_t           m_clear   = 0x101820ff;
    Camera             m_camera  {};
    glm::vec3          m_ambient {0.05f, 0.05f, 0.08f};
    std::vector<Light> m_lights;
    RenderSettings     m_settings;

    glm::vec3 m_sky_sun_dir   {0.0f, 1.0f, 0.0f};
    glm::vec3 m_sky_zenith    {0.08f, 0.15f, 0.40f};
    glm::vec3 m_sky_horizon   {0.50f, 0.60f, 0.70f};
    glm::vec3 m_sky_sun_color {3.0f, 2.5f, 2.0f};
    float     m_sky_sun_cos_r = 0.9997f;
    bool      m_has_sky       = false;

    // Atmospheric sky parameters (procedural sky)
    float m_sky_sun_intensity    = 20.0f;
    float m_sky_turbidity        = 2.5f;
    float m_sky_rayleigh_scale   = 1.0f;
    float m_sky_mie_strength     = 1.0f;
    float m_sky_sun_bloom        = 50.0f;
    float m_sky_night_brightness = 1.0f;
    float m_sky_sky_exposure     = 1.0f;
    glm::vec3 m_sky_tint         {1.0f, 1.0f, 1.0f};
    glm::vec3 m_sky_sun_tint     {1.0f, 1.0f, 1.0f};
    glm::vec3 m_sky_night_color  {0.001f, 0.002f, 0.008f};
    float m_sky_star_density     = 0.003f;
    float m_sky_star_brightness  = 2.5f;
    glm::vec3 m_sky_ground_color {0.03f, 0.025f, 0.02f};
    float m_sky_time             = 0.0f;
};

} // namespace sol
