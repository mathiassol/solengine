#pragma once
#include "sol/export.h"
#include "sol/scene/component.h"  // ETickGroup
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace sol {

// UE5 FMassProcessor equivalent.
// Subclass this and register with EcsWorld::register_processor().
class SOL_API IProcessor {
public:
    virtual ~IProcessor() = default;

    // Unique name for dependency ordering and debugging.
    virtual const char* processor_name() const = 0;

    // Tick phase (default: PostPhysics — most simulation logic).
    virtual ETickGroup tick_group() const { return ETickGroup::PostPhysics; }

    // Whether this processor's execute() is thread-safe and may run in parallel
    // with other CanTickInParallel processors in the same tick group.
    virtual bool can_tick_in_parallel() const { return false; }

    // Names of processors this one must run AFTER (within same tick group).
    virtual std::vector<std::string> run_after() const { return {}; }

    // The actual work. Called by EcsWorld::tick_processors().
    virtual void execute(entt::registry& registry, float dt) = 0;

    // Optional: called once after registration. Use to cache views/archetypes.
    virtual void initialize(entt::registry& /*registry*/) {}
};

} // namespace sol
