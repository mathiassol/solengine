// sol.exe — SolEngine runtime and CLI tool.
//
// Usage:
//   sol help                        show this help
//   sol version                     show engine version
//   sol selftest                    run engine self-test
//   sol run <path/to/game.dll>      load and run a game module
//   sol new  <name>                 scaffold a new game project
//   sol build [project_dir]         cmake --build the project
//   sol pack  [project_dir]         cmake monolithic build (no runtime DLLs)
//
// Shorthand:
//   sol <path/to/game.dll>          same as sol run <path>

#include "sol/engine.h"
#include "sol/api.h"
#include "sol/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

#if defined(_WIN32)
    #include <windows.h>
    using LibHandle = HMODULE;
    static LibHandle lib_open (const char* p)              { return LoadLibraryA(p); }
    static void*     lib_sym  (LibHandle h, const char* n) { return (void*)GetProcAddress(h, n); }
    static void      lib_close(LibHandle h)                { FreeLibrary(h); }
#else
    #include <dlfcn.h>
    using LibHandle = void*;
    static LibHandle lib_open (const char* p)              { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
    static void*     lib_sym  (LibHandle h, const char* n) { return dlsym(h, n); }
    static void      lib_close(LibHandle h)                { dlclose(h); }
#endif

// ── helpers ──────────────────────────────────────────────────────────────────

static void print_help() {
    std::puts(
        "SolEngine v0.1.0\n"
        "\n"
        "USAGE\n"
        "  sol <command> [args]\n"
        "\n"
        "COMMANDS\n"
        "  run  <game.dll>       Load and run a game module\n"
        "  new  <name>           Scaffold a new game project\n"
        "  build [dir]           Build project in [dir] (default: current dir)\n"
        "  pack  [dir]           Build standalone exe with engine baked in\n"
        "  selftest              Run engine subsystem self-test\n"
        "  version               Print version\n"
        "  help                  Show this help\n"
        "\n"
        "EXAMPLES\n"
        "  sol run build/out/mygame.dll\n"
        "  sol new mygame\n"
        "  sol build mygame/\n"
        "  sol pack  mygame/\n"
    );
}

static int run_cmake(const std::string& cmd) {
    int rc = std::system(cmd.c_str());
#if defined(_WIN32)
    return rc;
#else
    return WEXITSTATUS(rc);
#endif
}

// Write a file; creates parent directories if needed.
static bool write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    if (!f) { std::fprintf(stderr, "sol: cannot write %s\n", p.string().c_str()); return false; }
    f << content;
    return true;
}

// ── command: run ─────────────────────────────────────────────────────────────

static int cmd_run(const char* dll_path) {
    LibHandle lib = lib_open(dll_path);
    if (!lib) {
        std::fprintf(stderr, "sol: failed to load '%s'\n", dll_path);
        return 4;
    }
    auto get_api = (SolGetGameApiFn)lib_sym(lib, "sol_get_game_api");
    if (!get_api) {
        std::fprintf(stderr, "sol: '%s' is missing sol_get_game_api()\n", dll_path);
        lib_close(lib);
        return 5;
    }
    const SolGameApi* api = get_api();
    if (!api) {
        std::fprintf(stderr, "sol: sol_get_game_api() returned null\n");
        lib_close(lib);
        return 6;
    }

    sol::EngineConfig cfg;
    cfg.title = (api->name && api->name[0]) ? api->name : "Sol Game";

    int rc;
    {
        sol::Engine engine;
        if (!engine.init(cfg)) { lib_close(lib); return 2; }
        rc = engine.run(*api);
        engine.shutdown();
    }
    lib_close(lib);
    return rc;
}

// ── command: selftest ─────────────────────────────────────────────────────────

static int cmd_selftest() {
    sol::EngineConfig cfg;
    cfg.title = "Sol Self-Test";
    sol::Engine engine;
    if (!engine.init(cfg)) return 2;
    bool ok = engine.self_test();
    engine.shutdown();
    return ok ? 0 : 3;
}

// ── command: new ─────────────────────────────────────────────────────────────

static int cmd_new(const char* name) {
    fs::path dir = name;
    if (fs::exists(dir)) {
        std::fprintf(stderr, "sol: directory '%s' already exists\n", name);
        return 1;
    }

    if (!write_file(dir / "CMakeLists.txt",
        std::string("cmake_minimum_required(VERSION 3.20)\n"
        "project(") + name + ")\n"
        "\n"
        "# ─── Path to SolEngine root ─────────────────────────────────────\n"
        "# Adjust SOL_ENGINE_DIR if SolEngine lives elsewhere.\n"
        "set(SOL_ENGINE_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/..\"\n"
        "    CACHE PATH \"SolEngine root directory\")\n"
        "\n"
        "set(SOL_OUT ${CMAKE_BINARY_DIR}/out)\n"
        "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${SOL_OUT})\n"
        "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${SOL_OUT})\n"
        "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${SOL_OUT}/lib)\n"
        "\n"
        "option(SOL_MONOLITHIC \"Build standalone exe (no runtime DLLs)\" OFF)\n"
        "\n"
        "add_subdirectory(${SOL_ENGINE_DIR} solengine)\n"
        "\n"
        "sol_add_game(\n"
        "    TARGET  " + std::string(name) + "\n"
        "    SOURCES src/game.cpp)\n")) return 1;

    if (!write_file(dir / "project.sol",
        std::string("{\n"
        "  \"sol_project\": 1,\n"
        "  \"name\": \"") + name + "\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"main_scene\": \"scenes/main.solscene\",\n"
        "  \"window\": {\n"
        "    \"title\": \"" + name + "\",\n"
        "    \"width\": 1280,\n"
        "    \"height\": 720,\n"
        "    \"vsync\": true\n"
        "  }\n"
        "}\n")) return 1;

    if (!write_file(dir / "scenes" / "main.solscene",
        "{\n"
        "  \"name\": \"Main\",\n"
        "  \"root\": {\n"
        "    \"type\": \"Node3D\",\n"
        "    \"name\": \"Root\",\n"
        "    \"children\": [\n"
        "      {\n"
        "        \"type\": \"DirectionalLight\",\n"
        "        \"name\": \"Sun\",\n"
        "        \"rotation\": [-45.0, 30.0, 0.0],\n"
        "        \"intensity\": 3.0,\n"
        "        \"cast_shadow\": true\n"
        "      }\n"
        "    ]\n"
        "  }\n"
        "}\n")) return 1;

    if (!write_file(dir / "src" / "game.cpp",
        std::string("// ") + name + " — SolEngine game\n"
        "#include \"sol/api.h\"\n"
        "#include \"sol/engine.h\"\n"
        "#include \"sol/log.h\"\n"
        "#include \"sol/project/project.h\"\n"
        "#include \"sol/scene/scene_manager.h\"\n"
        "\n"
        "static void on_init(sol::Engine* e) {\n"
        "    ImGui::SetCurrentContext(e->imgui_context());\n"
        "    sol::ProjectConfig cfg = sol::ProjectConfig::load(\"project.sol\");\n"
        "    SOL_INFO(std::string(\"Game init: \") + cfg.name);\n"
        "    if (!e->scene_manager().load_scene(cfg.main_scene, *e)) {\n"
        "        SOL_ERROR(\"Failed to load main scene\");\n"
        "        e->quit();\n"
        "    }\n"
        "}\n"
        "\n"
        "static void on_update(sol::Engine* e, float dt) {\n"
        "    e->scene_manager().update(*e, dt);\n"
        "}\n"
        "\n"
        "static void on_render(sol::Engine* e) {\n"
        "    e->scene_manager().render(*e);\n"
        "}\n"
        "\n"
        "static void on_shutdown(sol::Engine* e) {\n"
        "    e->scene_manager().unload(*e);\n"
        "}\n"
        "\n"
        "static const SolGameApi k_api = {\n"
        "    SOL_ABI_VERSION, \"" + std::string(name) + "\",\n"
        "    on_init, on_update, on_render, on_shutdown\n"
        "};\n"
        "\n"
        "SOL_EXPORT const SolGameApi* sol_get_game_api() { return &k_api; }\n")) return 1;

    std::printf("Created project '%s'.\n\n"
                "  Build (DLL mode):\n"
                "    cd %s\n"
                "    cmake -S . -B build -G Ninja\n"
                "    sol build %s\n"
                "    sol run build/out/%s.dll\n\n"
                "  Build (standalone exe):\n"
                "    sol pack %s\n\n",
                name, name, name, name, name);
    return 0;
}

// ── command: build ────────────────────────────────────────────────────────────

static int cmd_build(const char* dir) {
    fs::path project_dir = dir ? fs::path(dir) : fs::current_path();
    fs::path build_dir   = project_dir / "build";

    std::string cfg_cmd = "cmake -S \"" + project_dir.string() +
                          "\" -B \"" + build_dir.string() +
                          "\" -G Ninja -DCMAKE_BUILD_TYPE=Release";
    std::string bld_cmd = "cmake --build \"" + build_dir.string() +
                          "\" --config Release";

    if (!fs::exists(build_dir / "CMakeCache.txt")) {
        std::printf("[sol] Configuring...\n");
        if (run_cmake(cfg_cmd) != 0) {
            std::fprintf(stderr, "sol: CMake configure failed\n"); return 1;
        }
    }
    std::printf("[sol] Building...\n");
    if (run_cmake(bld_cmd) != 0) {
        std::fprintf(stderr, "sol: Build failed\n"); return 1;
    }
    std::printf("[sol] Build complete.\n");
    return 0;
}

// ── command: pack ─────────────────────────────────────────────────────────────

static int cmd_pack(const char* dir) {
    fs::path project_dir = dir ? fs::path(dir) : fs::current_path();
    fs::path build_dir   = project_dir / "build_pack";

    std::string cfg_cmd = "cmake -S \"" + project_dir.string() +
                          "\" -B \"" + build_dir.string() +
                          "\" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSOL_MONOLITHIC=ON";
    std::string bld_cmd = "cmake --build \"" + build_dir.string() +
                          "\" --config Release";

    std::printf("[sol] Configuring for monolithic build...\n");
    if (run_cmake(cfg_cmd) != 0) {
        std::fprintf(stderr, "sol: CMake configure failed\n"); return 1;
    }
    std::printf("[sol] Building standalone exe...\n");
    if (run_cmake(bld_cmd) != 0) {
        std::fprintf(stderr, "sol: Build failed\n"); return 1;
    }

    std::printf("[sol] Packed! Output: %s\n",
        (build_dir / "out").string().c_str());
    return 0;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 0; }

    const char* cmd = argv[1];

    // help / version
    if (!std::strcmp(cmd, "help") || !std::strcmp(cmd, "--help") || !std::strcmp(cmd, "-h")) {
        print_help(); return 0;
    }
    if (!std::strcmp(cmd, "version") || !std::strcmp(cmd, "--version")) {
        std::puts("SolEngine 0.1.0"); return 0;
    }

    // selftest
    if (!std::strcmp(cmd, "selftest") || !std::strcmp(cmd, "--selftest")) {
        return cmd_selftest();
    }

    // new
    if (!std::strcmp(cmd, "new")) {
        if (argc < 3) { std::fprintf(stderr, "Usage: sol new <name>\n"); return 1; }
        return cmd_new(argv[2]);
    }

    // build
    if (!std::strcmp(cmd, "build")) {
        return cmd_build(argc >= 3 ? argv[2] : nullptr);
    }

    // pack
    if (!std::strcmp(cmd, "pack")) {
        return cmd_pack(argc >= 3 ? argv[2] : nullptr);
    }

    // run (explicit)
    if (!std::strcmp(cmd, "run")) {
        if (argc < 3) { std::fprintf(stderr, "Usage: sol run <game.dll>\n"); return 1; }
        return cmd_run(argv[2]);
    }

    // Backward compat: sol <game.dll>
    // If argv[1] looks like a file path (contains . or / or \), treat as game DLL
    const char* s = cmd;
    bool looks_like_path = false;
    for (; *s; ++s) {
        if (*s == '.' || *s == '/' || *s == '\\') { looks_like_path = true; break; }
    }
    if (looks_like_path) {
        return cmd_run(cmd);
    }

    std::fprintf(stderr, "sol: unknown command '%s'\n", cmd);
    print_help();
    return 1;
}
