#pragma once
#include "gltf_loader.h"
#include "model_loader.h"
#include <string>
#include <memory>

namespace sol {

class FbxLoader {
public:
    std::shared_ptr<GltfModel> load(const std::string& path, const ModelLoadParams& params);
};

} // namespace sol
