#include "sol/scene/fly_cam_controller.h"
#include "sol/engine.h"
#include "sol/log.h"
#include "sol/reflect.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/scene.h"
#include "sol/scene/model_node.h"
#include "sol/scene/light_node.h"
#include "sol/scene/node3d.h"
#include "sol/render/renderer.h"
#include "render/vk/vk_renderer.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <filesystem>
#include <string>

namespace sol {

// ---------------------------------------------------------------------------
// Reflection registration
// ---------------------------------------------------------------------------

SOL_REFLECT_BEGIN(FlyCamController)
    SOL_FIELD(yaw,          sol::FieldType::Float)
    SOL_FIELD(pitch,        sol::FieldType::Float)
    SOL_FIELD(move_speed,   sol::FieldType::Float)
    SOL_FIELD(mouse_sens,   sol::FieldType::Float)
    SOL_FIELD(key_look_spd, sol::FieldType::Float)
    SOL_FIELD(fov,          sol::FieldType::Float)
    SOL_FIELD(near_clip,    sol::FieldType::Float)
    SOL_FIELD(far_clip,     sol::FieldType::Float)
    SOL_FIELD(models_dir,   sol::FieldType::AssetPath)
    SOL_FIELD(show_ui,      sol::FieldType::Bool)
SOL_REFLECT_END()

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

glm::vec3 FlyCamController::cam_forward() const {
    float yr = glm::radians(yaw);
    float pr = glm::radians(pitch);
    return glm::normalize(glm::vec3(
        std::cos(pr) * std::sin(yr),
        std::sin(pr),
        std::cos(pr) * std::cos(yr)));
}

Camera FlyCamController::make_camera() const {
    Camera cam;
    cam.position      = position;
    cam.target        = position + cam_forward();
    cam.up            = {0.0f, 1.0f, 0.0f};
    cam.fov_y_radians = glm::radians(fov);
    cam.near_plane    = near_clip;
    cam.far_plane     = far_clip;
    return cam;
}

// ---------------------------------------------------------------------------
// Model scanning / loading
// ---------------------------------------------------------------------------

void FlyCamController::scan_models() {
    namespace fs = std::filesystem;
    m_models.clear();

    if (fs::is_directory(models_dir)) {
        for (auto& e : fs::directory_iterator(models_dir)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            if (ext == ".glb" || ext == ".gltf" || ext == ".fbx" || ext == ".FBX" || ext == ".blend")
                m_models.push_back(e.path().generic_string());
        }
        std::sort(m_models.begin(), m_models.end());
    }

    // Fallback: model.glb next to the scene
    if (m_models.empty() && std::filesystem::exists("model.glb"))
        m_models.push_back("model.glb");
}

// Build a cloned copy of this controller preserving all runtime state.
std::unique_ptr<FlyCamController> FlyCamController::clone_self_() const {
    auto ctrl            = std::make_unique<FlyCamController>();
    ctrl->name           = name;
    ctrl->yaw            = yaw;
    ctrl->pitch          = pitch;
    ctrl->position       = position;
    ctrl->move_speed     = move_speed;
    ctrl->mouse_sens     = mouse_sens;
    ctrl->key_look_spd   = key_look_spd;
    ctrl->fov            = fov;
    ctrl->near_clip      = near_clip;
    ctrl->far_clip       = far_clip;
    ctrl->models_dir     = models_dir;
    ctrl->show_ui        = show_ui;
    ctrl->extra_scenes   = extra_scenes;
    ctrl->m_models       = m_models;
    ctrl->m_model_idx    = m_model_idx;
    ctrl->m_mouse_captured = m_mouse_captured;
    ctrl->m_is_clone     = true;
    ctrl->m_scene_cooldown = m_scene_cooldown;
    ctrl->m_n_prev       = m_n_prev;   // carry key state so held keys don't re-trigger
    ctrl->m_p_prev       = m_p_prev;
    return ctrl;
}

// Load a scene file, inject the controller clone, and request a deferred swap.
void FlyCamController::load_scene_entry(Engine& engine) {
    const std::string& path = extra_scenes[m_model_idx];
    SOL_INFO("FlyCamController: loading scene " + path);

    auto scene = Scene::load(path, engine);
    if (!scene || !scene->root()) {
        SOL_WARN("FlyCamController: failed to load scene '" + path + "'");
        return;
    }

    // Remove any FlyCamController nodes that were accidentally saved into the
    // scene file (editor saves the runtime scene which may contain injected clones).
    {
        std::vector<Node*> stale;
        for (auto& ch : scene->root()->children())
            if (dynamic_cast<FlyCamController*>(ch.get()))
                stale.push_back(ch.get());
        for (Node* n : stale)
            scene->root()->remove_child(n);
    }

    scene->root()->add_child(clone_self_());
    engine.scene_manager().request_scene(std::move(scene));
}

// Load a GLB model entry, building a lightweight scene around it.
void FlyCamController::load_model_entry(Engine& engine) {
    int model_idx = m_model_idx - static_cast<int>(extra_scenes.size());
    if (model_idx < 0 || model_idx >= static_cast<int>(m_models.size())) return;

    const std::string& glb = m_models[model_idx];
    SOL_INFO("FlyCamController: loading " + glb);

    auto scene = std::make_unique<Scene>();
    scene->name = "ModelViewer";

    auto root  = std::make_unique<Node3D>();
    root->name = "Root";

    root->add_child(clone_self_());

    auto model  = std::make_unique<ModelNode>();
    model->name = "Model";
    model->path = glb;
    root->add_child(std::move(model));

    auto light        = std::make_unique<DirectionalLight>();
    light->name       = "Sun";
    light->rotation   = {-45.0f, 30.0f, 0.0f};
    light->intensity  = 6.0f;
    light->cast_shadow = true;
    root->add_child(std::move(light));

    scene->set_root(std::move(root));
    engine.scene_manager().request_scene(std::move(scene));
}

// Dispatch to the correct entry loader based on the current index.
void FlyCamController::load_current_entry(Engine& engine) {
    if (m_model_idx < static_cast<int>(extra_scenes.size()))
        load_scene_entry(engine);
    else
        load_model_entry(engine);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void FlyCamController::on_ready(Engine& engine) {
    scan_models();
    engine.renderer().set_camera(make_camera());

    // Load default HDR sky (scenes can override via hdr_sky field)
    const std::string default_hdr = "models/sky.hdr";
    if (std::filesystem::exists(default_hdr))
        engine.renderer().set_hdr_sky(default_hdr);

    // If freshly loaded from JSON (not a clone from cycling),
    // start at entry 0 and request an immediate load.
    if (!m_is_clone) {
        m_model_idx = 0;
        int total = static_cast<int>(extra_scenes.size() + m_models.size());
        if (total > 0)
            load_current_entry(engine);
        else if (m_models.empty())
            SOL_WARN("FlyCamController: no models found in '" + models_dir + "'");
    }
}

void FlyCamController::on_update(Engine& engine, float dt) {
    // ---- Tab: toggle HUD ----
    bool tab = engine.key_down(GLFW_KEY_TAB);
    if (tab && !m_tab_prev) show_ui = !show_ui;
    m_tab_prev = tab;

    // ---- Escape: release mouse ----
    bool esc = engine.key_down(GLFW_KEY_ESCAPE);
    if (esc && !m_esc_prev && m_mouse_captured) {
        m_mouse_captured = false;
        m_first_mouse    = true;
        engine.set_cursor_captured(false);
    }
    m_esc_prev = esc;

    // ---- Left-click: capture mouse ----
    bool lmb = engine.mouse_button_down(GLFW_MOUSE_BUTTON_LEFT);
    bool imgui_wants_mouse = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
    if (lmb && !m_lmb_prev && !m_mouse_captured && !imgui_wants_mouse) {
        m_mouse_captured = true;
        m_first_mouse    = true;
        engine.set_cursor_captured(true);
    }
    m_lmb_prev = lmb;

    // ---- N / P: cycle entries (scenes + models) ----
    int total = static_cast<int>(extra_scenes.size() + m_models.size());
    if (m_scene_cooldown > 0.0f) m_scene_cooldown -= dt;

    bool n_key = engine.key_down(GLFW_KEY_N);
    if (n_key && !m_n_prev && total > 0 && m_scene_cooldown <= 0.0f) {
        m_model_idx = (m_model_idx + 1) % total;
        m_scene_cooldown = 0.5f;    // set BEFORE cloning so the clone inherits it
        load_current_entry(engine);
    }
    m_n_prev = n_key;

    bool p_key = engine.key_down(GLFW_KEY_P);
    if (p_key && !m_p_prev && total > 0 && m_scene_cooldown <= 0.0f) {
        m_model_idx = ((m_model_idx - 1) + total) % total;
        m_scene_cooldown = 0.5f;    // set BEFORE cloning so the clone inherits it
        load_current_entry(engine);
    }
    m_p_prev = p_key;

    // ---- Mouse look (when captured) ----
    if (m_mouse_captured) {
        double mx, my;
        engine.cursor_position(mx, my);
        if (m_first_mouse) { m_prev_mx = mx; m_prev_my = my; m_first_mouse = false; }
        yaw   -= static_cast<float>(mx - m_prev_mx) * mouse_sens;
        pitch -= static_cast<float>(my - m_prev_my) * mouse_sens;
        m_prev_mx = mx; m_prev_my = my;
    }

    // ---- Arrow-key look (always) ----
    if (engine.key_down(GLFW_KEY_LEFT))  yaw   += key_look_spd * dt;
    if (engine.key_down(GLFW_KEY_RIGHT)) yaw   -= key_look_spd * dt;
    if (engine.key_down(GLFW_KEY_UP))    pitch += key_look_spd * dt;
    if (engine.key_down(GLFW_KEY_DOWN))  pitch -= key_look_spd * dt;
    pitch = std::clamp(pitch, -89.0f, 89.0f);

    // ---- WASD / Q E fly movement ----
    glm::vec3 fwd   = cam_forward();
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    float     spd   = move_speed * dt;
    if (engine.key_down(GLFW_KEY_W)) position += fwd   * spd;
    if (engine.key_down(GLFW_KEY_S)) position -= fwd   * spd;
    if (engine.key_down(GLFW_KEY_A)) position -= right * spd;
    if (engine.key_down(GLFW_KEY_D)) position += right * spd;
    if (engine.key_down(GLFW_KEY_E)) position.y += spd;
    if (engine.key_down(GLFW_KEY_Q)) position.y -= spd;

    // ---- Push camera to renderer ----
    engine.renderer().set_camera(make_camera());

    // ---- HUD ----
    if (show_ui && !engine.is_editor_mode()) draw_hud(engine);
}

void FlyCamController::on_destroy(Engine& engine) {
    if (m_mouse_captured) {
        engine.set_cursor_captured(false);
        m_mouse_captured = false;
    }
}

// ---------------------------------------------------------------------------
// HUD overlay
// ---------------------------------------------------------------------------

void FlyCamController::draw_hud(Engine& engine) {
    if (!ImGui::GetCurrentContext()) return;

    // ---- compact info overlay (top-left) ----
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##hud", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove           |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextColored({0.4f, 0.9f, 1.0f, 1.0f}, "SolEngine  Model Viewer");
    ImGui::Separator();

    int total = static_cast<int>(extra_scenes.size() + m_models.size());
    if (total > 0) {
        namespace fs = std::filesystem;
        int extra = static_cast<int>(extra_scenes.size());
        if (m_model_idx < extra) {
            std::string sname = fs::path(extra_scenes[m_model_idx]).stem().string();
            ImGui::Text("Scene [%d/%d]: %s", m_model_idx + 1, total, sname.c_str());
        } else {
            int mi = m_model_idx - extra;
            std::string fname = fs::path(m_models[mi]).filename().string();
            ImGui::Text("Model [%d/%d]: %s", m_model_idx + 1, total, fname.c_str());
        }
    } else {
        ImGui::TextDisabled("No entries found (models_dir: %s)", models_dir.c_str());
    }

    ImGui::Separator();
    ImGui::Text("Pos: (%.1f, %.1f, %.1f)", position.x, position.y, position.z);
    ImGui::Text("Yaw: %.1f  Pitch: %.1f", yaw, pitch);
    ImGui::Separator();
    ImGui::TextDisabled("WASD / Q E = fly   LClick = lock mouse");
    ImGui::TextDisabled("Arrows = look       N / P = cycle model");
    ImGui::TextDisabled("Esc = release mouse   Tab = toggle HUD");
    ImGui::Separator();
    float fps = engine.delta_time() > 0.0f ? 1.0f / engine.delta_time() : 0.0f;
    ImGui::Text("%.2f ms  (%.0f fps)", engine.delta_time() * 1000.0f, fps);
    ImGui::Separator();
    if (ImGui::Button(m_show_render_settings ? "Render Settings [open]" : "Render Settings"))
        m_show_render_settings = !m_show_render_settings;

    ImGui::End();

    // ---- render settings panel ----
    if (!m_show_render_settings) return;

    auto& rs = engine.renderer().settings();

    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({10, 260}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Render Settings", &m_show_render_settings);

    // ---- Post-Processing ----------------------------------------
    if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* tonemap_items[] = { "ACES (cinematic)", "Reinhard (soft)", "Linear (raw HDR)", "AgX (filmic)" };
        ImGui::Combo("Tonemap", &rs.tonemap_mode, tonemap_items, 4);

        ImGui::SliderFloat("Exposure", &rs.exposure, 0.1f, 4.0f, "%.2f");

        ImGui::Spacing();
        ImGui::Checkbox("Bloom", &rs.bloom_enabled);
        ImGui::BeginDisabled(!rs.bloom_enabled);
        ImGui::SliderFloat("Bloom Threshold", &rs.bloom_threshold, 0.3f, 4.0f, "%.2f");
        ImGui::SliderFloat("Bloom Intensity", &rs.bloom_intensity, 0.0f, 1.0f, "%.3f");
        ImGui::EndDisabled();
    }

    // ---- Lighting -----------------------------------------------
    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Shadows", &rs.shadows_enabled);
        ImGui::BeginDisabled(!rs.shadows_enabled);

        // Shadow technique — live-edits the directional light in the current scene
        {
            DirectionalLight* sun = nullptr;
            if (auto* scene = engine.scene_manager().current())
                if (auto* root = scene->root())
                    sun = root->find_first<DirectionalLight>();
            ImGui::BeginDisabled(!sun);
            static const char* tech_items[] = { "None", "PCF", "PCSS", "VSM" };
            int tech = sun ? sun->shadow_mode : 1;
            if (ImGui::Combo("Shadow Tech", &tech, tech_items, 4) && sun)
                sun->shadow_mode = tech;
            ImGui::EndDisabled();
        }
        ImGui::Separator();
        ImGui::SliderFloat("CSM Lambda", &rs.csm_lambda, 0.0f,  1.0f,   "%.2f");
        ImGui::Separator();
        ImGui::TextDisabled("-- Shadow Quality --");
        static const char* quality_items[] = { "Low (8 samples)", "Medium (16 samples)", "High (32 samples)" };
        ImGui::Combo("Quality", &rs.shadow_quality, quality_items, 3);
        ImGui::SliderFloat("Bias Const",  &rs.shadow_bias_const, 0.0f, 0.005f, "%.5f");
        ImGui::SliderFloat("Bias Slope",  &rs.shadow_bias_slope, 0.0f, 0.005f, "%.4f");
        ImGui::SliderFloat("PCF Radius",  &rs.shadow_pcf_radius, 0.5f, 4.0f,   "%.1f px");
        ImGui::SliderFloat("PCSS Light",  &rs.shadow_pcss_light, 0.5f, 20.0f,  "%.1f m");
        ImGui::SliderFloat("VSM Light Bleed",   &rs.vsm_light_bleed,  0.0f, 0.5f,  "%.3f");
        ImGui::SliderFloat("VSM Min Variance",  &rs.vsm_min_variance, 0.0f, 1e-9f, "%.2e");
        ImGui::Separator();
        ImGui::TextDisabled("-- Contact Shadows (screen-space) --");
        ImGui::SliderFloat("CS Distance",  &rs.contact_shadow_distance,  0.0f, 2.0f,  "%.2f m");
        ImGui::BeginDisabled(rs.contact_shadow_distance < 0.001f);
        ImGui::SliderFloat("CS Thickness", &rs.contact_shadow_thickness, 0.05f, 2.0f, "%.2f m");
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextDisabled("-- Temporal Shadow Filtering --");
        ImGui::Checkbox("Temporal Shadows", &rs.temporal_shadow_enabled);
        ImGui::BeginDisabled(!rs.temporal_shadow_enabled);
        ImGui::SliderFloat("TAA Alpha",    &rs.temporal_shadow_alpha,    0.05f, 0.5f,   "%.2f");
        ImGui::SliderFloat("TAA Max Dist", &rs.temporal_shadow_max_dist, 5.0f,  100.0f, "%.0f m");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SliderFloat("Ambient Scale", &rs.ambient_scale, 0.0f, 3.0f, "%.2f");
    }

    // ---- SSAO -----------------------------------------------
    if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable SSAO", &rs.ssao_enabled);
        ImGui::BeginDisabled(!rs.ssao_enabled);
        ImGui::SliderFloat("Radius",   &rs.ssao_radius,   0.1f, 2.0f,  "%.3f");
        ImGui::SliderFloat("Bias",     &rs.ssao_bias,     0.001f, 0.1f, "%.4f");
        ImGui::SliderFloat("Power",    &rs.ssao_power,    0.5f, 5.0f,  "%.2f");
        ImGui::SliderFloat("Strength", &rs.ssao_strength, 0.0f, 1.0f,  "%.2f");
        ImGui::EndDisabled();
    }

    // ---- SSR -----------------------------------------------
    if (ImGui::CollapsingHeader("SSR (Screen Space Reflections)")) {
        ImGui::Checkbox("Enable SSR", &rs.ssr_enabled);
        ImGui::BeginDisabled(!rs.ssr_enabled);
        ImGui::SliderInt  ("Steps",            &rs.ssr_steps,           16, 128);
        ImGui::SliderFloat("Thickness",        &rs.ssr_thickness,       0.05f, 2.0f,  "%.2f");
        ImGui::SliderFloat("Max Distance",     &rs.ssr_max_distance,    1.0f, 50.0f,  "%.1f");
        ImGui::SliderFloat("Roughness Cutoff", &rs.ssr_roughness_cutoff,0.1f, 1.0f,  "%.2f");
        ImGui::SliderFloat("Intensity",        &rs.ssr_intensity,       0.0f, 2.0f,  "%.2f");
        ImGui::SliderFloat("Temporal Blend",   &rs.ssr_temporal_blend,  0.05f, 0.5f, "%.2f");
        ImGui::EndDisabled();
    }

    // ---- IBL -----------------------------------------------
    if (ImGui::CollapsingHeader("IBL (Image-Based Lighting)")) {
        ImGui::Checkbox("Enable IBL", &rs.ibl_enabled);
        if (rs.ibl_enabled) {
            auto* r = VulkanRenderer::get();
            bool ready = r && r->ibl_ready();
            ImGui::SameLine();
            if (ready) ImGui::TextColored(ImVec4(0,1,0,1), "(Ready)");
            else       ImGui::TextColored(ImVec4(1,0.5f,0,1), "(Not computed)");
            if (ImGui::Button("Rebuild IBL")) {
                if (r) r->request_ibl_rebuild();
            }
            ImGui::SliderFloat("IBL Intensity",  &rs.ibl_intensity,      0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("Diffuse Scale",  &rs.ibl_diffuse_scale,  0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Specular Scale", &rs.ibl_specular_scale, 0.0f, 2.0f, "%.2f");
        }
    }

    // ---- Anti-Aliasing -----------------------------------------------
    if (ImGui::CollapsingHeader("Anti-Aliasing", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* aa_items[] = { "None", "MSAA 2x", "MSAA 4x", "MSAA 8x", "TAA" };
        ImGui::Combo("Mode", (int*)&rs.aa_mode, aa_items, 5);
        if (rs.aa_mode == AaMode::TAA) {
            ImGui::SliderFloat("Blend Alpha",      &rs.taa_blend,          0.01f, 0.5f,  "%.3f");
            ImGui::SliderFloat("Variance Gamma",   &rs.taa_variance_gamma, 0.5f,  3.0f,  "%.2f");
            ImGui::SliderFloat("Sharpening",       &rs.taa_sharpening,     0.0f,  1.0f,  "%.2f");
            ImGui::TextDisabled("Blend: lower=smoother/ghostier; Gamma: higher=less ghosting");
        }
    }

    // ---- Performance / Budgeting ------------------------------------
    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        // FPS cap
        static const int   cap_values[] = { 0, 15, 20, 30, 45, 60, 90, 120 };
        static const char* cap_labels[] = { "Unlimited", "15 fps (low-power)", "20 fps", "30 fps (RDP recommended)", "45 fps", "60 fps", "90 fps", "120 fps" };
        constexpr int CAP_COUNT = 8;
        int cap_idx = 0;
        for (int i = 0; i < CAP_COUNT; ++i) if (cap_values[i] == rs.fps_cap) { cap_idx = i; break; }
        if (ImGui::Combo("FPS Cap", &cap_idx, cap_labels, CAP_COUNT))
            rs.fps_cap = cap_values[cap_idx];
        ImGui::TextDisabled("30 fps frees GPU headroom for Remote Desktop");

        ImGui::Spacing();

        // Texture dimension cap (applies to newly loaded textures only)
        static const int   dim_values[] = { 0, 512, 1024, 2048, 4096 };
        static const char* dim_labels[] = { "Unlimited", "512 px", "1024 px", "2048 px", "4096 px" };
        constexpr int DIM_COUNT = 5;
        int dim_idx = 0;
        for (int i = 0; i < DIM_COUNT; ++i) if (dim_values[i] == rs.max_texture_dim) { dim_idx = i; break; }
        if (ImGui::Combo("Max Tex Dim", &dim_idx, dim_labels, DIM_COUNT))
            rs.max_texture_dim = dim_values[dim_idx];
        ImGui::TextDisabled("Down-scales oversized textures on load (reload scene to apply)");

        ImGui::Spacing();

        // VRAM usage readout
        auto* vk = VulkanRenderer::get();
        if (vk) {
            float vram_mb = vk->texture_vram_mb();
            ImVec4 col = vram_mb < 512.0f  ? ImVec4(0.2f,1.0f,0.3f,1.0f)
                       : vram_mb < 1024.0f ? ImVec4(1.0f,0.9f,0.1f,1.0f)
                                           : ImVec4(1.0f,0.3f,0.2f,1.0f);
            ImGui::Text("Texture VRAM:"); ImGui::SameLine();
            ImGui::TextColored(col, "%.1f MB", vram_mb);
        }
    }

    // ---- Debug Views --------------------------------------------
    if (ImGui::CollapsingHeader("Render Debug")) {
        static const char* dbg_items[] = {
            "Off (normal render)",
            "Albedo",
            "World Normals",
            "Metallic",
            "Roughness",
            "AO",
            "Emissive",
            "Cascade Debug",
        };
        ImGui::Combo("View", &rs.debug_view, dbg_items, 8);
        if (rs.debug_view > 0)
            ImGui::TextColored({1.0f,0.8f,0.2f,1.0f}, "Debug view active — lighting bypassed");
    }

    ImGui::End();
}

} // namespace sol
