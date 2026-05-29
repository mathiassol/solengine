#include "sol/script/script_engine.h"
#include "sol/engine.h"
#include "sol/input/input_manager.h"
#include "sol/log.h"
#include "sol/scene/node.h"
#include "sol/scene/node3d.h"
#include "sol/scene/mesh_node.h"
#include "sol/scene/light_node.h"
#include "sol/scene/camera3d.h"
#include "sol/scene/script_node.h"
#include "sol/scene/character_body3d.h"
#include "sol/scene/area3d.h"
#include "sol/scene/rigid_body3d.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/scene.h"
#include "sol/scene/lua_component.h"
#include "sol/scene/audio_stream_player.h"
#include "sol/scene/audio_stream_player3d.h"
#include "sol/audio/audio_engine.h"
#include "physics/physics.h"
#include "platform/window.h"
#include "sol/scene/node_factory.h"
#include "sol/scene/model_node.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <LuaBridge/LuaBridge.h>
#include <cstdio>
#include <string>

namespace sol {

static glm::vec3 vec3_add(const glm::vec3& a, const glm::vec3& b) { return a + b; }
static glm::vec3 vec3_sub(const glm::vec3& a, const glm::vec3& b) { return a - b; }
static glm::vec3 vec3_mul_scalar(const glm::vec3& a, float s)     { return a * s; }
static float vec3_length(const glm::vec3& v)                       { return glm::length(v); }
static glm::vec3 vec3_normalized(const glm::vec3& v)               { return glm::normalize(v); }
static float vec3_dot(const glm::vec3& a, const glm::vec3& b)      { return glm::dot(a, b); }
static glm::vec3 vec3_cross(const glm::vec3& a, const glm::vec3& b){ return glm::cross(a, b); }
static std::string vec3_tostring(const glm::vec3& v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Vec3(%.3f, %.3f, %.3f)", v.x, v.y, v.z);
    return buf;
}
static glm::vec3 vec3_new(float x, float y, float z) { return {x, y, z}; }

static Node* node_find(Node* self, const std::string& name)          { return self ? self->find(name) : nullptr; }
static Node* node_parent(Node* self)                                  { return self ? self->parent() : nullptr; }
static std::string node_get_name(const Node* self)                    { return self ? self->name : ""; }
static void node_set_name(Node* self, const std::string& n)           { if (self) self->name = n; }
static std::string node_get_script(const Node* self)                  { return self ? self->script_path : ""; }
static void node_set_script(Node* self, const std::string& p)         { if (self) self->script_path = p; }
static std::string node_type_name(const Node* self)                   { return self ? self->type_name() : ""; }
static int node_child_count(const Node* self)                         { return self ? static_cast<int>(self->children().size()) : 0; }
static Node* node_get_child(Node* self, int i) {
    if (!self || i < 0 || i >= static_cast<int>(self->children().size()))
        return nullptr;
    return self->children()[static_cast<size_t>(i)].get();
}

// Node3D::find returns Node3D* so scripts can access spatial properties on found nodes.
static Node3D* n3d_find(Node3D* self, const std::string& name) {
    if (!self) return nullptr;
    return dynamic_cast<Node3D*>(self->find(name));
}
static Node3D* n3d_parent(Node3D* self) {
    if (!self) return nullptr;
    return dynamic_cast<Node3D*>(self->parent());
}

static glm::vec3 n3d_get_pos(const Node3D* n)        { return n ? n->position : glm::vec3{}; }
static void n3d_set_pos(Node3D* n, glm::vec3 v)      { if (n) n->position = v; }
static glm::vec3 n3d_get_rot(const Node3D* n)        { return n ? n->rotation : glm::vec3{}; }
static void n3d_set_rot(Node3D* n, glm::vec3 v)      { if (n) n->rotation = v; }
static glm::vec3 n3d_get_scl(const Node3D* n)        { return n ? n->scale : glm::vec3{1.0f, 1.0f, 1.0f}; }
static void n3d_set_scl(Node3D* n, glm::vec3 v)      { if (n) n->scale = v; }
static glm::vec3 n3d_forward(const Node3D* n)        { return n ? n->forward() : glm::vec3{0.0f, 0.0f, -1.0f}; }
static glm::vec3 n3d_right(const Node3D* n)          { return n ? n->right() : glm::vec3{1.0f, 0.0f, 0.0f}; }
static glm::vec3 n3d_up(const Node3D* n)             { return n ? n->up() : glm::vec3{0.0f, 1.0f, 0.0f}; }

static float n3d_get_px(const Node3D* n) { return n ? n->position.x : 0.0f; }
static void n3d_set_px(Node3D* n, float v) { if (n) n->position.x = v; }
static float n3d_get_py(const Node3D* n) { return n ? n->position.y : 0.0f; }
static void n3d_set_py(Node3D* n, float v) { if (n) n->position.y = v; }
static float n3d_get_pz(const Node3D* n) { return n ? n->position.z : 0.0f; }
static void n3d_set_pz(Node3D* n, float v) { if (n) n->position.z = v; }
static float n3d_get_rx(const Node3D* n) { return n ? n->rotation.x : 0.0f; }
static void n3d_set_rx(Node3D* n, float v) { if (n) n->rotation.x = v; }
static float n3d_get_ry(const Node3D* n) { return n ? n->rotation.y : 0.0f; }
static void n3d_set_ry(Node3D* n, float v) { if (n) n->rotation.y = v; }
static float n3d_get_rz(const Node3D* n) { return n ? n->rotation.z : 0.0f; }
static void n3d_set_rz(Node3D* n, float v) { if (n) n->rotation.z = v; }

static std::string mesh_get_name(const MeshNode* n) { return n ? n->mesh_name : ""; }
static void mesh_set_name(MeshNode* n, const std::string& s) { if (n) n->mesh_name = s; }
static std::string mesh_get_path(const MeshNode* n) { return n ? n->mesh_path : ""; }
static void mesh_set_path(MeshNode* n, const std::string& s) { if (n) n->mesh_path = s; }

static glm::vec3 pl_get_color(const PointLight* n)         { return n ? n->color : glm::vec3{1.0f}; }
static void pl_set_color(PointLight* n, glm::vec3 v)       { if (n) n->color = v; }
static float pl_get_intensity(const PointLight* n)         { return n ? n->intensity : 1.0f; }
static void pl_set_intensity(PointLight* n, float v)       { if (n) n->intensity = v; }
static float pl_get_range(const PointLight* n)             { return n ? n->range : 10.0f; }
static void pl_set_range(PointLight* n, float v)           { if (n) n->range = v; }

static glm::vec3 dl_get_color(const DirectionalLight* n)   { return n ? n->color : glm::vec3{1.0f}; }
static void dl_set_color(DirectionalLight* n, glm::vec3 v) { if (n) n->color = v; }
static float dl_get_intensity(const DirectionalLight* n)   { return n ? n->intensity : 1.0f; }
static void dl_set_intensity(DirectionalLight* n, float v) { if (n) n->intensity = v; }

// CharacterBody3D helpers
static bool      cb3d_is_on_ground   (CharacterBody3D* n)               { return n && n->is_on_ground(); }
static glm::vec3 cb3d_get_velocity   (CharacterBody3D* n)               { return n ? n->get_velocity() : glm::vec3{}; }
static void      cb3d_move_and_slide (CharacterBody3D* n, glm::vec3 v)  { if (n) n->move_and_slide(v); }

// Area3D helpers
// get_overlapping_bodies() returns a Lua array-table of node userdata.
static int area3d_get_overlapping_bodies(lua_State* L) {
    auto area_res = luabridge::get<Area3D*>(L, 1);
    if (!area_res || !*area_res) { lua_newtable(L); return 1; }
    Area3D* area = *area_res;

    auto bodies = area->get_overlapping_bodies();
    lua_newtable(L);
    int idx = 1;
    for (Node* n : bodies) {
        if      (auto* rb = dynamic_cast<RigidBody3D*>(n))      luabridge::push(L, rb);
        else if (auto* cb = dynamic_cast<CharacterBody3D*>(n))  luabridge::push(L, cb);
        else if (auto* a3 = dynamic_cast<Area3D*>(n))           luabridge::push(L, a3);
        else if (auto* n3 = dynamic_cast<Node3D*>(n))           luabridge::push(L, n3);
        else                                                     luabridge::push(L, n);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}
static bool area3d_is_overlapping_with(Area3D* a, Node* n) { return a && a->is_overlapping_with(n); }
static int  area3d_overlap_count      (Area3D* a)           { return a ? a->overlap_count() : 0; }

// Cursor / capture helpers
static float engine_cursor_x(Engine* e) {
    if (!e) return 0.0f;
    double x = 0.0, y = 0.0; e->cursor_position(x, y); return static_cast<float>(x);
}
static float engine_cursor_y(Engine* e) {
    if (!e) return 0.0f;
    double x = 0.0, y = 0.0; e->cursor_position(x, y); return static_cast<float>(y);
}
static void engine_set_cursor_captured(Engine* e, bool c) { if (e) e->set_cursor_captured(c); }

static float cam_get_fov(const Camera3D* n)         { return n ? n->fov : 70.0f; }
static void cam_set_fov(Camera3D* n, float v)       { if (n) n->fov = v; }
static float cam_get_near(const Camera3D* n)        { return n ? n->near_clip : 0.05f; }
static void cam_set_near(Camera3D* n, float v)      { if (n) n->near_clip = v; }
static float cam_get_far(const Camera3D* n)         { return n ? n->far_clip : 1000.0f; }
static void cam_set_far(Camera3D* n, float v)       { if (n) n->far_clip = v; }
static bool cam_get_current(const Camera3D* n)      { return n ? n->current : false; }
static void cam_set_current(Camera3D* n, bool v)    { if (n) n->current = v; }

static float engine_dt     (const Engine* e) { return e ? e->delta_time()   : 0.0f; }
static float engine_elapsed(const Engine* e) { return e ? e->elapsed_time() : 0.0f; }
static void engine_quit(Engine* e) { if (e) e->quit(); }
static bool engine_key(Engine* e, int k) { return e && e->key_down(k); }
static bool engine_mouse(Engine* e, int b) { return e && e->mouse_button_down(b); }
static void engine_load_scene(Engine* e, const std::string& p) {
    if (e)
        e->scene_manager().load_scene(p, *e);
}
static Node* engine_find_node(Engine* e, const std::string& name) {
    if (!e)
        return nullptr;
    auto* scene = e->scene_manager().current_scene();
    if (!scene || !scene->root())
        return nullptr;
    return scene->root()->find(name);
}
static void engine_log(Engine*, const std::string& msg) {
    sol::log::info("[Lua] " + msg);
}

// AudioStreamPlayer helpers
static std::string asp_get_clip (const AudioStreamPlayer* n) { return n ? n->clip_path : ""; }
static void asp_set_clip (AudioStreamPlayer* n, const std::string& s) { if (n) n->clip_path = s; }
static float asp_get_vol (const AudioStreamPlayer* n) { return n ? n->volume : 1.0f; }
static void asp_set_vol  (AudioStreamPlayer* n, float v) { if (n) { n->volume = v; n->apply_params(); } }
static float asp_get_pitch(const AudioStreamPlayer* n) { return n ? n->pitch : 1.0f; }
static void asp_set_pitch (AudioStreamPlayer* n, float v) { if (n) { n->pitch = v; n->apply_params(); } }
static bool asp_get_loop (const AudioStreamPlayer* n) { return n && n->loop; }
static void asp_set_loop (AudioStreamPlayer* n, bool v) { if (n) { n->loop = v; n->apply_params(); } }
static bool asp_get_autoplay(const AudioStreamPlayer* n) { return n && n->autoplay; }
static void asp_set_autoplay(AudioStreamPlayer* n, bool v) { if (n) n->autoplay = v; }
static std::string asp_get_bus(const AudioStreamPlayer* n) { return n ? n->bus : "SFX"; }
static void asp_set_bus(AudioStreamPlayer* n, const std::string& s) { if (n) n->bus = s; }
static void asp_play      (AudioStreamPlayer* n) { if (n) n->play(); }
static void asp_stop      (AudioStreamPlayer* n) { if (n) n->stop(); }
static void asp_pause     (AudioStreamPlayer* n) { if (n) n->pause(); }
static bool asp_is_playing(AudioStreamPlayer* n) { return n && n->is_playing(); }

// AudioStreamPlayer3D helpers
static std::string asp3_get_clip(const AudioStreamPlayer3D* n) { return n ? n->clip_path : ""; }
static void asp3_set_clip(AudioStreamPlayer3D* n, const std::string& s) { if (n) n->clip_path = s; }
static float asp3_get_vol(const AudioStreamPlayer3D* n) { return n ? n->volume : 1.0f; }
static void asp3_set_vol (AudioStreamPlayer3D* n, float v) { if (n) { n->volume = v; n->apply_params(); } }
static float asp3_get_pitch(const AudioStreamPlayer3D* n) { return n ? n->pitch : 1.0f; }
static void asp3_set_pitch (AudioStreamPlayer3D* n, float v) { if (n) { n->pitch = v; n->apply_params(); } }
static bool asp3_get_loop(const AudioStreamPlayer3D* n) { return n && n->loop; }
static void asp3_set_loop(AudioStreamPlayer3D* n, bool v) { if (n) { n->loop = v; n->apply_params(); } }
static float asp3_get_maxdist(const AudioStreamPlayer3D* n) { return n ? n->max_distance : 20.0f; }
static void asp3_set_maxdist (AudioStreamPlayer3D* n, float v) { if (n) { n->max_distance = v; n->apply_params(); } }
static float asp3_get_atten(const AudioStreamPlayer3D* n) { return n ? n->attenuation : 1.0f; }
static void asp3_set_atten (AudioStreamPlayer3D* n, float v) { if (n) { n->attenuation = v; n->apply_params(); } }
static void asp3_play      (AudioStreamPlayer3D* n) { if (n) n->play(); }
static void asp3_stop      (AudioStreamPlayer3D* n) { if (n) n->stop(); }
static void asp3_pause     (AudioStreamPlayer3D* n) { if (n) n->pause(); }
static bool asp3_is_playing(AudioStreamPlayer3D* n) { return n && n->is_playing(); }

// AudioEngine helpers
static void ae_set_master_vol(AudioEngine* ae, float v) { if (ae) ae->set_master_volume(v); }
static float ae_get_master_vol(AudioEngine* ae) { return ae ? ae->get_master_volume() : 1.0f; }
static void ae_set_bus_vol(AudioEngine* ae, const std::string& b, float v) { if (ae) ae->set_bus_volume(b, v); }
static float ae_get_bus_vol(AudioEngine* ae, const std::string& b) { return ae ? ae->get_bus_volume(b) : 1.0f; }

// Engine audio shortcuts
static void engine_set_master_vol(Engine* e, float v) { if (e) e->audio().set_master_volume(v); }
static float engine_get_master_vol(Engine* e) { return e ? e->audio().get_master_volume() : 1.0f; }
static void engine_set_bus_vol(Engine* e, const std::string& b, float v) { if (e) e->audio().set_bus_volume(b, v); }
static float engine_get_bus_vol(Engine* e, const std::string& b) { return e ? e->audio().get_bus_volume(b) : 1.0f; }
static AudioEngine* engine_audio(Engine* e) { return e ? &e->audio() : nullptr; }
static void engine_play_sound(Engine* e, const std::string& p) {
    if (e) e->audio().play_oneshot(p);
}
static void engine_play_sound_bus(Engine* e, const std::string& p, const std::string& b) {
    if (e) e->audio().play_oneshot(p, b);
}

// ---- RigidBody3D helpers ----
static glm::vec3 rb3d_get_vel   (RigidBody3D* n)               { return n ? n->get_velocity() : glm::vec3{}; }
static void      rb3d_set_vel   (RigidBody3D* n, glm::vec3 v)  { if (n) n->set_velocity(v); }
static glm::vec3 rb3d_get_avel  (RigidBody3D* n)               { return n ? n->get_angular_velocity() : glm::vec3{}; }
static void      rb3d_set_avel  (RigidBody3D* n, glm::vec3 v)  { if (n) n->set_angular_velocity(v); }
static void      rb3d_impulse   (RigidBody3D* n, glm::vec3 v)  { if (n) n->apply_impulse(v); }
static void      rb3d_force     (RigidBody3D* n, glm::vec3 v)  { if (n) n->apply_force(v); }
static void      rb3d_torque    (RigidBody3D* n, glm::vec3 v)  { if (n) n->apply_torque_impulse(v); }
static void      rb3d_freeze_rot(RigidBody3D* n, bool f)       { if (n) n->freeze_rotation(f); }
static void      rb3d_kinematic (RigidBody3D* n, bool k)       { if (n) n->set_kinematic(k); }
static float     rb3d_get_mass  (const RigidBody3D* n)         { return n ? n->mass : 1.0f; }
static void      rb3d_set_mass  (RigidBody3D* n, float v)      { if (n) n->mass = v; }
static float     rb3d_get_grav  (const RigidBody3D* n)         { return n ? n->gravity_scale : 1.0f; }
static void      rb3d_set_grav  (RigidBody3D* n, float v)      { if (n) n->gravity_scale = v; }

// ---- Node tag helpers ----
static void node_add_tag   (Node* n, const std::string& tag) { if (n) n->add_tag(tag); }
static bool node_has_tag   (const Node* n, const std::string& tag) { return n && n->has_tag(tag); }
static void node_remove_tag(Node* n, const std::string& tag) { if (n) n->remove_tag(tag); }

// ---- Node3D visibility + world position ----
static bool      n3d_get_visible (const Node3D* n)     { return n && n->visible; }
static void      n3d_set_visible (Node3D* n, bool v)   { if (n) n->visible = v; }
static glm::vec3 n3d_get_world_pos(const Node3D* n) {
    if (!n) return {};
    return glm::vec3(n->global_transform()[3]);
}

// ---- Engine screen size ----
static int engine_screen_w(Engine* e) { return e ? e->window().width()  : 0; }
static int engine_screen_h(Engine* e) { return e ? e->window().height() : 0; }

// ---- Engine get_root_node ----
static Node* engine_get_root(Engine* e) {
    if (!e) return nullptr;
    Scene* sc = e->scene_manager().current_scene();
    return sc ? sc->root() : nullptr;
}

// ---- Engine add_node (deferred) ----
static void engine_add_node(Engine* e, Node* parent, Node* child) {
    if (!e || !parent || !child) return;
    Scene* sc = e->scene_manager().current_scene();
    if (!sc) return;
    auto owned = e->script().take_pending_node(child);
    if (!owned) {
        SOL_WARN("add_node: node not in pending pool — already added?");
        return;
    }
    sc->spawn_node(parent, std::move(owned), *e);
}

// ---- Engine destroy_node (deferred) ----
static void engine_destroy_node(Engine* e, Node* node) {
    if (!e || !node) return;
    Scene* sc = e->scene_manager().current_scene();
    if (!sc) return;
    sc->despawn_node(node, *e);
}

// ---- Engine create_node (raw Lua C-function) ----
static int engine_create_node_lua(lua_State* L) {
    auto e_res = luabridge::get<Engine*>(L, 1);
    if (!e_res || !*e_res) { lua_pushnil(L); return 1; }
    Engine* e = *e_res;
    const char* type_name = luaL_checkstring(L, 2);
    auto node = NodeFactory::instance().create(type_name);
    if (!node) {
        SOL_WARN(std::string("create_node: unknown type '") + type_name + "'");
        lua_pushnil(L); return 1;
    }
    Node* raw = node.get();
    e->script().track_pending_node(std::move(node));
    if      (auto* rb = dynamic_cast<RigidBody3D*>(raw))  (void)luabridge::push(L, rb);
    else if (auto* n3 = dynamic_cast<Node3D*>(raw))       (void)luabridge::push(L, n3);
    else                                                   (void)luabridge::push(L, raw);
    return 1;
}

// ---- Engine instantiate_model (raw Lua C-function) ----
// Creates a ModelNode from a file path and spawns it into the current scene.
static int engine_instantiate_model_lua(lua_State* L) {
    auto e_res = luabridge::get<Engine*>(L, 1);
    if (!e_res || !*e_res) { lua_pushnil(L); return 1; }
    Engine* e = *e_res;
    const char* path = luaL_checkstring(L, 2);

    Scene* sc = e->scene_manager().current_scene();
    if (!sc || !sc->root()) { lua_pushnil(L); return 1; }

    // Derive a name from the file stem
    std::string node_name = path;
    auto slash = node_name.find_last_of("/\\");
    if (slash != std::string::npos) node_name = node_name.substr(slash + 1);
    auto dot = node_name.rfind('.');
    if (dot != std::string::npos) node_name = node_name.substr(0, dot);

    auto node = std::make_unique<ModelNode>();
    node->path = path;
    node->name = node_name;

    Node* raw = node.get();
    sc->spawn_node(sc->root(), std::move(node), *e);

    if (auto* n3 = dynamic_cast<Node3D*>(raw)) (void)luabridge::push(L, n3);
    else                                        (void)luabridge::push(L, raw);
    return 1;
}

// ---- Engine find_nodes_by_tag (raw Lua C-function) ----
static int engine_find_by_tag_lua(lua_State* L) {
    auto e_res = luabridge::get<Engine*>(L, 1);
    if (!e_res || !*e_res) { lua_newtable(L); return 1; }
    Engine* e = *e_res;
    const char* tag = luaL_checkstring(L, 2);
    lua_newtable(L);
    Scene* sc = e->scene_manager().current_scene();
    if (!sc || !sc->root()) return 1;
    int idx = 1;
    sc->root()->each<Node>([&](Node& n) {
        if (!n.has_tag(tag)) return;
        if      (auto* rb = dynamic_cast<RigidBody3D*>(&n))  (void)luabridge::push(L, rb);
        else if (auto* n3 = dynamic_cast<Node3D*>(&n))       (void)luabridge::push(L, n3);
        else                                                  (void)luabridge::push(L, &n);
        lua_rawseti(L, -2, idx++);
    });
    return 1;
}

// ---- Engine create_timer (raw Lua C-function) ----
static int engine_create_timer_lua(lua_State* L) {
    auto e_res = luabridge::get<Engine*>(L, 1);
    if (!e_res || !*e_res) { lua_pushinteger(L, -1); return 1; }
    Engine* e = *e_res;
    float duration  = static_cast<float>(luaL_checknumber(L, 2));
    luaL_checktype(L, 3, LUA_TFUNCTION);
    bool repeating  = lua_toboolean(L, 4) != 0;
    lua_pushvalue(L, 3);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    int id = e->script().add_timer(duration, cb_ref, repeating);
    lua_pushinteger(L, id);
    return 1;
}

static void engine_cancel_timer(Engine* e, int timer_id) {
    if (e) e->script().cancel_timer(timer_id);
}

// ---- Engine physics queries ----
// Returns a Lua table: { hit=bool, position=Vec3, normal=Vec3, distance=float, node=Node* }
static int engine_raycast_lua(lua_State* L) {
    auto e_res = luabridge::get<Engine*>(L, 1);
    Engine* e = e_res ? *e_res : nullptr;
    auto org_res = luabridge::get<glm::vec3>(L, 2);
    auto dir_res = luabridge::get<glm::vec3>(L, 3);
    if (!org_res || !dir_res) { lua_pushnil(L); return 1; }
    glm::vec3 origin = *org_res;
    glm::vec3 dir    = *dir_res;
    float     dist   = static_cast<float>(luaL_optnumber(L, 4, 1000.0));
    Node*     ignore = nullptr;
    if (lua_gettop(L) >= 5 && !lua_isnil(L, 5)) {
        auto n_res = luabridge::get<Node*>(L, 5);
        if (n_res) ignore = *n_res;
    }

    if (!e) { lua_pushnil(L); return 1; }

    RaycastHit h = e->physics().raycast(origin, dir, dist, ignore);

    lua_newtable(L);
    lua_pushboolean(L, h.hit ? 1 : 0); lua_setfield(L, -2, "hit");
    luabridge::push(L, h.position);    lua_setfield(L, -2, "position");
    luabridge::push(L, h.normal);      lua_setfield(L, -2, "normal");
    lua_pushnumber(L, static_cast<lua_Number>(h.distance)); lua_setfield(L, -2, "distance");
    if (h.node) {
        if (auto* rb = dynamic_cast<RigidBody3D*>(h.node))           (void)luabridge::push(L, rb);
        else if (auto* cb = dynamic_cast<CharacterBody3D*>(h.node))  (void)luabridge::push(L, cb);
        else if (auto* n3 = dynamic_cast<Node3D*>(h.node))           (void)luabridge::push(L, n3);
        else                                                          (void)luabridge::push(L, h.node);
    } else {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "node");
    return 1;
}

// Returns a Lua array-table of nodes overlapping the sphere.
static int engine_overlap_sphere_lua(lua_State* L) {
    auto e_res = luabridge::get<Engine*>(L, 1);
    Engine* e = e_res ? *e_res : nullptr;
    auto c_res = luabridge::get<glm::vec3>(L, 2);
    if (!c_res) { lua_newtable(L); return 1; }
    glm::vec3 center = *c_res;
    float radius = static_cast<float>(luaL_checknumber(L, 3));

    lua_newtable(L);
    if (!e) return 1;

    auto nodes = e->physics().overlap_sphere(center, radius);
    int idx = 1;
    for (Node* n : nodes) {
        if (auto* rb = dynamic_cast<RigidBody3D*>(n))           (void)luabridge::push(L, rb);
        else if (auto* cb = dynamic_cast<CharacterBody3D*>(n))  (void)luabridge::push(L, cb);
        else if (auto* n3 = dynamic_cast<Node3D*>(n))           (void)luabridge::push(L, n3);
        else                                                     (void)luabridge::push(L, n);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Static C-function callbacks for the Lua Input table.
// Using a static Engine* avoids per-call Lua stack upvalue overhead.
// Safe because there is exactly one Engine and one ScriptEngine per process.
// ---------------------------------------------------------------------------
struct InputCb {
    static Engine* eng;
    static InputManager& im() { return eng->input(); }

    static int is_pressed(lua_State* L) {
        const char* a = luaL_checkstring(L, 1);
        lua_pushboolean(L, eng && im().is_pressed(a) ? 1 : 0);
        return 1;
    }
    static int is_just_pressed(lua_State* L) {
        const char* a = luaL_checkstring(L, 1);
        lua_pushboolean(L, eng && im().is_just_pressed(a) ? 1 : 0);
        return 1;
    }
    static int is_just_released(lua_State* L) {
        const char* a = luaL_checkstring(L, 1);
        lua_pushboolean(L, eng && im().is_just_released(a) ? 1 : 0);
        return 1;
    }
    static int get_strength(lua_State* L) {
        const char* a = luaL_checkstring(L, 1);
        lua_pushnumber(L, eng ? im().get_strength(a) : 0.0);
        return 1;
    }
    static int get_axis(lua_State* L) {
        const char* neg = luaL_checkstring(L, 1);
        const char* pos = luaL_checkstring(L, 2);
        lua_pushnumber(L, eng ? im().get_axis(neg, pos) : 0.0);
        return 1;
    }
    static int get_vector(lua_State* L) {
        const char* nx = luaL_checkstring(L, 1);
        const char* px = luaL_checkstring(L, 2);
        const char* ny = luaL_checkstring(L, 3);
        const char* py = luaL_checkstring(L, 4);
        if (eng) {
            auto v = im().get_vector(nx, px, ny, py);
            lua_pushnumber(L, v.x);
            lua_pushnumber(L, v.y);
        } else {
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
        }
        return 2;
    }
    static int get_mouse_delta(lua_State* L) {
        if (eng) {
            auto d = im().get_mouse_delta();
            lua_pushnumber(L, d.x);
            lua_pushnumber(L, d.y);
        } else {
            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
        }
        return 2;
    }
    static int get_scroll(lua_State* L) {
        lua_pushnumber(L, eng ? im().get_scroll() : 0.0);
        return 1;
    }
    static int push_context(lua_State* L) {
        const char* c = luaL_checkstring(L, 1);
        if (eng) im().push_context(c);
        return 0;
    }
    static int pop_context(lua_State* L) {
        const char* c = luaL_checkstring(L, 1);
        if (eng) im().pop_context(c);
        return 0;
    }
    static int has_context(lua_State* L) {
        const char* c = luaL_checkstring(L, 1);
        lua_pushboolean(L, eng && im().has_context(c) ? 1 : 0);
        return 1;
    }
    // Input.set_key_binding("jump", Input.KEY_SPACE)
    // Replaces all bindings on the action with a single key binding.
    static int set_key_binding(lua_State* L) {
        const char* action = luaL_checkstring(L, 1);
        int code = static_cast<int>(luaL_checkinteger(L, 2));
        if (eng) {
            im().clear_bindings(action);
            ActionBinding ab;
            ab.type = BindingType::Key;
            ab.code = code;
            im().add_binding(action, ab);
        }
        return 0;
    }
    // Input.set_mouse_binding("fire", 0)  -- 0=left, 1=right, 2=middle
    static int set_mouse_binding(lua_State* L) {
        const char* action = luaL_checkstring(L, 1);
        int btn = static_cast<int>(luaL_checkinteger(L, 2));
        if (eng) {
            im().clear_bindings(action);
            ActionBinding ab;
            ab.type = BindingType::MouseButton;
            ab.code = btn;
            im().add_binding(action, ab);
        }
        return 0;
    }
    static int clear_bindings(lua_State* L) {
        const char* a = luaL_checkstring(L, 1);
        if (eng) im().clear_bindings(a);
        return 0;
    }
    static int save_bindings(lua_State* L) {
        std::string path = "input_bindings.json";
        if (lua_gettop(L) >= 1 && lua_isstring(L, 1))
            path = lua_tostring(L, 1);
        if (eng) im().save_bindings(path);
        return 0;
    }
    static int load_bindings(lua_State* L) {
        std::string path = "input_bindings.json";
        if (lua_gettop(L) >= 1 && lua_isstring(L, 1))
            path = lua_tostring(L, 1);
        if (eng) im().load_bindings(path);
        return 0;
    }
};
Engine* InputCb::eng = nullptr;

void ScriptEngine::register_bindings() {
    auto& L = *m_lua;

    luabridge::getGlobalNamespace(&L)
        .beginNamespace("sol")
            .beginClass<glm::vec3>("Vec3")
                .addConstructor<void (*)(void)>()
                .addPropertyReadWrite("x", &glm::vec3::x)
                .addPropertyReadWrite("y", &glm::vec3::y)
                .addPropertyReadWrite("z", &glm::vec3::z)
                .addFunction("length", &vec3_length)
                .addFunction("normalized", &vec3_normalized)
                .addFunction("dot", &vec3_dot)
                .addFunction("cross", &vec3_cross)
                .addFunction("__tostring", &vec3_tostring)
                .addFunction("__add", &vec3_add)
                .addFunction("__sub", &vec3_sub)
                .addFunction("__mul", &vec3_mul_scalar)
            .endClass()

            .beginClass<Node>("Node")
                .addProperty("name", &node_get_name, &node_set_name)
                .addProperty("script_path", &node_get_script, &node_set_script)
                .addFunction("type_name", &node_type_name)
                .addFunction("find", &node_find)
                .addFunction("parent", &node_parent)
                .addFunction("child_count", &node_child_count)
                .addFunction("get_child", &node_get_child)
                .addFunction("add_tag",    &node_add_tag)
                .addFunction("has_tag",    &node_has_tag)
                .addFunction("remove_tag", &node_remove_tag)
            .endClass()

            .deriveClass<Node3D, Node>("Node3D")
                .addProperty("position", &n3d_get_pos, &n3d_set_pos)
                .addProperty("rotation", &n3d_get_rot, &n3d_set_rot)
                .addProperty("scale", &n3d_get_scl, &n3d_set_scl)
                .addProperty("position_x", &n3d_get_px, &n3d_set_px)
                .addProperty("position_y", &n3d_get_py, &n3d_set_py)
                .addProperty("position_z", &n3d_get_pz, &n3d_set_pz)
                .addProperty("rotation_x", &n3d_get_rx, &n3d_set_rx)
                .addProperty("rotation_y", &n3d_get_ry, &n3d_set_ry)
                .addProperty("rotation_z", &n3d_get_rz, &n3d_set_rz)
                .addFunction("forward", &n3d_forward)
                .addFunction("right", &n3d_right)
                .addFunction("up", &n3d_up)
                .addFunction("find", &n3d_find)
                .addFunction("parent", &n3d_parent)
                .addFunction("add_lua_component", [this](Node3D* n, const std::string& path) {
                    auto comp = std::make_unique<LuaComponent>(path);
                    LuaComponent* raw = static_cast<LuaComponent*>(n->add_component(std::move(comp)));
                    if (m_engine)
                        this->component_ready(raw, *m_engine);
                })
                .addProperty("visible",            &n3d_get_visible,   &n3d_set_visible)
                .addFunction("get_world_position", &n3d_get_world_pos)
            .endClass()

            .deriveClass<Camera3D, Node3D>("Camera3D")
                .addProperty("fov", &cam_get_fov, &cam_set_fov)
                .addProperty("near_clip", &cam_get_near, &cam_set_near)
                .addProperty("far_clip", &cam_get_far, &cam_set_far)
                .addProperty("current", &cam_get_current, &cam_set_current)
            .endClass()

            .deriveClass<MeshNode, Node3D>("MeshNode")
                .addProperty("mesh_name", &mesh_get_name, &mesh_set_name)
                .addProperty("mesh_path", &mesh_get_path, &mesh_set_path)
            .endClass()

            .deriveClass<PointLight, Node3D>("PointLight")
                .addProperty("color", &pl_get_color, &pl_set_color)
                .addProperty("intensity", &pl_get_intensity, &pl_set_intensity)
                .addProperty("range", &pl_get_range, &pl_set_range)
            .endClass()

            .deriveClass<DirectionalLight, Node3D>("DirectionalLight")
                .addProperty("color", &dl_get_color, &dl_set_color)
                .addProperty("intensity", &dl_get_intensity, &dl_set_intensity)
            .endClass()

            .deriveClass<ScriptNode, Node3D>("ScriptNode")
            .endClass()

            .deriveClass<CharacterBody3D, Node3D>("CharacterBody3D")
                .addFunction("is_on_ground",   &cb3d_is_on_ground)
                .addFunction("get_velocity",   &cb3d_get_velocity)
                .addFunction("move_and_slide", &cb3d_move_and_slide)
            .endClass()

            .deriveClass<Area3D, Node3D>("Area3D")
                .addFunction("get_overlapping_bodies", area3d_get_overlapping_bodies)
                .addFunction("is_overlapping_with",    area3d_is_overlapping_with)
                .addFunction("overlap_count",          area3d_overlap_count)
            .endClass()

            .deriveClass<RigidBody3D, Node3D>("RigidBody3D")
                .addProperty("mass",          &rb3d_get_mass,  &rb3d_set_mass)
                .addProperty("gravity_scale", &rb3d_get_grav,  &rb3d_set_grav)
                .addFunction("get_velocity",          &rb3d_get_vel)
                .addFunction("set_velocity",          &rb3d_set_vel)
                .addFunction("get_angular_velocity",  &rb3d_get_avel)
                .addFunction("set_angular_velocity",  &rb3d_set_avel)
                .addFunction("apply_impulse",         &rb3d_impulse)
                .addFunction("apply_force",           &rb3d_force)
                .addFunction("apply_torque_impulse",  &rb3d_torque)
                .addFunction("freeze_rotation",       &rb3d_freeze_rot)
                .addFunction("set_kinematic",         &rb3d_kinematic)
            .endClass()

            .deriveClass<AudioStreamPlayer, Node>("AudioStreamPlayer")
                .addProperty("clip_path",  &asp_get_clip,     &asp_set_clip)
                .addProperty("volume",     &asp_get_vol,      &asp_set_vol)
                .addProperty("pitch",      &asp_get_pitch,    &asp_set_pitch)
                .addProperty("loop",       &asp_get_loop,     &asp_set_loop)
                .addProperty("autoplay",   &asp_get_autoplay, &asp_set_autoplay)
                .addProperty("bus",        &asp_get_bus,      &asp_set_bus)
                .addFunction("play",       &asp_play)
                .addFunction("stop",       &asp_stop)
                .addFunction("pause",      &asp_pause)
                .addFunction("is_playing", &asp_is_playing)
            .endClass()

            .deriveClass<AudioStreamPlayer3D, Node3D>("AudioStreamPlayer3D")
                .addProperty("clip_path",    &asp3_get_clip,    &asp3_set_clip)
                .addProperty("volume",       &asp3_get_vol,     &asp3_set_vol)
                .addProperty("pitch",        &asp3_get_pitch,   &asp3_set_pitch)
                .addProperty("loop",         &asp3_get_loop,    &asp3_set_loop)
                .addProperty("max_distance", &asp3_get_maxdist, &asp3_set_maxdist)
                .addProperty("attenuation",  &asp3_get_atten,   &asp3_set_atten)
                .addFunction("play",         &asp3_play)
                .addFunction("stop",         &asp3_stop)
                .addFunction("pause",        &asp3_pause)
                .addFunction("is_playing",   &asp3_is_playing)
            .endClass()

            .beginClass<AudioEngine>("AudioEngine")
                .addFunction("set_master_volume", &ae_set_master_vol)
                .addFunction("get_master_volume", &ae_get_master_vol)
                .addFunction("set_bus_volume",    &ae_set_bus_vol)
                .addFunction("get_bus_volume",    &ae_get_bus_vol)
            .endClass()

            .beginClass<Engine>("Engine")
                .addProperty("delta_time",   &engine_dt)
                .addProperty("elapsed_time", &engine_elapsed)
                .addFunction("quit", &engine_quit)
                .addFunction("key_down", &engine_key)
                .addFunction("mouse_down", &engine_mouse)
                .addFunction("load_scene", &engine_load_scene)
                .addFunction("find_node", &engine_find_node)
                .addFunction("log", &engine_log)
                .addFunction("cursor_x", &engine_cursor_x)
                .addFunction("cursor_y", &engine_cursor_y)
                .addFunction("set_cursor_captured", &engine_set_cursor_captured)
                .addFunction("set_master_volume",   &engine_set_master_vol)
                .addFunction("get_master_volume",   &engine_get_master_vol)
                .addFunction("set_bus_volume",      &engine_set_bus_vol)
                .addFunction("get_bus_volume",      &engine_get_bus_vol)
                .addFunction("audio",               &engine_audio)
                .addFunction("play_sound",          &engine_play_sound)
                .addFunction("play_sound_bus",      &engine_play_sound_bus)
                .addFunction("raycast",            &engine_raycast_lua)
                .addFunction("overlap_sphere",     &engine_overlap_sphere_lua)
                .addFunction("create_node",        engine_create_node_lua)
                .addFunction("instantiate_model",  engine_instantiate_model_lua)
                .addFunction("add_node",      &engine_add_node)
                .addFunction("destroy_node",  &engine_destroy_node)
                .addFunction("get_root_node", &engine_get_root)
                .addFunction("find_by_tag",   engine_find_by_tag_lua)
                .addFunction("create_timer",  engine_create_timer_lua)
                .addFunction("cancel_timer",  &engine_cancel_timer)
                .addFunction("screen_width",  &engine_screen_w)
                .addFunction("screen_height", &engine_screen_h)
            .endClass()

            .addFunction("vec3", &vec3_new)
        .endNamespace();

    (void)luabridge::push(&L, m_engine);
    lua_setglobal(&L, "engine");

    lua_newtable(&L);
    auto set_const = [&](const char* name, int val) {
        lua_pushstring(&L, name);
        lua_pushinteger(&L, val);
        lua_settable(&L, -3);
    };
    set_const("KEY_SPACE", 32);
    set_const("KEY_A", 65); set_const("KEY_B", 66); set_const("KEY_C", 67);
    set_const("KEY_D", 68); set_const("KEY_E", 69); set_const("KEY_F", 70);
    set_const("KEY_G", 71); set_const("KEY_H", 72); set_const("KEY_I", 73);
    set_const("KEY_J", 74); set_const("KEY_K", 75); set_const("KEY_L", 76);
    set_const("KEY_M", 77); set_const("KEY_N", 78); set_const("KEY_O", 79);
    set_const("KEY_P", 80); set_const("KEY_Q", 81); set_const("KEY_R", 82);
    set_const("KEY_S", 83); set_const("KEY_T", 84); set_const("KEY_U", 85);
    set_const("KEY_V", 86); set_const("KEY_W", 87); set_const("KEY_X", 88);
    set_const("KEY_Y", 89); set_const("KEY_Z", 90);
    set_const("KEY_0", 48); set_const("KEY_1", 49); set_const("KEY_2", 50);
    set_const("KEY_3", 51); set_const("KEY_4", 52); set_const("KEY_5", 53);
    set_const("KEY_6", 54); set_const("KEY_7", 55); set_const("KEY_8", 56);
    set_const("KEY_9", 57);
    set_const("KEY_ESCAPE", 256);
    set_const("KEY_ENTER", 257);
    set_const("KEY_TAB", 258);
    set_const("KEY_RIGHT", 262); set_const("KEY_LEFT", 263);
    set_const("KEY_DOWN", 264); set_const("KEY_UP", 265);
    set_const("KEY_LSHIFT", 340); set_const("KEY_LCTRL", 341);
    set_const("KEY_RSHIFT", 344); set_const("KEY_RCTRL", 345);
    set_const("MOUSE_LEFT", 0); set_const("MOUSE_RIGHT", 1); set_const("MOUSE_MIDDLE", 2);

    // --- InputManager action functions ---
    InputCb::eng = m_engine;
    auto add_fn = [&](const char* name, lua_CFunction fn) {
        lua_pushstring(&L, name);
        lua_pushcfunction(&L, fn);
        lua_settable(&L, -3);
    };
    add_fn("is_pressed",        InputCb::is_pressed);
    add_fn("is_just_pressed",   InputCb::is_just_pressed);
    add_fn("is_just_released",  InputCb::is_just_released);
    add_fn("get_strength",      InputCb::get_strength);
    add_fn("get_axis",          InputCb::get_axis);
    add_fn("get_vector",        InputCb::get_vector);
    add_fn("get_mouse_delta",   InputCb::get_mouse_delta);
    add_fn("get_scroll",        InputCb::get_scroll);
    add_fn("push_context",      InputCb::push_context);
    add_fn("pop_context",       InputCb::pop_context);
    add_fn("has_context",       InputCb::has_context);
    add_fn("set_key_binding",   InputCb::set_key_binding);
    add_fn("set_mouse_binding", InputCb::set_mouse_binding);
    add_fn("clear_bindings",    InputCb::clear_bindings);
    add_fn("save_bindings",     InputCb::save_bindings);
    add_fn("load_bindings",     InputCb::load_bindings);

    lua_setglobal(&L, "Input");

    // -------------------------------------------------------------------------
    // Lua convenience namespaces — thin wrappers around engine: functions so
    // scripts can use Scene.create_node(), Physics.raycast(), etc.
    // -------------------------------------------------------------------------
    static const char* bootstrap = R"LUA(
-- Vec3 global alias (sol.Vec3 / sol.vec3 → Vec3 / vec3)
Vec3 = sol.Vec3
vec3 = sol.vec3

-- Scene namespace
Scene = {}
function Scene.create_node(type_name)
    local n = engine:create_node(type_name)
    if n then engine:add_node(engine:get_root_node(), n) end
    return n
end
function Scene.instantiate_model(path)
    return engine:instantiate_model(path)
end
function Scene.get_node(name)
    return engine:find_node(name)
end
function Scene.get_root()
    return engine:get_root_node()
end
function Scene.add_node(parent, child)
    engine:add_node(parent, child)
end
function Scene.destroy_node(node)
    engine:destroy_node(node)
end
function Scene.load(name)
    engine:load_scene(name)
end
function Scene.find_by_tag(tag)
    return engine:find_by_tag(tag)
end

-- Physics namespace
Physics = {}
function Physics.raycast(origin, direction, max_dist, ignore)
    return engine:raycast(origin, direction, max_dist or 1000.0, ignore)
end
function Physics.overlap_sphere(center, radius)
    return engine:overlap_sphere(center, radius)
end

-- Audio namespace
Audio = {}
function Audio.play_oneshot(path)
    engine:play_sound(path)
end
function Audio.play_oneshot_bus(path, bus)
    engine:play_sound_bus(path, bus)
end

-- Log namespace
Log = {}
function Log.info(msg)  engine:log("[info] "  .. tostring(msg)) end
function Log.warn(msg)  engine:log("[warn] "  .. tostring(msg)) end
function Log.error(msg) engine:log("[error] " .. tostring(msg)) end

-- Input action aliases (is_action_pressed → is_pressed, etc.)
Input.is_action_pressed       = Input.is_pressed
Input.is_action_just_pressed  = Input.is_just_pressed
Input.is_action_just_released = Input.is_just_released
)LUA";

    if (luaL_dostring(&L, bootstrap) != LUA_OK) {
        const char* err = lua_tostring(&L, -1);
        SOL_WARN(std::string("Lua bootstrap error: ") + (err ? err : "?"));
        lua_pop(&L, 1);
    }
}

ScriptEngine::ScriptEngine() = default;
ScriptEngine::~ScriptEngine() { shutdown(); }

void ScriptEngine::update_timers(float dt) {
    if (!m_lua) return;
    for (auto& t : m_timers) {
        if (t.dead) continue;
        t.remaining -= dt;
        if (t.remaining <= 0.0f) {
            if (t.callback_ref != -1) {
                lua_rawgeti(m_lua, LUA_REGISTRYINDEX, t.callback_ref);
                if (lua_isfunction(m_lua, -1))
                    lua_pcall(m_lua, 0, 0, 0);
                else
                    lua_pop(m_lua, 1);
            }
            if (t.interval > 0.0f) {
                t.remaining += t.interval;
            } else {
                if (t.callback_ref != -1) {
                    luaL_unref(m_lua, LUA_REGISTRYINDEX, t.callback_ref);
                    t.callback_ref = -1;
                }
                t.dead = true;
            }
        }
    }
    m_timers.erase(
        std::remove_if(m_timers.begin(), m_timers.end(),
                       [](const LuaTimer& t){ return t.dead; }),
        m_timers.end());
}

int ScriptEngine::add_timer(float duration, int cb_ref, bool repeating) {
    LuaTimer t;
    t.id           = m_next_timer_id++;
    t.remaining    = duration;
    t.interval     = repeating ? duration : 0.0f;
    t.callback_ref = cb_ref;
    m_timers.push_back(t);
    return t.id;
}

void ScriptEngine::cancel_timer(int id) {
    for (auto& t : m_timers) {
        if (t.id == id && !t.dead) {
            if (t.callback_ref != -1 && m_lua) {
                luaL_unref(m_lua, LUA_REGISTRYINDEX, t.callback_ref);
                t.callback_ref = -1;
            }
            t.dead = true;
            break;
        }
    }
}

void ScriptEngine::track_pending_node(std::unique_ptr<Node> node) {
    Node* raw = node.get();
    m_pending_nodes[raw] = std::move(node);
}

std::unique_ptr<Node> ScriptEngine::take_pending_node(Node* raw) {
    auto it = m_pending_nodes.find(raw);
    if (it == m_pending_nodes.end()) return nullptr;
    auto owned = std::move(it->second);
    m_pending_nodes.erase(it);
    return owned;
}

bool ScriptEngine::init(Engine& engine) {
    if (m_lua)
        return true;

    m_engine = &engine;
    m_lua = luaL_newstate();
    if (!m_lua)
        return false;

    luaL_openlibs(m_lua);
    register_bindings();
    return true;
}

void ScriptEngine::shutdown() {
    if (m_lua) {
        for (auto& [path, ref] : m_script_cache)
            luaL_unref(m_lua, LUA_REGISTRYINDEX, ref);
        m_script_cache.clear();

        for (auto& [node, inst] : m_instances) {
            if (inst.instance_ref != LUA_NOREF)
                luaL_unref(m_lua, LUA_REGISTRYINDEX, inst.instance_ref);
        }
        m_instances.clear();

        for (auto& [comp, inst] : m_comp_instances) {
            if (inst.instance_ref != LUA_NOREF)
                luaL_unref(m_lua, LUA_REGISTRYINDEX, inst.instance_ref);
        }
        m_comp_instances.clear();

        lua_close(m_lua);
        m_lua = nullptr;
    }

    m_engine = nullptr;
}

bool ScriptEngine::exec_string(const std::string& code) {
    if (!m_lua)
        return false;

    const int err = luaL_dostring(m_lua, code.c_str());
    if (err != LUA_OK) {
        sol::log::error(std::string("[Lua] ") + lua_tostring(m_lua, -1));
        lua_pop(m_lua, 1);
        return false;
    }

    lua_settop(m_lua, 0);
    return true;
}

bool ScriptEngine::exec_file(const std::string& path) {
    if (!m_lua)
        return false;

    const int err = luaL_dofile(m_lua, path.c_str());
    if (err != LUA_OK) {
        sol::log::error(std::string("[Lua] ") + lua_tostring(m_lua, -1));
        lua_pop(m_lua, 1);
        return false;
    }

    lua_settop(m_lua, 0);
    return true;
}

bool ScriptEngine::load_script_table(const std::string& path, int& out_ref) {
    auto it = m_script_cache.find(path);
    if (it != m_script_cache.end()) {
        out_ref = it->second;
        return true;
    }

    int err = luaL_loadfile(m_lua, path.c_str());
    if (err != LUA_OK) {
        sol::log::error(std::string("[Lua] load '") + path + "': " + lua_tostring(m_lua, -1));
        lua_pop(m_lua, 1);
        return false;
    }

    err = lua_pcall(m_lua, 0, 1, 0);
    if (err != LUA_OK) {
        sol::log::error(std::string("[Lua] exec '") + path + "': " + lua_tostring(m_lua, -1));
        lua_pop(m_lua, 1);
        return false;
    }

    if (!lua_istable(m_lua, -1)) {
        sol::log::error(std::string("[Lua] '") + path + "' did not return a table");
        lua_pop(m_lua, 1);
        return false;
    }

    out_ref = luaL_ref(m_lua, LUA_REGISTRYINDEX);
    m_script_cache[path] = out_ref;
    return true;
}

bool ScriptEngine::ensure_instance(Node* node, Engine& engine) {
    (void)engine;
    if (!node)
        return false;
    if (m_instances.count(node))
        return true;

    int class_ref = LUA_NOREF;
    if (!load_script_table(node->script_path, class_ref))
        return false;

    lua_newtable(m_lua);
    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, class_ref);
    lua_pushvalue(m_lua, -1);
    lua_setfield(m_lua, -2, "__index");
    lua_setmetatable(m_lua, -2);

    if (auto* sn = dynamic_cast<ScriptNode*>(node))
        (void)luabridge::push(m_lua, sn);
    else if (auto* cam = dynamic_cast<Camera3D*>(node))
        (void)luabridge::push(m_lua, cam);
    else if (auto* mn = dynamic_cast<MeshNode*>(node))
        (void)luabridge::push(m_lua, mn);
    else if (auto* pl = dynamic_cast<PointLight*>(node))
        (void)luabridge::push(m_lua, pl);
    else if (auto* dl = dynamic_cast<DirectionalLight*>(node))
        (void)luabridge::push(m_lua, dl);
    else if (auto* cb = dynamic_cast<CharacterBody3D*>(node))
        (void)luabridge::push(m_lua, cb);
    else if (auto* rb = dynamic_cast<RigidBody3D*>(node))
        (void)luabridge::push(m_lua, rb);
    else if (auto* area = dynamic_cast<Area3D*>(node))
        (void)luabridge::push(m_lua, area);
    else if (auto* n3 = dynamic_cast<Node3D*>(node))
        (void)luabridge::push(m_lua, n3);
    else
        (void)luabridge::push(m_lua, node);
    lua_setfield(m_lua, -2, "node");

    ScriptInstance inst;
    auto check = [&](const char* method) {
        lua_getfield(m_lua, -1, method);
        const bool has = lua_isfunction(m_lua, -1);
        lua_pop(m_lua, 1);
        return has;
    };
    inst.has_on_ready = check("on_ready");
    inst.has_on_update = check("on_update");
    inst.has_on_destroy = check("on_destroy");

    inst.instance_ref = luaL_ref(m_lua, LUA_REGISTRYINDEX);
    m_instances[node] = inst;
    return true;
}

void ScriptEngine::call_method(int instance_ref, const char* method, float arg, bool has_arg) {
    if (!m_lua || instance_ref == LUA_NOREF)
        return;

    const int stack_top = lua_gettop(m_lua);

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, instance_ref);
    lua_getfield(m_lua, -1, method);
    if (!lua_isfunction(m_lua, -1)) {
        lua_settop(m_lua, stack_top);
        return;
    }

    lua_pushvalue(m_lua, -2);
    int nargs = 1;
    if (has_arg) {
        lua_pushnumber(m_lua, static_cast<lua_Number>(arg));
        nargs = 2;
    }
    lua_remove(m_lua, -nargs - 2);

    const int err = lua_pcall(m_lua, nargs, 0, 0);
    if (err != LUA_OK) {
        sol::log::error(std::string("[Lua] ") + method + ": " + lua_tostring(m_lua, -1));
        lua_pop(m_lua, 1);
    }

    lua_settop(m_lua, stack_top);
}

void ScriptEngine::node_ready(Node* node, Engine& engine) {
    if (!m_lua || !node || node->script_path.empty())
        return;
    if (!ensure_instance(node, engine))
        return;

    auto& inst = m_instances[node];
    if (!inst.has_on_ready)
        return;

    call_method(inst.instance_ref, "on_ready");
}

void ScriptEngine::node_update(Node* node, Engine& engine, float dt) {
    (void)engine;
    if (!m_lua || !node || node->script_path.empty())
        return;

    auto it = m_instances.find(node);
    if (it == m_instances.end() || !it->second.has_on_update)
        return;

    call_method(it->second.instance_ref, "on_update", dt, true);
}

void ScriptEngine::node_detach(Node* node) {
    if (!m_lua || !node)
        return;

    auto it = m_instances.find(node);
    if (it == m_instances.end())
        return;

    if (it->second.has_on_destroy)
        call_method(it->second.instance_ref, "on_destroy");

    if (it->second.instance_ref != LUA_NOREF)
        luaL_unref(m_lua, LUA_REGISTRYINDEX, it->second.instance_ref);
    m_instances.erase(it);
}

void ScriptEngine::reload_script(const std::string& path) {
    if (!m_lua || path.empty())
        return;

    // Evict the cached script table so it gets reloaded from disk.
    auto cit = m_script_cache.find(path);
    if (cit != m_script_cache.end()) {
        luaL_unref(m_lua, LUA_REGISTRYINDEX, cit->second);
        m_script_cache.erase(cit);
    }

    // Collect all nodes that use this script (must snapshot before detach erases entries).
    std::vector<Node*> affected;
    for (auto& [node, inst] : m_instances) {
        if (node && node->script_path == path)
            affected.push_back(node);
    }

    // Detach old instances then re-run on_ready with the reloaded script.
    for (Node* node : affected) {
        node_detach(node);
        if (m_engine)
            node_ready(node, *m_engine);
    }

    sol::log::info(std::string("[Lua] Reloaded: ") + path +
                   " (" + std::to_string(affected.size()) + " instance(s))");
}

// ---------------------------------------------------------------------------
// LuaComponent dispatch
// ---------------------------------------------------------------------------

bool ScriptEngine::ensure_component_instance(LuaComponent* comp, Engine& engine) {
    (void)engine;
    if (!comp || comp->m_script_path.empty()) return false;
    if (m_comp_instances.count(comp)) return true;

    int class_ref = LUA_NOREF;
    if (!load_script_table(comp->m_script_path, class_ref)) return false;

    lua_newtable(m_lua);
    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, class_ref);
    lua_pushvalue(m_lua, -1);
    lua_setfield(m_lua, -2, "__index");
    lua_setmetatable(m_lua, -2);

    // Store owner node userdata in instance.node
    Node* owner = comp->owner;
    if (owner) {
        if (auto* cb = dynamic_cast<CharacterBody3D*>(owner))
            (void)luabridge::push(m_lua, cb);
        else if (auto* rb = dynamic_cast<RigidBody3D*>(owner))
            (void)luabridge::push(m_lua, rb);
        else if (auto* area = dynamic_cast<Area3D*>(owner))
            (void)luabridge::push(m_lua, area);
        else if (auto* cam = dynamic_cast<Camera3D*>(owner))
            (void)luabridge::push(m_lua, cam);
        else if (auto* mn = dynamic_cast<MeshNode*>(owner))
            (void)luabridge::push(m_lua, mn);
        else if (auto* pl = dynamic_cast<PointLight*>(owner))
            (void)luabridge::push(m_lua, pl);
        else if (auto* dl = dynamic_cast<DirectionalLight*>(owner))
            (void)luabridge::push(m_lua, dl);
        else if (auto* n3 = dynamic_cast<Node3D*>(owner))
            (void)luabridge::push(m_lua, n3);
        else
            (void)luabridge::push(m_lua, owner);
    } else {
        lua_pushnil(m_lua);
    }
    lua_setfield(m_lua, -2, "node");

    ScriptInstance inst;
    auto check = [&](const char* method) {
        lua_getfield(m_lua, -1, method);
        bool has = lua_isfunction(m_lua, -1);
        lua_pop(m_lua, 1);
        return has;
    };
    inst.has_on_ready   = check("on_ready");
    inst.has_on_update  = check("on_update");
    inst.has_on_destroy = check("on_destroy");
    inst.instance_ref   = luaL_ref(m_lua, LUA_REGISTRYINDEX);
    m_comp_instances[comp] = inst;
    return true;
}

void ScriptEngine::component_ready(LuaComponent* comp, Engine& engine) {
    if (!m_lua || !comp || comp->m_script_path.empty()) return;
    if (!ensure_component_instance(comp, engine)) return;
    auto& inst = m_comp_instances[comp];
    if (inst.has_on_ready) call_method(inst.instance_ref, "on_ready");
}

void ScriptEngine::component_update(LuaComponent* comp, Engine& engine, float dt) {
    (void)engine;
    if (!m_lua || !comp) return;
    auto it = m_comp_instances.find(comp);
    if (it == m_comp_instances.end() || !it->second.has_on_update) return;
    call_method(it->second.instance_ref, "on_update", dt, true);
}

void ScriptEngine::component_detach(LuaComponent* comp) {
    if (!m_lua || !comp) return;
    auto it = m_comp_instances.find(comp);
    if (it == m_comp_instances.end()) return;
    if (it->second.has_on_destroy)
        call_method(it->second.instance_ref, "on_destroy");
    if (it->second.instance_ref != LUA_NOREF)
        luaL_unref(m_lua, LUA_REGISTRYINDEX, it->second.instance_ref);
    m_comp_instances.erase(it);
}

// ---------------------------------------------------------------------------
// node_event_with_node_arg — fire a callback on target's script passing `arg`
// as a Node* (Lua userdata). Used for collision and area enter/exit events.
// ---------------------------------------------------------------------------
void ScriptEngine::node_event_with_node_arg(Node* target, const char* method, Node* arg, Engine& engine) {
    if (!m_lua || !target) return;

    // Try script-node instance first
    auto it = m_instances.find(target);
    if (it == m_instances.end()) {
        // No instance yet — try to create one if a script is assigned
        if (target->script_path.empty()) goto try_components;
        if (!ensure_instance(target, engine)) goto try_components;
        it = m_instances.find(target);
        if (it == m_instances.end()) goto try_components;
    }

    {
        const int instance_ref = it->second.instance_ref;
        if (instance_ref == LUA_NOREF) goto try_components;

        const int stack_top = lua_gettop(m_lua);
        lua_rawgeti(m_lua, LUA_REGISTRYINDEX, instance_ref);
        lua_getfield(m_lua, -1, method);
        if (!lua_isfunction(m_lua, -1)) {
            lua_settop(m_lua, stack_top);
            goto try_components;
        }
        lua_pushvalue(m_lua, -2); // self

        // Push arg with most-derived type
        if (arg) {
            if (auto* rb = dynamic_cast<RigidBody3D*>(arg))      luabridge::push(m_lua, rb);
            else if (auto* cb = dynamic_cast<CharacterBody3D*>(arg)) luabridge::push(m_lua, cb);
            else if (auto* area = dynamic_cast<Area3D*>(arg))    luabridge::push(m_lua, area);
            else if (auto* n3 = dynamic_cast<Node3D*>(arg))      luabridge::push(m_lua, n3);
            else                                                 luabridge::push(m_lua, arg);
        } else {
            lua_pushnil(m_lua);
        }

        lua_remove(m_lua, -4); // remove duplicate instance table
        const int err = lua_pcall(m_lua, 2, 0, 0);
        if (err != LUA_OK) {
            sol::log::error(std::string("[Lua] ") + method + ": " + lua_tostring(m_lua, -1));
            lua_pop(m_lua, 1);
        }
        lua_settop(m_lua, stack_top);
    }

try_components:
    // Also fire on LuaComponents attached to the target node
    for (auto& [comp, inst] : m_comp_instances) {
        if (!comp || comp->owner != target) continue;
        if (inst.instance_ref == LUA_NOREF) continue;

        const int stack_top = lua_gettop(m_lua);
        lua_rawgeti(m_lua, LUA_REGISTRYINDEX, inst.instance_ref);
        lua_getfield(m_lua, -1, method);
        if (!lua_isfunction(m_lua, -1)) { lua_settop(m_lua, stack_top); continue; }
        lua_pushvalue(m_lua, -2);

        if (arg) {
            if (auto* rb = dynamic_cast<RigidBody3D*>(arg))      luabridge::push(m_lua, rb);
            else if (auto* cb = dynamic_cast<CharacterBody3D*>(arg)) luabridge::push(m_lua, cb);
            else if (auto* area = dynamic_cast<Area3D*>(arg))    luabridge::push(m_lua, area);
            else if (auto* n3 = dynamic_cast<Node3D*>(arg))      luabridge::push(m_lua, n3);
            else                                                 luabridge::push(m_lua, arg);
        } else {
            lua_pushnil(m_lua);
        }

        lua_remove(m_lua, -4);
        const int err = lua_pcall(m_lua, 2, 0, 0);
        if (err != LUA_OK) {
            sol::log::error(std::string("[Lua] ") + method + ": " + lua_tostring(m_lua, -1));
            lua_pop(m_lua, 1);
        }
        lua_settop(m_lua, stack_top);
    }
}

} // namespace sol
