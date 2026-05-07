// demo.cpp — First-Person Demo
// Controls:
//   WASD            — move
//   Mouse / Arrows  — look
//   Space           — jump
//   Escape          — toggle mouse capture
//   Tab             — toggle debug UI

#include "sol/api.h"
#include "sol/engine.h"
#include "sol/log.h"
#include "sol/project/project.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/character_body3d.h"
#include "sol/scene/camera3d.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <string>

namespace {

static sol::CharacterBody3D* s_player  = nullptr;
static sol::Camera3D*        s_camera  = nullptr;

static double s_last_mx = 0.0, s_last_my = 0.0;
static bool   s_first_mouse = true;
static bool   s_captured    = true;
static bool   s_show_ui     = true;

static float  s_yaw    = 0.0f;
static float  s_pitch  = 0.0f;

static const float MOUSE_SENS  = 0.08f;
static const float ARROW_SENS  = 90.0f;   // degrees per second
static const float MOVE_SPEED  = 5.0f;
static const float JUMP_SPEED  = 6.0f;
static const float PITCH_LIMIT = 88.0f;

void on_init(sol::Engine* engine) {
    ImGui::SetCurrentContext(engine->imgui_context());

    sol::ProjectConfig cfg = sol::ProjectConfig::load("project.sol");
    SOL_INFO("Demo: " + cfg.name + " v" + cfg.version);

    if (!engine->scene_manager().load_scene(cfg.main_scene, *engine)) {
        SOL_ERROR("Demo: failed to load main scene");
        engine->quit();
        return;
    }

    sol::Scene* scene = engine->scene_manager().current();
    if (scene && scene->root()) {
        s_player = scene->root()->find_first<sol::CharacterBody3D>();
        s_camera = scene->root()->find_first<sol::Camera3D>();
    }

    if (!s_player) SOL_ERROR("Demo: CharacterBody3D not found in scene");
    if (!s_camera) SOL_ERROR("Demo: Camera3D not found in scene");

    if (s_player) s_yaw   = s_player->rotation.y;
    if (s_camera) s_pitch = s_camera->rotation.x;

    s_captured    = true;
    s_first_mouse = true;
    engine->set_cursor_captured(true);

    SOL_INFO("Demo ready — WASD move, mouse look, Space jump, Esc cursor, Tab UI");
}

void on_update(sol::Engine* engine, float dt) {
    static bool tab_prev = false;
    bool tab_now = engine->key_down(GLFW_KEY_TAB);
    if (tab_now && !tab_prev) s_show_ui = !s_show_ui;
    tab_prev = tab_now;

    static bool esc_prev = false;
    bool esc_now = engine->key_down(GLFW_KEY_ESCAPE);
    if (esc_now && !esc_prev) {
        s_captured = !s_captured;
        s_first_mouse = true;
        engine->set_cursor_captured(s_captured);
    }
    esc_prev = esc_now;

    if (s_captured) {
        double mx, my;
        engine->cursor_position(mx, my);

        if (s_first_mouse) {
            s_last_mx = mx; s_last_my = my;
            s_first_mouse = false;
        }

        float dx = float(mx - s_last_mx) * MOUSE_SENS;
        float dy = float(my - s_last_my) * MOUSE_SENS;
        s_last_mx = mx; s_last_my = my;

        s_yaw   -= dx;
        s_pitch -= dy;
        s_pitch  = std::clamp(s_pitch, -PITCH_LIMIT, PITCH_LIMIT);
    }

    // Arrow key look (works regardless of mouse capture)
    if (engine->key_down(GLFW_KEY_LEFT))  s_yaw   += ARROW_SENS * dt;
    if (engine->key_down(GLFW_KEY_RIGHT)) s_yaw   -= ARROW_SENS * dt;
    if (engine->key_down(GLFW_KEY_UP))    s_pitch += ARROW_SENS * dt;
    if (engine->key_down(GLFW_KEY_DOWN))  s_pitch -= ARROW_SENS * dt;
    s_pitch = std::clamp(s_pitch, -PITCH_LIMIT, PITCH_LIMIT);

    if (s_player) s_player->rotation.y = s_yaw;
    if (s_camera) s_camera->rotation.x = s_pitch;

    if (s_player) {
        glm::vec3 input(0.0f);
        if (engine->key_down(GLFW_KEY_W)) input += s_player->forward();
        if (engine->key_down(GLFW_KEY_S)) input -= s_player->forward();
        if (engine->key_down(GLFW_KEY_D)) input += s_player->right();
        if (engine->key_down(GLFW_KEY_A)) input -= s_player->right();

        input.y = 0.0f;
        float len = glm::length(input);
        if (len > 1e-4f) input = (input / len) * MOVE_SPEED;

        glm::vec3 cur_vel = s_player->get_velocity();
        float vy = cur_vel.y;

        static bool space_prev = false;
        bool space_now = engine->key_down(GLFW_KEY_SPACE);
        if (space_now && !space_prev && s_player->is_on_ground())
            vy = JUMP_SPEED;
        space_prev = space_now;

        s_player->move_and_slide(glm::vec3(input.x, vy, input.z));
    }

    engine->scene_manager().update(*engine, dt);
}

void on_render(sol::Engine* engine) {
    engine->scene_manager().render(*engine);

    if (s_show_ui) {
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("Debug", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings);

        ImGui::Text("SolEngine Demo  [Tab=UI  Esc=cursor  Arrows=look]");
        ImGui::Separator();
        if (s_player) {
            auto& p = s_player->position;
            ImGui::Text("Pos: %.2f  %.2f  %.2f", p.x, p.y, p.z);
            auto v = s_player->get_velocity();
            ImGui::Text("Vel: %.2f  %.2f  %.2f", v.x, v.y, v.z);
            ImGui::Text("Ground: %s", s_player->is_on_ground() ? "yes" : "no");
        }
        ImGui::Text("Yaw: %.1f   Pitch: %.1f", s_yaw, s_pitch);
        ImGui::Text("FT:  %.3f ms", engine->delta_time() * 1000.0f);
        ImGui::End();

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        float cx = io.DisplaySize.x * 0.5f;
        float cy = io.DisplaySize.y * 0.5f;
        dl->AddLine({cx - 10, cy}, {cx + 10, cy}, IM_COL32(255,255,255,180), 1.5f);
        dl->AddLine({cx, cy - 10}, {cx, cy + 10}, IM_COL32(255,255,255,180), 1.5f);
    }
}

void on_shutdown(sol::Engine* engine) {
    engine->set_cursor_captured(false);
    engine->scene_manager().unload(*engine);
    s_player = nullptr;
    s_camera = nullptr;
}

} // anonymous namespace

SOL_EXPORT const SolGameApi* sol_get_game_api() {
    static const SolGameApi api = {
        SOL_ABI_VERSION,
        "SolEngine Demo",
        on_init,
        on_update,
        on_render,
        on_shutdown
    };
    return &api;
}
