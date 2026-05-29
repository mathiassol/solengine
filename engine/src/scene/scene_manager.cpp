// scene_manager.cpp

#include "sol/scene/scene_manager.h"
#include "sol/engine.h"
#include "sol/log.h"

namespace sol {

bool SceneManager::load_scene(const std::string& path, Engine& engine) {
    auto scene = Scene::load(path, engine);
    if (!scene) {
        SOL_ERROR("SceneManager: failed to load scene '" + path + "'");
        return false;
    }
    set_scene(std::move(scene), engine);
    return true;
}

void SceneManager::set_scene(std::unique_ptr<Scene> scene, Engine& engine) {
    if (m_scene) m_scene->on_destroy(engine);
    m_scene = std::move(scene);
    if (m_scene) m_scene->on_ready(engine);
}

void SceneManager::request_scene(std::unique_ptr<Scene> scene) {
    // Deferred swap — applied at the top of the next frame via flush_pending().
    // Safe to call from within on_update / on_ready without use-after-free.
    m_pending_scene = std::move(scene);
}

void SceneManager::flush_pending(Engine& engine) {
    if (m_pending_scene) {
        // Wait for all in-flight GPU work to finish before destroying old scene resources.
        // This prevents use-after-free of Vulkan buffers/images still referenced by
        // in-flight command buffers from previous frames.
        engine.renderer().wait_idle();
        SOL_INFO("SceneManager: swapping to scene '" + m_pending_scene->name + "'");
        set_scene(std::move(m_pending_scene), engine);
    }
}

void SceneManager::update(Engine& engine, float dt) {
    if (m_scene) m_scene->update(engine, dt);
}

void SceneManager::update_pre_physics(Engine& engine, float dt) {
    if (m_scene) m_scene->update_pre_physics(engine, dt);
}

void SceneManager::update_post_physics(Engine& engine, float dt) {
    if (m_scene) m_scene->update_post_physics(engine, dt);
}

void SceneManager::render(Engine& engine) {
    if (m_scene) m_scene->render(engine);
}

void SceneManager::unload(Engine& engine) {
    if (m_scene) { m_scene->on_destroy(engine); m_scene.reset(); }
}

std::unique_ptr<Scene> SceneManager::detach_scene_raw() {
    return std::move(m_scene);
}

void SceneManager::attach_scene_raw(std::unique_ptr<Scene> scene) {
    m_scene = std::move(scene);
}

void SceneManager::cancel_pending() {
    m_pending_scene.reset();
}

} // namespace sol
