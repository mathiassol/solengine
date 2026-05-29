#include "sol/engine.h"
#include "sol/log.h"
#include "sol/perf/profiler.h"
#include "sol/audio/audio_engine.h"
#include "sol/scene/camera3d.h"
#include "sol/input/input_manager.h"

#include "platform/window.h"
#include "render/vk/vk_renderer.h"
#include "physics/physics.h"
#include "ui/imgui_layer.h"
#include "asset/model_loader.h"

#include "sol/scene/node_factory.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/scene.h"
#include "sol/scene/node3d.h"
#include "sol/scene/mesh_node.h"
#include "sol/scene/light_node.h"
#include "sol/scene/world_environment.h"
#include "sol/script/script_engine.h"
#include "sol/reflect.h"

#include <GLFW/glfw3.h>
#include <fstream>
#include <filesystem>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <thread>

namespace sol {

Engine::Engine()  = default;
Engine::~Engine() { shutdown(); }

bool Engine::init(const EngineConfig& cfg) {
    m_cfg     = cfg;
    register_builtin_node_types();
    m_window  = std::make_unique<Window>();
    if (!m_renderer) m_renderer = std::make_unique<VulkanRenderer>();
    m_physics = std::make_unique<PhysicsWorld>();
    m_imgui   = std::make_unique<ImGuiLayer>();
    m_assets  = std::make_unique<ModelLoader>();
    m_scene_manager = std::make_unique<SceneManager>();
    m_script  = std::make_unique<ScriptEngine>();

    if (!m_window->init(cfg.title, cfg.width, cfg.height))      return false;
    if (!m_renderer->init(*m_window))                           return false;
    if (!m_physics->init())                                     return false;
    if (!m_imgui->init(m_window->handle()))                     return false;

    m_input = std::make_unique<InputManager>();
    m_input->set_window(m_window->handle());

    if (!m_script->init(*this))                                 return false;

    m_audio = std::make_unique<AudioEngine>();
    if (!m_audio->init()) {
        SOL_ERROR("AudioEngine init failed (continuing without audio)");
    }

    m_running = true;
    SOL_INFO("Engine initialized");
    return true;
}

bool Engine::init_for_editor(void* hwnd, void* hinstance, int w, int h) {
    m_cfg = {};
    register_builtin_node_types();
    m_window.reset();
    m_imgui.reset();

    auto vk = std::make_unique<VulkanRenderer>();
    if (!vk->init_win32(hwnd, hinstance, w, h)) return false;
    m_renderer = std::move(vk);

    m_physics = std::make_unique<PhysicsWorld>();
    if (!m_physics->init()) return false;
    m_assets = std::make_unique<ModelLoader>();
    m_scene_manager = std::make_unique<SceneManager>();
    m_script = std::make_unique<ScriptEngine>();
    m_input  = std::make_unique<InputManager>();
    // No GLFW window in editor mode — InputManager works without one.
    if (!m_script->init(*this)) return false;

    m_audio = std::make_unique<AudioEngine>();
    if (!m_audio->init()) {
        SOL_WARN("AudioEngine init failed in editor mode");
    }

    m_imgui = std::make_unique<ImGuiLayer>();
    if (!m_imgui->init_editor()) {
        SOL_WARN("ImGui editor init failed — gizmo disabled");
        m_imgui.reset();
    }

    m_running = true;
    m_editor_mode = true;
    SOL_INFO("Engine initialized (editor mode)");
    return true;
}

void Engine::tick_one_frame(float dt,
                            std::function<void()> on_update,
                            std::function<void()> on_render) {
    if (!m_running) return;
    m_dt = dt;
    m_elapsed += m_dt;
    Profiler::instance().reset();

    if (m_input) {
        m_input->begin_frame();
        m_input->update(nullptr);  // editor: no GLFW window
    }

    if (m_scene_manager) m_scene_manager->flush_pending(*this);

    SOL_PROFILE_BEGIN("PrePhysics");
    if (m_scene_manager) m_scene_manager->update_pre_physics(*this, m_dt);
    m_ecs_world.tick(ETickGroup::PrePhysics, m_dt);
    m_ecs_world.tick_processors(ETickGroup::PrePhysics, m_dt);
    SOL_PROFILE_END("PrePhysics");

    SOL_PROFILE_BEGIN("Physics");
    if (m_physics) m_physics->step(m_dt);
    if (m_physics && m_script) m_physics->dispatch_contacts(*this);
    SOL_PROFILE_END("Physics");

    SOL_PROFILE_BEGIN("PostPhysics");
    if (m_scene_manager) m_scene_manager->update_post_physics(*this, m_dt);
    m_ecs_world.tick(ETickGroup::PostPhysics, m_dt);
    m_ecs_world.tick_processors(ETickGroup::PostPhysics, m_dt);
    m_ecs_world.tick(ETickGroup::PostUpdateWork, m_dt);
    m_ecs_world.tick_processors(ETickGroup::PostUpdateWork, m_dt);
    SOL_PROFILE_END("PostPhysics");

    if (m_script) m_script->update_timers(m_dt);

    // Update audio listener from active Camera3D
    if (m_audio && m_audio->is_initialized() && m_scene_manager) {
        auto* scene = m_scene_manager->current_scene();
        if (scene) {
            if (Camera3D* cam = scene->active_camera()) {
                m_audio->update_listener(cam->position, cam->forward(), cam->up());
            }
        }
    }

    if (on_update) on_update();  // late custom hook
    if (m_renderer) {
        m_renderer->begin_frame();
        if (on_render) on_render();
        m_renderer->end_frame();
    }
}


void Engine::shutdown() {
    if (m_audio)        m_audio->shutdown();
    if (m_imgui)        m_imgui->shutdown();
    if (m_scene_manager) m_scene_manager->unload(*this);
    if (m_script)   m_script->shutdown();
    if (m_physics)  m_physics->shutdown();
    if (m_renderer) m_renderer->shutdown();
    if (m_window)   m_window->shutdown();
    m_input.reset();
    m_scene_manager.reset();
    m_script.reset();
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
        m_elapsed += m_dt;
        last = now;

        m_input->begin_frame();   // must be before poll so scroll accumulator is fresh
        m_window->poll();
        m_input->update(m_window->handle());  // sample GLFW state after poll
        if (m_window->width() > 0 && m_window->height() > 0 &&
            (m_renderer->width() != m_window->width() || m_renderer->height() != m_window->height())) {
            m_renderer->resize(m_window->width(), m_window->height());
        }
        // Apply any deferred scene swap requested from on_update / on_ready.
        m_scene_manager->flush_pending(*this);

        Profiler::instance().reset();
        m_imgui->begin_frame();

        SOL_PROFILE_BEGIN("PrePhysics");
        m_scene_manager->update_pre_physics(*this, m_dt);
        m_ecs_world.tick(ETickGroup::PrePhysics, m_dt);
        m_ecs_world.tick_processors(ETickGroup::PrePhysics, m_dt);
        SOL_PROFILE_END("PrePhysics");

        SOL_PROFILE_BEGIN("Physics");
        m_physics->step(m_dt);
        SOL_PROFILE_END("Physics");

        SOL_PROFILE_BEGIN("PostPhysics");
        m_scene_manager->update_post_physics(*this, m_dt);
        m_ecs_world.tick(ETickGroup::PostPhysics, m_dt);
        m_ecs_world.tick_processors(ETickGroup::PostPhysics, m_dt);
        m_ecs_world.tick(ETickGroup::PostUpdateWork, m_dt);
        m_ecs_world.tick_processors(ETickGroup::PostUpdateWork, m_dt);
        SOL_PROFILE_END("PostPhysics");

        if (m_script) m_script->update_timers(m_dt);

        // Update audio listener from active Camera3D
        if (m_audio && m_audio->is_initialized()) {
            auto* scene = m_scene_manager->current_scene();
            if (scene) {
                if (Camera3D* cam = scene->active_camera()) {
                    m_audio->update_listener(cam->position, cam->forward(), cam->up());
                }
            }
        }

        if (game.on_update) game.on_update(this, m_dt);  // late hook
        m_renderer->begin_frame();
        if (game.on_render) game.on_render(this);
        m_imgui->end_frame();
        m_renderer->end_frame();

        // FPS cap: sleep to avoid burning GPU/CPU cycles we don't need.
        // Especially important when developing over Remote Desktop.
        const int cap = m_renderer->settings().fps_cap;
        if (cap > 0) {
            using namespace std::chrono;
            auto target  = duration<double>(1.0 / cap);
            auto elapsed = steady_clock::now() - last;
            auto remain  = duration_cast<microseconds>(target - elapsed);
            if (remain > microseconds(500))
                std::this_thread::sleep_for(remain);
        }
    }

    if (game.on_shutdown) game.on_shutdown(this);
    return 0;
}

// ---------------------------------------------------------------------------
//  Lightweight self-test helpers
// ---------------------------------------------------------------------------
namespace {

#define ST_CHECK(cond, msg)                                          \
    do {                                                             \
        if (!(cond)) { SOL_ERROR(std::string("  [FAIL] ") + (msg)); \
                       return false; }                               \
    } while (false)

#define ST_PASS(msg) SOL_INFO(std::string("  [ok]   ") + (msg))

static bool test_ecs(Engine& eng) {
    struct Pos { glm::vec3 v; };
    struct Vel { glm::vec3 v; };
    auto& reg = eng.registry();
    auto e = reg.create();
    reg.emplace<Pos>(e, glm::vec3(0));
    reg.emplace<Vel>(e, glm::vec3(1, 2, 3));
    int seen = 0;
    for (auto [_, p, v] : reg.view<Pos, Vel>().each()) { p.v += v.v; ++seen; }
    ST_CHECK(seen == 1, "ECS: view returned wrong count");
    ST_CHECK(reg.get<Pos>(e).v == glm::vec3(1,2,3), "ECS: component value wrong");
    reg.destroy(e);
    ST_PASS("EnTT ECS (create / view / destroy)");
    return true;
}

static bool test_math(Engine&) {
    glm::mat4 t = glm::translate(glm::mat4(1.0f), {1, 2, 3});
    ST_CHECK(t[3].x == 1.0f && t[3].y == 2.0f && t[3].z == 3.0f, "GLM translate");
    glm::mat4 inv = glm::inverse(t);
    glm::mat4 id  = t * inv;
    for (int i = 0; i < 4; ++i)
        ST_CHECK(std::abs(id[i][i] - 1.0f) < 1e-5f, "GLM inverse");
    ST_PASS("GLM matrix math");
    return true;
}

static bool test_renderer(Engine& eng) {
    ST_CHECK(dynamic_cast<VulkanRenderer*>(&eng.renderer()),
             "Expected VulkanRenderer");
    auto& s = eng.renderer().settings();
    s.exposure = 2.0f;
    ST_CHECK(eng.renderer().settings().exposure == 2.0f, "RenderSettings write");
    s.exposure = 1.0f;
    ST_PASS("Vulkan renderer + RenderSettings");
    return true;
}

static bool test_physics(Engine& eng) {
    using namespace JPH;
    BodyInterface& bi = eng.physics().system()->GetBodyInterface();

    BoxShapeSettings fs(Vec3(50, 1, 50)); fs.SetEmbedded();
    auto fsr = fs.Create();
    BodyCreationSettings floor_bcs(fsr.Get(), RVec3(0,-1,0),
        Quat::sIdentity(), EMotionType::Static, 0);
    Body* floor = bi.CreateBody(floor_bcs);
    bi.AddBody(floor->GetID(), EActivation::DontActivate);

    BodyCreationSettings sphere_bcs(new SphereShape(0.5f), RVec3(0,5,0),
        Quat::sIdentity(), EMotionType::Dynamic, 1);
    BodyID sphere = bi.CreateAndAddBody(sphere_bcs, EActivation::Activate);

    eng.physics().system()->OptimizeBroadPhase();
    for (int i = 0; i < 60; ++i) eng.physics().step(1.0f / 60.0f);

    float y = bi.GetCenterOfMassPosition(sphere).GetY();
    bi.RemoveBody(sphere);    bi.DestroyBody(sphere);
    bi.RemoveBody(floor->GetID()); bi.DestroyBody(floor->GetID());
    ST_CHECK(y < 4.9f, "Physics: sphere did not fall (y=" + std::to_string(y) + ")");
    ST_PASS("Jolt physics (sphere free-fall)");
    return true;
}

static bool test_imgui(Engine& eng) {
    eng.imgui().begin_frame();
    ImGui::Begin("selftest"); ImGui::Text("hello"); ImGui::End();
    eng.imgui().end_frame();
    ST_PASS("ImGui frame round-trip");
    return true;
}

static bool test_reflection(Engine&) {
    // Node3D must be registered
    auto* td = ComponentRegistry::instance().find("Node3D");
    ST_CHECK(td != nullptr, "Reflection: Node3D not found");
    bool found_pos = false;
    for (auto& f : td->fields)
        if (std::string(f.name) == "position") { found_pos = true; break; }
    ST_CHECK(found_pos, "Reflection: Node3D missing 'position' field");

    // WorldEnvironment must be registered with sky_mode
    auto* we = ComponentRegistry::instance().find("WorldEnvironment");
    ST_CHECK(we != nullptr, "Reflection: WorldEnvironment not registered");
    bool found_sky = false;
    for (auto& f : we->fields)
        if (std::string(f.name) == "sky_mode") { found_sky = true; break; }
    ST_CHECK(found_sky, "Reflection: WorldEnvironment missing 'sky_mode'");
    ST_PASS("Reflection registry (Node3D + WorldEnvironment)");
    return true;
}

static bool test_scene_serialise(Engine& eng) {
    // Build a small scene in memory
    auto scene = std::make_unique<Scene>();
    scene->name = "selftest_scene";

    auto root = std::make_unique<Node3D>();
    root->name = "Root";
    root->position = {1.0f, 2.0f, 3.0f};

    auto we = std::make_unique<WorldEnvironment>();
    we->name = "Env";
    we->tonemap_mode  = 3;   // AgX
    we->ssao_enabled  = true;
    we->bloom_enabled = false;
    we->sky_mode      = 0;
    root->add_child(std::move(we));

    auto dl = std::make_unique<DirectionalLight>();
    dl->name         = "Sun";
    dl->color        = {1.0f, 0.9f, 0.8f};
    dl->cast_shadow  = true;
    dl->csm_far      = 120.0f;
    root->add_child(std::move(dl));

    scene->set_root(std::move(root));

    // Save to a temp file
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "sol_selftest.solscene";
    ST_CHECK(scene->save(tmp.string()), "Scene::save failed");

    // Reload
    auto loaded = Scene::load(tmp.string(), eng);
    ST_CHECK(loaded != nullptr, "Scene::load returned null");
    ST_CHECK(loaded->name == "selftest_scene", "Scene name mismatch");

    Node* r = loaded->root();
    ST_CHECK(r != nullptr, "Loaded scene has no root");
    ST_CHECK(std::string(r->type_name()) == "Node3D", "Root type wrong");

    auto* root3d = dynamic_cast<Node3D*>(r);
    ST_CHECK(root3d && std::abs(root3d->position.x - 1.0f) < 1e-5f,
             "Root position not preserved");

    auto* env = r->find_first<WorldEnvironment>();
    ST_CHECK(env != nullptr, "WorldEnvironment child not found after load");
    ST_CHECK(env->tonemap_mode == 3,    "WE tonemap_mode not preserved");
    ST_CHECK(env->ssao_enabled == true, "WE ssao_enabled not preserved");
    ST_CHECK(env->bloom_enabled == false,"WE bloom_enabled not preserved");

    auto* sun = r->find_first<DirectionalLight>();
    ST_CHECK(sun != nullptr, "DirectionalLight child not found after load");
    ST_CHECK(std::abs(sun->color.r - 1.0f) < 1e-5f, "DirLight color not preserved");
    ST_CHECK(std::abs(sun->csm_far - 120.0f) < 1e-3f, "DirLight csm_far not preserved");

    fs::remove(tmp);
    ST_PASS("Scene save + load round-trip (Node3D / WorldEnvironment / DirectionalLight)");
    return true;
}

static bool test_asset_loader(Engine& eng) {
    // Try loading a non-existent path — must return null gracefully, not crash.
    auto bad = eng.assets().load("__no_such_file__.glb");
    ST_CHECK(bad == nullptr, "AssetLoader should return null for missing file");
    ST_PASS("Asset loader (graceful missing-file handling)");
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------

bool Engine::self_test() {
    SOL_INFO("=== Sol Engine self-test ===");
    int pass = 0, fail = 0;

    auto run = [&](const char* label, auto fn) {
        if (fn(*this)) { ++pass; }
        else           { ++fail; SOL_ERROR(std::string("FAILED: ") + label); }
    };

    run("ECS",                test_ecs);
    run("Math",               test_math);
    run("Renderer",           test_renderer);
    run("Physics",            test_physics);
    run("ImGui",              test_imgui);
    run("Reflection",         test_reflection);
    run("Scene serialisation",test_scene_serialise);
    run("Asset loader",       test_asset_loader);

    if (fail == 0) {
        SOL_INFO("=== self-test PASSED  (" + std::to_string(pass) + "/" +
                 std::to_string(pass) + ") ===");
        return true;
    } else {
        SOL_ERROR("=== self-test FAILED  (" + std::to_string(fail) + " failure(s), " +
                  std::to_string(pass) + " passed) ===");
        return false;
    }
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
