#include "asset/gltf_loader.h"
#include "sol/log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <unordered_map>

namespace sol {

static void read_floats(const cgltf_accessor* acc, float* out, size_t comps) {
    if (!acc) return;
    for (size_t i = 0; i < acc->count; ++i)
        cgltf_accessor_read_float(acc, i, out + i * comps, comps);
}

static glm::mat4 node_local_matrix(const cgltf_node* node) {
    if (node->has_matrix) {
        glm::mat4 m;
        std::memcpy(&m, node->matrix, sizeof(m));
        return m;
    }
    glm::vec3 t(0.0f), s(1.0f);
    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
    if (node->has_translation)
        t = {node->translation[0], node->translation[1], node->translation[2]};
    if (node->has_rotation)
        r = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
    if (node->has_scale)
        s = {node->scale[0], node->scale[1], node->scale[2]};
    return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}

static void process_node(
    const cgltf_node* node,
    const glm::mat4&  parent_xform,
    GltfModel&        out,
    const std::unordered_map<const cgltf_image*, int>& image_idx)
{
    glm::mat4 world = parent_xform * node_local_matrix(node);

    if (node->mesh) {
        const cgltf_mesh& gm = *node->mesh;
        for (size_t pi = 0; pi < gm.primitives_count; ++pi) {
            const cgltf_primitive& prim = gm.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            GltfMesh mesh;
            mesh.name           = gm.name ? gm.name : "mesh";
            mesh.node_transform = world;

            const cgltf_accessor* pos_acc = nullptr;
            const cgltf_accessor* nrm_acc = nullptr;
            const cgltf_accessor* uv_acc  = nullptr;
            const cgltf_accessor* tan_acc = nullptr;

            for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
                const auto& at = prim.attributes[ai];
                switch (at.type) {
                    case cgltf_attribute_type_position: pos_acc = at.data; break;
                    case cgltf_attribute_type_normal:   nrm_acc = at.data; break;
                    case cgltf_attribute_type_tangent:  tan_acc = at.data; break;
                    case cgltf_attribute_type_texcoord:
                        if (at.index == 0) uv_acc = at.data; break;
                    default: break;
                }
            }

            if (!pos_acc) continue;

            const size_t vc = pos_acc->count;
            mesh.vertices.resize(vc);

            {
                std::vector<float> tmp(vc * 3);
                read_floats(pos_acc, tmp.data(), 3);
                for (size_t i = 0; i < vc; ++i)
                    mesh.vertices[i].position = {tmp[i*3], tmp[i*3+1], tmp[i*3+2]};
            }

            if (nrm_acc && nrm_acc->count == vc) {
                std::vector<float> tmp(vc * 3);
                read_floats(nrm_acc, tmp.data(), 3);
                for (size_t i = 0; i < vc; ++i)
                    mesh.vertices[i].normal = {tmp[i*3], tmp[i*3+1], tmp[i*3+2]};
            }

            if (uv_acc && uv_acc->count == vc) {
                std::vector<float> tmp(vc * 2);
                read_floats(uv_acc, tmp.data(), 2);
                for (size_t i = 0; i < vc; ++i)
                    mesh.vertices[i].uv = {tmp[i*2], tmp[i*2+1]};
            }

            if (tan_acc && tan_acc->count == vc) {
                std::vector<float> tmp(vc * 4);
                read_floats(tan_acc, tmp.data(), 4);
                for (size_t i = 0; i < vc; ++i)
                    mesh.vertices[i].tangent = {tmp[i*4], tmp[i*4+1], tmp[i*4+2], tmp[i*4+3]};
            }

            if (prim.indices) {
                mesh.indices.resize(prim.indices->count);
                for (size_t i = 0; i < prim.indices->count; ++i)
                    mesh.indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
            } else {
                mesh.indices.resize(vc);
                for (uint32_t i = 0; i < (uint32_t)vc; ++i) mesh.indices[i] = i;
            }

            if (prim.material) {
                const auto& mat = *prim.material;
                const auto& pbr = mat.pbr_metallic_roughness;
                mesh.base_color  = {pbr.base_color_factor[0], pbr.base_color_factor[1],
                                    pbr.base_color_factor[2], pbr.base_color_factor[3]};
                mesh.metallic    = pbr.metallic_factor;
                mesh.roughness   = pbr.roughness_factor;
                mesh.double_sided = true; // GLB meshes are often viewed from inside (rooms/interiors); force double-sided for correct rendering under Vulkan Y-flip CW convention
                mesh.emissive = { mat.emissive_factor[0],
                                  mat.emissive_factor[1],
                                  mat.emissive_factor[2] };

                auto tex_idx = [&](const cgltf_texture_view& tv) -> int {
                    if (!tv.texture || !tv.texture->image) return -1;
                    auto it = image_idx.find(tv.texture->image);
                    return it != image_idx.end() ? it->second : -1;
                };
                mesh.albedo_tex   = tex_idx(pbr.base_color_texture);
                mesh.normal_tex   = tex_idx(mat.normal_texture);
                mesh.mr_tex       = tex_idx(pbr.metallic_roughness_texture);
                mesh.emissive_tex = tex_idx(mat.emissive_texture);
                mesh.alpha_mode   = (int)mat.alpha_mode;  // 0=opaque 1=mask 2=blend
                mesh.alpha_cutoff = mat.alpha_cutoff > 0.0f ? mat.alpha_cutoff : 0.5f;

                // KHR_materials_pbrSpecularGlossiness fallback
                // (some GLBs use this older extension instead of standard metallic-roughness)
                if (mat.has_pbr_specular_glossiness) {
                    const auto& sg = mat.pbr_specular_glossiness;
                    if (mesh.albedo_tex < 0)
                        mesh.albedo_tex = tex_idx(sg.diffuse_texture);
                    // Use diffuse_factor if it differs from the default (1,1,1,1)
                    const auto& df = sg.diffuse_factor;
                    mesh.base_color = {df[0], df[1], df[2], df[3]};
                    // Map glossiness → roughness; no metallic in SG workflow
                    mesh.roughness = 1.0f - sg.glossiness_factor;
                    mesh.metallic  = 0.0f;
                }
            }

            out.meshes.push_back(std::move(mesh));
        }
    }

    for (size_t i = 0; i < node->children_count; ++i)
        process_node(node->children[i], world, out, image_idx);
}

std::shared_ptr<GltfModel> GltfLoader::load(const std::string& path) {
    cgltf_options opts{};
    cgltf_data*   data = nullptr;

    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success) {
        SOL_ERROR(std::string("gltf parse failed: ") + path);
        return nullptr;
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        SOL_ERROR(std::string("gltf load buffers failed: ") + path);
        cgltf_free(data);
        return nullptr;
    }

    auto model  = std::make_shared<GltfModel>();
    model->path = path;

    // ---- Decode all embedded images first ----
    std::unordered_map<const cgltf_image*, int> image_idx;
    for (size_t i = 0; i < data->images_count; ++i) {
        const cgltf_image& img  = data->images[i];
        int                slot = (int)model->textures.size();
        bool               ok   = false;

        // GLB-embedded image: data lives in buffer_view
        if (img.buffer_view && img.buffer_view->buffer && img.buffer_view->buffer->data) {
            const auto* buf  = static_cast<const uint8_t*>(img.buffer_view->buffer->data)
                               + img.buffer_view->offset;
            const int   sz   = (int)img.buffer_view->size;
            int w, h, c;
            stbi_uc* px = stbi_load_from_memory(buf, sz, &w, &h, &c, 4);
            if (px) {
                model->textures.push_back(Texture::from_rgba8(px, w, h));
                stbi_image_free(px);
                ok = true;
            }
        }

        if (!ok) model->textures.push_back(Texture{});  // invalid placeholder
        image_idx[&data->images[i]] = slot;
    }

    // ---- Traverse scene node hierarchy ----
    const cgltf_scene* scene = data->scene
                             ? data->scene
                             : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene) {
        for (size_t i = 0; i < scene->nodes_count; ++i)
            process_node(scene->nodes[i], glm::mat4(1.0f), *model, image_idx);
    } else {
        // Fallback: iterate all nodes that have no parent
        for (size_t i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent)
                process_node(&data->nodes[i], glm::mat4(1.0f), *model, image_idx);
    }

    cgltf_free(data);
    SOL_INFO(std::string("Loaded GLTF: ") + path + " (" +
             std::to_string(model->meshes.size()) + " meshes, " +
             std::to_string(model->textures.size()) + " textures)");
    return model;
}

} // namespace sol
