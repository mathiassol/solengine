#include "sol/render/mesh.h"
#include "sol/log.h"

#include <bgfx/bgfx.h>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <utility>

namespace sol {

namespace {

const bgfx::VertexLayout& vertex_layout() {
    static bgfx::VertexLayout layout = []() {
        bgfx::VertexLayout l;
        l.begin()
         .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
         .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
         .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
         .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
         .add(bgfx::Attrib::Tangent,   4, bgfx::AttribType::Float)
         .end();
        return l;
    }();
    return layout;
}

} // namespace

const bgfx::VertexLayout& sol_default_vertex_layout() { return vertex_layout(); }

Mesh::~Mesh() { destroy_(); }

Mesh::Mesh(Mesh&& o) noexcept
    : m_vbh_idx(o.m_vbh_idx), m_ibh_idx(o.m_ibh_idx),
      m_vertex_count(o.m_vertex_count), m_index_count(o.m_index_count) {
    o.m_vbh_idx = 0xffff; o.m_ibh_idx = 0xffff;
    o.m_vertex_count = 0;  o.m_index_count  = 0;
}

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        destroy_();
        m_vbh_idx = o.m_vbh_idx; m_ibh_idx = o.m_ibh_idx;
        m_vertex_count = o.m_vertex_count; m_index_count = o.m_index_count;
        o.m_vbh_idx = 0xffff; o.m_ibh_idx = 0xffff;
        o.m_vertex_count = 0;  o.m_index_count  = 0;
    }
    return *this;
}

void Mesh::destroy_() {
    if (m_vbh_idx != 0xffff) { bgfx::VertexBufferHandle h{m_vbh_idx}; bgfx::destroy(h); m_vbh_idx = 0xffff; }
    if (m_ibh_idx != 0xffff) { bgfx::IndexBufferHandle  h{m_ibh_idx}; bgfx::destroy(h); m_ibh_idx = 0xffff; }
    m_vertex_count = 0; m_index_count = 0;
}

Mesh Mesh::create(const Vertex* verts, size_t vcount,
                  const uint32_t* indices, size_t icount) {
    if (!verts || !indices || vcount == 0 || icount == 0) {
        SOL_ERROR("Mesh::create: empty input"); return {};
    }
    const bgfx::Memory* vmem = bgfx::copy(verts,   uint32_t(vcount * sizeof(Vertex)));
    const bgfx::Memory* imem = bgfx::copy(indices, uint32_t(icount * sizeof(uint32_t)));
    bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(vmem, vertex_layout());
    bgfx::IndexBufferHandle  ibh = bgfx::createIndexBuffer (imem, BGFX_BUFFER_INDEX32);
    if (!bgfx::isValid(vbh) || !bgfx::isValid(ibh)) {
        if (bgfx::isValid(vbh)) bgfx::destroy(vbh);
        if (bgfx::isValid(ibh)) bgfx::destroy(ibh);
        SOL_ERROR("Mesh::create: bgfx buffer creation failed"); return {};
    }
    Mesh m;
    m.m_vbh_idx = vbh.idx; m.m_ibh_idx = ibh.idx;
    m.m_vertex_count = (uint32_t)vcount; m.m_index_count = (uint32_t)icount;
    return m;
}

namespace primitives {

Mesh make_cube(float size) {
    const float h = size * 0.5f;
    // 6 faces × 4 verts — tangents computed from UV direction per face
    // Face tangents (xyz=tangent dir, w=bitangent sign):
    //   +X: (0,0,1,1)   -X: (0,0,-1,1)
    //   +Y: (1,0,0,1)   -Y: (1,0,0,1)
    //   +Z: (-1,0,0,1)  -Z: (1,0,0,1)
    const Vertex v[] = {
        // +X
        {{ h,-h,-h},{ 1,0,0},{0,1},0xffffffff,{0,0, 1,1}},
        {{ h, h,-h},{ 1,0,0},{0,0},0xffffffff,{0,0, 1,1}},
        {{ h, h, h},{ 1,0,0},{1,0},0xffffffff,{0,0, 1,1}},
        {{ h,-h, h},{ 1,0,0},{1,1},0xffffffff,{0,0, 1,1}},
        // -X
        {{-h,-h, h},{-1,0,0},{0,1},0xffffffff,{0,0,-1,1}},
        {{-h, h, h},{-1,0,0},{0,0},0xffffffff,{0,0,-1,1}},
        {{-h, h,-h},{-1,0,0},{1,0},0xffffffff,{0,0,-1,1}},
        {{-h,-h,-h},{-1,0,0},{1,1},0xffffffff,{0,0,-1,1}},
        // +Y
        {{-h, h,-h},{0, 1,0},{0,1},0xffffffff,{1,0,0,1}},
        {{-h, h, h},{0, 1,0},{0,0},0xffffffff,{1,0,0,1}},
        {{ h, h, h},{0, 1,0},{1,0},0xffffffff,{1,0,0,1}},
        {{ h, h,-h},{0, 1,0},{1,1},0xffffffff,{1,0,0,1}},
        // -Y
        {{-h,-h, h},{0,-1,0},{0,1},0xffffffff,{1,0,0,1}},
        {{-h,-h,-h},{0,-1,0},{0,0},0xffffffff,{1,0,0,1}},
        {{ h,-h,-h},{0,-1,0},{1,0},0xffffffff,{1,0,0,1}},
        {{ h,-h, h},{0,-1,0},{1,1},0xffffffff,{1,0,0,1}},
        // +Z
        {{ h,-h, h},{0,0, 1},{0,1},0xffffffff,{-1,0,0,1}},
        {{ h, h, h},{0,0, 1},{0,0},0xffffffff,{-1,0,0,1}},
        {{-h, h, h},{0,0, 1},{1,0},0xffffffff,{-1,0,0,1}},
        {{-h,-h, h},{0,0, 1},{1,1},0xffffffff,{-1,0,0,1}},
        // -Z
        {{-h,-h,-h},{0,0,-1},{0,1},0xffffffff,{1,0,0,1}},
        {{-h, h,-h},{0,0,-1},{0,0},0xffffffff,{1,0,0,1}},
        {{ h, h,-h},{0,0,-1},{1,0},0xffffffff,{1,0,0,1}},
        {{ h,-h,-h},{0,0,-1},{1,1},0xffffffff,{1,0,0,1}},
    };
    std::vector<uint32_t> idx;
    idx.reserve(36);
    for (uint32_t f = 0; f < 6; ++f) {
        const uint32_t b = f * 4;
        idx.push_back(b+0); idx.push_back(b+2); idx.push_back(b+1);
        idx.push_back(b+0); idx.push_back(b+3); idx.push_back(b+2);
    }
    return Mesh::create(v, sizeof(v)/sizeof(v[0]), idx.data(), idx.size());
}

Mesh make_plane(float size, int subdivisions) {
    if (subdivisions < 1) subdivisions = 1;
    const int   n = subdivisions + 1;
    const float h = size * 0.5f;
    std::vector<Vertex> verts;
    verts.reserve(n * n);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float fx = (float)x / (float)subdivisions;
            const float fy = (float)y / (float)subdivisions;
            Vertex vv;
            vv.position = {fx * size - h, 0.0f, fy * size - h};
            vv.normal   = {0.0f, 1.0f, 0.0f};
            vv.uv       = {fx, fy};
            vv.tangent  = {1.0f, 0.0f, 0.0f, 1.0f}; // U = +X
            verts.push_back(vv);
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(subdivisions * subdivisions * 6);
    for (int y = 0; y < subdivisions; ++y) {
        for (int x = 0; x < subdivisions; ++x) {
            const uint32_t i0 = y * n + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + n;
            const uint32_t i3 = i2 + 1;
            idx.push_back(i0); idx.push_back(i1); idx.push_back(i2);
            idx.push_back(i1); idx.push_back(i3); idx.push_back(i2);
        }
    }
    return Mesh::create(verts, idx);
}

Mesh make_sphere(float radius, int slices, int stacks) {
    if (slices < 3) slices = 3;
    if (stacks < 2) stacks = 2;
    std::vector<Vertex> verts;
    verts.reserve((stacks + 1) * (slices + 1));
    for (int i = 0; i <= stacks; ++i) {
        const float v   = (float)i / (float)stacks;
        const float phi = v * glm::pi<float>();
        const float sp  = std::sin(phi);
        const float cp  = std::cos(phi);
        for (int j = 0; j <= slices; ++j) {
            const float u  = (float)j / (float)slices;
            const float th = u * glm::two_pi<float>();
            const float st = std::sin(th);
            const float ct = std::cos(th);
            glm::vec3 n{sp * ct, cp, sp * st};
            Vertex vv;
            vv.position = n * radius;
            vv.normal   = n;
            vv.uv       = {u, v};
            // Tangent = dPos/dTheta normalized: (-sin(th), 0, cos(th))
            // (U direction = increasing longitude)
            if (sp > 0.001f)
                vv.tangent = {-st, 0.0f, ct, 1.0f};
            else
                vv.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
            verts.push_back(vv);
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(stacks * slices * 6);
    const int row = slices + 1;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t a = i * row + j;
            const uint32_t b = a + 1;
            const uint32_t c = a + row;
            const uint32_t d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    }
    return Mesh::create(verts, idx);
}

} // namespace primitives
} // namespace sol
