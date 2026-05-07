#pragma once
#include "sol/export.h"
#include <glm/glm.hpp>

namespace sol {

enum class LightType : uint8_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};

struct SOL_API Light {
    LightType type         = LightType::Directional;
    glm::vec3 direction    {0.0f, -1.0f, 0.0f}; // normalized, world-space toward-light (for Directional/Spot)
    glm::vec3 position     {0.0f,  0.0f, 0.0f}; // world-space (for Point/Spot)
    glm::vec3 color        {1.0f,  1.0f, 1.0f};
    float     intensity    = 1.0f;
    float     range        = 10.0f;              // Point/Spot attenuation radius
    float     inner_angle  = 20.0f;              // Spot inner cone half-angle (degrees)
    float     outer_angle  = 35.0f;              // Spot outer cone half-angle (degrees)
    bool      cast_shadow  = false;              // only first directional with cast_shadow=true gets shadow map
};

} // namespace sol
