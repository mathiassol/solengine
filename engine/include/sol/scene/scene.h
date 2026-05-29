#pragma once
#include "sol/export.h"
#include "sol/scene/node.h"
#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace sol {

class Engine;
class Camera3D;
class DirectionalLight;

class SOL_API Scene {
public:
    std::string name;
    std::string path;   // file the scene was loaded from
    std::string hdr_sky; // optional HDR sky path (relative to CWD); empty = use default

    Node* root() const { return m_root.get(); }
    void  set_root(std::unique_ptr<Node> root) { m_root = std::move(root); }

    // Detach the root (for SceneInstance to adopt it).
    std::unique_ptr<Node> detach_root() { return std::move(m_root); }

    // Called once after load to initialise all nodes (Jolt bodies, GPU meshes…).
    void on_ready  (Engine& engine);
    void on_destroy(Engine& engine);

    // Per-frame: update physics nodes + call on_update on all nodes.
    void update(Engine& engine, float dt);

    // Phased update — call update_pre_physics, step Jolt, then update_post_physics.
    void update_pre_physics (Engine& engine, float dt);
    void update_post_physics(Engine& engine, float dt);

    // Per-frame: set camera + light on the renderer, then traverse + draw.
    void render(Engine& engine);

    // --- Serialisation ---
    // Load a scene from a JSON .solscene file.
    static std::unique_ptr<Scene> load(const std::string& path, Engine& engine);

    // Save to a JSON .solscene file.
    bool save(const std::string& path) const;

    // In-memory serialisation helpers (for editor tab state snapshots).
    static std::unique_ptr<Scene> load_from_string(const std::string& json_str, Engine& engine);
    std::string save_to_string() const;

    // Find the active Camera3D and the first DirectionalLight.
    Camera3D*       active_camera()    const;
    DirectionalLight* active_light()   const;

    // ---- Deferred command queue -----------------------------------------
    // Queue a lambda to execute at the end of the current update() call.
    // Safe to call during node/component traversal; the lambda runs after the
    // full tree walk completes.  Any commands enqueued inside the lambda are
    // also flushed before update() returns (loop-safe).
    //
    // Use this instead of direct add_child / remove_child during on_update to
    // avoid iterator invalidation.
    void defer(std::function<void()> cmd);

    // Spawn a new node as child of parent and call on_ready. Deferred-safe.
    void spawn_node(Node* parent, std::unique_ptr<Node> child, Engine& engine);
    // Remove and destroy a node. Deferred-safe.
    void despawn_node(Node* node, Engine& engine);

private:
    std::unique_ptr<Node> m_root;
    std::vector<std::function<void()>> m_deferred;

    void flush_deferred();
    void ready_node      (Engine& engine, Node* node);
    void destroy_node    (Engine& engine, Node* node);
    void update_node_phase(Engine& engine, Node* node, float dt, bool pre_physics);
    void render_node     (Engine& engine, Node* node, const glm::mat4& parent_xform);
};

} // namespace sol
