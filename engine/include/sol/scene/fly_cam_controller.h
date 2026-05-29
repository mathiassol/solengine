#pragma once
#include "sol/scene/node3d.h"
#include "sol/render/camera.h"
#include <string>
#include <vector>

namespace sol {

// Data-driven fly camera + model-cycling controller.
//
// Drop this node into any scene as a direct child of the root.
// It drives the renderer camera each frame and optionally cycles through
// all .glb / .gltf files found in `models_dir`.
//
// Controls:
//   WASD / Q E     — fly forward / back / strafe / up / down
//   Left-click     — capture mouse for look
//   Escape         — release mouse
//   Mouse (locked) — look
//   Arrow keys     — look (always, even without mouse lock)
//   N / P          — next / previous model
//   Tab            — toggle HUD overlay
//
// Serialisable JSON fields:
//   yaw, pitch, position  — initial camera state
//   move_speed            — m/s  (default 5)
//   mouse_sens            — deg / pixel (default 0.15)
//   key_look_spd          — deg / s via arrow keys (default 60)
//   fov                   — vertical FOV in degrees (default 60)
//   near_clip, far_clip   — clipping planes
//   models_dir            — directory to scan (default "models")
//   show_ui               — show HUD overlay (default true)

class SOL_API FlyCamController : public Node3D {
public:
    // Serialisable fields
    float       yaw          =  180.0f;   // heading, degrees
    float       pitch        =  -10.0f;   // elevation, degrees
    float       move_speed   =    5.0f;   // m / s
    float       mouse_sens   =    0.15f;  // deg / pixel
    float       key_look_spd =   60.0f;   // deg / s
    float       fov          =   60.0f;   // vertical FOV, degrees
    float       near_clip    =    0.05f;
    float       far_clip     = 1000.0f;
    std::string models_dir   = "models";
    bool        show_ui      = true;

    // Optional list of .solscene files prepended to the model cycle.
    // Cycling with N/P goes: extra_scenes[0], extra_scenes[1], ..., model[0], model[1], ...
    std::vector<std::string> extra_scenes;

    const char* type_name() const override { return "FlyCamController"; }
    bool        is_clone()  const { return m_is_clone; }

    void on_ready  (Engine& engine)          override;
    void on_update (Engine& engine, float dt) override;
    void on_destroy(Engine& engine)          override;

private:
    // Runtime state — not serialised
    std::vector<std::string> m_models;
    int    m_model_idx      = 0;
    bool   m_mouse_captured = false;
    bool   m_first_mouse    = true;
    double m_prev_mx = 0.0, m_prev_my = 0.0;
    bool   m_is_clone       = false;  // true when created by load_current_model (skip initial load)

    // Input debounce
    float m_scene_cooldown = 0.0f;  // seconds remaining after a scene swap

    // Edge-detect flags
    bool m_tab_prev = false, m_esc_prev = false;
    bool m_n_prev   = false, m_p_prev   = false;
    bool m_lmb_prev = false;
    bool m_show_render_settings = false;

    void      scan_models();
    void      load_current_entry(Engine& engine);
    void      load_model_entry  (Engine& engine);
    void      load_scene_entry  (Engine& engine);
    std::unique_ptr<FlyCamController> clone_self_() const;
    glm::vec3 cam_forward() const;
    Camera    make_camera()  const;
    void      draw_hud(Engine& engine);
};

} // namespace sol
