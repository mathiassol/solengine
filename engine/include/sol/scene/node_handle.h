#pragma once
#include "sol/export.h"
#include <cstdint>
#include <vector>

namespace sol {

class Node;

// ---------------------------------------------------------------------------
//  NodeHandle
//
//  Stable, stale-safe reference to a Node.
//  Uses an index + generation counter so that holding a handle to a destroyed
//  node returns nullptr instead of a dangling pointer.
//
//  Equivalent to the index+generation pattern in UE5's FMassEntityHandle and
//  most modern ECS implementations.
//
//  Usage:
//    NodeHandle h = engine.node_registry().register_node(my_node);
//    // ... later, even after the node might be deleted:
//    Node* n = engine.node_registry().resolve(h); // nullptr if stale
// ---------------------------------------------------------------------------
struct SOL_API NodeHandle {
    static constexpr uint32_t INVALID = 0u;

    uint32_t index      = INVALID;
    uint32_t generation = 0u;

    bool is_valid() const { return index != INVALID; }

    bool operator==(const NodeHandle& o) const {
        return index == o.index && generation == o.generation;
    }
    bool operator!=(const NodeHandle& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
//  NodeRegistry
//
//  Central, per-engine mapping from NodeHandle to Node*.
//  Stored on Engine; one registry per engine/world.
//
//  Thread safety: not thread-safe in Stage 0; all calls must be on the main
//  thread.  Stage 3 adds a read-lock for the task-graph.
// ---------------------------------------------------------------------------
class SOL_API NodeRegistry {
public:
    NodeRegistry();

    // Register a node and return its new stable handle.
    NodeHandle register_node(Node* node);

    // Unregister a node — bumps the generation so existing handles become
    // stale.  The slot is recycled for the next registration.
    void unregister_node(NodeHandle handle);

    // Resolve — returns nullptr if the handle is stale, invalid, or the node
    // was already unregistered.
    Node* resolve(NodeHandle handle) const;

    bool is_alive(NodeHandle handle) const;

    // Number of currently live nodes in the registry.
    uint32_t live_count() const { return m_live; }

private:
    struct Slot {
        Node*    node       = nullptr;
        uint32_t generation = 0u;
        bool     occupied   = false;
    };

    std::vector<Slot>     m_slots;
    std::vector<uint32_t> m_free_list;
    uint32_t              m_live = 0u;
};

} // namespace sol
