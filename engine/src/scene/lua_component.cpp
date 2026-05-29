#include "sol/scene/lua_component.h"
#include "sol/engine.h"
#include "sol/script/script_engine.h"

namespace sol {

LuaComponent::LuaComponent(std::string path) : m_script_path(std::move(path)) {}

void LuaComponent::on_ready(Engine& engine) {
    if (engine.has_script())
        engine.script().component_ready(this, engine);
}

void LuaComponent::on_update(Engine& engine, float dt) {
    if (engine.has_script())
        engine.script().component_update(this, engine, dt);
}

void LuaComponent::on_destroy(Engine& engine) {
    if (engine.has_script())
        engine.script().component_detach(this);
}

} // namespace sol
