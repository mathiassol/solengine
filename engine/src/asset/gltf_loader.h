#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace sol {

struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t>   indices;
    std::string             name;
};

struct GltfModel {
    std::string         path;
    std::vector<Mesh>   meshes;
};

class GltfLoader {
public:
    // Loads a .glb or .gltf file from disk. Returns nullptr on failure.
    std::shared_ptr<GltfModel> load(const std::string& path);
};

} // namespace sol
