#pragma once
#include "sol/export.h"
#include "sol/engine.h"
#include "sol/project/project.h"
#include "sol/scene/node.h"
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <functional>

namespace sol {

class Node3D;

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
    void set_gizmo_visible(bool v);

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
    Node* create_node(const std::string& type, Node* parent = nullptr);
    bool remove_node(Node* node);
    void rename_node(Node* node, const std::string& new_name);
    float frame_fps() const;
    int   frame_draw_calls() const;
    void  set_gizmo_space(bool local);
    bool  gizmo_space_local() const;
    void  set_gizmo_undo_callback(std::function<void(Node3D*, glm::vec3, glm::vec3, glm::vec3,
                                                                glm::vec3, glm::vec3, glm::vec3)> cb);
    Node* duplicate_node(Node* node);

    // Reload material textures on a MeshNode from its mat_*_path fields.
    void apply_mesh_material_textures(Node* node);

    // Save the current scene to its current path or the provided path.
    bool save_scene(const std::string& path = "");

    // Serialise current scene to an in-memory JSON string (for tab state snapshots).
    std::string scene_state_to_string() const;

    // Restore a scene from a JSON string previously returned by scene_state_to_string().
    // original_path is stored back into scene->path so Ctrl+S still works.
    bool load_scene_from_string(const std::string& json_str, const std::string& original_path = "");

    // ── Hot Scene Slot API ─────────────────────────────────────────────────────
    // Each SceneEditor tab owns one slot.  All slots stay alive (on_ready'd) in
    // memory simultaneously; switching tabs only swaps scene pointers — no
    // on_destroy / on_ready overhead, no disk I/O.

    // Allocate a new slot.  The first call adopts the scene currently in the
    // SceneManager (it becomes the "active" slot).  Subsequent calls create an
    // empty slot.  Returns the slot ID (>= 0).
    int  create_scene_slot();

    // Destroy a slot.  If it is the active slot the scene is unloaded (on_destroy).
    // If it is an inactive slot the stored scene is destroyed via on_destroy.
    void destroy_scene_slot(int slot_id);

    // Load a scene file into a slot.
    // Active slot: deferred via request_scene (safe, normal load path).
    // Inactive slot: loads immediately, calls on_ready, stores in slot.
    bool load_scene_into_slot(int slot_id, const std::string& path);

    // Request switching to a slot at the start of the next tick.
    // Just swaps scene pointers — does NOT call on_ready / on_destroy.
    void activate_scene_slot(int slot_id);

    // Returns the currently active slot id (-1 if none).
    int  active_scene_slot() const;

    // Write a named field on a node through the component registry.
    void set_field(Node* node, const std::string& field_name, const void* data);
    void set_editor_draw_fn(std::function<void()> fn);
    void imgui_add_text(const char* utf8);

    // Returns the ImGuiContext* created by the engine DLL.
    // The editor EXE must call ImGui::SetCurrentContext() with this after
    // open_for_editor() to sync the EXE's static GImGui pointer.
    ImGuiContext* imgui_get_context() const;

private:
    void render_editor_gizmo_();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sol
