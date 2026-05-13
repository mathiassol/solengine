#pragma once
#include "sol/export.h"
#include <glm/glm.hpp>

namespace sol {

class Texture;

enum class AlphaMode : uint8_t {
    Opaque = 0,  // fully opaque, no alpha testing
    Mask   = 1,  // cutout: discard fragments where alpha < alpha_cutoff
    Blend  = 2,  // alpha-blended transparency (drawn back-to-front)
};

// PBR metallic-roughness material.
struct SOL_API Material {
    // --- Base / albedo ---
    glm::vec4      base_color   {1.0f, 1.0f, 1.0f, 1.0f};
    const Texture* albedo       = nullptr;   // optional, multiplied by base_color

    // --- PBR ---
    float          metallic     = 0.0f;
    float          roughness    = 0.5f;
    glm::vec3      emissive     {0.0f, 0.0f, 0.0f};
    const Texture* normal_map   = nullptr;
    const Texture* mr_map       = nullptr;   // G=roughness B=metallic (glTF convention)
    const Texture* emissive_tex = nullptr;   // RGB emissive map (optional)

    // --- Transparency ---
    AlphaMode      alpha_mode   = AlphaMode::Opaque;
    float          alpha_cutoff = 0.5f;      // threshold for AlphaMode::Mask

    // --- Flags ---
    bool           lit          = true;
    bool           double_sided = false;
};

} // namespace sol
