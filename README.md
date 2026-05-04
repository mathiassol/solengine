# Sol Engine

A minimal C++20 game engine scaffold:

| Concern   | Library |
|-----------|---------|
| Rendering | [bgfx](https://github.com/bkaradzic/bgfx) (via `bgfx.cmake`) |
| ECS       | [EnTT](https://github.com/skypjack/entt) |
| Math      | [GLM](https://github.com/g-truc/glm) |
| Physics   | [Jolt](https://github.com/jrouwe/JoltPhysics) |
| UI        | [Dear ImGui](https://github.com/ocornut/imgui) |
| Assets    | [cgltf](https://github.com/jkuhlmann/cgltf) (GLB / GLTF) |
| Window    | [GLFW](https://github.com/glfw/glfw) |

## Architecture

* `engine_core` (shared lib, `sol_engine.dll`) — all subsystems.
* `engine` (executable, `sol.exe`) — runtime/launcher; loads game DLLs.
* `voxel_demo.dll`, `space_demo.dll` — game modules.

The engine is built **first**, then re-used to run any number of games.
Each game DLL exports a single C entry point:

```cpp
SOL_EXPORT const SolGameApi* sol_get_game_api();
```

The returned `SolGameApi` provides `on_init / on_update / on_render / on_shutdown`
callbacks the engine drives in its main loop.

## Build (Windows / MinGW UCRT64)

```powershell
cmake -S . -B build -G Ninja
cmake --build build
```

First configure clones bgfx, Jolt, EnTT, GLM, GLFW, ImGui and cgltf via
`FetchContent` — expect a few hundred MB and a slow first build.

## Run

```powershell
# Self-test: opens a window, inits every subsystem, runs a Jolt sim,
# pumps an ImGui frame, then exits with code 0.
.\build\out\sol.exe --selftest

# Launch a demo game module:
.\build\out\sol.exe .\build\out\voxel_demo.dll
.\build\out\sol.exe .\build\out\space_demo.dll
```

## Layout

```
engine/
  include/sol/        public engine + game-ABI headers
  src/                window, render, physics, ecs glue, asset, ui, log
demos/
  voxel/              MC-style voxel chunk demo
  space/              Newtonian space sim demo
cmake/
  Dependencies.cmake  FetchContent for all third-party libs
```

## TODO (left as deliberate stubs)

* bgfx render backend for ImGui draw data.
* Voxel greedy mesher + bgfx vertex/index buffer upload.
* Hot-reload of game DLLs.
* GLTF material/texture upload to bgfx.
