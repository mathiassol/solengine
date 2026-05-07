#pragma once
#include "sol/export.h"
#include "sol/scene/node.h"
#include <string>
#include <memory>

namespace sol {

class Engine;
class Camera3D;
class DirectionalLight;

class SOL_API Scene {
public:
    std::string name;
    std::string path;   // file the scene was loaded from

    Node* root() const { return m_root.get(); }
    void  set_root(std::unique_ptr<Node> root) { m_root = std::move(root); }

    // Detach the root (for SceneInstance to adopt it).
    std::unique_ptr<Node> detach_root() { return std::move(m_root); }

    // Called once after load to initialise all nodes (Jolt bodies, GPU meshes…).
    void on_ready  (Engine& engine);
    void on_destroy(Engine& engine);

    // Per-frame: update physics nodes + call on_update on all nodes.
    void update(Engine& engine, float dt);

    // Per-frame: set camera + light on the renderer, then traverse + draw.
    void render(Engine& engine);

    // --- Serialisation ---
    // Load a scene from a JSON .solscene file.
    static std::unique_ptr<Scene> load(const std::string& path, Engine& engine);

    // Save to a JSON .solscene file.
    bool save(const std::string& path) const;

    // Find the active Camera3D and the first DirectionalLight.
    Camera3D*       active_camera()    const;
    DirectionalLight* active_light()   const;

private:
    std::unique_ptr<Node> m_root;

    void ready_node (Engine& engine, Node* node);
    void destroy_node(Engine& engine, Node* node);
    void update_node (Engine& engine, Node* node, float dt);
    void render_node (Engine& engine, Node* node, const glm::mat4& parent_xform);
};

} // namespace sol
