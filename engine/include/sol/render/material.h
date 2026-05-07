#pragma once
#include "sol/export.h"
#include <glm/glm.hpp>

namespace sol {

class Texture;

// PBR metallic-roughness material.
// Backward-compatible: old code that only sets base_color/lit/double_sided still works.
struct SOL_API Material {
    // --- Base / albedo ---
    glm::vec4      base_color   {1.0f, 1.0f, 1.0f, 1.0f};
    const Texture* albedo       = nullptr;   // optional, multiplied by base_color

    // --- PBR ---
    float          metallic     = 0.0f;      // 0 = dielectric, 1 = metallic
    float          roughness    = 0.5f;      // 0 = mirror, 1 = fully rough
    glm::vec3      emissive     {0.0f, 0.0f, 0.0f};
    const Texture* normal_map   = nullptr;   // tangent-space normal map (optional)
    const Texture* mr_map       = nullptr;   // G=roughness B=metallic (glTF convention, optional)

    // --- Flags ---
    bool           lit          = true;      // false = unlit / emissive-only (ignore lighting)
    bool           double_sided = false;
};

} // namespace sol
