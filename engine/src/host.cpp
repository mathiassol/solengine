#include "sol/host.h"
#include "sol/engine.h"
#include "sol/log.h"
#include "sol/project/project.h"
#include "sol/input/input_manager.h"
#include "sol/scene/model_node.h"
#include "sol/scene/mesh_node.h"
#include "sol/scene/node3d.h"
#include "sol/scene/camera3d.h"
#include "sol/scene/light_node.h"
#include "sol/scene/node_factory.h"
#include "sol/scene/scene.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/world_environment.h"
#include "sol/scene/lua_component.h"
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
#include <unordered_map>

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
    bool          gizmo_was_using     = false;
    glm::vec3     gizmo_drag_start_pos{};
    glm::vec3     gizmo_drag_start_rot{};
    glm::vec3     gizmo_drag_start_scale{};
    Node3D*       gizmo_drag_node     = nullptr;
    std::function<void(Node3D*, glm::vec3, glm::vec3, glm::vec3,
                                 glm::vec3, glm::vec3, glm::vec3)> gizmo_undo_callback;
    bool          gizmo_local_space   = false;
    bool          gizmo_visible       = true;   // set false by editor when non-scene tab active
    std::function<void()> editor_draw_fn;

    // ── Hot Scene Slot state ───────────────────────────────────────────────
    static constexpr int kNoSlot = -2; // sentinel: no pending slot switch
    std::unordered_map<int, std::unique_ptr<Scene>> m_scene_slots; // inactive parked scenes
    int m_next_slot_id    = 0;
    int m_active_slot_id  = -1;   // -1 = no slot adopted yet
    int m_pending_slot_id = kNoSlot; // slot to switch to at next tick start

    Camera build_editor_camera() const {
        const float yaw_rad = glm::radians(editor_camera_yaw);
        const float pitch_rad = glm::radians(editor_camera_pitch);
        glm::vec3 forward {
            std::cos(pitch_rad) * std::sin(yaw_rad),
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

    m_impl->engine.input().load_actions(m_impl->project);

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
            // Scene update driven by Engine::run() phased tick.
            (void)e; (void)dt;
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

    m_impl->engine.input().load_actions(m_impl->project);

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

    // ── Hot slot switch (deferred from activate_scene_slot) ──────────────────
    if (m_impl->m_pending_slot_id != Impl::kNoSlot) {
        const int target = m_impl->m_pending_slot_id;
        m_impl->m_pending_slot_id = Impl::kNoSlot;

        if (target != m_impl->m_active_slot_id) {
            // Cancel any pending scene swap that was queued via load_scene() — it
            // was for the old tab and would stomp the incoming slot's scene.
            m_impl->engine.scene_manager().cancel_pending();

            // Wait for all in-flight GPU work before touching scene resources.
            m_impl->engine.renderer().wait_idle();

            // Park the currently active scene into its slot (raw: no on_destroy).
            if (m_impl->m_active_slot_id >= 0) {
                m_impl->m_scene_slots[m_impl->m_active_slot_id] =
                    m_impl->engine.scene_manager().detach_scene_raw();
            }

            // Pull the target slot's scene into the SceneManager (raw: no on_ready).
            auto sit = m_impl->m_scene_slots.find(target);
            if (sit != m_impl->m_scene_slots.end()) {
                m_impl->engine.scene_manager().attach_scene_raw(std::move(sit->second));
                sit->second = nullptr; // slot is now "active" (scene lives in SceneManager)
            }

            m_impl->m_active_slot_id  = target;
            m_impl->selected_node     = nullptr;
            m_impl->gizmo_drag_node   = nullptr;
        }
    }

    ImGuiLayer* imgui_layer = m_impl->engine.imgui_ptr();
    const bool editor_imgui = imgui_layer && imgui_layer->initialized() && imgui_layer->is_editor_mode();

    // Start the editor ImGui frame BEFORE scene update so that any ImGui calls
    // made by game nodes during update/render land inside a valid frame and do
    // not crash. The frame is submitted (Render) inside the render lambda.
    if (editor_imgui) {
        ImGui::SetCurrentContext(m_impl->engine.imgui_context());
        imgui_layer->begin_frame_editor(m_impl->viewport_w, m_impl->viewport_h, dt);
    }

    m_impl->engine.tick_one_frame(dt,
        [this] {
            // Scene update driven by tick_one_frame phased tick.
        },
        [this, editor_imgui, imgui_layer] {
            m_impl->engine.scene_manager().render(m_impl->engine);

            if (m_impl->editor_mode && m_impl->editor_cam_active) {
                m_impl->engine.renderer().set_camera(m_impl->build_editor_camera());
            }

            if (editor_imgui) {
                if (m_impl->editor_draw_fn) {
                    m_impl->editor_draw_fn();
                }
                if (m_impl->selected_node && m_impl->gizmo_visible) {
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
    m_impl->gizmo_drag_node = nullptr;
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

    SOL_INFO("EngineHost::load_scene: requested '" + scene_path + "'");

    std::error_code ec;
    if (!std::filesystem::exists(scene_path, ec)) {
        SOL_ERROR("EngineHost: scene file not found: '" + scene_path + "' (cwd: " +
                  std::filesystem::current_path(ec).string() + ")");
        return false;
    }

    auto scene = Scene::load(scene_path, m_impl->engine);
    if (!scene) {
        SOL_ERROR("EngineHost: Scene::load failed for '" + scene_path + "'");
        return false;
    }

    SOL_INFO("EngineHost: scene '" + scene->name + "' loaded OK, queuing deferred swap");

    // Defer the scene swap to the start of the next tick (flush_pending),
    // so we are never mid-frame (between begin_frame/end_frame) when GPU
    // resources from the old scene are destroyed.  Clearing selections
    // immediately is safe — they will just be null for one frame.
    m_impl->engine.scene_manager().request_scene(std::move(scene));
    m_impl->selected_node = nullptr;
    m_impl->gizmo_drag_node = nullptr;
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

Node* EngineHost::create_node(const std::string& type, Node* parent) {
    if (!m_impl->open) return nullptr;
    Scene* scene = m_impl->engine.scene_manager().current();
    if (!scene || !scene->root()) return nullptr;

    Node* target = parent ? parent : scene->root();
    std::unique_ptr<Node> node = NodeFactory::instance().create(type);
    if (!node) {
        SOL_WARN("create_node: unknown type '" + type + "'");
        return nullptr;
    }

    node->name = type;
    Node* raw = node.get();
    target->add_child(std::move(node));
    raw->on_ready(m_impl->engine);
    return raw;
}

bool EngineHost::remove_node(Node* node) {
    if (!m_impl->open || !node) return false;
    Scene* scene = m_impl->engine.scene_manager().current();
    if (!scene || !scene->root() || node == scene->root()) return false;

    if (m_impl->selected_node) {
        for (Node* cur = m_impl->selected_node; cur; cur = cur->parent()) {
            if (cur == node) {
                m_impl->selected_node = nullptr;
                break;
            }
        }
    }
    if (m_impl->gizmo_drag_node) {
        for (Node* cur = m_impl->gizmo_drag_node; cur; cur = cur->parent()) {
            if (cur == node) {
                m_impl->gizmo_drag_node = nullptr;
                break;
            }
        }
    }

    Node* parent = node->parent();
    return parent ? parent->remove_child(node) : false;
}

void EngineHost::rename_node(Node* node, const std::string& new_name) {
    if (node) node->name = new_name;
}

float EngineHost::frame_fps() const {
    if (!m_impl->open || m_impl->last_dt <= 0.0f) return 0.0f;
    return 1.0f / m_impl->last_dt;
}

int EngineHost::frame_draw_calls() const {
    if (!m_impl->open) return 0;
    return m_impl->engine.renderer().draw_call_count();
}

void EngineHost::set_gizmo_space(bool local) {
    m_impl->gizmo_local_space = local;
}

bool EngineHost::gizmo_space_local() const {
    return m_impl->gizmo_local_space;
}

void EngineHost::set_gizmo_undo_callback(
    std::function<void(Node3D*, glm::vec3, glm::vec3, glm::vec3,
                                glm::vec3, glm::vec3, glm::vec3)> cb) {
    m_impl->gizmo_undo_callback = std::move(cb);
}

Node* EngineHost::duplicate_node(Node* node) {
    if (!node || !m_impl->open) return nullptr;
    Scene* scene = m_impl->engine.scene_manager().current();
    if (!scene || !scene->root()) return nullptr;
    Node* parent = node->parent();
    if (!parent) return nullptr;

    const std::string type = node->type_name();
    std::unique_ptr<Node> new_unique = NodeFactory::instance().create(type);
    if (!new_unique) return nullptr;

    Node* new_node = new_unique.get();
    new_node->name = node->name + "_copy";
    parent->add_child(std::move(new_unique));

    const TypeDesc* desc = ComponentRegistry::instance().find(type);
    if (desc) {
        for (const auto& field : desc->fields) {
            if (field.type == FieldType::SectionHeader) continue;
            void* src = field.ptr(node);
            if (src) set_field(new_node, field.name, src);
        }
    }
    new_node->script_path = node->script_path;

    // Duplicate attached LuaComponents.
    for (const auto& comp : node->components()) {
        if (std::strcmp(comp->component_type(), "LuaComponent") == 0) {
            if (const auto* lc = dynamic_cast<const LuaComponent*>(comp.get()))
                new_node->add_component(std::make_unique<LuaComponent>(lc->script_path()));
        }
    }

    new_node->on_ready(m_impl->engine);
    return new_node;
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

std::string EngineHost::scene_state_to_string() const {
    if (!m_impl->open) return {};
    Scene* scene = m_impl->engine.scene_manager().current();
    if (!scene) return {};
    return scene->save_to_string();
}

bool EngineHost::load_scene_from_string(const std::string& json_str, const std::string& original_path) {
    if (!m_impl->open) return false;
    if (json_str.empty()) {
        // Restore to empty scene
        auto empty = std::make_unique<Scene>();
        empty->name = "Untitled";
        m_impl->engine.scene_manager().request_scene(std::move(empty));
        m_impl->selected_node  = nullptr;
        m_impl->gizmo_drag_node = nullptr;
        return true;
    }
    auto scene = Scene::load_from_string(json_str, m_impl->engine);
    if (!scene) return false;
    if (!original_path.empty()) scene->path = original_path;
    m_impl->engine.scene_manager().request_scene(std::move(scene));
    m_impl->selected_node  = nullptr;
    m_impl->gizmo_drag_node = nullptr;
    return true;
}

// ── Hot Scene Slot API ──────────────────────────────────────────────────────

int EngineHost::create_scene_slot() {
    const int id = m_impl->m_next_slot_id++;
    if (m_impl->m_active_slot_id < 0) {
        // First slot: adopt whatever is already in the SceneManager.
        m_impl->m_active_slot_id = id;
        m_impl->m_scene_slots[id] = nullptr; // scene lives in SceneManager
    } else {
        // Subsequent slot: starts empty.
        m_impl->m_scene_slots[id] = nullptr;
    }
    return id;
}

void EngineHost::destroy_scene_slot(int slot_id) {
    if (!m_impl->open) return;
    if (slot_id == m_impl->m_active_slot_id) {
        m_impl->engine.renderer().wait_idle();
        m_impl->engine.scene_manager().unload(m_impl->engine);
        m_impl->m_scene_slots.erase(slot_id);
        m_impl->m_active_slot_id = -1;
        m_impl->selected_node   = nullptr;
        m_impl->gizmo_drag_node = nullptr;
    } else {
        auto it = m_impl->m_scene_slots.find(slot_id);
        if (it != m_impl->m_scene_slots.end()) {
            if (it->second)
                it->second->on_destroy(m_impl->engine);
            m_impl->m_scene_slots.erase(it);
        }
    }
}

bool EngineHost::load_scene_into_slot(int slot_id, const std::string& path) {
    if (!m_impl->open) return false;
    if (slot_id == m_impl->m_active_slot_id) {
        return load_scene(path);
    }
    auto it = m_impl->m_scene_slots.find(slot_id);
    if (it == m_impl->m_scene_slots.end()) {
        SOL_WARN("EngineHost::load_scene_into_slot: unknown slot " + std::to_string(slot_id));
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        SOL_ERROR("EngineHost::load_scene_into_slot: file not found: " + path);
        return false;
    }
    if (it->second) it->second->on_destroy(m_impl->engine);
    auto scene = Scene::load(path, m_impl->engine);
    if (!scene) {
        SOL_ERROR("EngineHost::load_scene_into_slot: failed to load: " + path);
        it->second = nullptr;
        return false;
    }
    scene->on_ready(m_impl->engine);
    it->second = std::move(scene);
    return true;
}

void EngineHost::activate_scene_slot(int slot_id) {
    if (!m_impl->open) return;
    if (!m_impl->m_scene_slots.count(slot_id)) {
        SOL_WARN("EngineHost::activate_scene_slot: unknown slot " + std::to_string(slot_id));
        return;
    }
    m_impl->m_pending_slot_id = slot_id;
}

int EngineHost::active_scene_slot() const {
    return m_impl->m_active_slot_id;
}

void EngineHost::set_field(Node* node, const std::string& field_name, const void* data) {
    if (!node || !data) return;
    const TypeDesc* desc = ComponentRegistry::instance().find(node->type_name());
    if (!desc) return;
    for (const auto& field : desc->fields) {
        if (field.name != field_name) continue;
        if (field.type == FieldType::SectionHeader) continue;
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

void EngineHost::set_editor_draw_fn(std::function<void()> fn) {
    m_impl->editor_draw_fn = std::move(fn);
}

void EngineHost::set_gizmo_visible(bool v) {
    m_impl->gizmo_visible = v;
}

void EngineHost::imgui_add_text(const char* utf8) {
    if (!utf8 || !m_impl->open || !m_impl->engine.imgui_context()) return;
    ImGui::SetCurrentContext(m_impl->engine.imgui_context());
    ImGui::GetIO().AddInputCharactersUTF8(utf8);
}

ImGuiContext* EngineHost::imgui_get_context() const {
    if (!m_impl->open) return nullptr;
    return m_impl->engine.imgui_context();
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
    // Note: do NOT flip proj[1][1] — ImGuizmo expects OpenGL convention (Y-up NDC).
    // The Vulkan Y-flip is a rendering pipeline concern only.

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
        m_impl->gizmo_local_space ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
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

    // Gizmo drag start detection
    if (ImGuizmo::IsUsing() && !m_impl->gizmo_was_using) {
        m_impl->gizmo_drag_start_pos   = node3d->position;
        m_impl->gizmo_drag_start_rot   = node3d->rotation;
        m_impl->gizmo_drag_start_scale = node3d->scale;
        m_impl->gizmo_drag_node        = node3d;
        m_impl->gizmo_was_using        = true;
    }

    // Gizmo drag end detection
    if (!ImGuizmo::IsUsing() && m_impl->gizmo_was_using) {
        if (m_impl->gizmo_undo_callback && m_impl->gizmo_drag_node) {
            m_impl->gizmo_undo_callback(
                m_impl->gizmo_drag_node,
                m_impl->gizmo_drag_start_pos,  m_impl->gizmo_drag_start_rot,  m_impl->gizmo_drag_start_scale,
                m_impl->gizmo_drag_node->position, m_impl->gizmo_drag_node->rotation, m_impl->gizmo_drag_node->scale);
        }
        m_impl->gizmo_was_using = false;
        m_impl->gizmo_drag_node = nullptr;
    }

    m_impl->gizmo_hovered = ImGuizmo::IsOver();
    m_impl->gizmo_active = ImGuizmo::IsUsing();
}

void EngineHost::apply_mesh_material_textures(Node* node) {
    if (!m_impl->open || !node) return;
    if (auto* mn = dynamic_cast<MeshNode*>(node))
        mn->apply_material_textures(m_impl->engine);
}

} // namespace sol
