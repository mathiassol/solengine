# SolEngine

A C++20 game engine built on proven open-source libraries.

| Concern        | Library |
|----------------|---------|
| Rendering      | [bgfx](https://github.com/bkaradzic/bgfx) (D3D11 / GL / Vulkan / Metal) |
| ECS            | [EnTT](https://github.com/skypjack/entt) |
| Math           | [GLM](https://github.com/g-truc/glm) |
| Physics        | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) |
| UI             | [Dear ImGui](https://github.com/ocornut/imgui) |
| Assets         | [cgltf](https://github.com/jkuhlmann/cgltf) (GLB / GLTF) |
| Window         | [GLFW](https://github.com/glfw/glfw) |
| Scenes / JSON  | [nlohmann/json](https://github.com/nlohmann/json) |

## Features

- **Forward PBR renderer** — Cook-Torrance BRDF, metallic/roughness workflow
- **Lighting** — directional + point + spot lights (up to 8), PCF shadow maps
- **HDR pipeline** — RGBA16F framebuffer, ACES tonemap, Kawase bloom
- **Skybox** — equirectangular HDR → cube map
- **Scene system** — Godot-style node tree (`.solscene` JSON), runtime loading
- **Engine/Game split** — engine compiles to `sol.exe` + `sol_engine.dll`; games are separate DLLs loaded at runtime
- **CLI** — all tools via `sol.exe` sub-commands (`run`, `build`, `new`)

## Quick Start (Windows)

**First build** — configure + compile everything from scratch:
```bat
build.bat
```

**Run the model viewer demo:**
```bat
runDemo.bat
```

**Wipe build outputs:**
```bat
wipe.bat
```

First configure clones bgfx, Jolt, EnTT, GLM, GLFW, ImGui, cgltf and nlohmann/json
via FetchContent — expect several hundred MB and a slow first build (~10 min).

## Bat Files

| File | What it does |
|------|--------------|
| `build.bat` | CMake configure + full build (engine + demo) |
| `runDemo.bat` | Incremental build then launch the model viewer |
| `wipe.bat` | Delete `build/` so next build starts clean |

## Demo Controls

| Key / Input | Action |
|-------------|--------|
| **WASD** | Fly forward / left / back / right |
| **Q / E** | Fly down / up |
| **Left-click** | Capture mouse for look |
| **Mouse move** | Look around (while captured) |
| **Arrow keys** | Look around (keyboard) |
| **Escape** | Release mouse |
| **N / P** | Next / previous model in `demo/models/` |
| **Tab** | Toggle ImGui HUD |

Drop any `.glb` / `.gltf` file into `demo/models/` and cycle through them with N/P.

## Architecture

```
sol.exe          — engine runtime / CLI launcher
sol_engine.dll   — all engine subsystems (renderer, physics, ECS, scene)
demo.dll         — demo game module (loaded by sol.exe at runtime)
```

Each game DLL exports one C entry point:

```cpp
SOL_EXPORT const SolGameApi* sol_get_game_api();
```

The returned `SolGameApi` provides `on_init / on_update / on_render / on_shutdown`
callbacks driven by the engine main loop.

## Project Layout

```
engine/
  include/sol/          public engine + game-ABI headers
  src/
    render/             renderer, shaders, HDR/bloom/shadow passes
    scene/              scene manager, node hierarchy, GLTF loader
    physics/            Jolt wrapper
    ecs/                EnTT helpers
    asset/              texture + mesh upload
    ui/                 ImGui integration
    window/             GLFW window
    log/                logging macros
demo/
  src/demo.cpp          model viewer (fly cam, model cycling)
  models/               drop GLBs here — N/P cycles through them
  scenes/               .solscene files
cmake/
  Dependencies.cmake    FetchContent for all third-party libs
  SolGame.cmake         helper for game DLL projects
```

## Roadmap

See [review.md](review.md) for the full roadmap from current state to a production
engine with deferred rendering, SSAO, SSR, GI, in-engine editor, Lua scripting,
audio, and proper tooling.
