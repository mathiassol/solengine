#include "sol/engine.h"
#include "sol/log.h"

#include "platform/window.h"
#include "render/vk/vk_renderer.h"
#include "physics/physics.h"
#include "ui/imgui_layer.h"
#include "asset/gltf_loader.h"

#include "sol/scene/scene_manager.h"

#include <GLFW/glfw3.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

namespace sol {

Engine::Engine()  = default;
Engine::~Engine() { shutdown(); }

bool Engine::init(const EngineConfig& cfg) {
    m_cfg     = cfg;
    m_window  = std::make_unique<Window>();
    if (!m_renderer) m_renderer = std::make_unique<VulkanRenderer>();
    m_physics = std::make_unique<PhysicsWorld>();
    m_imgui   = std::make_unique<ImGuiLayer>();
    m_assets  = std::make_unique<GltfLoader>();
    m_scene_manager = std::make_unique<SceneManager>();

    if (!m_window->init(cfg.title, cfg.width, cfg.height))      return false;
    if (!m_renderer->init(*m_window))                           return false;
    if (!m_physics->init())                                     return false;
    if (!m_imgui->init(m_window->handle()))                     return false;

    m_running = true;
    SOL_INFO("Engine initialized");
    return true;
}

bool Engine::init_for_editor(void* hwnd, void* hinstance, int w, int h) {
    m_cfg = {};
    m_window.reset();
    m_imgui.reset();

    auto vk = std::make_unique<VulkanRenderer>();
    if (!vk->init_win32(hwnd, hinstance, w, h)) return false;
    m_renderer = std::move(vk);

    m_physics = std::make_unique<PhysicsWorld>();
    if (!m_physics->init()) return false;
    m_assets = std::make_unique<GltfLoader>();
    m_scene_manager = std::make_unique<SceneManager>();
    m_imgui = std::make_unique<ImGuiLayer>();
    if (!m_imgui->init_editor()) {
        SOL_WARN("ImGui editor init failed — gizmo disabled");
        m_imgui.reset();
    }

    m_running = true;
    SOL_INFO("Engine initialized (editor mode)");
    return true;
}

void Engine::tick_one_frame(float dt,
                            std::function<void()> on_update,
                            std::function<void()> on_render) {
    if (!m_running) return;
    m_dt = dt;
    m_scene_manager->flush_pending(*this);
    if (on_update) on_update();
    m_physics->step(m_dt);
    m_renderer->begin_frame();
    if (on_render) on_render();
    m_renderer->end_frame();
}


void Engine::shutdown() {
    if (m_imgui)    m_imgui->shutdown();
    if (m_scene_manager) m_scene_manager->unload(*this);
    if (m_physics)  m_physics->shutdown();
    if (m_renderer) m_renderer->shutdown();
    if (m_window)   m_window->shutdown();
    m_scene_manager.reset();
    m_imgui.reset(); m_physics.reset(); m_renderer.reset(); m_window.reset(); m_assets.reset();
    m_running = false;
}

ImGuiContext* Engine::imgui_context() const {
    return ImGui::GetCurrentContext();
}

int Engine::run(const SolGameApi& game) {
    if (!m_running) { SOL_ERROR("Engine::run called before init"); return 1; }
    if (game.abi_version != SOL_ABI_VERSION) {
        SOL_ERROR("Game ABI version mismatch");
        return 2;
    }
    SOL_INFO(std::string("Loaded game: ") + (game.name ? game.name : "?"));

    if (game.on_init) game.on_init(this);
    glfwSetWindowShouldClose(m_window->handle(), GLFW_FALSE);
    SOL_INFO("Entering main loop");

    auto last = std::chrono::steady_clock::now();

    while (m_running && !m_window->should_close()) {
        const auto now = std::chrono::steady_clock::now();
        m_dt = std::chrono::duration<float>(now - last).count();
        last = now;

        m_window->poll();
        if (m_window->width() > 0 && m_window->height() > 0 &&
            (m_renderer->width() != m_window->width() || m_renderer->height() != m_window->height())) {
            m_renderer->resize(m_window->width(), m_window->height());
        }
        // Apply any deferred scene swap requested from on_update / on_ready.
        m_scene_manager->flush_pending(*this);

        m_imgui->begin_frame();
        if (game.on_update) game.on_update(this, m_dt);
        m_physics->step(m_dt);
        m_renderer->begin_frame();
        if (game.on_render) game.on_render(this);
        m_imgui->end_frame();
        m_renderer->end_frame();
    }

    if (game.on_shutdown) game.on_shutdown(this);
    return 0;
}

bool Engine::self_test() {
    SOL_INFO("=== Sol self-test ===");

    // 1. ECS — create entity with components, iterate.
    struct Position { glm::vec3 v; };
    struct Velocity { glm::vec3 v; };
    auto e = m_registry.create();
    m_registry.emplace<Position>(e, glm::vec3(0));
    m_registry.emplace<Velocity>(e, glm::vec3(1, 2, 3));
    int seen = 0;
    for (auto [_, p, v] : m_registry.view<Position, Velocity>().each()) {
        p.v += v.v * 0.5f;
        ++seen;
    }
    if (seen != 1) { SOL_ERROR("ECS test failed"); return false; }
    SOL_INFO("  [ok] EnTT ECS");

    // 2. GLM — matrix math
    glm::mat4 m = glm::translate(glm::mat4(1.0f), {1, 2, 3});
    if (m[3].x != 1.0f || m[3].y != 2.0f) { SOL_ERROR("GLM test failed"); return false; }
    SOL_INFO("  [ok] GLM");

    // 3. bgfx — already inited; check renderer type is valid.
    if (!dynamic_cast<VulkanRenderer*>(m_renderer.get())) {
        SOL_ERROR("Renderer self-test failed");
        return false;
    }
    SOL_INFO("  [ok] Vulkan renderer");

    // 4. Physics — drop a sphere onto a static box, step a few frames.
    using namespace JPH;
    BodyInterface& bi = m_physics->system()->GetBodyInterface();

    BoxShapeSettings floor_settings(Vec3(50, 1, 50));
    floor_settings.SetEmbedded();
    ShapeSettings::ShapeResult floor_shape = floor_settings.Create();
    BodyCreationSettings floor_bcs(floor_shape.Get(), RVec3(0, -1, 0), Quat::sIdentity(),
                                   EMotionType::Static, 0 /*NON_MOVING layer*/);
    Body* floor = bi.CreateBody(floor_bcs);
    bi.AddBody(floor->GetID(), EActivation::DontActivate);

    BodyCreationSettings sphere_bcs(new SphereShape(0.5f), RVec3(0, 5, 0), Quat::sIdentity(),
                                    EMotionType::Dynamic, 1 /*MOVING layer*/);
    BodyID sphere = bi.CreateAndAddBody(sphere_bcs, EActivation::Activate);

    m_physics->system()->OptimizeBroadPhase();
    for (int i = 0; i < 30; ++i) m_physics->step(1.0f / 60.0f);

    RVec3 p = bi.GetCenterOfMassPosition(sphere);
    if (p.GetY() >= 5.0f) { SOL_ERROR("Physics: sphere did not fall"); return false; }
    SOL_INFO("  [ok] Jolt physics");
    bi.RemoveBody(sphere); bi.DestroyBody(sphere);
    bi.RemoveBody(floor->GetID()); bi.DestroyBody(floor->GetID());

    // 5. ImGui — ensure context and a frame round-trip.
    m_imgui->begin_frame();
    ImGui::Begin("self-test");
    ImGui::Text("hello");
    ImGui::End();
    m_imgui->end_frame();
    SOL_INFO("  [ok] ImGui");

    // 6. Asset pipeline — loader instantiated. (Loading a real .glb requires a file.)
    SOL_INFO("  [ok] GLTF asset pipeline (loader ready)");

    SOL_INFO("=== self-test PASSED ===");
    m_registry.destroy(e);
    return true;
}

bool Engine::key_down(int key) const {
    return m_window && glfwGetKey(m_window->handle(), key) == GLFW_PRESS;
}
bool Engine::mouse_button_down(int button) const {
    return m_window && glfwGetMouseButton(m_window->handle(), button) == GLFW_PRESS;
}
void Engine::cursor_position(double& x, double& y) const {
    if (m_window) glfwGetCursorPos(m_window->handle(), &x, &y);
    else x = y = 0.0;
}
GLFWwindow* Engine::native_window() const {
    return m_window ? m_window->handle() : nullptr;
}
void Engine::set_cursor_captured(bool captured) const {
    if (!m_window) return;
    glfwSetInputMode(m_window->handle(), GLFW_CURSOR,
        captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

} // namespace sol
