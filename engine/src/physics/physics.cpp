#include "physics/physics.h"
#include "sol/log.h"
#include "sol/scene/area3d.h"
#include "sol/engine.h"
#include "sol/script/script_engine.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>

#include <cstdarg>
#include <cstdio>

JPH_SUPPRESS_WARNINGS

using namespace JPH;

namespace sol {

// ---- Layer setup -------------------------------------------------------
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

// Pass-all filters used for queries
class AllowAllBPFilter final : public BroadPhaseLayerFilter {
public:
    bool ShouldCollide(BroadPhaseLayer) const override { return true; }
};
class AllowAllObjFilter final : public ObjectLayerFilter {
public:
    bool ShouldCollide(ObjectLayer) const override { return true; }
};

// Singletons kept alive for the duration of PhysicsWorld
struct JoltStatics {
    BPLayerInterfaceImpl     bp_iface;
    ObjectVsBPFilter         obj_vs_bp;
    ObjectLayerPairFilterImpl obj_pair;
};
static JoltStatics* g_statics = nullptr;
static bool g_jolt_globals = false;

// ---- Contact listener --------------------------------------------------

struct PhysicsWorld::SolContactListener final : public ContactListener {
    PhysicsWorld* world = nullptr;
    explicit SolContactListener(PhysicsWorld* w) : world(w) {}

    void OnContactAdded(const Body& b1, const Body& b2,
                        const ContactManifold&, ContactSettings&) override {
        world->push_contact({
            b1.GetID().GetIndexAndSequenceNumber(),
            b2.GetID().GetIndexAndSequenceNumber(),
            true
        });
    }

    void OnContactRemoved(const SubShapeIDPair& pair) override {
        world->push_contact({
            pair.GetBody1ID().GetIndexAndSequenceNumber(),
            pair.GetBody2ID().GetIndexAndSequenceNumber(),
            false
        });
    }
};

// ---- PhysicsWorld public API -------------------------------------------

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

    m_contact_listener = std::make_unique<SolContactListener>(this);
    m_system->SetContactListener(m_contact_listener.get());

    m_initialized = true;
    SOL_INFO("Physics initialized (Jolt)");
    return true;
}

void PhysicsWorld::shutdown() {
    if (!m_initialized) return;
    m_system->SetContactListener(nullptr);
    m_contact_listener.reset();
    m_system.reset();
    m_jobs.reset();
    m_temp.reset();
    m_body_to_node.clear();
    m_sensor_bodies.clear();
    m_initialized = false;
}

JPH::TempAllocator& PhysicsWorld::temp_alloc() const { return *m_temp; }

void PhysicsWorld::step(float dt) {
    if (!m_initialized) return;
    constexpr int collision_steps = 1;
    m_system->Update(dt, collision_steps, m_temp.get(), m_jobs.get());
}

// ---- Body registry -----------------------------------------------------

void PhysicsWorld::register_body(uint32_t body_id, Node* node, bool is_sensor) {
    m_body_to_node[body_id] = node;
    if (is_sensor) m_sensor_bodies.insert(body_id);
}

void PhysicsWorld::unregister_body(uint32_t body_id) {
    m_body_to_node.erase(body_id);
    m_sensor_bodies.erase(body_id);
}

Node* PhysicsWorld::node_for_body(uint32_t body_id) const {
    auto it = m_body_to_node.find(body_id);
    return it != m_body_to_node.end() ? it->second : nullptr;
}

bool PhysicsWorld::is_sensor_body(uint32_t body_id) const {
    return m_sensor_bodies.count(body_id) > 0;
}

// ---- Contact event queue -----------------------------------------------

void PhysicsWorld::push_contact(const ContactEvent& e) {
    std::lock_guard<std::mutex> lock(m_contact_mutex);
    m_pending_contacts.push_back(e);
}

void PhysicsWorld::dispatch_contacts(Engine& engine) {
    std::vector<ContactEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_contact_mutex);
        events.swap(m_pending_contacts);
    }

    if (events.empty()) return;

    for (const auto& e : events) {
        Node* node_a = node_for_body(e.body_a);
        Node* node_b = node_for_body(e.body_b);
        if (!node_a || !node_b) continue;

        bool a_sensor = is_sensor_body(e.body_a);
        bool b_sensor = is_sensor_body(e.body_b);

        // Area3D sensor callbacks
        if (a_sensor) {
            if (auto* area = dynamic_cast<Area3D*>(node_a)) {
                if (e.added) area->_on_body_entered(node_b, engine);
                else         area->_on_body_exited (node_b, engine);
            }
        }
        if (b_sensor) {
            if (auto* area = dynamic_cast<Area3D*>(node_b)) {
                if (e.added) area->_on_body_entered(node_a, engine);
                else         area->_on_body_exited (node_a, engine);
            }
        }

        // Regular body-to-body collision callbacks (non-sensor only)
        if (!a_sensor && !b_sensor && engine.has_script()) {
            const char* cb = e.added ? "on_collision_enter" : "on_collision_exit";
            engine.script().node_event_with_node_arg(node_a, cb, node_b, engine);
            engine.script().node_event_with_node_arg(node_b, cb, node_a, engine);
        }
    }
}

// ---- Raycast -----------------------------------------------------------

// Body filter that skips all bodies belonging to a specific node.
class IgnoreNodeBodyFilter final : public BodyFilter {
    const PhysicsWorld& m_world;
    const Node*         m_ignore;
public:
    IgnoreNodeBodyFilter(const PhysicsWorld& w, const Node* n)
        : m_world(w), m_ignore(n) {}

    bool ShouldCollide(const BodyID& id) const override {
        if (!m_ignore) return true;
        return m_world.node_for_body(id.GetIndexAndSequenceNumber()) != m_ignore;
    }
    bool ShouldCollideLocked(const Body&) const override { return true; }
};

RaycastHit PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& dir,
                                  float max_dist, Node* ignore) const {
    RaycastHit result;
    if (!m_system) return result;

    // Normalise direction defensively
    glm::vec3 d = (glm::length(dir) > 1e-6f) ? glm::normalize(dir) : glm::vec3{0,0,-1};

    RRayCast ray{
        RVec3(origin.x, origin.y, origin.z),
        Vec3(d.x * max_dist, d.y * max_dist, d.z * max_dist)
    };

    RayCastResult hit;
    AllowAllBPFilter    bp_filter;
    AllowAllObjFilter   obj_filter;
    IgnoreNodeBodyFilter body_filter(*this, ignore);

    if (!m_system->GetNarrowPhaseQuery().CastRay(ray, hit, bp_filter, obj_filter, body_filter))
        return result;

    result.hit      = true;
    result.distance = hit.mFraction * max_dist;
    result.node     = node_for_body(hit.mBodyID.GetIndexAndSequenceNumber());

    RVec3 hit_pos = ray.GetPointOnRay(hit.mFraction);
    result.position = { (float)hit_pos.GetX(), (float)hit_pos.GetY(), (float)hit_pos.GetZ() };

    // Retrieve surface normal from the body
    BodyLockRead lock(m_system->GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) {
        Vec3 norm = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hit_pos);
        result.normal = { norm.GetX(), norm.GetY(), norm.GetZ() };
    }

    return result;
}

// ---- Overlap sphere ----------------------------------------------------

class SphereBodyCollector final : public CollideShapeBodyCollector {
    const PhysicsWorld& m_world;
    std::vector<Node*>& m_out;
public:
    SphereBodyCollector(const PhysicsWorld& w, std::vector<Node*>& out)
        : m_world(w), m_out(out) {}

    void AddHit(const BodyID& id) override {
        if (Node* n = m_world.node_for_body(id.GetIndexAndSequenceNumber()))
            m_out.push_back(n);
    }
};

std::vector<Node*> PhysicsWorld::overlap_sphere(const glm::vec3& center, float radius) const {
    std::vector<Node*> results;
    if (!m_system) return results;

    AllowAllBPFilter   bp_filter;
    AllowAllObjFilter  obj_filter;
    SphereBodyCollector collector(*this, results);

    m_system->GetBroadPhaseQuery().CollideSphere(
        Vec3(center.x, center.y, center.z),
        radius, collector, bp_filter, obj_filter);

    return results;
}

} // namespace sol
