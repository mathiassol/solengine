#pragma once
#include "sol/scene/node3d.h"
#include "sol/export.h"

namespace sol {

// ScriptNode: a node whose behavior is entirely defined by a Lua script.
// script_path (inherited from Node) points to the .lua file.
// In Stage 1, the ScriptEngine will call on_ready/on_update/on_render/on_destroy
// from the Lua script.
class SOL_API ScriptNode : public Node3D {
public:
    const char* type_name() const override { return "ScriptNode"; }
    ~ScriptNode() override = default;
};

} // namespace sol
