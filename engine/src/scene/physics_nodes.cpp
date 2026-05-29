// physics_nodes.cpp — StaticBody3D, CharacterBody3D, Area3D, RigidBody3D.

#include "sol/scene/static_body3d.h"
#include "sol/scene/character_body3d.h"
#include "sol/scene/collision_shape3d.h"
#include "sol/scene/area3d.h"
#include "sol/scene/rigid_body3d.h"
#include "sol/engine.h"
#include "sol/script/script_engine.h"
#include "physics/physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

namespace sol {

// Object layers used throughout the engine
static constexpr JPH::ObjectLayer LAYER_NON_MOVING = 0;
static constexpr JPH::ObjectLayer LAYER_MOVING     = 1;

// ============================================================
//  Simple pass-all collision filters for CharacterVirtual
// ============================================================

class AllowAllBPFilter final : public JPH::BroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::BroadPhaseLayer) const override { return true; }
};

class AllowAllObjFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer) const override { return true; }
};

// ============================================================
//  StaticBody3D
// ============================================================

static JPH::ShapeRefC make_shape(const CollisionShape3D& cs) {
    switch (cs.shape) {
    case CollisionShape3D::Shape::Sphere:
        return new JPH::SphereShape(cs.radius);
    case CollisionShape3D::Shape::Capsule:
        return new JPH::CapsuleShape(cs.height * 0.5f, cs.radius);
    default: {
        JPH::BoxShapeSettings settings(
            JPH::Vec3(cs.extents.x, cs.extents.y, cs.extents.z));
        auto r = settings.Create();
        return r.IsValid() ? r.Get() : nullptr;
    }
    }
}

void StaticBody3D::on_ready(Engine& engine) {
    auto* ps = engine.physics().system();
    if (!ps) return;

    JPH::BodyInterface& bi = ps->GetBodyInterface();
    glm::mat4 world        = global_transform();

    each<CollisionShape3D>([&](CollisionShape3D& cs) {
        JPH::ShapeRefC shape = make_shape(cs);
        if (!shape) return;

        glm::mat4 cs_world  = world * cs.local_transform();
        glm::vec3 pos_glm   = glm::vec3(cs_world[3]);
        JPH::Vec3 jpos(pos_glm.x, pos_glm.y, pos_glm.z);
        JPH::Quat jrot = JPH::Quat::sIdentity();

        JPH::BodyCreationSettings settings(
            shape, jpos, jrot,
            JPH::EMotionType::Static,
            LAYER_NON_MOVING);

        JPH::Body* body = bi.CreateBody(settings);
        if (body) {
            bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);
            uint32_t raw = body->GetID().GetIndexAndSequenceNumber();
            m_body_ids.push_back(raw);
            engine.physics().register_body(raw, this);
        }
    });
}

void StaticBody3D::on_destroy(Engine& engine) {
    auto* ps = engine.physics().system();
    if (!ps) return;

    JPH::BodyInterface& bi = ps->GetBodyInterface();
    for (uint32_t raw : m_body_ids) {
        engine.physics().unregister_body(raw);
        JPH::BodyID id(raw);
        bi.RemoveBody(id);
        bi.DestroyBody(id);
    }
    m_body_ids.clear();
}

// ============================================================
//  CharacterBody3D — pimpl definition
// ============================================================

struct CharacterBody3D::Impl {
    JPH::Ref<JPH::CharacterVirtual> character;
};

CharacterBody3D::CharacterBody3D()  : m_impl(std::make_unique<Impl>()) {}
CharacterBody3D::~CharacterBody3D() = default;

static JPH::RefConst<JPH::Shape> make_character_shape(float radius, float half_cylinder_height) {
    // CapsuleShape: half the *cylinder* height (caps add radius on each end)
    return new JPH::CapsuleShape(half_cylinder_height, radius);
}

void CharacterBody3D::on_ready(Engine& engine) {
    auto* ps = engine.physics().system();
    if (!ps) return;

    JPH::CharacterVirtualSettings settings;
    settings.mMaxSlopeAngle            = JPH::DegreesToRadians(45.0f);
    settings.mMaxStrength              = 100.0f;
    settings.mShape                    = make_character_shape(capsule_radius, capsule_height * 0.5f);
    settings.mBackFaceMode             = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mCharacterPadding         = 0.02f;
    settings.mPenetrationRecoverySpeed = 1.0f;
    settings.mPredictiveContactDistance= 0.1f;
    settings.mSupportingVolume         = JPH::Plane(JPH::Vec3::sAxisY(), -capsule_radius);

    JPH::RVec3 jpos(position.x, position.y, position.z);
    m_impl->character = new JPH::CharacterVirtual(&settings, jpos,
                                                   JPH::Quat::sIdentity(),
                                                   0, ps);
}

void CharacterBody3D::on_update(Engine& engine, float dt) {
    if (!m_impl->character) return;

    AllowAllBPFilter  bp_filter;
    AllowAllObjFilter obj_filter;
    JPH::BodyFilter   body_filter;
    JPH::ShapeFilter  shape_filter;

    JPH::CharacterVirtual::ExtendedUpdateSettings us;
    m_impl->character->ExtendedUpdate(
        dt,
        JPH::Vec3(0, -9.81f, 0),
        us,
        bp_filter,
        obj_filter,
        body_filter,
        shape_filter,
        engine.physics().temp_alloc());

    // Sync Node3D position from physics result
    JPH::RVec3 p = m_impl->character->GetPosition();
    position = glm::vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
}

void CharacterBody3D::on_destroy(Engine& /*engine*/) {
    m_impl->character = nullptr;
}

bool CharacterBody3D::is_on_ground() const {
    if (!m_impl->character) return false;
    return m_impl->character->GetGroundState()
           == JPH::CharacterVirtual::EGroundState::OnGround;
}

glm::vec3 CharacterBody3D::get_velocity() const {
    if (!m_impl->character) return {};
    JPH::Vec3 v = m_impl->character->GetLinearVelocity();
    return { v.GetX(), v.GetY(), v.GetZ() };
}

void CharacterBody3D::move_and_slide(const glm::vec3& velocity) {
    if (!m_impl->character) return;
    m_impl->character->SetLinearVelocity(
        JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

// ============================================================
//  Area3D — sensor trigger volume
// ============================================================

void Area3D::on_ready(Engine& engine) {
    auto* ps = engine.physics().system();
    if (!ps) return;

    JPH::BodyInterface& bi = ps->GetBodyInterface();
    glm::mat4 world        = global_transform();

    each<CollisionShape3D>([&](CollisionShape3D& cs) {
        JPH::ShapeRefC shape = make_shape(cs);
        if (!shape) return;

        glm::mat4 cs_world = world * cs.local_transform();
        glm::vec3 pos_glm  = glm::vec3(cs_world[3]);
        JPH::Vec3 jpos(pos_glm.x, pos_glm.y, pos_glm.z);

        JPH::BodyCreationSettings settings(
            shape, jpos, JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            LAYER_NON_MOVING);
        settings.mIsSensor = true;

        JPH::Body* body = bi.CreateBody(settings);
        if (body) {
            bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);
            uint32_t raw = body->GetID().GetIndexAndSequenceNumber();
            m_body_ids.push_back(raw);
            engine.physics().register_body(raw, this, /*is_sensor=*/true);
        }
    });
}

void Area3D::on_destroy(Engine& engine) {
    auto* ps = engine.physics().system();
    if (!ps) return;

    JPH::BodyInterface& bi = ps->GetBodyInterface();
    for (uint32_t raw : m_body_ids) {
        engine.physics().unregister_body(raw);
        JPH::BodyID id(raw);
        bi.RemoveBody(id);
        bi.DestroyBody(id);
    }
    m_body_ids.clear();
    m_overlapping.clear();
}

void Area3D::_on_body_entered(Node* other, Engine& engine) {
    auto [it, inserted] = m_overlapping.insert(other);
    if (!inserted) return; // already tracked
    if (engine.has_script()) {
        engine.script().node_event_with_node_arg(this, "on_body_entered", other, engine);
        // Also bubble to direct parent — lets a ScriptNode parent listen without
        // needing script_path set on the Area3D itself.
        if (Node* p = parent(); p && !p->script_path.empty())
            engine.script().node_event_with_node_arg(p, "on_body_entered", other, engine);
    }
}

void Area3D::_on_body_exited(Node* other, Engine& engine) {
    if (m_overlapping.erase(other) == 0) return;
    if (engine.has_script()) {
        engine.script().node_event_with_node_arg(this, "on_body_exited", other, engine);
        if (Node* p = parent(); p && !p->script_path.empty())
            engine.script().node_event_with_node_arg(p, "on_body_exited", other, engine);
    }
}

std::vector<Node*> Area3D::get_overlapping_bodies() const {
    return std::vector<Node*>(m_overlapping.begin(), m_overlapping.end());
}

bool Area3D::is_overlapping_with(Node* node) const {
    return m_overlapping.count(node) > 0;
}

int Area3D::overlap_count() const {
    return static_cast<int>(m_overlapping.size());
}

// ============================================================
//  RigidBody3D — dynamic physics body
// ============================================================

struct RigidBody3D::Impl {
    JPH::BodyID         body_id;
    JPH::PhysicsSystem* ps    = nullptr;
    bool                valid = false;
};

RigidBody3D::RigidBody3D()  : m_impl(std::make_unique<Impl>()) {}
RigidBody3D::~RigidBody3D() = default;

void RigidBody3D::on_ready(Engine& engine) {
    auto* ps = engine.physics().system();
    if (!ps) return;

    JPH::ShapeRefC shape;
    each<CollisionShape3D>([&](CollisionShape3D& cs) {
        if (!shape) shape = make_shape(cs);
    });
    if (!shape) shape = new JPH::SphereShape(0.5f);

    glm::vec3 pos = position;
    JPH::BodyCreationSettings settings(
        shape,
        JPH::RVec3(pos.x, pos.y, pos.z),
        JPH::Quat::sIdentity(),
        is_kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
        LAYER_MOVING);
    settings.mOverrideMassProperties             = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass       = mass;
    settings.mGravityFactor                      = gravity_scale;
    settings.mAllowedDOFs                        = JPH::EAllowedDOFs::All;

    JPH::BodyInterface& bi = ps->GetBodyInterface();
    JPH::Body* body = bi.CreateBody(settings);
    if (!body) return;

    bi.AddBody(body->GetID(), JPH::EActivation::Activate);
    m_impl->body_id = body->GetID();
    m_impl->ps      = ps;
    m_impl->valid   = true;

    uint32_t raw = m_impl->body_id.GetIndexAndSequenceNumber();
    m_body_ids.push_back(raw);
    engine.physics().register_body(raw, this);
}

void RigidBody3D::on_update(Engine& /*engine*/, float /*dt*/) {
    if (!m_impl->valid) return;

    // Sync visual position from physics result
    JPH::BodyLockRead lock(m_impl->ps->GetBodyLockInterface(), m_impl->body_id);
    if (lock.Succeeded()) {
        JPH::RVec3 p = lock.GetBody().GetPosition();
        position = glm::vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
        // Convert quaternion to Euler angles (radians)
        JPH::Quat q = lock.GetBody().GetRotation();
        glm::quat gq(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        rotation = glm::eulerAngles(gq);
    }
}

void RigidBody3D::on_destroy(Engine& engine) {
    if (!m_impl->valid) return;

    auto* ps = engine.physics().system();
    if (ps) {
        engine.physics().unregister_body(m_impl->body_id.GetIndexAndSequenceNumber());
        JPH::BodyInterface& bi = ps->GetBodyInterface();
        bi.RemoveBody(m_impl->body_id);
        bi.DestroyBody(m_impl->body_id);
    }
    m_impl->valid = false;
    m_body_ids.clear();
}

void RigidBody3D::apply_force(const glm::vec3& force) {
    if (!m_impl->valid) return;
    m_impl->ps->GetBodyInterface().AddForce(
        m_impl->body_id, JPH::Vec3(force.x, force.y, force.z));
}

void RigidBody3D::apply_impulse(const glm::vec3& impulse) {
    if (!m_impl->valid) return;
    m_impl->ps->GetBodyInterface().AddImpulse(
        m_impl->body_id, JPH::Vec3(impulse.x, impulse.y, impulse.z));
}

void RigidBody3D::apply_torque_impulse(const glm::vec3& torque) {
    if (!m_impl->valid) return;
    m_impl->ps->GetBodyInterface().AddAngularImpulse(
        m_impl->body_id, JPH::Vec3(torque.x, torque.y, torque.z));
}

glm::vec3 RigidBody3D::get_velocity() const {
    if (!m_impl->valid) return {};
    JPH::Vec3 v = m_impl->ps->GetBodyInterface().GetLinearVelocity(m_impl->body_id);
    return { v.GetX(), v.GetY(), v.GetZ() };
}

void RigidBody3D::set_velocity(const glm::vec3& v) {
    if (!m_impl->valid) return;
    m_impl->ps->GetBodyInterface().SetLinearVelocity(
        m_impl->body_id, JPH::Vec3(v.x, v.y, v.z));
}

glm::vec3 RigidBody3D::get_angular_velocity() const {
    if (!m_impl->valid) return {};
    JPH::Vec3 v = m_impl->ps->GetBodyInterface().GetAngularVelocity(m_impl->body_id);
    return { v.GetX(), v.GetY(), v.GetZ() };
}

void RigidBody3D::set_angular_velocity(const glm::vec3& v) {
    if (!m_impl->valid) return;
    m_impl->ps->GetBodyInterface().SetAngularVelocity(
        m_impl->body_id, JPH::Vec3(v.x, v.y, v.z));
}

void RigidBody3D::freeze_rotation(bool freeze) {
    if (!m_impl->valid) return;
    // Use angular damping to simulate rotation lock (works on all Jolt versions).
    JPH::BodyLockWrite lock(m_impl->ps->GetBodyLockInterface(), m_impl->body_id);
    if (lock.Succeeded()) {
        if (freeze) {
            lock.GetBody().GetMotionProperties()->SetAngularDamping(1e6f);
            lock.GetBody().GetMotionProperties()->SetAngularVelocity(JPH::Vec3::sZero());
        } else {
            lock.GetBody().GetMotionProperties()->SetAngularDamping(0.05f); // default
        }
    }
}

void RigidBody3D::set_kinematic(bool kinematic) {
    if (!m_impl->valid) return;
    m_impl->ps->GetBodyInterface().SetMotionType(
        m_impl->body_id,
        kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
        JPH::EActivation::Activate);
}

} // namespace sol
