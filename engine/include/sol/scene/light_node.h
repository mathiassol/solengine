#pragma once
#include "sol/scene/node3d.h"
#include <glm/glm.hpp>

namespace sol {

class SOL_API DirectionalLight : public Node3D {
public:
    glm::vec3 color       {1.0f, 1.0f, 1.0f};
    float     intensity   = 1.0f;
    bool      cast_shadow = true; // shadows on by default for scene lights

    const char* type_name() const override { return "DirectionalLight"; }
    glm::vec3 world_direction() const { return forward(); }
};

class SOL_API PointLight : public Node3D {
public:
    glm::vec3 color     {1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;
    float     range     = 10.0f;

    const char* type_name() const override { return "PointLight"; }
};

} // namespace sol
