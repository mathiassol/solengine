#pragma once
#include "sol/scene/node3d.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace sol {

class Engine;

// Dynamic rigid body driven by the physics solver (Jolt).
// CollisionShape3D children define its shape.
//
// Exposed to Lua:
//   :apply_force(vec3)          -- continuous force (N), reset each frame
//   :apply_impulse(vec3)        -- instant impulse (N·s)
//   :apply_torque_impulse(vec3) -- angular impulse
//   :get_velocity() -> vec3
//   :set_velocity(vec3)
//   :get_angular_velocity() -> vec3
//   :set_angular_velocity(vec3)
//   .mass                       -- (read/write before on_ready)
//   .gravity_scale              -- (read/write before on_ready)
//   :freeze_rotation(bool)      -- lock angular DOF
//   :set_kinematic(bool)        -- switch between dynamic and kinematic
class SOL_API RigidBody3D : public Node3D {
public:
    float mass          = 1.0f;
    float gravity_scale = 1.0f;
    bool  is_kinematic  = false;

    const char* type_name() const override { return "RigidBody3D"; }

    // Must be explicitly declared so unique_ptr<Impl> compiles.
    RigidBody3D();
    ~RigidBody3D() override;

    void on_ready  (Engine& engine) override;
    void on_update (Engine& engine, float dt) override;
    void on_destroy(Engine& engine) override;

    void      apply_force         (const glm::vec3& force);
    void      apply_impulse       (const glm::vec3& impulse);
    void      apply_torque_impulse(const glm::vec3& torque);
    glm::vec3 get_velocity        () const;
    void      set_velocity        (const glm::vec3& v);
    glm::vec3 get_angular_velocity() const;
    void      set_angular_velocity(const glm::vec3& v);
    void      freeze_rotation     (bool freeze);
    void      set_kinematic       (bool kinematic);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::vector<uint32_t> m_body_ids;
};

} // namespace sol
