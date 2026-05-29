#pragma once
#include "sol/export.h"
#include <cstdint>

namespace sol {

class Engine;
class Node;

// ---------------------------------------------------------------------------
//  ETickGroup
//
//  Controls when a node or component receives its on_update call.
//  Mirrors UE5's tick group ordering: physics sits between PrePhysics and
//  PostPhysics so game logic that reads rigid-body results runs at the right
//  time.
//
//  Stage 0: all existing nodes default to PrePhysics (no behaviour change).
//  Stage 1: engine.cpp splits the update loop to step Jolt between phases.
// ---------------------------------------------------------------------------
enum class ETickGroup : uint8_t {
    PrePhysics     = 0,  // default — logic before physics simulation step
    DuringPhysics  = 1,  // reserved for future physics-concurrent reads
    PostPhysics    = 2,  // after Jolt step — safe to read rigid-body results
    PostUpdateWork = 3,  // final pass: cameras, VFX, UI late-update
};

// ---------------------------------------------------------------------------
//  IComponent
//
//  Base class for all attachable behaviour blocks.
//  A component is owned by a Node (stored in Node::m_components).
//  Lifecycle is dispatched by Scene traversal alongside the node lifecycle.
//
//  Equivalent to UActorComponent in UE5.
//
//  Usage:
//    struct HealthComponent : sol::IComponent {
//        const char* component_type() const override { return "HealthComponent"; }
//        void on_update(sol::Engine&, float dt) override { hp -= drain * dt; }
//        float hp = 100.f, drain = 1.f;
//    };
//    node->add_component(std::make_unique<HealthComponent>());
// ---------------------------------------------------------------------------
class SOL_API IComponent {
public:
    virtual ~IComponent() = default;

    // Unique type name used for lookup and serialisation.
    virtual const char* component_type() const = 0;

    // Which tick phase this component's on_update runs in.
    // In Stage 0 all components run in the single update pass regardless;
    // Stage 1 will honour the group when the loop is split.
    virtual ETickGroup tick_group() const { return ETickGroup::PrePhysics; }

    // Lifecycle — mirrors Node lifecycle, called by Scene traversal.
    virtual void on_ready  (Engine& /*engine*/)           {}
    virtual void on_update (Engine& /*engine*/, float /*dt*/) {}
    virtual void on_destroy(Engine& /*engine*/)           {}

// Stage 3-C infrastructure: parallel tick flag.
    // Scene traversal will honour this in Stage 4; currently informational only.
    bool can_tick_in_parallel = false;

    // Back-pointer to owning node, set by Node::add_component().
    Node* owner = nullptr;
};

} // namespace sol
