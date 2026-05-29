#pragma once
#include "sol/export.h"
#include "sol/scene/node3d.h"

namespace sol {

class Engine;

// Actor: convenience Node3D subclass with UE5-style lifecycle names.
// Derive from Actor when you want begin_play/end_play/tick naming.
// Component management is inherited from Node (add_component, get_component<T>).
class SOL_API Actor : public Node3D {
public:
    const char* type_name() const override { return "Actor"; }

    // UE5-style lifecycle — override these instead of on_ready/on_update/on_destroy.
    virtual void begin_play(Engine& engine)           {}
    virtual void end_play  (Engine& engine)           {}
    virtual void tick      (Engine& engine, float dt) {}

    // Maps UE5 lifecycle names to Sol lifecycle.
    void on_ready  (Engine& engine)           override;
    void on_update (Engine& engine, float dt) override;
    void on_destroy(Engine& engine)           override;
};

} // namespace sol
