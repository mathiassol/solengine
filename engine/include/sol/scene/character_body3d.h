#pragma once
#include "sol/scene/node3d.h"
#include <glm/glm.hpp>
#include <memory>

namespace sol {

// Kinematic character body (first-person / third-person player controller).
// Uses Jolt's CharacterVirtual — not subject to the rigid-body solver but still
// collides with static and dynamic geometry.
//
// Usage (each frame in on_update or game code):
//   character->move_and_slide(desired_velocity, dt);
//   // Then read back position (it's updated automatically in on_update).
class SOL_API CharacterBody3D : public Node3D {
public:
    float capsule_radius = 0.35f;
    float capsule_height = 1.5f;   // cylinder height, caps are extra

    const char* type_name() const override { return "CharacterBody3D"; }

    // Must be explicitly declared so unique_ptr<Impl> compiles with incomplete Impl.
    CharacterBody3D();
    ~CharacterBody3D() override;

    void on_ready  (Engine& engine) override;
    void on_update (Engine& engine, float dt) override;
    void on_destroy(Engine& engine) override;

    // Drive movement each frame (call before on_update physics integration).
    // velocity is in world space (m/s).  Gravity is added internally.
    void         move_and_slide(const glm::vec3& velocity);
    bool         is_on_ground() const;
    glm::vec3    get_velocity() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sol
