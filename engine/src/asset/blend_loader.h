#pragma once
#include "model_loader.h"
#include <string>
#include <memory>

namespace sol {

struct GltfModel;

// Exports a .blend file to a cached .glb via Blender's CLI, then loads the GLB.
// The exported .glb is stored next to the .blend file and is only re-generated
// when the .blend file is newer than the cache.
//
// Blender discovery order:
//   1. BLENDER_EXE environment variable
//   2. C:\Program Files\Blender Foundation\Blender X.Y\blender.exe (highest version wins)
//   3. Linux/macOS standard paths
class BlendLoader {
public:
    std::shared_ptr<GltfModel> load(const std::string& blend_path, const ModelLoadParams& params);

private:
    std::string find_blender() const;
    std::string get_cache_path(const std::string& blend_path) const;
    bool        needs_export(const std::string& blend_path, const std::string& cache_path) const;
    bool        export_to_glb(const std::string& blend_path, const std::string& output_path) const;
};

} // namespace sol
