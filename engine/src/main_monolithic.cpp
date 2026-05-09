// main_monolithic.cpp
// Entry point for monolithic (single-exe) builds.
// The game's sol_get_game_api() is linked directly — no DLL loading.
#include "sol/engine.h"
#include "sol/api.h"
#include "sol/log.h"

// Defined in the game's source (same binary)
extern "C" const SolGameApi* sol_get_game_api();

int main(int argc, char** argv) {
    const SolGameApi* api = sol_get_game_api();
    if (!api) {
        SOL_ERROR("sol_get_game_api() returned null");
        return 1;
    }

    sol::EngineConfig cfg;
    cfg.title = (api->name && api->name[0]) ? api->name : "Sol Game";

    sol::Engine engine;
    if (!engine.init(cfg)) return 2;
    int rc = engine.run(*api);
    engine.shutdown();
    return rc;
}
