#pragma once
#include "sol/export.h"
#include "sol/scene/component.h"
#include <string>

struct lua_State;

namespace sol {

class Engine;
class ScriptEngine;

// LuaComponent: a Lua-scripted IComponent.
// The script follows the same convention as node scripts:
//   - returns a table M with M.__index = M
//   - function M:on_ready()  -- self.node = owner node userdata
//   - function M:on_update(dt)
//   - function M:on_destroy()
//   - 'engine' global is available
//
// Attach from C++: node->add_component(std::make_unique<LuaComponent>("scripts/health.lua"));
// Attach from Lua: node:add_lua_component("scripts/health.lua")
class SOL_API LuaComponent : public IComponent {
public:
    explicit LuaComponent(std::string script_path);

    const char* component_type() const override { return "LuaComponent"; }
    ETickGroup  tick_group()     const override { return m_tick_group; }

    void on_ready  (Engine& engine)           override;
    void on_update (Engine& engine, float dt) override;
    void on_destroy(Engine& engine)           override;

    const std::string& script_path() const { return m_script_path; }
    void set_script_path(const std::string& p) { m_script_path = p; }
    void set_tick_group(ETickGroup g)       { m_tick_group = g; }

private:
    std::string m_script_path;
    ETickGroup  m_tick_group   = ETickGroup::PrePhysics;
    int         m_instance_ref = -1;  // LUA_NOREF equivalent; managed by ScriptEngine

    friend class ScriptEngine;
};

} // namespace sol
