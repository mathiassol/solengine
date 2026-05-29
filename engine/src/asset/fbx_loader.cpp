#include "asset/fbx_loader.h"
#include "sol/log.h"

#include <ufbx.h>
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

namespace sol {

// ─── helpers ───────────────────────────────────────────────────────────────

static glm::vec3 to_vec3(ufbx_vec3 v) { return {(float)v.x, (float)v.y, (float)v.z}; }
static glm::vec2 to_vec2(ufbx_vec2 v) { return {(float)v.x, (float)v.y}; }

static glm::mat4 to_mat4(const ufbx_matrix& m) {
    return glm::mat4(
        glm::vec4((float)m.cols[0].x, (float)m.cols[0].y, (float)m.cols[0].z, 0.0f),
        glm::vec4((float)m.cols[1].x, (float)m.cols[1].y, (float)m.cols[1].z, 0.0f),
        glm::vec4((float)m.cols[2].x, (float)m.cols[2].y, (float)m.cols[2].z, 0.0f),
        glm::vec4((float)m.cols[3].x, (float)m.cols[3].y, (float)m.cols[3].z, 1.0f)
    );
}

// Resolve a texture filename: try multiple search locations, return first match.
static std::string resolve_texture(const std::string& filename,
                                    const std::string& fbx_dir,
                                    const std::string& extra_search_dir)
{
    if (filename.empty()) return "";

    auto try_path = [](const std::string& p) -> std::string {
        return fs::exists(p) ? p : "";
    };

    // 1. Try as-is (may be absolute or relative to CWD)
    if (auto p = try_path(filename); !p.empty()) return p;

    // 2. Just the filename component next to the FBX
    std::string fname = fs::path(filename).filename().string();
    if (auto p = try_path(fbx_dir + "/" + fname); !p.empty()) return p;

    // 3. Sibling Textures/ directory
    if (auto p = try_path(fbx_dir + "/../Textures/" + fname); !p.empty()) return p;
    if (auto p = try_path(fbx_dir + "/Textures/" + fname); !p.empty()) return p;

    // 4. Extra search dir
    if (!extra_search_dir.empty()) {
        if (auto p = try_path(extra_search_dir + "/" + fname); !p.empty()) return p;
        if (auto p = try_path(extra_search_dir + "/" + filename); !p.empty()) return p;
    }

    return "";
}

// Load a texture from file path. Returns Texture{} (invalid) on failure.
static Texture load_texture_file(const std::string& path) {
    if (path.empty()) return Texture{};
    int w, h, c;
    stbi_uc* px = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!px) return Texture{};
    Texture t = Texture::from_rgba8(px, w, h);
    stbi_image_free(px);
    return t;
}

// Build a combined MR texture (G=roughness, B=metallic) from separate maps.
static Texture pack_mr_texture(const std::string& roughness_path,
                                const std::string& metalness_path,
                                float default_roughness,
                                float default_metallic)
{
    int rw = 1, rh = 1, mw = 1, mh = 1;
    stbi_uc* rpx = nullptr;
    stbi_uc* mpx = nullptr;

    int rc, mc;
    if (!roughness_path.empty()) rpx = stbi_load(roughness_path.c_str(), &rw, &rh, &rc, 1);
    if (!metalness_path.empty()) mpx = stbi_load(metalness_path.c_str(), &mw, &mh, &mc, 1);

    // Use the larger dimension as output size
    int w = std::max(rw, mw);
    int h = std::max(rh, mh);

    std::vector<uint8_t> rgba(w * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 4;
            rgba[idx + 0] = 255; // R unused
            // Roughness in G
            if (rpx) {
                int rx = (int)(x * (float)(rw - 1) / (w - 1 + 1e-5f));
                int ry = (int)(y * (float)(rh - 1) / (h - 1 + 1e-5f));
                rgba[idx + 1] = rpx[ry * rw + rx];
            } else {
                rgba[idx + 1] = (uint8_t)(default_roughness * 255.0f);
            }
            // Metallic in B
            if (mpx) {
                int mx2 = (int)(x * (float)(mw - 1) / (w - 1 + 1e-5f));
                int my2 = (int)(y * (float)(mh - 1) / (h - 1 + 1e-5f));
                rgba[idx + 2] = mpx[my2 * mw + mx2];
            } else {
                rgba[idx + 2] = (uint8_t)(default_metallic * 255.0f);
            }
            rgba[idx + 3] = 255;
        }
    }

    if (rpx) stbi_image_free(rpx);
    if (mpx) stbi_image_free(mpx);

    return Texture::from_rgba8(rgba.data(), w, h);
}

// ─── main loader ──────────────────────────────────────────────────────────

std::shared_ptr<GltfModel> FbxLoader::load(const std::string& path, const ModelLoadParams& params)
{
    ufbx_load_opts opts{};
    opts.target_axes            = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters     = 1.0f;
    opts.generate_missing_normals = true;

    ufbx_error error{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        SOL_ERROR("FbxLoader: failed to load '" + path + "': " + std::string(error.description.data));
        return nullptr;
    }

    std::string fbx_dir = fs::path(path).parent_path().string();
    if (fbx_dir.empty()) fbx_dir = ".";

    auto model  = std::make_shared<GltfModel>();
    model->path = path;

    // texture_index map: path string → index in model->textures (-1 if failed)
    std::unordered_map<std::string, int> tex_cache;

    auto add_texture = [&](const std::string& resolved_path) -> int {
        if (resolved_path.empty()) return -1;
        auto it = tex_cache.find(resolved_path);
        if (it != tex_cache.end()) return it->second;
        Texture t = load_texture_file(resolved_path);
        if (!t.valid()) {
            tex_cache[resolved_path] = -1;
            return -1;
        }
        int idx = (int)model->textures.size();
        model->textures.push_back(std::move(t));
        tex_cache[resolved_path] = idx;
        return idx;
    };

    auto add_mr_texture = [&](const std::string& roughness_path, const std::string& metalness_path) -> int {
        std::string key = roughness_path + "|" + metalness_path;
        auto it = tex_cache.find(key);
        if (it != tex_cache.end()) return it->second;
        Texture t = pack_mr_texture(roughness_path, metalness_path,
                                     params.default_roughness, params.default_metallic);
        if (!t.valid()) {
            tex_cache[key] = -1;
            return -1;
        }
        int idx = (int)model->textures.size();
        model->textures.push_back(std::move(t));
        tex_cache[key] = idx;
        return idx;
    };

    auto get_tex_filename = [](const ufbx_texture* tex) -> std::string {
        if (!tex) return "";
        if (tex->relative_filename.length > 0) return std::string(tex->relative_filename.data);
        if (tex->absolute_filename.length > 0) return std::string(tex->absolute_filename.data);
        return "";
    };

    // Process mesh nodes
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        if (!node->mesh) continue;

        const ufbx_mesh* mesh = node->mesh;
        glm::mat4 world_xform = to_mat4(node->geometry_to_world);

        // Triangulation buffer (max 64 triangles per face)
        std::vector<uint32_t> tri_buf(mesh->max_face_triangles * 3);

        size_t num_parts = mesh->material_parts.count > 0 ? mesh->material_parts.count : 1;

        for (size_t pi = 0; pi < num_parts; ++pi) {
            GltfMesh out_mesh;
            out_mesh.name           = node->name.data ? node->name.data : "mesh";
            out_mesh.node_transform = world_xform;
            out_mesh.double_sided   = true;

            // Get material for this part
            const ufbx_material* mat = nullptr;
            if (mesh->materials.count > 0) {
                if (mesh->material_parts.count > 0 && pi < mesh->material_parts.count) {
                    if (pi < mesh->materials.count)
                        mat = mesh->materials.data[pi];
                } else {
                    mat = mesh->materials.data[0];
                }
            }

            // Resolve textures for this material
            int albedo_idx = -1;
            int normal_idx = -1;
            int mr_idx     = -1;

            out_mesh.metallic  = params.default_metallic;
            out_mesh.roughness = params.default_roughness;

            if (mat) {
                out_mesh.base_color = {
                    (float)mat->fbx.maps[UFBX_MATERIAL_FBX_DIFFUSE_COLOR].value_vec3.x,
                    (float)mat->fbx.maps[UFBX_MATERIAL_FBX_DIFFUSE_COLOR].value_vec3.y,
                    (float)mat->fbx.maps[UFBX_MATERIAL_FBX_DIFFUSE_COLOR].value_vec3.z,
                    1.0f
                };

                // Albedo
                std::string albedo_path;
                if (!params.albedo_override.empty()) {
                    albedo_path = resolve_texture(params.albedo_override, fbx_dir, params.texture_search_dir);
                }
                if (albedo_path.empty()) {
                    std::string fn = get_tex_filename(mat->pbr.maps[UFBX_MATERIAL_PBR_BASE_COLOR].texture);
                    if (fn.empty()) fn = get_tex_filename(mat->fbx.maps[UFBX_MATERIAL_FBX_DIFFUSE_COLOR].texture);
                    if (!fn.empty()) albedo_path = resolve_texture(fn, fbx_dir, params.texture_search_dir);
                }
                albedo_idx = add_texture(albedo_path);

                // Normal map
                std::string normal_path;
                if (!params.normal_override.empty()) {
                    normal_path = resolve_texture(params.normal_override, fbx_dir, params.texture_search_dir);
                }
                if (normal_path.empty()) {
                    std::string fn = get_tex_filename(mat->pbr.maps[UFBX_MATERIAL_PBR_NORMAL_MAP].texture);
                    if (fn.empty()) fn = get_tex_filename(mat->fbx.maps[UFBX_MATERIAL_FBX_NORMAL_MAP].texture);
                    if (fn.empty()) fn = get_tex_filename(mat->fbx.maps[UFBX_MATERIAL_FBX_BUMP].texture);
                    if (!fn.empty()) normal_path = resolve_texture(fn, fbx_dir, params.texture_search_dir);
                }
                normal_idx = add_texture(normal_path);

                // Roughness + metalness → packed MR texture
                std::string roughness_path;
                std::string metalness_path;
                if (!params.roughness_override.empty())
                    roughness_path = resolve_texture(params.roughness_override, fbx_dir, params.texture_search_dir);
                if (!params.metalness_override.empty())
                    metalness_path = resolve_texture(params.metalness_override, fbx_dir, params.texture_search_dir);

                if (roughness_path.empty()) {
                    std::string fn = get_tex_filename(mat->pbr.maps[UFBX_MATERIAL_PBR_ROUGHNESS].texture);
                    if (!fn.empty()) roughness_path = resolve_texture(fn, fbx_dir, params.texture_search_dir);
                }
                if (metalness_path.empty()) {
                    std::string fn = get_tex_filename(mat->pbr.maps[UFBX_MATERIAL_PBR_METALNESS].texture);
                    if (!fn.empty()) metalness_path = resolve_texture(fn, fbx_dir, params.texture_search_dir);
                }

                if (!roughness_path.empty() || !metalness_path.empty()) {
                    mr_idx = add_mr_texture(roughness_path, metalness_path);
                }
            }

            out_mesh.albedo_tex  = albedo_idx;
            out_mesh.normal_tex  = normal_idx;
            out_mesh.mr_tex      = mr_idx;

            // Build geometry
            if (mesh->material_parts.count > 0 && pi < mesh->material_parts.count) {
                const ufbx_mesh_part& part = mesh->material_parts.data[pi];
                for (size_t fii = 0; fii < part.face_indices.count; ++fii) {
                    uint32_t fi = part.face_indices.data[fii];
                    ufbx_face face = mesh->faces.data[fi];
                    uint32_t ntris = ufbx_triangulate_face(tri_buf.data(), tri_buf.size(), mesh, face);
                    for (uint32_t ti = 0; ti < ntris; ++ti) {
                        for (int vi = 0; vi < 3; ++vi) {
                            uint32_t idx = tri_buf[ti * 3 + vi];
                            MeshVertex v;
                            v.position = to_vec3(ufbx_get_vertex_vec3(&mesh->vertex_position, idx));
                            if (mesh->vertex_normal.exists)
                                v.normal = to_vec3(ufbx_get_vertex_vec3(&mesh->vertex_normal, idx));
                            if (mesh->vertex_uv.exists) {
                                glm::vec2 uv = to_vec2(ufbx_get_vertex_vec2(&mesh->vertex_uv, idx));
                                v.uv = {uv.x, 1.0f - uv.y};
                            }
                            if (mesh->vertex_tangent.exists) {
                                ufbx_vec3 t = ufbx_get_vertex_vec3(&mesh->vertex_tangent, idx);
                                v.tangent = {(float)t.x, (float)t.y, (float)t.z, 1.0f};
                            }
                            out_mesh.indices.push_back((uint32_t)out_mesh.vertices.size());
                            out_mesh.vertices.push_back(v);
                        }
                    }
                }
            } else {
                // No material parts — process all faces
                for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
                    ufbx_face face = mesh->faces.data[fi];
                    uint32_t ntris = ufbx_triangulate_face(tri_buf.data(), tri_buf.size(), mesh, face);
                    for (uint32_t ti = 0; ti < ntris; ++ti) {
                        for (int vi = 0; vi < 3; ++vi) {
                            uint32_t idx = tri_buf[ti * 3 + vi];
                            MeshVertex v;
                            v.position = to_vec3(ufbx_get_vertex_vec3(&mesh->vertex_position, idx));
                            if (mesh->vertex_normal.exists)
                                v.normal = to_vec3(ufbx_get_vertex_vec3(&mesh->vertex_normal, idx));
                            if (mesh->vertex_uv.exists) {
                                glm::vec2 uv = to_vec2(ufbx_get_vertex_vec2(&mesh->vertex_uv, idx));
                                v.uv = {uv.x, 1.0f - uv.y};
                            }
                            if (mesh->vertex_tangent.exists) {
                                ufbx_vec3 t = ufbx_get_vertex_vec3(&mesh->vertex_tangent, idx);
                                v.tangent = {(float)t.x, (float)t.y, (float)t.z, 1.0f};
                            }
                            out_mesh.indices.push_back((uint32_t)out_mesh.vertices.size());
                            out_mesh.vertices.push_back(v);
                        }
                    }
                }
            }

            if (!out_mesh.vertices.empty())
                model->meshes.push_back(std::move(out_mesh));
        }
    }

    ufbx_free_scene(scene);

    SOL_INFO("FbxLoader: loaded '" + path + "' (" +
             std::to_string(model->meshes.size()) + " meshes, " +
             std::to_string(model->textures.size()) + " textures)");
    return model;
}

} // namespace sol
