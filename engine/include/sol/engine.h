#pragma once

#include "sol/export.h"
#include "sol/api.h"
#include "sol/render/camera.h"
#include "sol/render/material.h"
#include "sol/render/mesh.h"
#include "sol/render/renderer.h"
#include "sol/render/texture.h"
#include "sol/scene/scene_manager.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <memory>
#include <string>

struct GLFWwindow;

namespace sol {

class Window;
class Renderer;
class PhysicsWorld;
class ImGuiLayer;
class GltfLoader;
class SceneManager;

struct EngineConfig {
    std::string title  = "Sol Engine";
    int         width  = 1280;
    int         height = 720;
    bool        vsync  = true;
};

// Single facade exposed to game DLLs.
class SOL_API Engine {
public:
    Engine();
    ~Engine();

    bool init(const EngineConfig& cfg);
    void shutdown();

    // Inject a custom renderer backend before calling init().
    // Engine takes ownership.  If not called, the default bgfx renderer is used.
    void set_renderer(std::unique_ptr<Renderer> r) { m_renderer = std::move(r); }

    // Run the main loop with a loaded game.
    int  run(const SolGameApi& game);

    // Subsystem access (lifetimes owned by Engine).
    entt::registry& registry()     { return m_registry; }
    Window&         window()       { return *m_window; }
    Renderer&       renderer()     { return *m_renderer; }
    PhysicsWorld&   physics()      { return *m_physics; }
    ImGuiLayer&     imgui()        { return *m_imgui; }
    GltfLoader&     assets()       { return *m_assets; }
    SceneManager&   scene_manager(){ return *m_scene_manager; }

    float delta_time() const { return m_dt; }
    bool  running()    const { return m_running; }
    void  quit() { m_running = false; }

    // Input helpers — pass GLFW_KEY_* / GLFW_MOUSE_BUTTON_* constants.
    SOL_API bool          key_down          (int glfw_key)    const;
    SOL_API bool          mouse_button_down (int glfw_button) const;
    SOL_API void          cursor_position   (double& x, double& y) const;
    SOL_API GLFWwindow*   native_window     ()                const;
    SOL_API void          set_cursor_captured(bool captured)  const;

    // Returns the active ImGui context so game DLLs (which have their own
    // statically-linked copy of ImGui) can call ImGui::SetCurrentContext().
    SOL_API ImGuiContext* imgui_context() const;

    // Self-test: exercises every subsystem and returns true on success.
    // Used by `sol --selftest` and as the init demo.
    bool self_test();

private:
    EngineConfig                    m_cfg;
    entt::registry                  m_registry;
    std::unique_ptr<Window>         m_window;
    std::unique_ptr<Renderer>       m_renderer;
    std::unique_ptr<PhysicsWorld>   m_physics;
    std::unique_ptr<ImGuiLayer>     m_imgui;
    std::unique_ptr<GltfLoader>     m_assets;
    std::unique_ptr<SceneManager>   m_scene_manager;
    float                           m_dt      = 0.0f;
    bool                            m_running = false;
};

} // namespace sol
