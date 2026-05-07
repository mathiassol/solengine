#pragma once
#include "sol/export.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sol {

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal  {0.0f, 1.0f, 0.0f};
    glm::vec2 uv      {0.0f};
    uint32_t  color = 0xffffffff; // RGBA8 packed
    glm::vec4 tangent {1.0f, 0.0f, 0.0f, 1.0f}; // xyz=tangent, w=bitangent sign
};

class SOL_API Mesh {
public:
    Mesh() = default;
    ~Mesh();
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;

    static Mesh create(const Vertex* verts, size_t vertex_count,
                       const uint32_t* indices, size_t index_count);
    static Mesh create(const std::vector<Vertex>& verts,
                       const std::vector<uint32_t>& indices) {
        return create(verts.data(), verts.size(), indices.data(), indices.size());
    }

    bool     valid()        const { return m_index_count > 0; }
    uint32_t index_count()  const { return m_index_count; }
    uint32_t vertex_count() const { return m_vertex_count; }
    uint16_t vbh_idx()      const { return m_vbh_idx; }
    uint16_t ibh_idx()      const { return m_ibh_idx; }

private:
    uint16_t m_vbh_idx      = 0xffff;
    uint16_t m_ibh_idx      = 0xffff;
    uint32_t m_vertex_count = 0;
    uint32_t m_index_count  = 0;
    void destroy_();
};

namespace primitives {
SOL_API Mesh make_cube  (float size = 1.0f);
SOL_API Mesh make_plane (float size = 1.0f, int subdivisions = 1);
SOL_API Mesh make_sphere(float radius = 0.5f, int slices = 24, int stacks = 16);
} // namespace primitives

} // namespace sol
