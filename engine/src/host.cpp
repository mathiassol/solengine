#include "sol/host.h"
#include "sol/engine.h"
#include "sol/log.h"
#include "sol/project/project.h"
#include "sol/scene/model_node.h"
#include "sol/scene/node3d.h"
#include "sol/scene/camera3d.h"
#include "sol/scene/scene.h"
#include "sol/scene/scene_manager.h"
#include "sol/reflect.h"
#include "sol/api.h"
#include "ui/imgui_layer.h"
#include "ui/ImGuizmo.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <system_error>

#include <imgui.h>

namespace sol {

namespace {

Camera make_editor_camera(const glm::vec3& position, float yaw_deg, float pitch_deg, float aspect) {
    const float yaw = glm::radians(yaw_deg);
    const float pitch = glm::radians(pitch_deg);

    glm::vec3 forward(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::cos(yaw));
    if (glm::dot(forward, forward) <= 0.0f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
    forward = glm::normalize(forward);

    Camera camera;
    camera.position = position;
    camera.target = position + forward;
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fov_y_radians = glm::radians(60.0f);
    camera.near_plane = 0.05f;
    camera.far_plane = 2000.0f;
    (void)aspect;
    return camera;
}

bool intersect_unit_box(const glm::vec3& ray_origin, const glm::vec3& ray_dir, float& out_t) {
    float t_min = 0.0f;
    float t_max = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ray_origin[axis];
        const float dir = ray_dir[axis];
        if (std::abs(dir) < 1e-6f) {
            if (origin < -0.5f || origin > 0.5f) return false;
            continue;
        }

        float t1 = (-0.5f - origin) / dir;
        float t2 = ( 0.5f - origin) / dir;
        if (t1 > t2) std::swap(t1, t2);
        t_min = std::max(t_min, t1);
        t_max = std::min(t_max, t2);
        if (t_min > t_max) return false;
    }

    out_t = t_min;
    return true;
}

} // namespace

struct EngineHost::Impl {
    Engine        engine;
    ProjectConfig project;
    bool          open = false;
    bool          editor_mode = false;
    bool          editor_cam_active = false;
    int           viewport_w = 1280;
    int           viewport_h = 720;
    float         last_dt = 0.016f;
    glm::vec3     editor_camera_position {0.0f, 5.0f, 15.0f};
    float         editor_camera_yaw = 0.0f;
    float         editor_camera_pitch = -15.0f;
    float         editor_move_speed = 6.0f;
    float         editor_look_sens = 0.12f;
    bool          editor_fly_camera = false;
    std::array<bool, 7> camera_actions {};
    glm::vec2     mouse_pos {0.0f};
    glm::vec2     mouse_delta {0.0f};
    Node*         selected_node = nullptr;
    EditorGizmoMode gizmo_mode = EditorGizmoMode::Translate;
    bool          gizmo_hovered = false;
    bool          gizmo_active = false;

    Camera build_editor_camera() const {
        const float yaw_rad = glm::radians(editor_camera_yaw);
        const float pitch_rad = glm::radians(editor_camera_pitch);
        glm::vec3 forward {
            std::cos(pitch_rad) * std::sin(-yaw_rad),
            std::sin(pitch_rad),
            std::cos(pitch_rad) * std::cos(yaw_rad)
        };
        if (glm::dot(forward, forward) <= 0.0f) {
            forward = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        forward = glm::normalize(forward);

        Camera cam;
        cam.position = editor_camera_position;
        cam.target = editor_camera_position + forward;
        cam.up = {0.0f, 1.0f, 0.0f};
        cam.fov_y_radians = glm::radians(70.0f);
        cam.near_plane = 0.05f;
        cam.far_plane = 1000.0f;
        return cam;
    }
};

EngineHost::EngineHost()
    : m_impl(std::make_unique<Impl>()) {}

EngineHost::~EngineHost() {
    close();
}

bool EngineHost::open(const std::string& project_dir) {
    close();

    const std::filesystem::path dir = project_dir.empty() ? std::filesystem::path(".") : std::filesystem::path(project_dir);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        SOL_ERROR("EngineHost: project directory not found: " + dir.string());
        return false;
    }

    std::filesystem::current_path(dir, ec);
    if (ec) {
        SOL_ERROR("EngineHost: failed to change cwd to '" + dir.string() + "': " + ec.message());
        return false;
    }

    m_impl->project = ProjectConfig::load("project.sol");

    EngineConfig cfg;
    cfg.title = m_impl->project.window_title.empty() ? m_impl->project.name : m_impl->project.window_title;
    cfg.width = m_impl->project.window_width;
    cfg.height = m_impl->project.window_height;
    cfg.vsync = m_impl->project.window_vsync;

    if (!m_impl->engine.init(cfg)) {
        return false;
    }

    m_impl->editor_mode = false;
    m_impl->selected_node = nullptr;
    m_impl->open = true;
    SOL_INFO(std::string("EngineHost opened project: ") + m_impl->project.name);
    return true;
}

int EngineHost::run() {
    if (!m_impl->open) {
        SOL_ERROR("EngineHost::run called before open");
        return 1;
    }

    ImGui::SetCurrentContext(m_impl->engine.imgui_context());
    if (!m_impl->project.main_scene.empty()) {
        if (!load_scene(m_impl->project.main_scene)) {
            return 1;
        }
    } else {
        SOL_WARN("EngineHost: project has no main_scene");
    }

    const SolGameApi api = {
        SOL_ABI_VERSION,
        m_impl->project.name.c_str(),
        nullptr,
        [](Engine* e, float dt) {
            e->scene_manager().update(*e, dt);
        },
        [](Engine* e) {
            e->scene_manager().render(*e);
        },
        [](Engine* e) {
            e->scene_manager().unload(*e);
            e->set_cursor_captured(false);
        }
    };

    return m_impl->engine.run(api);
}

int EngineHost::run_game_api(const SolGameApi& api) {
    if (!m_impl->open) {
        SOL_ERROR("EngineHost::run_game_api called before open");
        return 1;
    }
    return m_impl->engine.run(api);
}

bool EngineHost::open_for_editor(void* hwnd, void* hinstance, int w, int h,
                                  const std::string& project_dir) {
    close();

    const std::filesystem::path dir = project_dir.empty() ? std::filesystem::path(".") : std::filesystem::path(project_dir);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        SOL_ERROR("EngineHost: project directory not found: " + dir.string());
        return false;
    }

    std::filesystem::current_path(dir, ec);
    if (ec) {
        SOL_ERROR("EngineHost: failed to change cwd to '" + dir.string() + "': " + ec.message());
        return false;
    }

    m_impl->project = ProjectConfig::load("project.sol");
    m_impl->viewport_w = std::max(w, 1);
    m_impl->viewport_h = std::max(h, 1);
    if (!m_impl->engine.init_for_editor(hwnd, hinstance, w, h)) {
        return false;
    }

    m_impl->editor_mode = true;
    m_impl->selected_node = nullptr;
    m_impl->open = true;
    if (!m_impl->project.main_scene.empty()) {
        if (!load_scene(m_impl->project.main_scene)) {
            SOL_WARN("EngineHost: could not load main_scene in editor mode");
        }
    }

    SOL_INFO(std::string("EngineHost opened for editor: ") + m_impl->project.name);
    return true;
}

void EngineHost::resize(int w, int h) {
    if (!m_impl->open) return;
    m_impl->engine.renderer().resize(w, h);
}

void EngineHost::set_editor_camera(const glm::vec3& pos, float yaw_deg, float pitch_deg) {
    m_impl->editor_camera_position = pos;
    m_impl->editor_camera_yaw = yaw_deg;
    m_impl->editor_camera_pitch = pitch_deg;
}

void EngineHost::set_editor_camera_active(bool active) {
    m_impl->editor_cam_active = active;
}

bool EngineHost::is_editor_camera_active() const {
    return m_impl->editor_cam_active;
}

glm::vec3 EngineHost::editor_camera_pos() const {
    return m_impl->editor_camera_position;
}

float EngineHost::editor_camera_yaw() const {
    return m_impl->editor_camera_yaw;
}

float EngineHost::editor_camera_pitch() const {
    return m_impl->editor_camera_pitch;
}

void EngineHost::imgui_viewport_size(int w, int h) {
    m_impl->viewport_w = std::max(w, 1);
    m_impl->viewport_h = std::max(h, 1);
}

void EngineHost::imgui_mouse_pos(float x, float y) {
    m_impl->mouse_pos = {x, y};
    if (!m_impl->open || !m_impl->engine.imgui_context()) return;
    ImGui::SetCurrentContext(m_impl->engine.imgui_context());
    ImGui::GetIO().AddMousePosEvent(x, y);
}

void EngineHost::imgui_mouse_button(int button, bool pressed) {
    if (!m_impl->open || !m_impl->engine.imgui_context()) return;
    ImGui::SetCurrentContext(m_impl->engine.imgui_context());
    ImGui::GetIO().AddMouseButtonEvent(button, pressed);
}

void EngineHost::imgui_mouse_wheel(float delta) {
    if (!m_impl->open || !m_impl->engine.imgui_context()) return;
    ImGui::SetCurrentContext(m_impl->engine.imgui_context());
    ImGui::GetIO().AddMouseWheelEvent(0.0f, delta);
}

void EngineHost::set_editor_viewport_size(int w, int h) {
    imgui_viewport_size(w, h);
}

void EngineHost::set_editor_fly_camera(bool active) {
    m_impl->editor_fly_camera = active;
}

void EngineHost::set_editor_camera_action(EditorCameraAction action, bool active) {
    m_impl->camera_actions[static_cast<size_t>(action)] = active;
}

void EngineHost::add_editor_mouse_delta(float dx, float dy) {
    m_impl->mouse_delta += glm::vec2(dx, dy);
}

void EngineHost::set_imgui_focused(bool focused) {
    if (!m_impl->open || !m_impl->engine.imgui_context()) return;
    ImGui::SetCurrentContext(m_impl->engine.imgui_context());
    ImGui::GetIO().AddFocusEvent(focused);
}

void EngineHost::set_imgui_mouse_pos(float x, float y) {
    imgui_mouse_pos(x, y);
}

void EngineHost::set_imgui_mouse_button(int button, bool down) {
    imgui_mouse_button(button, down);
}

void EngineHost::add_imgui_mouse_wheel(float wheel_x, float wheel_y) {
    if (wheel_x != 0.0f) {
        if (!m_impl->open || !m_impl->engine.imgui_context()) return;
        ImGui::SetCurrentContext(m_impl->engine.imgui_context());
        ImGui::GetIO().AddMouseWheelEvent(wheel_x, wheel_y);
        return;
    }
    imgui_mouse_wheel(wheel_y);
}

void EngineHost::set_imgui_key(ImGuiKey key, bool down) {
    if (key == ImGuiKey_None || !m_impl->open || !m_impl->engine.imgui_context()) return;
    ImGui::SetCurrentContext(m_impl->engine.imgui_context());
    ImGui::GetIO().AddKeyEvent(key, down);
}

void EngineHost::clear_editor_input() {
    m_impl->editor_fly_camera = false;
    m_impl->camera_actions.fill(false);
    m_impl->mouse_delta = {0.0f, 0.0f};
}

void EngineHost::set_selected_node(Node* node) {
    m_impl->selected_node = node;
}

Node* EngineHost::selected_node() const {
    return m_impl->selected_node;
}

void EngineHost::set_gizmo_operation(int op) {
    switch (op) {
    case 1: m_impl->gizmo_mode = EditorGizmoMode::Rotate; break;
    case 2: m_impl->gizmo_mode = EditorGizmoMode::Scale; break;
    default: m_impl->gizmo_mode = EditorGizmoMode::Translate; break;
    }
}

void EngineHost::set_gizmo_mode(EditorGizmoMode mode) {
    m_impl->gizmo_mode = mode;
}

EditorGizmoMode EngineHost::gizmo_mode() const {
    return m_impl->gizmo_mode;
}

Node* EngineHost::pick_node(float screen_x, float screen_y) const {
    if (!m_impl->open) return nullptr;

    Scene* scene = m_impl->engine.scene_manager().current();
    if (!scene || !scene->root()) return nullptr;

    const auto& renderer = m_impl->engine.renderer();
    const float W = static_cast<float>(m_impl->viewport_w > 0 ? m_impl->viewport_w : renderer.width());
    const float H = static_cast<float>(m_impl->viewport_h > 0 ? m_impl->viewport_h : renderer.height());
    if (W <= 0.0f || H <= 0.0f) return nullptr;

    Camera cam = renderer.camera();
    if (m_impl->editor_cam_active) {
        cam = m_impl->build_editor_camera();
    }
    const float aspect = W / H;

    const float ndcX = (2.0f * screen_x / W) - 1.0f;
    const float ndcY = -(2.0f * screen_y / H) + 1.0f;

    const glm::mat4 view = cam.view();
    const glm::mat4 proj = cam.proj(aspect, false);
    const glm::mat4 invVP = glm::inverse(proj * view);

    glm::vec4 near4 = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 far4 = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (near4.w == 0.0f || far4.w == 0.0f) return nullptr;
    near4 /= near4.w;
    far4 /= far4.w;

    const glm::vec3 ray_origin = glm::vec3(near4);
    const glm::vec3 ray_dir = glm::normalize(glm::vec3(far4) - ray_origin);

    Node* best = nullptr;
    float best_t = std::numeric_limits<float>::max();

    std::function<void(Node*)> walk = [&](Node* node) {
        if (node == scene->root()) {
            for (const auto& child : node->children()) walk(child.get());
            return;
        }

        if (auto* n3d = dynamic_cast<Node3D*>(node)) {
            const glm::mat4 world = n3d->global_transform();
            const glm::vec3 center = glm::vec3(world[3]);
            glm::vec3 sc = glm::abs(n3d->scale);
            float radius = glm::length(sc) * 0.5f;
            radius = std::max(radius, 0.3f);

            const glm::vec3 oc = ray_origin - center;
            const float b = glm::dot(oc, ray_dir);
            const float c = glm::dot(oc, oc) - radius * radius;
            const float disc = b * b - c;
            if (disc >= 0.0f) {
                const float t = -b - std::sqrt(disc);
                if (t > 0.001f && t < best_t) {
                    best_t = t;
                    best = node;
                }
            }
        }

        for (const auto& child : node->children()) walk(child.get());
    };

    walk(scene->root());
    return best;
}

void EngineHost::tick(float dt) {
    if (!m_impl->open) return;
    m_impl->last_dt = dt;

    ImGuiLayer* imgui_layer = m_impl->engine.imgui_ptr();
    const bool editor_imgui = imgui_layer && imgui_layer->initialized() && imgui_layer->is_editor_mode();

    // Start ImGui frame BEFORE on_update so scene nodes (e.g. FlyCamController)
    // can safely make ImGui calls during their update pass.
    if (editor_imgui) {
        ImGui::SetCurrentContext(m_impl->engine.imgui_context());
        imgui_layer->begin_frame_editor(m_impl->viewport_w, m_impl->viewport_h, dt);
    }

    m_impl->engine.tick_one_frame(dt,
        [this] {
            m_impl->engine.scene_manager().update(m_impl->engine, m_impl->engine.delta_time());
        },
        [this, editor_imgui, imgui_layer] {
            m_impl->engine.scene_manager().render(m_impl->engine);

            if (m_impl->editor_mode && m_impl->editor_cam_active) {
                m_impl->engine.renderer().set_camera(m_impl->build_editor_camera());
            }

            if (editor_imgui) {
                if (m_impl->selected_node) {
                    render_editor_gizmo_();
                }
                imgui_layer->end_frame();
            }
        }
    );

    m_impl->mouse_delta = {0.0f, 0.0f};
}

void EngineHost::close() {
    if (!m_impl->open) {
        return;
    }
    m_impl->engine.shutdown();
    m_impl->selected_node = nullptr;
    m_impl->editor_mode = false;
    m_impl->open = false;
}

bool EngineHost::is_open() const {
    return m_impl->open;
}

Engine& EngineHost::engine() {
    return m_impl->engine;
}

const ProjectConfig& EngineHost::project() const {
    return m_impl->project;
}

bool EngineHost::load_scene(const std::string& scene_path) {
    if (!m_impl->open) {
        SOL_ERROR("EngineHost::load_scene called before open");
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(scene_path, ec)) {
        SOL_ERROR("EngineHost: scene file not found: " + scene_path);
        return false;
    }

    auto scene = Scene::load(scene_path, m_impl->engine);
    if (!scene) {
        return false;
    }

    m_impl->engine.scene_manager().set_scene(std::move(scene), m_impl->engine);
    m_impl->selected_node = nullptr;
    return true;
}

Node* EngineHost::scene_root() const {
    if (!m_impl->open) return nullptr;
    Scene* scene = m_impl->engine.scene_manager().current();
    return scene ? scene->root() : nullptr;
}

Node* EngineHost::instantiate_model(const std::string& path, Node* parent) {
    if (!m_impl->open) return nullptr;
    Scene* scene = m_impl->engine.scene_manager().current();
    if (!scene || !scene->root()) return nullptr;

    Node* target = parent ? parent : scene->root();

    std::string node_name = path;
    auto slash = node_name.find_last_of("/\\");
    if (slash != std::string::npos) node_name = node_name.substr(slash + 1);
    auto dot = node_name.rfind('.');
    if (dot != std::string::npos) node_name = node_name.substr(0, dot);

    auto node = std::make_unique<ModelNode>();
    node->path = path;
    node->name = node_name;
    node->position = glm::vec3(0.0f, 0.0f, 0.0f);

    ModelNode* raw = node.get();
    target->add_child(std::move(node));
    raw->on_ready(m_impl->engine);

    return raw;
}

bool EngineHost::save_scene(const std::string& path) {
    if (!m_impl->open) return false;
    Scene* scene = m_impl->engine.scene_manager().current();
    if (!scene) return false;

    std::string save_path = path.empty() ? scene->path : path;
    if (save_path.empty()) {
        SOL_ERROR("EngineHost::save_scene: no path provided and scene has no path");
        return false;
    }

    bool ok = scene->save(save_path);
    if (ok) {
        scene->path = save_path;
        SOL_INFO("Scene saved: " + save_path);
    } else {
        SOL_ERROR("EngineHost::save_scene: failed to write " + save_path);
    }
    return ok;
}

void EngineHost::set_field(Node* node, const std::string& field_name, const void* data) {
    if (!node || !data) return;
    const TypeDesc* desc = ComponentRegistry::instance().find(node->type_name());
    if (!desc) return;
    for (const auto& field : desc->fields) {
        if (field.name != field_name) continue;
        void* ptr = field.ptr(node);
        switch (field.type) {
            case FieldType::Float:
                *static_cast<float*>(ptr) = *static_cast<const float*>(data);
                break;
            case FieldType::Bool:
                *static_cast<bool*>(ptr) = *static_cast<const bool*>(data);
                break;
            case FieldType::Int:
            case FieldType::EnumInt:
                *static_cast<int*>(ptr) = *static_cast<const int*>(data);
                break;
            case FieldType::Vec3:
            case FieldType::Color3:
                std::memcpy(ptr, data, 12);
                break;
            case FieldType::Vec4:
            case FieldType::Color4:
                std::memcpy(ptr, data, 16);
                break;
            case FieldType::String:
            case FieldType::AssetPath:
                *static_cast<std::string*>(ptr) = *static_cast<const std::string*>(data);
                break;
        }
        return;
    }
}

void EngineHost::render_editor_gizmo_() {
    m_impl->gizmo_hovered = false;
    m_impl->gizmo_active = false;

    if (!m_impl->editor_mode || !ImGui::GetCurrentContext()) return;
    auto* node3d = dynamic_cast<Node3D*>(m_impl->selected_node);
    if (!node3d || m_impl->viewport_w <= 0 || m_impl->viewport_h <= 0) return;

    const auto& renderer = m_impl->engine.renderer();
    const float aspect = m_impl->viewport_h > 0
        ? static_cast<float>(m_impl->viewport_w) / static_cast<float>(m_impl->viewport_h)
        : 1.0f;

    const Camera& cam = renderer.camera();
    glm::mat4 view = cam.view();
    glm::mat4 proj = cam.proj(aspect, true);
    proj[1][1] *= -1.0f;

    glm::mat4 world = node3d->global_transform();

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    switch (m_impl->gizmo_mode) {
    case EditorGizmoMode::Rotate: op = ImGuizmo::ROTATE; break;
    case EditorGizmoMode::Scale:  op = ImGuizmo::SCALE; break;
    default: break;
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    // Do NOT call SetDrawlist() — BeginFrame() creates a fullscreen "gizmo"
    // overlay window and sets gContext.mDrawList to its window draw list.
    // Overriding with GetBackgroundDrawList() causes IsHoveringWindow() to call
    // FindWindowByName("##Background") which returns nullptr → crash.
    ImGuizmo::SetRect(0.0f, 0.0f,
        static_cast<float>(m_impl->viewport_w),
        static_cast<float>(m_impl->viewport_h));

    const bool changed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        op,
        ImGuizmo::WORLD,
        glm::value_ptr(world),
        nullptr);

    if (changed) {
        glm::mat4 local = world;
        if (auto* parent3d = dynamic_cast<Node3D*>(node3d->parent())) {
            local = glm::inverse(parent3d->global_transform()) * world;
        }

        glm::vec3 translation {};
        glm::vec3 rotation_deg {};
        glm::vec3 scale {};
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(local),
            glm::value_ptr(translation),
            glm::value_ptr(rotation_deg),
            glm::value_ptr(scale));

        node3d->position = translation;
        node3d->rotation = rotation_deg;
        node3d->scale = scale;
    }

    m_impl->gizmo_hovered = ImGuizmo::IsOver();
    m_impl->gizmo_active = ImGuizmo::IsUsing();
}

} // namespace sol
