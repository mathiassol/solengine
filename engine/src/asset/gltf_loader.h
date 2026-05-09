#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include "sol/render/texture.h"

namespace sol {

struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent {1.0f, 0.0f, 0.0f, 1.0f};
};

struct GltfMesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t>   indices;
    std::string             name;
    glm::mat4               node_transform {1.0f};  // accumulated from GLTF node hierarchy
    glm::vec4               base_color {1.0f, 1.0f, 1.0f, 1.0f};
    float                   metallic   = 0.0f;
    float                   roughness  = 0.5f;
    glm::vec3               emissive   {0.0f, 0.0f, 0.0f};
    bool                    double_sided = false;
    int                     albedo_tex   = -1;  // index into GltfModel::textures, -1 = none
    int                     normal_tex   = -1;
    int                     mr_tex       = -1;
    int                     emissive_tex = -1;
};

struct GltfModel {
    std::string            path;
    std::vector<GltfMesh>  meshes;
    std::vector<Texture>   textures;  // decoded GPU textures (RGBA8)
};

class GltfLoader {
public:
    std::shared_ptr<GltfModel> load(const std::string& path);
};

} // namespace sol
