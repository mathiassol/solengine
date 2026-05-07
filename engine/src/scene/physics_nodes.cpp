// physics_nodes.cpp — StaticBody3D and CharacterBody3D implementation.

#include "sol/scene/static_body3d.h"
#include "sol/scene/character_body3d.h"
#include "sol/scene/collision_shape3d.h"
#include "sol/engine.h"
#include "physics/physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
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
            m_body_ids.push_back(body->GetID().GetIndexAndSequenceNumber());
        }
    });
}

void StaticBody3D::on_destroy(Engine& engine) {
    auto* ps = engine.physics().system();
    if (!ps) return;

    JPH::BodyInterface& bi = ps->GetBodyInterface();
    for (uint32_t raw : m_body_ids) {
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

} // namespace sol
