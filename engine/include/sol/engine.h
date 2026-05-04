#pragma once

#include "sol/export.h"
#include "sol/api.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <string>

struct GLFWwindow;

namespace sol {

class Window;
class Renderer;
class PhysicsWorld;
class ImGuiLayer;
class GltfLoader;

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

    // Run the main loop with a loaded game.
    int  run(const SolGameApi& game);

    // Subsystem access (lifetimes owned by Engine).
    entt::registry& registry()  { return m_registry; }
    Window&         window()    { return *m_window; }
    Renderer&       renderer()  { return *m_renderer; }
    PhysicsWorld&   physics()   { return *m_physics; }
    ImGuiLayer&     imgui()     { return *m_imgui; }
    GltfLoader&     assets()    { return *m_assets; }

    float delta_time() const { return m_dt; }
    bool  running()    const { return m_running; }
    void  quit() { m_running = false; }

    // Self-test: exercises every subsystem and returns true on success.
    // Used by `sol --selftest` and as the init demo.
    bool self_test();

private:
    EngineConfig                  m_cfg;
    entt::registry                m_registry;
    std::unique_ptr<Window>       m_window;
    std::unique_ptr<Renderer>     m_renderer;
    std::unique_ptr<PhysicsWorld> m_physics;
    std::unique_ptr<ImGuiLayer>   m_imgui;
    std::unique_ptr<GltfLoader>   m_assets;
    float                         m_dt      = 0.0f;
    bool                          m_running = false;
};

} // namespace sol
