#pragma once
#include "sol/scene/node3d.h"
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace sol {

class Engine;

// Sensor (trigger) volume.  CollisionShape3D children define its shape.
// Unlike StaticBody3D the Jolt bodies are created with mIsSensor = true so
// they do not generate contact forces — only enter/exit events.
//
// Lua callbacks — define any of these in a script attached to Area3D
// (via script_path) OR in a script on the direct parent node:
//
//   function on_body_entered(self, other_node)  end
//   function on_body_exited (self, other_node)  end
//
// Lua methods exposed:
//   area:get_overlapping_bodies()     -> table of Node* (current overlaps)
//   area:is_overlapping_with(node)    -> bool
//   area:overlap_count()              -> int
class SOL_API Area3D : public Node3D {
public:
    const char* type_name() const override { return "Area3D"; }

    void on_ready  (Engine& engine) override;
    void on_destroy(Engine& engine) override;

    // Query current overlaps from Lua/C++.
    std::vector<Node*> get_overlapping_bodies() const;
    bool               is_overlapping_with(Node* node) const;
    int                overlap_count()                  const;

    // Called by PhysicsWorld::dispatch_contacts — not meant for direct user use.
    void _on_body_entered(Node* other, Engine& engine);
    void _on_body_exited (Node* other, Engine& engine);

private:
    std::vector<uint32_t>     m_body_ids;
    std::unordered_set<Node*> m_overlapping;
};

} // namespace sol
