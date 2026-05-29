#pragma once
#include "sol/export.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

struct lua_State;

namespace sol {

class Engine;
class Node;
class Node3D;
class LuaComponent;

class SOL_API ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&)            = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    bool init(Engine& engine);
    void shutdown();

    bool is_initialized() const { return m_lua != nullptr; }

    bool exec_string(const std::string& code);
    bool exec_file(const std::string& path);

    void node_ready(Node* node, Engine& engine);
    void node_update(Node* node, Engine& engine, float dt);
    void node_detach(Node* node);

    void component_ready (LuaComponent* comp, Engine& engine);
    void component_update(LuaComponent* comp, Engine& engine, float dt);
    void component_detach(LuaComponent* comp);

    // Hot-reload a script by path — detaches and re-initialises all instances using it.
    void reload_script(const std::string& path);

    // Fire a named callback on a node's script, passing another Node* as the argument.
    // Used for collision / area enter-exit callbacks.
    void node_event_with_node_arg(Node* target, const char* method, Node* arg, Engine& engine);

    // Timer tick — call each frame from engine.cpp
    void update_timers(float dt);
    int  add_timer(float duration, int cb_ref, bool repeating);
    void cancel_timer(int id);

    // Node pool helpers (used by Lua create_node / add_node)
    void track_pending_node(std::unique_ptr<Node> node);
    std::unique_ptr<Node> take_pending_node(Node* raw);

    lua_State* state() const { return m_lua; }

private:
    struct ScriptInstance {
        int instance_ref = -1;
        bool has_on_ready = false;
        bool has_on_update = false;
        bool has_on_destroy = false;
    };

    lua_State* m_lua = nullptr;
    Engine* m_engine = nullptr;

    // Timer system
    struct LuaTimer {
        int   id           = 0;
        float remaining    = 0.0f;
        float interval     = 0.0f;  // >0 = repeating
        int   callback_ref = -1;
        bool  dead         = false;
    };
    int m_next_timer_id = 1;
    std::vector<LuaTimer> m_timers;

    // Pending node pool (owned before parenting via add_node)
    std::unordered_map<Node*, std::unique_ptr<Node>> m_pending_nodes;

    void register_bindings();
    bool load_script_table(const std::string& path, int& out_ref);
    bool ensure_instance(Node* node, Engine& engine);
    bool ensure_component_instance(LuaComponent* comp, Engine& engine);
    void call_method(int instance_ref, const char* method, float arg = 0.0f, bool has_arg = false);

    std::unordered_map<Node*, ScriptInstance>          m_instances;
    std::unordered_map<std::string, int>               m_script_cache;
    std::unordered_map<LuaComponent*, ScriptInstance>  m_comp_instances;
};

} // namespace sol
