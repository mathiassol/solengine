// engine.exe — runtime/launcher.
//
// Usage:
//   sol --selftest           run init self-test, no game
//   sol path/to/game.dll     load a game DLL and run it
//
#include "sol/engine.h"
#include "sol/api.h"
#include "sol/log.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
    #include <windows.h>
    using LibHandle = HMODULE;
    static LibHandle lib_open(const char* p)        { return LoadLibraryA(p); }
    static void*     lib_sym (LibHandle h, const char* n) { return (void*)GetProcAddress(h, n); }
    static void      lib_close(LibHandle h)         { FreeLibrary(h); }
#else
    #include <dlfcn.h>
    using LibHandle = void*;
    static LibHandle lib_open(const char* p)        { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
    static void*     lib_sym (LibHandle h, const char* n) { return dlsym(h, n); }
    static void      lib_close(LibHandle h)         { dlclose(h); }
#endif

static void print_usage() {
    std::puts("Sol Engine\n"
              "  sol --selftest          run init self-test, no game\n"
              "  sol <path/to/game.dll>  load and run a game module");
}

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 1; }

    sol::EngineConfig cfg;
    cfg.title = "Sol Engine";

    if (std::strcmp(argv[1], "--selftest") == 0) {
        sol::Engine engine;
        if (!engine.init(cfg)) return 2;
        bool ok = engine.self_test();
        engine.shutdown();
        return ok ? 0 : 3;
    }

    // Load the game DLL before creating the engine so we can validate it early.
    LibHandle lib = lib_open(argv[1]);
    if (!lib) {
        SOL_ERROR(std::string("failed to load game module: ") + argv[1]);
        return 4;
    }
    auto get_api = (SolGetGameApiFn)lib_sym(lib, "sol_get_game_api");
    if (!get_api) {
        SOL_ERROR("game module is missing sol_get_game_api()");
        lib_close(lib);
        return 5;
    }
    const SolGameApi* api = get_api();
    if (!api) { SOL_ERROR("sol_get_game_api returned null"); lib_close(lib); return 6; }

    // Engine lives in a nested scope so its destructor (which cleans up the EnTT
    // registry, including component storage pools that have vtables instantiated
    // inside the game DLL) runs BEFORE lib_close() unmaps the DLL.
    int rc;
    {
        sol::Engine engine;
        if (!engine.init(cfg)) { lib_close(lib); return 2; }
        rc = engine.run(*api);
        engine.shutdown();
    } // engine (and EnTT registry) fully destroyed here

    lib_close(lib);
    return rc;
}
