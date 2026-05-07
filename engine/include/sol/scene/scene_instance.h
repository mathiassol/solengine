#pragma once
#include "sol/scene/node3d.h"
#include <string>

namespace sol {

// Embeds another .solscene at this node's position.
// On on_ready the referenced scene is loaded and its root node is inserted as
// a child of this node, so find_first<T>() traversal works transparently.
class SOL_API SceneInstance : public Node3D {
public:
    std::string scene_path;  // relative path, e.g. "scenes/player.solscene"

    const char* type_name() const override { return "SceneInstance"; }

    void on_ready(Engine& engine) override;
};

} // namespace sol
