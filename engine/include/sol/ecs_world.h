#pragma once
#include "sol/export.h"
#include "sol/scene/component.h"   // ETickGroup
#include "sol/scene/processor.h"   // IProcessor  (Stage 3-A)
#include "sol/ecs/mass_command_buffer.h"  // MassCommandBuffer (Stage 3-A)
#include <entt/entt.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sol {

// ---------------------------------------------------------------------------
//  EcsWorld
//
//  Lightweight wrapper around entt::registry that adds a named system
//  registration and ordered execution pipeline.
//
//  Equivalent in spirit to UE5's UMassProcessingPhaseManager — processors
//  (systems) declare their tick group and are executed in registration order
//  within each group.
//
//  Usage:
//    auto& ecs = engine.ecs_world();
//
//    // Register a fragment type on an entity:
//    auto e = ecs.registry().create();
//    ecs.registry().emplace<VelocityFragment>(e, glm::vec3{1,0,0});
//
//    // Register a system to process it:
//    ecs.register_system("MoveSystem", ETickGroup::PostPhysics,
//        [](entt::registry& r, float dt) {
//            for (auto [e, v, t] : r.view<VelocityFragment, TransformFragment>().each())
//                t.position += v.velocity * dt;
//        });
//
//    // Tick from engine loop (Stage 1 wires this into the phase loop):
//    ecs.tick(ETickGroup::PostPhysics, dt);
// ---------------------------------------------------------------------------
class SOL_API EcsWorld {
public:
    using SystemFn = std::function<void(entt::registry&, float)>;

    EcsWorld()  = default;
    ~EcsWorld() = default;

    EcsWorld(const EcsWorld&)            = delete;
    EcsWorld& operator=(const EcsWorld&) = delete;

    entt::registry&       registry()       { return m_registry; }
    const entt::registry& registry() const { return m_registry; }

    // Register a system that ticks during the given group.
    // If a system with the same name already exists it is replaced.
    // Systems within the same group run in registration order.
    void register_system(std::string name, ETickGroup group, SystemFn fn);

    // Remove a system by name (no-op if not found).
    void unregister_system(const std::string& name);

    // Execute all systems registered for the given tick group.
    void tick(ETickGroup group, float dt);

    // Execute systems for ALL groups in order
    // (PrePhysics → DuringPhysics → PostPhysics → PostUpdateWork).
    void tick_all(float dt);

    uint32_t system_count() const { return static_cast<uint32_t>(m_systems.size()); }

    // ---- Processor pipeline (Stage 3-A) ------------------------------------
    // Register an IProcessor. Calls initialize() immediately.
    // Replaces any existing processor with the same processor_name().
    void register_processor(std::unique_ptr<IProcessor> p);

    // Remove a processor by name (no-op if not found).
    void unregister_processor(const std::string& name);

    // Execute all processors registered for the given tick group in dependency
    // order; processors flagged can_tick_in_parallel() run via std::async.
    // Flushes the shared MassCommandBuffer after all processors finish.
    void tick_processors(ETickGroup group, float dt);

    // Shared command buffer — auto-flushed after each tick_processors() call.
    MassCommandBuffer& command_buffer() { return m_command_buffer; }

    uint32_t processor_count() const { return static_cast<uint32_t>(m_processors.size()); }

private:
    struct SystemEntry {
        std::string name;
        ETickGroup  group;
        SystemFn    fn;
    };

    entt::registry           m_registry;
    std::vector<SystemEntry> m_systems;

    // Stage 3-A
    std::vector<std::unique_ptr<IProcessor>> m_processors;
    MassCommandBuffer                        m_command_buffer;
};

} // namespace sol
