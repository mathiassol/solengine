#pragma once
#include "gltf_loader.h"
#include <string>
#include <memory>
#include <unordered_map>

namespace sol {

// Parameters for controlling model loading — especially useful for FBX models
// with external textures, separate metalness/roughness maps, or albedo variants.
struct ModelLoadParams {
    std::string texture_search_dir;  // extra dir to search for external textures
    std::string albedo_override;     // force a specific albedo texture filename
    std::string normal_override;     // force a specific normal map filename
    std::string roughness_override;  // force roughness texture (FBX separate maps)
    std::string metalness_override;  // force metalness texture (FBX separate maps)
    float default_roughness = 0.7f;  // fallback roughness when no texture found
    float default_metallic  = 0.0f;  // fallback metallic when no texture found
};

// Unified model loader — dispatches to GltfLoader or FbxLoader by file extension.
// Handles: .glb, .gltf → cgltf   |   .fbx, .FBX → ufbx
//
// Engine-wide resource cache: same path+params returns the same shared_ptr<GltfModel>,
// sharing GPU mesh and texture buffers across nodes and scene reloads.
// The cache uses weak_ptr — entries expire automatically when no node holds a reference,
// so GPU memory is freed on scene unload and reloaded fresh on next use.
class ModelLoader {
public:
    std::shared_ptr<GltfModel> load(const std::string& path);
    std::shared_ptr<GltfModel> load(const std::string& path, const ModelLoadParams& params);

    // Remove entries whose shared_ptr has expired (all holders released).
    // Called automatically on each load; exposed for explicit use if needed.
    void prune_cache();

    // Number of currently live (non-expired) cached models.
    size_t cache_size() const;

private:
    // Cache key = canonical path + \x01-separated param fields.
    // \x01 is used as separator because it cannot appear in a valid file path.
    static std::string make_key(const std::string& path, const ModelLoadParams& params);

    // weak_ptr entries: alive while at least one node holds a shared_ptr to the model.
    std::unordered_map<std::string, std::weak_ptr<GltfModel>> m_cache;
};

} // namespace sol
