// demo.cpp — Fly-Cam Model Viewer
// Controls:
//   WASD              — fly forward / back / strafe
//   Left click        — capture mouse for look
//   Escape            — release mouse
//   Mouse             — look (when captured)
//   Arrow keys        — look (always)
//   N / P             — next / previous model
//   Tab               — toggle HUD

#include "sol/api.h"
#include "sol/engine.h"
#include "sol/log.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/scene.h"
#include "sol/scene/node3d.h"
#include "sol/scene/model_node.h"
#include "sol/scene/light_node.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------- fly camera
static glm::vec3 s_pos   = {0.0f, 1.5f, 5.0f};
static float     s_yaw   = 180.0f;  // degrees — looking toward –Z by default
static float     s_pitch = -10.0f;

static bool   s_mouse_captured = false;
static double s_prev_mx = 0, s_prev_my = 0;
static bool   s_first_mouse = true;

static const float MOVE_SPEED  = 5.0f;   // m/s
static const float KEY_LOOK    = 60.0f;  // deg/s
static const float MOUSE_SENS  = 0.15f;  // deg/pixel
static const float PITCH_MIN   = -89.0f;
static const float PITCH_MAX   =  89.0f;

static glm::vec3 cam_forward() {
    float yr = glm::radians(s_yaw);
    float pr = glm::radians(s_pitch);
    return glm::normalize(glm::vec3(
        std::cos(pr) * std::sin(yr),
        std::sin(pr),
        std::cos(pr) * std::cos(yr)));
}

static sol::Camera make_camera() {
    sol::Camera cam;
    cam.position      = s_pos;
    cam.target        = s_pos + cam_forward();
    cam.up            = {0.0f, 1.0f, 0.0f};
    cam.fov_y_radians = glm::radians(60.0f);
    cam.near_plane    = 0.05f;
    cam.far_plane     = 1000.0f;
    return cam;
}

// ---------------------------------------------------------------- model list
static std::vector<std::string> s_models;
static int                      s_model_idx = 0;
static bool                     s_show_ui   = true;

static void scan_models() {
    namespace fs = std::filesystem;
    s_models.clear();
    if (fs::is_directory("models")) {
        for (auto& e : fs::directory_iterator("models")) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            if (ext == ".glb" || ext == ".gltf")
                s_models.push_back(e.path().generic_string());
        }
        std::sort(s_models.begin(), s_models.end());
    }
    // Fallback: model.glb in demo root
    if (s_models.empty() && fs::exists("model.glb"))
        s_models.push_back("model.glb");
}

static void load_model(sol::Engine* engine) {
    if (s_models.empty()) { SOL_WARN("Model Viewer: no models found"); return; }

    const std::string& glb = s_models[s_model_idx];
    SOL_INFO("Model Viewer: loading " + glb);

    auto scene = std::make_unique<sol::Scene>();
    scene->name = "ModelViewer";

    auto root  = std::make_unique<sol::Node3D>();
    root->name = "Root";

    auto model  = std::make_unique<sol::ModelNode>();
    model->name = "Model";
    model->path = glb;
    root->add_child(std::move(model));

    auto light        = std::make_unique<sol::DirectionalLight>();
    light->name       = "Sun";
    light->rotation   = {-45.0f, 30.0f, 0.0f};
    light->intensity  = 6.0f;
    light->cast_shadow = true;
    root->add_child(std::move(light));

    scene->set_root(std::move(root));
    engine->scene_manager().set_scene(std::move(scene), *engine);
}

// ---------------------------------------------------------------- edge-key helpers
static bool s_tab_prev = false, s_esc_prev = false;
static bool s_n_prev   = false, s_p_prev   = false;
static bool s_lmb_prev = false;

// ---------------------------------------------------------------- entry points
void on_init(sol::Engine* engine) {
    ImGui::SetCurrentContext(engine->imgui_context());
    scan_models();
    load_model(engine);
    engine->set_cursor_captured(false);
    SOL_INFO("Model Viewer ready — WASD=fly  Mouse/Arrows=look  N/P=cycle  Tab=UI  LClick=lock  Esc=unlock");
}

void on_update(sol::Engine* engine, float dt) {
    // Tab — toggle HUD
    bool tab = engine->key_down(GLFW_KEY_TAB);
    if (tab && !s_tab_prev) s_show_ui = !s_show_ui;
    s_tab_prev = tab;

    // Escape — release mouse
    bool esc = engine->key_down(GLFW_KEY_ESCAPE);
    if (esc && !s_esc_prev && s_mouse_captured) {
        s_mouse_captured = false;
        s_first_mouse    = true;
        engine->set_cursor_captured(false);
    }
    s_esc_prev = esc;

    // Left-click — capture mouse (only if ImGui doesn't want it)
    bool lmb = engine->mouse_button_down(GLFW_MOUSE_BUTTON_LEFT);
    if (lmb && !s_lmb_prev && !s_mouse_captured && !ImGui::GetIO().WantCaptureMouse) {
        s_mouse_captured = true;
        s_first_mouse    = true;
        engine->set_cursor_captured(true);
    }
    s_lmb_prev = lmb;

    // Cycle models — N (next) / P (prev)
    bool n_key = engine->key_down(GLFW_KEY_N);
    if (n_key && !s_n_prev && !s_models.empty()) {
        s_model_idx = (s_model_idx + 1) % (int)s_models.size();
        load_model(engine);
    }
    s_n_prev = n_key;

    bool p_key = engine->key_down(GLFW_KEY_P);
    if (p_key && !s_p_prev && !s_models.empty()) {
        s_model_idx = ((s_model_idx - 1) + (int)s_models.size()) % (int)s_models.size();
        load_model(engine);
    }
    s_p_prev = p_key;

    // Mouse look (when captured)
    if (s_mouse_captured) {
        double mx, my;
        engine->cursor_position(mx, my);
        if (s_first_mouse) { s_prev_mx = mx; s_prev_my = my; s_first_mouse = false; }
        s_yaw   -= float(mx - s_prev_mx) * MOUSE_SENS;
        s_pitch -= float(my - s_prev_my) * MOUSE_SENS; // inverted Y
        s_prev_mx = mx; s_prev_my = my;
        s_pitch = std::clamp(s_pitch, PITCH_MIN, PITCH_MAX);
    }

    // Arrow-key look (always available)
    if (engine->key_down(GLFW_KEY_LEFT))  s_yaw   += KEY_LOOK * dt;
    if (engine->key_down(GLFW_KEY_RIGHT)) s_yaw   -= KEY_LOOK * dt;
    if (engine->key_down(GLFW_KEY_UP))    s_pitch += KEY_LOOK * dt;
    if (engine->key_down(GLFW_KEY_DOWN))  s_pitch -= KEY_LOOK * dt;
    s_pitch = std::clamp(s_pitch, PITCH_MIN, PITCH_MAX);

    // WASD fly movement
    glm::vec3 fwd   = cam_forward();
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    float     spd   = MOVE_SPEED * dt;
    if (engine->key_down(GLFW_KEY_W)) s_pos += fwd   * spd;
    if (engine->key_down(GLFW_KEY_S)) s_pos -= fwd   * spd;
    if (engine->key_down(GLFW_KEY_A)) s_pos -= right * spd;
    if (engine->key_down(GLFW_KEY_D)) s_pos += right * spd;
    if (engine->key_down(GLFW_KEY_E)) s_pos.y += spd;
    if (engine->key_down(GLFW_KEY_Q)) s_pos.y -= spd;

    engine->renderer().set_camera(make_camera());
    engine->scene_manager().update(*engine, dt);
}

void on_render(sol::Engine* engine) {
    engine->scene_manager().render(*engine);

    if (!s_show_ui) return;

    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##hud", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove           |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextColored({0.4f, 0.9f, 1.0f, 1.0f}, "SolEngine  Model Viewer");
    ImGui::Separator();

    // Model name
    if (!s_models.empty()) {
        namespace fs = std::filesystem;
        std::string fname = fs::path(s_models[s_model_idx]).filename().string();
        ImGui::Text("Model [%d/%d]: %s", s_model_idx + 1, (int)s_models.size(), fname.c_str());
    } else {
        ImGui::TextDisabled("No models found in models/");
    }

    ImGui::Separator();
    ImGui::Text("Pos: (%.1f, %.1f, %.1f)", s_pos.x, s_pos.y, s_pos.z);
    ImGui::Text("Yaw: %.1f  Pitch: %.1f", s_yaw, s_pitch);
    ImGui::Separator();
    ImGui::TextDisabled("WASD / Q E = fly   LClick = lock mouse");
    ImGui::TextDisabled("Arrows = look       N / P = cycle model");
    ImGui::TextDisabled("Esc = release mouse   Tab = toggle HUD");
    ImGui::Separator();
    float fps = engine->delta_time() > 0.0f ? 1.0f / engine->delta_time() : 0.0f;
    ImGui::Text("%.2f ms  (%.0f fps)", engine->delta_time() * 1000.0f, fps);

    ImGui::End();
}

void on_shutdown(sol::Engine* engine) {
    engine->scene_manager().unload(*engine);
    engine->set_cursor_captured(false);
}

} // anonymous namespace

SOL_EXPORT const SolGameApi* sol_get_game_api() {
    static const SolGameApi api = {
        SOL_ABI_VERSION,
        "SolEngine Model Viewer",
        on_init,
        on_update,
        on_render,
        on_shutdown
    };
    return &api;
}
