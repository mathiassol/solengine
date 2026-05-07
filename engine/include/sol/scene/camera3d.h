#pragma once
#include "sol/scene/node3d.h"
#include "sol/render/camera.h"

namespace sol {

// Camera node. Set current = true to make this the active view camera.
// The scene renderer picks the first Camera3D with current == true.
class SOL_API Camera3D : public Node3D {
public:
    float fov      = 70.0f;   // vertical field-of-view in degrees
    float near_clip = 0.05f;
    float far_clip  = 1000.0f;
    bool  current   = false;  // true = active camera

    const char* type_name() const override { return "Camera3D"; }

    // Build a sol::Camera from this node's current world transform.
    Camera to_camera(float aspect) const;
};

} // namespace sol
