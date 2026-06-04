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
#include "sol/scene/world_environment.h"
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
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>
#include <thread>
#include <mutex>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

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

// Material — base color
static float mesh_get_cr(const MeshNode* n) { return n ? n->material.base_color.r : 1.f; }
static void  mesh_set_cr(MeshNode* n, float v) { if (n) n->material.base_color.r = v; }
static float mesh_get_cg(const MeshNode* n) { return n ? n->material.base_color.g : 1.f; }
static void  mesh_set_cg(MeshNode* n, float v) { if (n) n->material.base_color.g = v; }
static float mesh_get_cb(const MeshNode* n) { return n ? n->material.base_color.b : 1.f; }
static void  mesh_set_cb(MeshNode* n, float v) { if (n) n->material.base_color.b = v; }
static float mesh_get_ca(const MeshNode* n) { return n ? n->material.base_color.a : 1.f; }
static void  mesh_set_ca(MeshNode* n, float v) { if (n) n->material.base_color.a = v; }
// Material — PBR
static float mesh_get_metallic (const MeshNode* n) { return n ? n->material.metallic  : 0.f; }
static void  mesh_set_metallic (MeshNode* n, float v) { if (n) n->material.metallic  = v; }
static float mesh_get_roughness(const MeshNode* n) { return n ? n->material.roughness : 0.5f; }
static void  mesh_set_roughness(MeshNode* n, float v) { if (n) n->material.roughness = v; }
// Material — emissive
static float mesh_get_er(const MeshNode* n) { return n ? n->material.emissive.r : 0.f; }
static void  mesh_set_er(MeshNode* n, float v) { if (n) n->material.emissive.r = v; }
static float mesh_get_eg(const MeshNode* n) { return n ? n->material.emissive.g : 0.f; }
static void  mesh_set_eg(MeshNode* n, float v) { if (n) n->material.emissive.g = v; }
static float mesh_get_eb(const MeshNode* n) { return n ? n->material.emissive.b : 0.f; }
static void  mesh_set_eb(MeshNode* n, float v) { if (n) n->material.emissive.b = v; }
// Material — flags
static bool mesh_get_lit(const MeshNode* n) { return n ? n->material.lit : true; }
static void mesh_set_lit(MeshNode* n, bool v) { if (n) n->material.lit = v; }

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
static MeshNode* engine_find_mesh_node(Engine* e, const std::string& name) {
    auto* n = engine_find_node(e, name);
    return dynamic_cast<MeshNode*>(n);
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

// --- WorldEnvironment helpers ---
static int    we_get_sky_mode(const WorldEnvironment* n)          { return n->sky_mode; }
static void   we_set_sky_mode(WorldEnvironment* n, int v)         { n->sky_mode = v; }
static std::string we_get_hdr_path(const WorldEnvironment* n)     { return n->hdr_path; }
static void   we_set_hdr_path(WorldEnvironment* n, const std::string& v) { n->hdr_path = v; }
static float  we_get_tod(const WorldEnvironment* n)               { return n->time_of_day; }
static void   we_set_tod(WorldEnvironment* n, float v)            { n->time_of_day = v; }
static bool   we_get_auto_sun(const WorldEnvironment* n)          { return n->auto_sun; }
static void   we_set_auto_sun(WorldEnvironment* n, bool v)        { n->auto_sun = v; }
static float  we_get_latitude(const WorldEnvironment* n)          { return n->latitude; }
static void   we_set_latitude(WorldEnvironment* n, float v)       { n->latitude = v; }
static float  we_get_turbidity(const WorldEnvironment* n)         { return n->turbidity; }
static void   we_set_turbidity(WorldEnvironment* n, float v)      { n->turbidity = v; }
static float  we_get_sun_int(const WorldEnvironment* n)           { return n->sun_intensity; }
static void   we_set_sun_int(WorldEnvironment* n, float v)        { n->sun_intensity = v; }
static float  we_get_rayleigh(const WorldEnvironment* n)          { return n->rayleigh_scale; }
static void   we_set_rayleigh(WorldEnvironment* n, float v)       { n->rayleigh_scale = v; }
static float  we_get_mie(const WorldEnvironment* n)               { return n->mie_strength; }
static void   we_set_mie(WorldEnvironment* n, float v)            { n->mie_strength = v; }
static float  we_get_bloom_mult(const WorldEnvironment* n)        { return n->sun_bloom_mult; }
static void   we_set_bloom_mult(WorldEnvironment* n, float v)     { n->sun_bloom_mult = v; }
static float  we_get_night(const WorldEnvironment* n)             { return n->night_brightness; }
static void   we_set_night(WorldEnvironment* n, float v)          { n->night_brightness = v; }
static float  we_get_sky_exp(const WorldEnvironment* n)           { return n->sky_exposure; }
static void   we_set_sky_exp(WorldEnvironment* n, float v)        { n->sky_exposure = v; }
static float  we_get_sun_disk(const WorldEnvironment* n)          { return n->sun_disk_size_deg; }
static void   we_set_sun_disk(WorldEnvironment* n, float v)       { n->sun_disk_size_deg = v; }
static float  we_get_exposure(const WorldEnvironment* n)          { return n->exposure; }
static void   we_set_exposure(WorldEnvironment* n, float v)       { n->exposure = v; }
static bool   we_get_bloom_en(const WorldEnvironment* n)          { return n->bloom_enabled; }
static void   we_set_bloom_en(WorldEnvironment* n, bool v)        { n->bloom_enabled = v; }
static float  we_get_bloom_th(const WorldEnvironment* n)          { return n->bloom_threshold; }
static void   we_set_bloom_th(WorldEnvironment* n, float v)       { n->bloom_threshold = v; }
static float  we_get_bloom_int(const WorldEnvironment* n)         { return n->bloom_intensity; }
static void   we_set_bloom_int(WorldEnvironment* n, float v)      { n->bloom_intensity = v; }
static float  we_get_ambient_int(const WorldEnvironment* n)       { return n->ambient_intensity; }
static void   we_set_ambient_int(WorldEnvironment* n, float v)    { n->ambient_intensity = v; }
static bool   we_get_fog_en(const WorldEnvironment* n)            { return n->fog_enabled; }
static void   we_set_fog_en(WorldEnvironment* n, bool v)          { n->fog_enabled = v; }
static float  we_get_fog_den(const WorldEnvironment* n)           { return n->fog_density; }
static void   we_set_fog_den(WorldEnvironment* n, float v)        { n->fog_density = v; }
static bool   we_get_vol_en(const WorldEnvironment* n)            { return n->vol_enabled; }
static void   we_set_vol_en(WorldEnvironment* n, bool v)          { n->vol_enabled = v; }
static float  we_get_vol_far(const WorldEnvironment* n)           { return n->vol_far; }
static void   we_set_vol_far(WorldEnvironment* n, float v)        { n->vol_far = v; }
static float  we_get_ssao_radius(const WorldEnvironment* n)       { return n->ssao_radius; }
static void   we_set_ssao_radius(WorldEnvironment* n, float v)    { n->ssao_radius = v; }
static bool   we_get_ssao_en(const WorldEnvironment* n)           { return n->ssao_enabled; }
static void   we_set_ssao_en(WorldEnvironment* n, bool v)          { n->ssao_enabled = v; }
// Sky color tinting helpers
static float  we_get_sky_tint_r(const WorldEnvironment* n)         { return n->sky_tint.r; }
static void   we_set_sky_tint_r(WorldEnvironment* n, float v)      { n->sky_tint.r = v; }
static float  we_get_sky_tint_g(const WorldEnvironment* n)         { return n->sky_tint.g; }
static void   we_set_sky_tint_g(WorldEnvironment* n, float v)      { n->sky_tint.g = v; }
static float  we_get_sky_tint_b(const WorldEnvironment* n)         { return n->sky_tint.b; }
static void   we_set_sky_tint_b(WorldEnvironment* n, float v)      { n->sky_tint.b = v; }
static float  we_get_sun_tint_r(const WorldEnvironment* n)         { return n->sun_color_tint.r; }
static void   we_set_sun_tint_r(WorldEnvironment* n, float v)      { n->sun_color_tint.r = v; }
static float  we_get_sun_tint_g(const WorldEnvironment* n)         { return n->sun_color_tint.g; }
static void   we_set_sun_tint_g(WorldEnvironment* n, float v)      { n->sun_color_tint.g = v; }
static float  we_get_sun_tint_b(const WorldEnvironment* n)         { return n->sun_color_tint.b; }
static void   we_set_sun_tint_b(WorldEnvironment* n, float v)      { n->sun_color_tint.b = v; }
static float  we_get_night_col_r(const WorldEnvironment* n)        { return n->night_sky_color.r; }
static void   we_set_night_col_r(WorldEnvironment* n, float v)     { n->night_sky_color.r = v; }
static float  we_get_night_col_g(const WorldEnvironment* n)        { return n->night_sky_color.g; }
static void   we_set_night_col_g(WorldEnvironment* n, float v)     { n->night_sky_color.g = v; }
static float  we_get_night_col_b(const WorldEnvironment* n)        { return n->night_sky_color.b; }
static void   we_set_night_col_b(WorldEnvironment* n, float v)     { n->night_sky_color.b = v; }
static float  we_get_ground_col_r(const WorldEnvironment* n)       { return n->ground_color.r; }
static void   we_set_ground_col_r(WorldEnvironment* n, float v)    { n->ground_color.r = v; }
static float  we_get_ground_col_g(const WorldEnvironment* n)       { return n->ground_color.g; }
static void   we_set_ground_col_g(WorldEnvironment* n, float v)    { n->ground_color.g = v; }
static float  we_get_ground_col_b(const WorldEnvironment* n)       { return n->ground_color.b; }
static void   we_set_ground_col_b(WorldEnvironment* n, float v)    { n->ground_color.b = v; }
static float  we_get_star_density(const WorldEnvironment* n)       { return n->star_density; }
static void   we_set_star_density(WorldEnvironment* n, float v)    { n->star_density = v; }
static float  we_get_star_brightness(const WorldEnvironment* n)    { return n->star_brightness; }
static void   we_set_star_brightness(WorldEnvironment* n, float v) { n->star_brightness = v; }

// --- HTTP implementation ---
struct HttpResult {
    bool        ok     = false;
    int         status = 0;
    std::string body;
    std::string error;
};

#ifdef _WIN32
static HttpResult http_request_sync(const std::string& method,
                                    const std::string& url,
                                    const std::string& post_body = "",
                                    const std::string& content_type = "application/json") {
    HttpResult result;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wurl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], wlen);

    URL_COMPONENTS comps{};
    comps.dwStructSize = sizeof(comps);
    wchar_t scheme_buf[16]{}, host_buf[512]{}, path_buf[4096]{};
    comps.lpszScheme   = scheme_buf;  comps.dwSchemeLength   = 16;
    comps.lpszHostName = host_buf;    comps.dwHostNameLength = 512;
    comps.lpszUrlPath  = path_buf;    comps.dwUrlPathLength  = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &comps)) {
        result.error = "Invalid URL";
        return result;
    }

    bool is_https = (comps.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = comps.nPort > 0 ? comps.nPort : (is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);

    HINTERNET session = WinHttpOpen(L"SolEngine/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { result.error = "WinHttpOpen failed"; return result; }

    HINTERNET connect = WinHttpConnect(session, host_buf, port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        result.error = "WinHttpConnect failed";
        return result;
    }

    int mlen = MultiByteToWideChar(CP_UTF8, 0, method.c_str(), -1, nullptr, 0);
    std::wstring wmethod(mlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, method.c_str(), -1, &wmethod[0], mlen);

    DWORD req_flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, wmethod.c_str(), path_buf,
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, req_flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        result.error = "WinHttpOpenRequest failed";
        return result;
    }

    LPVOID send_data = WINHTTP_NO_REQUEST_DATA;
    DWORD  send_size = 0;
    if (!post_body.empty()) {
        int ctlen = MultiByteToWideChar(CP_UTF8, 0, content_type.c_str(), -1, nullptr, 0);
        std::wstring wct(ctlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, content_type.c_str(), -1, &wct[0], ctlen);
        std::wstring header = L"Content-Type: " + wct;
        WinHttpAddRequestHeaders(request, header.c_str(), (DWORD)header.size(),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        send_data = (LPVOID)post_body.c_str();
        send_size = (DWORD)post_body.size();
    }

    BOOL sent = WinHttpSendRequest(request,
                                   WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   send_data, send_size, send_size, 0);
    if (sent) sent = WinHttpReceiveResponse(request, nullptr);

    if (sent) {
        DWORD status_code = 0;
        DWORD buf_size = sizeof(status_code);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status_code, &buf_size,
                            WINHTTP_NO_HEADER_INDEX);
        result.status = (int)status_code;
        result.ok = (status_code >= 200 && status_code < 300);

        DWORD bytes_avail = 0;
        while (WinHttpQueryDataAvailable(request, &bytes_avail) && bytes_avail > 0) {
            std::string chunk(bytes_avail, '\0');
            DWORD bytes_read = 0;
            WinHttpReadData(request, &chunk[0], bytes_avail, &bytes_read);
            result.body.append(chunk, 0, bytes_read);
        }
    } else {
        DWORD err = GetLastError();
        result.error = "Request failed (WinHTTP error " + std::to_string(err) + ")";
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}
#else
static HttpResult http_request_sync(const std::string& /*method*/,
                                    const std::string& /*url*/,
                                    const std::string& /*body*/ = "",
                                    const std::string& /*ct*/   = "") {
    return {false, 0, "", "HTTP not supported on this platform"};
}
#endif

// --- Http Lua C-functions ---
static ScriptEngine* g_script_engine_for_http = nullptr;

static int lua_http_get(lua_State* L) {
    size_t url_len = 0;
    const char* url = luaL_checklstring(L, 1, &url_len);
    auto r = http_request_sync("GET", std::string(url, url_len));
    lua_newtable(L);
    lua_pushboolean(L, r.ok ? 1 : 0); lua_setfield(L, -2, "ok");
    lua_pushinteger(L, r.status);      lua_setfield(L, -2, "status");
    lua_pushlstring(L, r.body.c_str(), r.body.size()); lua_setfield(L, -2, "body");
    lua_pushstring(L, r.error.c_str()); lua_setfield(L, -2, "error");
    return 1;
}

static int lua_http_post(lua_State* L) {
    size_t url_len = 0;
    const char* url = luaL_checklstring(L, 1, &url_len);
    std::string body = (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) ? lua_tostring(L, 2) : "";
    std::string ct   = (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) ? lua_tostring(L, 3) : "application/json";
    auto r = http_request_sync("POST", std::string(url, url_len), body, ct);
    lua_newtable(L);
    lua_pushboolean(L, r.ok ? 1 : 0); lua_setfield(L, -2, "ok");
    lua_pushinteger(L, r.status);      lua_setfield(L, -2, "status");
    lua_pushlstring(L, r.body.c_str(), r.body.size()); lua_setfield(L, -2, "body");
    lua_pushstring(L, r.error.c_str()); lua_setfield(L, -2, "error");
    return 1;
}

static int lua_http_get_async(lua_State* L) {
    size_t url_len = 0;
    const char* url_c = luaL_checklstring(L, 1, &url_len);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    std::string url(url_c, url_len);
    lua_pushvalue(L, 2);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ScriptEngine* se = g_script_engine_for_http;
    std::thread([se, url, cb_ref]() {
        auto r = http_request_sync("GET", url);
        if (se) se->push_http_result(r.ok, r.status, std::move(r.body), std::move(r.error), cb_ref);
    }).detach();
    return 0;
}

static int lua_http_post_async(lua_State* L) {
    size_t url_len = 0;
    const char* url_c = luaL_checklstring(L, 1, &url_len);
    std::string body = (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) ? lua_tostring(L, 2) : "";
    std::string ct   = (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) ? lua_tostring(L, 3) : "application/json";
    luaL_checktype(L, 4, LUA_TFUNCTION);
    std::string url(url_c, url_len);
    lua_pushvalue(L, 4);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ScriptEngine* se = g_script_engine_for_http;
    std::thread([se, url, body, ct, cb_ref]() {
        auto r = http_request_sync("POST", url, body, ct);
        if (se) se->push_http_result(r.ok, r.status, std::move(r.body), std::move(r.error), cb_ref);
    }).detach();
    return 0;
}

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
    // Must check most-derived first so MeshNode doesn't get pushed as Node3D
    if      (auto* p = dynamic_cast<MeshNode*>(raw))           (void)luabridge::push(L, p);
    else if (auto* p = dynamic_cast<RigidBody3D*>(raw))        (void)luabridge::push(L, p);
    else if (auto* p = dynamic_cast<CharacterBody3D*>(raw))    (void)luabridge::push(L, p);
    else if (auto* p = dynamic_cast<PointLight*>(raw))         (void)luabridge::push(L, p);
    else if (auto* p = dynamic_cast<DirectionalLight*>(raw))   (void)luabridge::push(L, p);
    else if (auto* p = dynamic_cast<Camera3D*>(raw))           (void)luabridge::push(L, p);
    else if (auto* p = dynamic_cast<ScriptNode*>(raw))         (void)luabridge::push(L, p);
    else if (auto* p = dynamic_cast<Area3D*>(raw))             (void)luabridge::push(L, p);
    else if (auto* p = dynamic_cast<Node3D*>(raw))             (void)luabridge::push(L, p);
    else                                                       (void)luabridge::push(L, raw);
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

static WorldEnvironment* engine_get_world_env(Engine* e) {
    if (!e) return nullptr;
    Scene* sc = e->scene_manager().current_scene();
    if (!sc || !sc->root()) return nullptr;
    return sc->root()->find_first<WorldEnvironment>();
}

// Recursively push a nlohmann::json value onto the Lua stack.
static void push_json(lua_State* L, const nlohmann::json& j) {
    if      (j.is_null())            lua_pushnil(L);
    else if (j.is_boolean())         lua_pushboolean(L, j.get<bool>() ? 1 : 0);
    else if (j.is_number_integer())  lua_pushinteger(L, (lua_Integer)j.get<int64_t>());
    else if (j.is_number_float())    lua_pushnumber(L, (lua_Number)j.get<double>());
    else if (j.is_string())          lua_pushstring(L, j.get<std::string>().c_str());
    else if (j.is_array()) {
        lua_newtable(L);
        for (int i = 0; i < (int)j.size(); i++) {
            push_json(L, j[i]);
            lua_rawseti(L, -2, i + 1);
        }
    } else if (j.is_object()) {
        lua_newtable(L);
        for (auto& [key, val] : j.items()) {
            push_json(L, val);
            lua_setfield(L, -2, key.c_str());
        }
    } else {
        lua_pushnil(L);
    }
}

// Single shared input text buffer for in-game text fields (name entry, etc.)
static char s_ui_input_buf[64] = "Player";

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
                .addProperty("mesh_name",  &mesh_get_name, &mesh_set_name)
                .addProperty("mesh_path",  &mesh_get_path, &mesh_set_path)
                // Material base color
                .addProperty("color_r",    &mesh_get_cr,       &mesh_set_cr)
                .addProperty("color_g",    &mesh_get_cg,       &mesh_set_cg)
                .addProperty("color_b",    &mesh_get_cb,       &mesh_set_cb)
                .addProperty("color_a",    &mesh_get_ca,       &mesh_set_ca)
                // PBR
                .addProperty("metallic",   &mesh_get_metallic,  &mesh_set_metallic)
                .addProperty("roughness",  &mesh_get_roughness, &mesh_set_roughness)
                // Emissive
                .addProperty("emissive_r", &mesh_get_er,        &mesh_set_er)
                .addProperty("emissive_g", &mesh_get_eg,        &mesh_set_eg)
                .addProperty("emissive_b", &mesh_get_eb,        &mesh_set_eb)
                // Flags
                .addProperty("lit",        &mesh_get_lit,       &mesh_set_lit)
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

            .deriveClass<WorldEnvironment, Node>("WorldEnvironment")
                .addProperty("sky_mode",         &we_get_sky_mode,    &we_set_sky_mode)
                .addProperty("hdr_path",          &we_get_hdr_path,    &we_set_hdr_path)
                .addProperty("time_of_day",       &we_get_tod,         &we_set_tod)
                .addProperty("auto_sun",          &we_get_auto_sun,    &we_set_auto_sun)
                .addProperty("latitude",          &we_get_latitude,    &we_set_latitude)
                .addProperty("turbidity",         &we_get_turbidity,   &we_set_turbidity)
                .addProperty("sun_intensity",     &we_get_sun_int,     &we_set_sun_int)
                .addProperty("rayleigh_scale",    &we_get_rayleigh,    &we_set_rayleigh)
                .addProperty("mie_strength",      &we_get_mie,         &we_set_mie)
                .addProperty("sun_bloom_mult",    &we_get_bloom_mult,  &we_set_bloom_mult)
                .addProperty("night_brightness",  &we_get_night,       &we_set_night)
                .addProperty("sky_exposure",      &we_get_sky_exp,     &we_set_sky_exp)
                .addProperty("sun_disk_size_deg", &we_get_sun_disk,    &we_set_sun_disk)
                .addProperty("exposure",          &we_get_exposure,    &we_set_exposure)
                .addProperty("bloom_enabled",     &we_get_bloom_en,    &we_set_bloom_en)
                .addProperty("bloom_threshold",   &we_get_bloom_th,    &we_set_bloom_th)
                .addProperty("bloom_intensity",   &we_get_bloom_int,   &we_set_bloom_int)
                .addProperty("ambient_intensity", &we_get_ambient_int, &we_set_ambient_int)
                .addProperty("fog_enabled",       &we_get_fog_en,      &we_set_fog_en)
                .addProperty("fog_density",       &we_get_fog_den,     &we_set_fog_den)
                .addProperty("vol_enabled",       &we_get_vol_en,      &we_set_vol_en)
                .addProperty("vol_far",           &we_get_vol_far,     &we_set_vol_far)
                .addProperty("ssao_enabled",      &we_get_ssao_en,         &we_set_ssao_en)
                .addProperty("ssao_radius",       &we_get_ssao_radius,     &we_set_ssao_radius)
                .addProperty("sky_tint_r",        &we_get_sky_tint_r,      &we_set_sky_tint_r)
                .addProperty("sky_tint_g",        &we_get_sky_tint_g,      &we_set_sky_tint_g)
                .addProperty("sky_tint_b",        &we_get_sky_tint_b,      &we_set_sky_tint_b)
                .addProperty("sun_tint_r",        &we_get_sun_tint_r,      &we_set_sun_tint_r)
                .addProperty("sun_tint_g",        &we_get_sun_tint_g,      &we_set_sun_tint_g)
                .addProperty("sun_tint_b",        &we_get_sun_tint_b,      &we_set_sun_tint_b)
                .addProperty("night_sky_color_r", &we_get_night_col_r,     &we_set_night_col_r)
                .addProperty("night_sky_color_g", &we_get_night_col_g,     &we_set_night_col_g)
                .addProperty("night_sky_color_b", &we_get_night_col_b,     &we_set_night_col_b)
                .addProperty("ground_color_r",    &we_get_ground_col_r,    &we_set_ground_col_r)
                .addProperty("ground_color_g",    &we_get_ground_col_g,    &we_set_ground_col_g)
                .addProperty("ground_color_b",    &we_get_ground_col_b,    &we_set_ground_col_b)
                .addProperty("star_density",      &we_get_star_density,    &we_set_star_density)
                .addProperty("star_brightness",   &we_get_star_brightness, &we_set_star_brightness)
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
                .addFunction("find_mesh_node", &engine_find_mesh_node)
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
                .addFunction("get_world_environment", &engine_get_world_env)
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

    // --- Http global ---
    g_script_engine_for_http = this;
    lua_newtable(&L);
    lua_pushcfunction(&L, lua_http_get);        lua_setfield(&L, -2, "get");
    lua_pushcfunction(&L, lua_http_post);       lua_setfield(&L, -2, "post");
    lua_pushcfunction(&L, lua_http_get_async);  lua_setfield(&L, -2, "get_async");
    lua_pushcfunction(&L, lua_http_post_async); lua_setfield(&L, -2, "post_async");
    lua_setglobal(&L, "Http");

    // --- UI global — ImGui wrappers for in-game HUD overlays ---
    // ImGui::NewFrame() is already called before Lua scripts run (engine game loop),
    // so it is safe to call any ImGui function from on_process / on_ready.
    {
        auto add_ui = [&](const char* name, lua_CFunction fn) {
            lua_pushcfunction(&L, fn);
            lua_setfield(&L, -2, name);
        };
        lua_newtable(&L);

        // UI.begin_panel(title [, alpha])
        // Opens a borderless overlay window. Call UI.end_panel() after.
        add_ui("begin_panel", [](lua_State* L) -> int {
            const char* title = lua_tostring(L, 1);
            float alpha = lua_isnumber(L, 2) ? (float)lua_tonumber(L, 2) : 0.75f;
            ImGui::SetNextWindowBgAlpha(alpha);
            ImGui::Begin(title ? title : "##panel", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
            return 0;
        });

        // UI.end_panel()
        add_ui("end_panel", [](lua_State* L) -> int {
            ImGui::End();
            return 0;
        });

        // UI.text(str)
        add_ui("text", [](lua_State* L) -> int {
            const char* s = lua_tostring(L, 1);
            ImGui::Text("%s", s ? s : "");
            return 0;
        });

        // UI.colored_text(r, g, b, a, str)
        add_ui("colored_text", [](lua_State* L) -> int {
            float r = (float)luaL_optnumber(L, 1, 1.0);
            float g = (float)luaL_optnumber(L, 2, 1.0);
            float b = (float)luaL_optnumber(L, 3, 1.0);
            float a = (float)luaL_optnumber(L, 4, 1.0);
            const char* s = lua_tostring(L, 5);
            ImGui::TextColored(ImVec4(r, g, b, a), "%s", s ? s : "");
            return 0;
        });

        // UI.button(label) -> bool  (returns true on click)
        add_ui("button", [](lua_State* L) -> int {
            const char* label = lua_tostring(L, 1);
            lua_pushboolean(L, ImGui::Button(label ? label : "") ? 1 : 0);
            return 1;
        });

        // UI.set_pos(x, y)  — call before begin_panel to position it
        add_ui("set_pos", [](lua_State* L) -> int {
            float x = (float)luaL_optnumber(L, 1, 10.0);
            float y = (float)luaL_optnumber(L, 2, 10.0);
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
            return 0;
        });

        // UI.set_size(w, h)
        add_ui("set_size", [](lua_State* L) -> int {
            float w = (float)luaL_optnumber(L, 1, 200.0);
            float h = (float)luaL_optnumber(L, 2, 100.0);
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
            return 0;
        });

        // UI.same_line()
        add_ui("same_line", [](lua_State* L) -> int {
            ImGui::SameLine();
            return 0;
        });

        // UI.separator()
        add_ui("separator", [](lua_State* L) -> int {
            ImGui::Separator();
            return 0;
        });

        // UI.dummy(w, h)  — invisible spacer
        add_ui("dummy", [](lua_State* L) -> int {
            float w = (float)luaL_optnumber(L, 1, 0.0);
            float h = (float)luaL_optnumber(L, 2, 4.0);
            ImGui::Dummy(ImVec2(w, h));
            return 0;
        });

        // UI.set_font_scale(scale)
        add_ui("set_font_scale", [](lua_State* L) -> int {
            ImGui::SetWindowFontScale((float)luaL_optnumber(L, 1, 1.0));
            return 0;
        });

        // UI.progress_bar(fraction [, label])
        add_ui("progress_bar", [](lua_State* L) -> int {
            float frac  = (float)luaL_optnumber(L, 1, 0.0);
            const char* lbl = lua_isnoneornil(L, 2) ? nullptr : lua_tostring(L, 2);
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), lbl);
            return 0;
        });

        // UI.input_text(label) → string: draw a text input field, returns current value
        add_ui("input_text", [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            ImGui::InputText(label, s_ui_input_buf, sizeof(s_ui_input_buf));
            lua_pushstring(L, s_ui_input_buf);
            return 1;
        });

        // UI.input_text_set(value): reset the shared input buffer
        add_ui("input_text_set", [](lua_State* L) -> int {
            const char* val = luaL_optstring(L, 1, "");
            strncpy(s_ui_input_buf, val, sizeof(s_ui_input_buf) - 1);
            s_ui_input_buf[sizeof(s_ui_input_buf) - 1] = '\0';
            return 0;
        });

        lua_setglobal(&L, "UI");
    }

    // --- Json global — parse JSON strings into Lua tables ---
    {
        lua_newtable(&L);
        lua_pushcfunction(&L, [](lua_State* L) -> int {
            const char* s = luaL_checkstring(L, 1);
            try {
                push_json(L, nlohmann::json::parse(s));
            } catch (...) {
                lua_pushnil(L);
            }
            return 1;
        });
        lua_setfield(&L, -2, "parse");
        lua_setglobal(&L, "Json");
    }

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
function Scene.get_mesh_node(name)
    return engine:find_mesh_node(name)
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
function Scene.get_world_environment()
    return engine:get_world_environment()
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

    // Dispatch completed async HTTP callbacks
    std::vector<PendingHttpCallback> ready;
    {
        std::lock_guard<std::mutex> lk(m_http_mutex);
        ready.swap(m_http_pending);
    }
    for (auto& cb : ready) {
        lua_rawgeti(m_lua, LUA_REGISTRYINDEX, cb.callback_ref);
        luaL_unref(m_lua, LUA_REGISTRYINDEX, cb.callback_ref);
        lua_newtable(m_lua);
        lua_pushboolean(m_lua, cb.ok ? 1 : 0);
        lua_setfield(m_lua, -2, "ok");
        lua_pushinteger(m_lua, cb.status);
        lua_setfield(m_lua, -2, "status");
        lua_pushlstring(m_lua, cb.body.c_str(), cb.body.size());
        lua_setfield(m_lua, -2, "body");
        lua_pushstring(m_lua, cb.error.c_str());
        lua_setfield(m_lua, -2, "error");
        if (lua_pcall(m_lua, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(m_lua, -1);
            SOL_WARN(std::string("Http async callback error: ") + (err ? err : "?"));
            lua_pop(m_lua, 1);
        }
    }

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

    g_script_engine_for_http = nullptr;
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

void ScriptEngine::push_http_result(bool ok, int status, std::string body, std::string error, int callback_ref) {
    std::lock_guard<std::mutex> lk(m_http_mutex);
    m_http_pending.push_back({ok, status, std::move(body), std::move(error), callback_ref});
}

} // namespace sol
