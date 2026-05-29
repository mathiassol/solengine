#pragma once
#include "sol/export.h"
#include "sol/scene/scene.h"
#include <memory>
#include <string>

namespace sol {

class Engine;

// Manages the currently active scene.
// Engine::scene_manager() returns this.
class SOL_API SceneManager {
public:
    // Load a scene from disk and make it current (calls on_ready).
    bool load_scene(const std::string& path, Engine& engine);

    // Replace current scene with an already-constructed scene (immediate).
    void set_scene(std::unique_ptr<Scene> scene, Engine& engine);

    // Queue a scene swap to happen at the start of the next frame.
    // Safe to call from inside on_update / on_ready — avoids use-after-free.
    void request_scene(std::unique_ptr<Scene> scene);

    // Apply any pending scene swap. Called by the engine at the top of each frame.
    void flush_pending(Engine& engine);

    // Returns the active scene (may be null before first load).
    Scene* current() const { return m_scene.get(); }
    Scene* current_scene() const { return m_scene.get(); }

    // Call from the game's on_update.
    void update(Engine& engine, float dt);

    // Phased update — mirrors Scene::update_pre/post_physics.
    void update_pre_physics (Engine& engine, float dt);
    void update_post_physics(Engine& engine, float dt);

    // Call from the game's on_render.
    void render(Engine& engine);

    // Graceful unload (calls on_destroy on all nodes).
    void unload(Engine& engine);

    // Raw attach/detach — no on_ready / on_destroy called.
    // Used by the hot scene slot system to swap scenes without re-initialising.
    std::unique_ptr<Scene> detach_scene_raw();
    void attach_scene_raw(std::unique_ptr<Scene> scene);

    // Cancel a pending (deferred) scene swap before it is applied.
    void cancel_pending();

private:
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Scene> m_pending_scene;
};

} // namespace sol
