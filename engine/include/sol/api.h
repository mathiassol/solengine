// Sol Engine — public game ABI.
// A game DLL exports `sol_get_game_api` returning a SolGameApi with
// callbacks the engine.exe runtime invokes each frame.
#pragma once

#include <cstdint>

#if defined(_WIN32)
    #define SOL_EXPORT extern "C" __declspec(dllexport)
    #define SOL_IMPORT extern "C" __declspec(dllimport)
#else
    #define SOL_EXPORT extern "C" __attribute__((visibility("default")))
    #define SOL_IMPORT extern "C"
#endif

namespace sol { class Engine; }

struct SolGameApi {
    uint32_t    abi_version;     // must equal SOL_ABI_VERSION
    const char* name;            // game display name

    void (*on_init)    (sol::Engine* engine);
    void (*on_update)  (sol::Engine* engine, float dt);
    void (*on_render)  (sol::Engine* engine);
    void (*on_shutdown)(sol::Engine* engine);
};

constexpr uint32_t SOL_ABI_VERSION = 1;

using SolGetGameApiFn = const SolGameApi* (*)();
