#pragma once
#include "sol/scene/node3d.h"
#include "sol/render/mesh.h"
#include "sol/render/material.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace sol {

struct GltfModel;  // forward-declare to avoid including internal header

// Loads a GLB/GLTF file and renders all its meshes.
// 'path' is relative to the process working directory.
class SOL_API ModelNode : public Node3D {
public:
    std::string path;

    const char* type_name() const override { return "ModelNode"; }
    void on_ready (Engine& engine) override;
    void on_render(Engine& engine, const glm::mat4& world_xform) override;

private:
    struct SubMesh {
        std::shared_ptr<Mesh> mesh;
        Material              mat;
        glm::mat4             node_xform {1.0f};  // from GLTF node hierarchy
    };
    std::vector<SubMesh>  m_submeshes;
    std::shared_ptr<void> m_model_ref;  // keeps GltfModel (and its textures) alive
};

} // namespace sol
