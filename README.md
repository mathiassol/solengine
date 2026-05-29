# SolEngine

A C++20 / Vulkan game engine with a Qt6 + ImGui editor and Lua scripting.

| Concern       | Library / Technology |
|---------------|----------------------|
| Rendering     | Vulkan (custom PBR renderer) |
| Editor        | Qt6 + Dear ImGui |
| Scripting     | Lua 5.4 (LuaBridge) |
| Physics       | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) |
| Audio         | miniaudio |
| Math          | [GLM](https://github.com/g-truc/glm) |
| Assets        | [cgltf](https://github.com/jkuhlmann/cgltf) (GLB / GLTF) |
| Scenes / JSON | [nlohmann/json](https://github.com/nlohmann/json) |

## Features

### Rendering
- **Vulkan PBR renderer** — Cook-Torrance BRDF, metallic/roughness workflow
- **IBL** — Image-Based Lighting from procedural sky or HDR panorama, always on
- **Cascaded Shadow Maps (CSM)** — PCF / PCSS soft shadows, temporal stabilisation
- **SSAO** — Screen-Space Ambient Occlusion
- **SSR** — Screen-Space Reflections
- **TAA** — Temporal Anti-Aliasing (YCoCg AABB, Catmull-Rom history)
- **Bloom** — Kawase multi-pass bloom
- **Volumetrics** — Fog / light scattering pass
- **HDR pipeline** — RGBA16F, ACES / AgX / Reinhard tonemapping, configurable exposure
- **Procedural sky** — gradient + sun disk, or equirectangular HDR panorama

### Editor (`sol_editor.exe`)
- **Scene Hierarchy** — tree view, drag/drop, duplicate, rename, multi-select
- **Inspector** — per-node field editor with undo/redo and gizmo transform
- **Material Editor** — PBR property editor + live preview sphere (GGX/Lambert)
- **Asset Browser** — project file tree with asset preview
- **Script Editor** — embedded Lua editor with syntax highlighting
- **World Settings** — sky, IBL, SSAO, SSR, TAA, bloom, volumetrics all in one panel
- **Viewport toolbar** — Lit / Unlit / debug views, gizmo mode switcher
- **Console** — log viewer with level filters
- **Multi-tab scene editing** — open multiple scenes simultaneously

### Scene System
- **17 node types** — organised in categories with Add Node menu + search
  - `Node3D`, `MeshNode`, `ModelNode`, `Camera3D`
  - `DirectionalLight`, `PointLight`
  - `RigidBody3D`, `StaticBody3D`, `CharacterBody3D`, `CollisionShape3D`, `Area3D`
  - `ScriptNode`, `LuaComponent`
  - `AudioStreamPlayer`, `AudioStreamPlayer3D`
  - `WorldEnvironment`, `SceneInstance`
- **`.solscene` JSON format** — deterministic, human-readable
- **ResourceCache** — engine-wide asset deduplication (meshes + textures)

### Lua Scripting
- `on_ready`, `on_update`, `on_destroy` lifecycle hooks
- **Raycast API** — `Physics.raycast(origin, dir, max_dist)`
- **Collision callbacks** — `on_collision_enter/exit` on rigid bodies
- **Area3D callbacks** — `on_body_entered/exited`, `get_overlapping_bodies()`
- **Node instantiation** — `Scene.create_node(type)`, `Scene.instantiate_model(path)`
- Full node property access — transform, material, light, audio, etc.
- Input via `Input.is_action_pressed / just_pressed / just_released`

### Physics & Audio
- Jolt Physics — rigid bodies, character controllers, raycasts, collision shapes
- miniaudio — 3D positional audio, OGG/WAV/MP3 streaming

## Quick Start (Windows)

**Build from source:**
```bat
build.bat
```

**Launch the editor:**
```bat
editor.bat
```

**Run the FPS demo directly:**
```bat
runDemo.bat
```

**Wipe build outputs:**
```bat
wipe.bat
```

> First build clones all dependencies via CMake FetchContent — expect ~300 MB and 10–15 min.

## Bat Files

| File | What it does |
|------|--------------|
| `build.bat` | CMake configure + full build (engine + editor + demo) |
| `editor.bat` | Incremental build then launch `sol_editor.exe` |
| `runDemo.bat` | Incremental build then launch the FPS demo |
| `install.bat` | Install built binaries to `%LOCALAPPDATA%\SolEngine\bin\` |
| `wipe.bat` | Delete `build/` for a clean rebuild |

## Architecture

```
sol_editor.exe   — Qt6 + ImGui editor (loads sol_engine.dll)
sol_engine.dll   — all engine subsystems: Vulkan renderer, physics, audio, Lua, scene
runtime/         — standalone runtime for shipping games
demo/            — FPS demo project (scenes, scripts, assets)
```

### Engine/Editor split
The engine is a DLL (`sol_engine.dll`). Both the editor and the runtime load it at
startup. Game logic lives in Lua scripts — no recompilation needed.

### Project Layout

```
engine/
  include/sol/        public API headers
  src/
    render/           Vulkan renderer, shaders, PBR, shadow, post-process
    scene/            scene manager, 17 node types, GLTF loader, serialiser
    physics/          Jolt wrapper, raycasts, callbacks
    script/           Lua engine, LuaBridge bindings
    audio/            miniaudio wrapper
    asset/            ResourceCache, texture/mesh upload
editor/
  src/                Qt6 + ImGui editor application
demo/
  scenes/             .solscene files
  scripts/            Lua game scripts
  assets/             models, textures, audio
cmake/
  Dependencies.cmake  FetchContent for all third-party libs
```

## Lua Scripting Quick Reference

```lua
-- Script attached to a node
local node = {}

function node.on_ready(self)
    print("Node ready: " .. self:get_name())
end

function node.on_update(self, dt)
    -- Raycast example
    local hit = Physics.raycast(Vec3(0,1,0), Vec3(0,-1,0), 100)
    if hit then
        print("Hit: " .. hit.node:get_name())
    end

    -- Spawn a node
    if Input.is_action_just_pressed("spawn") then
        local box = Scene.create_node("MeshNode")
        box:set_position(Vec3(0, 5, 0))
    end
end

return node
```

## Documentation

Full documentation at **[solengine.dev](https://solengine.mathiassol.com)** (or see `Website/`).

