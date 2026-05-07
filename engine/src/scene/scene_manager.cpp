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

void SceneManager::update(Engine& engine, float dt) {
    if (m_scene) m_scene->update(engine, dt);
}

void SceneManager::render(Engine& engine) {
    if (m_scene) m_scene->render(engine);
}

void SceneManager::unload(Engine& engine) {
    if (m_scene) { m_scene->on_destroy(engine); m_scene.reset(); }
}

} // namespace sol
