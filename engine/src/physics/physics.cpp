#include "physics/physics.h"
#include "sol/log.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <cstdarg>
#include <cstdio>

JPH_SUPPRESS_WARNINGS

using namespace JPH;

namespace sol {

// ---- Layer setup (kept tiny; can be expanded by games later) ----
namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING     = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
}
namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint32_t NUM_LAYERS = 2;
}

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override {
        return l == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "x"; }
#endif
};

class ObjectVsBPFilter final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer a, BroadPhaseLayer b) const override {
        if (a == Layers::NON_MOVING) return b == BroadPhaseLayers::MOVING;
        return true;
    }
};

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        if (a == Layers::NON_MOVING) return b == Layers::MOVING;
        return true;
    }
};

// Singletons kept alive for the duration of PhysicsWorld
struct JoltStatics {
    BPLayerInterfaceImpl     bp_iface;
    ObjectVsBPFilter         obj_vs_bp;
    ObjectLayerPairFilterImpl obj_pair;
};
static JoltStatics* g_statics = nullptr;

static bool g_jolt_globals = false;

PhysicsWorld::PhysicsWorld()  = default;
PhysicsWorld::~PhysicsWorld() = default;

bool PhysicsWorld::init() {
    if (!g_jolt_globals) {
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();
        g_jolt_globals = true;
    }
    if (!g_statics) g_statics = new JoltStatics();

    m_temp   = std::make_unique<TempAllocatorImpl>(10 * 1024 * 1024);
    m_jobs   = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, 2);
    m_system = std::make_unique<PhysicsSystem>();
    m_system->Init(2048, 0, 4096, 1024,
                   g_statics->bp_iface, g_statics->obj_vs_bp, g_statics->obj_pair);

    m_initialized = true;
    SOL_INFO("Physics initialized (Jolt)");
    return true;
}

void PhysicsWorld::shutdown() {
    if (!m_initialized) return;
    m_system.reset();
    m_jobs.reset();
    m_temp.reset();
    m_initialized = false;
    // Globals (Factory, statics) intentionally kept until process exit
    // to allow re-init without leaking.
}

JPH::TempAllocator& PhysicsWorld::temp_alloc() const { return *m_temp; }

void PhysicsWorld::step(float dt) {
    if (!m_initialized) return;
    constexpr int collision_steps = 1;
    m_system->Update(dt, collision_steps, m_temp.get(), m_jobs.get());
}

} // namespace sol
