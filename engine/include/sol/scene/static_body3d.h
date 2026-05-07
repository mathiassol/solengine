#pragma once
#include "sol/scene/node3d.h"
#include <vector>
#include <cstdint>

namespace sol {

// Immovable physics body.  CollisionShape3D children define its shape.
// Creates one Jolt static body per CollisionShape3D child on on_ready.
class SOL_API StaticBody3D : public Node3D {
public:
    const char* type_name() const override { return "StaticBody3D"; }

    void on_ready  (Engine& engine) override;
    void on_destroy(Engine& engine) override;

private:
    std::vector<uint32_t> m_body_ids;  // JPH::BodyID values
};

} // namespace sol
