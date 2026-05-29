#pragma once
#include "sol/scene/node3d.h"
#include "sol/render/mesh.h"
#include "sol/render/material.h"
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace sol {

struct GltfModel;

class SOL_API MeshNode : public Node3D {
public:
    // Primitive mesh type: "cube", "sphere", "plane", "capsule"
    // OR leave empty and set mesh_path for a .glb file
    std::string mesh_name {"cube"};

    // If non-empty: path to a .glb file (relative to working dir).
    // Takes precedence over mesh_name.
    std::string mesh_path;

    // Collision: if true, a static Jolt box body is created on ready,
    // sized to scale * 0.5 (unit-cube half-extents scaled by node scale).
    bool collision_enabled {true};

    Material material;

    const char* type_name() const override { return "MeshNode"; }

    void on_ready  (Engine& engine) override;
    void on_render (Engine& engine, const glm::mat4& world_xform) override;
    void on_destroy(Engine& engine) override;

    Mesh* mesh() const { return m_mesh.get(); }

    // Material texture override paths (relative to project root)
    std::string mat_albedo_path;
    std::string mat_normal_path;
    std::string mat_mr_path;
    std::string mat_emissive_path;

    // Reload owned textures from mat_*_path fields. No-op for empty paths.
    void apply_material_textures(Engine& engine);

    // Sub-mesh access (for GLB files loaded via mesh_path)
    int      submesh_count()              const { return (int)m_submeshes.size(); }
    Material* submesh_material(int idx)         { return (idx>=0 && idx<(int)m_submeshes.size()) ? &m_submeshes[idx].mat : nullptr; }
    const Material* submesh_material(int idx) const { return (idx>=0 && idx<(int)m_submeshes.size()) ? &m_submeshes[idx].mat : nullptr; }

private:
    // Primitive mesh path
    std::shared_ptr<Mesh> m_mesh;

    // GLB path — uses ModelNode-style sub-mesh list
    struct SubMesh {
        std::shared_ptr<Mesh> mesh;
        Material              mat;
        glm::mat4             node_xform {1.0f};
    };
    std::vector<SubMesh>  m_submeshes;
    std::shared_ptr<void> m_model_ref;

    // Physics
    std::vector<uint32_t> m_body_ids;

    // Owned textures loaded from mat_*_path fields
    std::shared_ptr<Texture> m_owned_albedo;
    std::shared_ptr<Texture> m_owned_normal;
    std::shared_ptr<Texture> m_owned_mr;
    std::shared_ptr<Texture> m_owned_emissive;
};

} // namespace sol
