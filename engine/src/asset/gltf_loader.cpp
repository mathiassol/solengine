#include "asset/gltf_loader.h"
#include "sol/log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstring>

namespace sol {

static void read_floats(const cgltf_accessor* acc, float* out, size_t comps) {
    if (!acc) return;
    for (size_t i = 0; i < acc->count; ++i)
        cgltf_accessor_read_float(acc, i, out + i * comps, comps);
}

std::shared_ptr<GltfModel> GltfLoader::load(const std::string& path) {
    cgltf_options opts{};
    cgltf_data* data = nullptr;

    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success) {
        SOL_ERROR(std::string("gltf parse failed: ") + path);
        return nullptr;
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        SOL_ERROR(std::string("gltf load buffers failed: ") + path);
        cgltf_free(data);
        return nullptr;
    }

    auto model = std::make_shared<GltfModel>();
    model->path = path;

    for (size_t mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& gm = data->meshes[mi];
        for (size_t pi = 0; pi < gm.primitives_count; ++pi) {
            const cgltf_primitive& prim = gm.primitives[pi];
            Mesh mesh;
            mesh.name = gm.name ? gm.name : "mesh";

            const cgltf_accessor* pos_acc = nullptr;
            const cgltf_accessor* nrm_acc = nullptr;
            const cgltf_accessor* uv_acc  = nullptr;
            for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
                const auto& at = prim.attributes[ai];
                if      (at.type == cgltf_attribute_type_position) pos_acc = at.data;
                else if (at.type == cgltf_attribute_type_normal)   nrm_acc = at.data;
                else if (at.type == cgltf_attribute_type_texcoord) uv_acc  = at.data;
            }
            if (!pos_acc) continue;

            const size_t vcount = pos_acc->count;
            mesh.vertices.resize(vcount);
            std::vector<float> tmp(vcount * 3, 0.0f);
            read_floats(pos_acc, tmp.data(), 3);
            for (size_t i = 0; i < vcount; ++i)
                mesh.vertices[i].position = { tmp[i*3], tmp[i*3+1], tmp[i*3+2] };
            if (nrm_acc && nrm_acc->count == vcount) {
                read_floats(nrm_acc, tmp.data(), 3);
                for (size_t i = 0; i < vcount; ++i)
                    mesh.vertices[i].normal = { tmp[i*3], tmp[i*3+1], tmp[i*3+2] };
            }
            if (uv_acc && uv_acc->count == vcount) {
                std::vector<float> uvs(vcount * 2, 0.0f);
                read_floats(uv_acc, uvs.data(), 2);
                for (size_t i = 0; i < vcount; ++i)
                    mesh.vertices[i].uv = { uvs[i*2], uvs[i*2+1] };
            }

            if (prim.indices) {
                mesh.indices.resize(prim.indices->count);
                for (size_t i = 0; i < prim.indices->count; ++i)
                    mesh.indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
            } else {
                mesh.indices.resize(vcount);
                for (uint32_t i = 0; i < (uint32_t)vcount; ++i) mesh.indices[i] = i;
            }
            model->meshes.push_back(std::move(mesh));
        }
    }

    cgltf_free(data);
    SOL_INFO(std::string("Loaded GLTF: ") + path);
    return model;
}

} // namespace sol
