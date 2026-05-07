#pragma once
#include "sol/scene/node3d.h"
#include <glm/glm.hpp>

namespace sol {

// Defines the shape used by a physics body parent (StaticBody3D / CharacterBody3D).
// Must be a direct child of the body node.
class SOL_API CollisionShape3D : public Node3D {
public:
    enum class Shape { Box, Sphere, Capsule };

    Shape     shape   = Shape::Box;
    glm::vec3 extents {0.5f, 0.5f, 0.5f};  // Box: half-extents (metres)
    float     radius  = 0.5f;              // Sphere / Capsule
    float     height  = 1.0f;             // Capsule total height (excluding caps)

    const char* type_name() const override { return "CollisionShape3D"; }
};

} // namespace sol
