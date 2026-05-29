#pragma once

#include "sol/export.h"
#include "sol/api.h"
#include "sol/ecs_world.h"
#include "sol/scene/node_handle.h"
#include "sol/render/camera.h"
#include "sol/render/material.h"
#include "sol/render/mesh.h"
#include "sol/render/renderer.h"
#include "sol/render/texture.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/model_node.h"
#include "sol/world/world_partition.h"  // Stage 3-B
#include "sol/audio/audio_engine.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <functional>
#include <memory>
#include <string>

struct GLFWwindow;

namespace sol {

class Window;
class Renderer;
class PhysicsWorld;
class ImGuiLayer;
class ModelLoader;
class SceneManager;
class ScriptEngine;
class AudioEngine;
class InputManager;

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

    // Editor-mode init: no GLFW window; embeds Vulkan + editor ImGui into a Win32 HWND.
    bool init_for_editor(void* hwnd, void* hinstance, int w, int h);

    // Single-frame tick for editor use. Call after init_for_editor().
    void tick_one_frame(float dt,
                        std::function<void()> on_update,
                        std::function<void()> on_render);

    // Inject a custom renderer backend before calling init().
    // Engine takes ownership.  If not called, the default bgfx renderer is used.
    void set_renderer(std::unique_ptr<Renderer> r) { m_renderer = std::move(r); }

    // Run the main loop with a loaded game.
    int  run(const SolGameApi& game);

    // Subsystem access (lifetimes owned by Engine).
    entt::registry& registry()     { return m_registry; }
    EcsWorld&       ecs_world()    { return m_ecs_world; }
    NodeRegistry&   node_registry(){ return m_node_registry; }
    Window&         window()       { return *m_window; }
    Renderer&       renderer()     { return *m_renderer; }
    PhysicsWorld&   physics()      { return *m_physics; }
    ImGuiLayer&     imgui()        { return *m_imgui; }
    ImGuiLayer*     imgui_ptr()    const { return m_imgui.get(); }
    ModelLoader&     assets()       { return *m_assets; }
    SceneManager&   scene_manager(){ return *m_scene_manager; }
    ScriptEngine&   script()       { return *m_script; }
    const ScriptEngine& script() const { return *m_script; }
    bool            has_script()   const { return static_cast<bool>(m_script); }
    AudioEngine&    audio()        { return *m_audio; }
    const AudioEngine& audio() const { return *m_audio; }

    InputManager&   input()        { return *m_input; }
    const InputManager& input() const { return *m_input; }

    // Stage 3-B: world partition data + streaming config.
    WorldPartition& world_partition() { return m_world_partition; }
    const WorldPartition& world_partition() const { return m_world_partition; }

    float delta_time()    const { return m_dt; }
    float elapsed_time()  const { return m_elapsed; }
    bool  running()       const { return m_running; }
    bool  is_editor_mode()const { return m_editor_mode; }
    void  quit() { m_running = false; }

    // Input helpers — pass GLFW_KEY_* / GLFW_MOUSE_BUTTON_* constants.
    bool          key_down          (int glfw_key)    const;
    bool          mouse_button_down (int glfw_button) const;
    void          cursor_position   (double& x, double& y) const;
    GLFWwindow*   native_window     ()                const;
    void          set_cursor_captured(bool captured)  const;

    // Returns the active ImGui context so game DLLs (which have their own
    // statically-linked copy of ImGui) can call ImGui::SetCurrentContext().
    ImGuiContext* imgui_context() const;

    // Self-test: exercises every subsystem and returns true on success.
    // Used by `sol --selftest` and as the init demo.
    bool self_test();

private:
    EngineConfig                    m_cfg;
    entt::registry                  m_registry;
    EcsWorld                        m_ecs_world;
    NodeRegistry                    m_node_registry;
    std::unique_ptr<Window>         m_window;
    std::unique_ptr<Renderer>       m_renderer;
    std::unique_ptr<PhysicsWorld>   m_physics;
    std::unique_ptr<ImGuiLayer>     m_imgui;
    std::unique_ptr<ModelLoader>     m_assets;
    std::unique_ptr<SceneManager>   m_scene_manager;
    std::unique_ptr<ScriptEngine>   m_script;
    std::unique_ptr<AudioEngine>    m_audio;
    std::unique_ptr<InputManager>   m_input;
    WorldPartition                  m_world_partition;  // Stage 3-B
    float                           m_dt          = 0.0f;
    float                           m_elapsed     = 0.0f;
    bool                            m_running     = false;
    bool                            m_editor_mode = false;
};

} // namespace sol
