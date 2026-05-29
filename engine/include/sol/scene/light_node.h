#pragma once
#include "sol/scene/node3d.h"
#include <glm/glm.hpp>

namespace sol {

class SOL_API DirectionalLight : public Node3D {
public:
    glm::vec3 color       {1.0f, 1.0f, 1.0f};
    float     intensity   = 1.0f;
    bool      cast_shadow = true; // shadows on by default for scene lights
    int       shadow_mode = 1;    // 0=None  1=PCF  2=PCSS  3=VSM

    float csm_far              = 100.0f;
    float csm_lambda           = 0.75f;
    int   shadow_quality       = 1;
    float shadow_pcf_radius    = 1.5f;
    float shadow_pcss_light    = 3.0f;
    float contact_shadow_dist  = 0.0f;
    float contact_shadow_thick = 0.5f;
    bool  temporal_shadow      = true;
    float temporal_shadow_alpha = 0.1f;

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
