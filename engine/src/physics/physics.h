#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <glm/glm.hpp>

namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class TempAllocator;
    class JobSystemThreadPool;
}

namespace sol {

class Node;
class Engine;
class Area3D;

// Result of a single closest-hit raycast.
struct RaycastHit {
    bool      hit      = false;
    glm::vec3 position {};
    glm::vec3 normal   {};
    float     distance = 0.f;
    Node*     node     = nullptr;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    bool init();
    void shutdown();
    void step(float dt);

    // Dispatch queued contact events to Area3D callbacks and script collision hooks.
    // Must be called from the main thread immediately after step().
    void dispatch_contacts(Engine& engine);

    JPH::PhysicsSystem*  system()     const { return m_system.get(); }
    JPH::TempAllocator&  temp_alloc() const;

    // ---- Body ↔ Node registry ----------------------------------------
    // Called by StaticBody3D / Area3D / RigidBody3D on on_ready / on_destroy.
    void  register_body  (uint32_t body_id, Node* node, bool is_sensor = false);
    void  unregister_body(uint32_t body_id);
    Node* node_for_body  (uint32_t body_id) const;
    bool  is_sensor_body (uint32_t body_id) const;

    // ---- Physics queries (call from main thread) ----------------------

    // Returns the closest hit along a ray, or hit==false if nothing was hit.
    // ignore: optional node whose bodies are skipped (e.g. the shooting character).
    RaycastHit raycast(const glm::vec3& origin,
                       const glm::vec3& dir,
                       float            max_dist,
                       Node*            ignore = nullptr) const;

    // Returns all nodes whose physics shapes overlap a sphere.
    std::vector<Node*> overlap_sphere(const glm::vec3& center, float radius) const;

    // ---- Contact event queue (written from Jolt worker threads) ------
    struct ContactEvent { uint32_t body_a, body_b; bool added; };
    void push_contact(const ContactEvent& e);

private:
    struct SolContactListener;

    std::unique_ptr<JPH::TempAllocatorImpl>   m_temp;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobs;
    std::unique_ptr<JPH::PhysicsSystem>       m_system;
    std::unique_ptr<SolContactListener>       m_contact_listener;
    bool m_initialized = false;

    std::unordered_map<uint32_t, Node*> m_body_to_node;
    std::unordered_set<uint32_t>        m_sensor_bodies;

    mutable std::mutex        m_contact_mutex;
    std::vector<ContactEvent> m_pending_contacts;
};

} // namespace sol
