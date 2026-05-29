#include "asset/model_loader.h"
#include "asset/blend_loader.h"
#include "asset/fbx_loader.h"
#include "asset/gltf_loader.h"
#include "sol/log.h"
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace sol {

static std::string ext_lower(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

// Build a canonical cache key from path + all params that affect the loaded result.
// Floats are formatted at fixed precision so equal values produce equal strings.
std::string ModelLoader::make_key(const std::string& path, const ModelLoadParams& p) {
    std::ostringstream oss;
    oss << path
        << '\x01' << p.texture_search_dir
        << '\x01' << p.albedo_override
        << '\x01' << p.normal_override
        << '\x01' << p.roughness_override
        << '\x01' << p.metalness_override
        << '\x01' << std::fixed << std::setprecision(6) << p.default_roughness
        << '\x01' << std::fixed << std::setprecision(6) << p.default_metallic;
    return oss.str();
}

std::shared_ptr<GltfModel> ModelLoader::load(const std::string& path) {
    return load(path, ModelLoadParams{});
}

std::shared_ptr<GltfModel> ModelLoader::load(const std::string& path, const ModelLoadParams& params) {
    const std::string key = make_key(path, params);

    // --- Cache lookup ---
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        if (auto sp = it->second.lock()) {
            SOL_INFO("ResourceCache: hit '" + path + "'");
            return sp;
        }
        // Weak pointer expired — all prior holders released it; remove stale entry.
        m_cache.erase(it);
    }

    // --- Cache miss: load from disk ---
    std::shared_ptr<GltfModel> model;
    auto ext = ext_lower(path);
    if (ext == ".fbx") {
        FbxLoader loader;
        model = loader.load(path, params);
    } else if (ext == ".blend") {
        BlendLoader loader;
        model = loader.load(path, params);
    } else {
        // GLTF/GLB — params not used (textures are embedded)
        GltfLoader loader;
        model = loader.load(path);
    }

    if (model) {
        m_cache[key] = model;  // store weak reference; expires when last node drops its ref
        prune_cache();         // keep the map tidy after each load
        SOL_INFO("ResourceCache: loaded '" + path + "' ("
                 + std::to_string(cache_size()) + " live entries)");
    }

    return model;
}

void ModelLoader::prune_cache() {
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        it = it->second.expired() ? m_cache.erase(it) : std::next(it);
    }
}

size_t ModelLoader::cache_size() const {
    size_t n = 0;
    for (const auto& [k, wp] : m_cache)
        if (!wp.expired()) ++n;
    return n;
}

} // namespace sol
