#pragma once
#include "sol/scene/node3d.h"
#include "sol/render/mesh.h"
#include "sol/render/material.h"
#include <string>
#include <memory>

namespace sol {

// Renders a mesh at this node's world transform.
// mesh_name: "cube", "sphere", "plane", or an asset path.
class SOL_API MeshNode : public Node3D {
public:
    std::string mesh_name {"cube"};
    Material    material;

    const char* type_name() const override { return "MeshNode"; }

    void on_ready (Engine& engine) override;
    void on_render(Engine& engine, const glm::mat4& world_xform) override;

    Mesh* mesh() const { return m_mesh.get(); }

private:
    std::shared_ptr<Mesh> m_mesh;
};

} // namespace sol
