#pragma once
#include "sol/export.h"
#include "sol/engine.h"
#include "sol/project/project.h"
#include "sol/scene/node.h"
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace sol {

enum class EditorGizmoMode {
    Translate,
    Rotate,
    Scale
};

enum class EditorCameraAction {
    Forward,
    Backward,
    Left,
    Right,
    Up,
    Down,
    Fast
};

// High-level project runner. Wraps Engine with project loading, scene management,
// and a single-frame tick API usable by both the runtime and the Qt editor.
class SOL_API EngineHost {
public:
    EngineHost();
    ~EngineHost();

    // Load project.sol from project_dir, init engine subsystems.
    // Changes cwd to project_dir so relative asset paths resolve correctly.
    bool open(const std::string& project_dir = ".");

    // Editor-mode open: embed renderer into the supplied Win32 HWND.
    // Does not create a GLFW window.
    bool open_for_editor(void* hwnd, void* hinstance, int w, int h,
                         const std::string& project_dir = ".");

    // Blocking main loop: loads main_scene, pumps frames until window closed.
    // Returns 0 on clean exit.
    int run();

    // Run with a game API (DLL backward-compat). Does NOT auto-load main_scene.
    int run_game_api(const SolGameApi& api);

    // Single-frame tick for editor use (driven by QTimer / external loop).
    // Must call open() or open_for_editor() first. dt = seconds since last tick.
    void tick(float dt);

    // Notify the engine that the viewport was resized.
    void resize(int w, int h);

    // --- Editor camera (overrides scene camera when active) ---
    void set_editor_camera(const glm::vec3& pos, float yaw_deg, float pitch_deg);
    void set_editor_camera_active(bool active);
    bool is_editor_camera_active() const;
    glm::vec3 editor_camera_pos() const;
    float editor_camera_yaw() const;
    float editor_camera_pitch() const;

    // --- Selection / Gizmo ---
    void set_selected_node(Node* node);
    Node* selected_node() const;
    void set_gizmo_operation(int op); // 0=TRANSLATE, 1=ROTATE, 2=SCALE

    // --- Ray picking ---
    Node* pick_node(float screen_x, float screen_y) const;

    // --- ImGui IO feeding (call from Qt event handlers) ---
    void imgui_mouse_pos(float x, float y);
    void imgui_mouse_button(int button, bool pressed);  // 0=left, 1=right, 2=middle
    void imgui_mouse_wheel(float delta);
    void imgui_viewport_size(int w, int h);

    // Legacy editor helpers kept for existing callers.
    void set_editor_viewport_size(int w, int h);
    void set_editor_fly_camera(bool active);
    void set_editor_camera_action(EditorCameraAction action, bool active);
    void add_editor_mouse_delta(float dx, float dy);

    void set_imgui_focused(bool focused);
    void set_imgui_mouse_pos(float x, float y);
    void set_imgui_mouse_button(int button, bool down);
    void add_imgui_mouse_wheel(float wheel_x, float wheel_y);
    void set_imgui_key(ImGuiKey key, bool down);
    void clear_editor_input();

    void set_gizmo_mode(EditorGizmoMode mode);
    EditorGizmoMode gizmo_mode() const;

    // Graceful shutdown.
    void close();

    bool                 is_open() const;
    Engine&              engine();
    const ProjectConfig& project() const;

    // Hot-swap the current scene (safe to call mid-tick).
    bool load_scene(const std::string& scene_path);

    // Returns the root node of the current active scene, or null if no scene loaded.
    Node* scene_root() const;

    // Instantiate a ModelNode from a .glb path (relative to project dir).
    // Adds it as a child of `parent` (or scene root if null), calls on_ready.
    // Returns the new node, or null on failure.
    Node* instantiate_model(const std::string& path, Node* parent = nullptr);

    // Save the current scene to its current path or the provided path.
    bool save_scene(const std::string& path = "");

    // Write a named field on a node through the component registry.
    void set_field(Node* node, const std::string& field_name, const void* data);

private:
    void render_editor_gizmo_();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sol
