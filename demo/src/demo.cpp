// demo.cpp — minimal project loader
// All game logic lives in scene nodes (FlyCamController, ModelNode, etc.)
// This file just wires up the engine lifecycle to load project.sol.

#include "sol/api.h"
#include "sol/engine.h"
#include "sol/log.h"
#include "sol/project/project.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/scene.h"

#include <imgui.h>
#include <filesystem>

namespace {

void on_init(sol::Engine* engine) {
    ImGui::SetCurrentContext(engine->imgui_context());

    auto cfg = sol::ProjectConfig::load("project.sol");
    if (!cfg.main_scene.empty() && std::filesystem::exists(cfg.main_scene)) {
        auto scene = sol::Scene::load(cfg.main_scene, *engine);
        if (scene)
            engine->scene_manager().set_scene(std::move(scene), *engine);
    } else {
        SOL_WARN("Demo: no main_scene found — check project.sol");
    }
}

void on_update(sol::Engine* engine, float dt) {
    // Scene update is now driven by Engine::run() via phased tick.
    // on_update is a late-frame hook for custom game logic.
    (void)engine; (void)dt;
}

void on_render(sol::Engine* engine) {
    engine->scene_manager().render(*engine);
}

void on_shutdown(sol::Engine* engine) {
    engine->scene_manager().unload(*engine);
    engine->set_cursor_captured(false);
}

} // anonymous namespace

SOL_EXPORT const SolGameApi* sol_get_game_api() {
    static const SolGameApi api = {
        SOL_ABI_VERSION,
        "SolEngine Demo",
        on_init, on_update, on_render, on_shutdown
    };
    return &api;
}
