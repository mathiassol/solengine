# SolEngine — State Review & Roadmap

## Current State

### What Works
| System | Technology | Status |
|--------|-----------|--------|
| Rendering | bgfx (D3D11 / GL / Vulkan / Metal) | ✅ Forward shading, meshes, materials, lighting |
| ECS | EnTT | ✅ Full component registry, views, groups |
| Physics | Jolt Physics | ✅ Rigid bodies, step, basic collision |
| UI | Dear ImGui | ✅ Every frame, HUD and debug panels |
| Math | GLM | ✅ Vectors, matrices, transforms |
| Assets | cgltf | ✅ GLB/GLTF mesh + material loading |
| Build | CMake + FetchContent | ✅ Engine compiles to DLL, games as separate DLLs |
| Game ABI | C function-pointer contract | ✅ Stable, hot-swappable game DLLs |
| Renderer abstraction | `sol::Renderer` pure-virtual | ✅ Just refactored — backends drop in |

### Known Weaknesses
- **One forward-shading pass** — no shadows, no PBR, no post-processing
- **Mesh is bgfx-coupled** — `vbh_idx / ibh_idx` are bgfx handles; a Vulkan backend would need its own mesh type
- **Input is raw GLFW** — key/button queries on `Engine`; no action maps, no gamepad
- **No event system** — subsystems call each other directly; no decoupled message bus
- **No audio** — silence
- **No editor** — everything is hard-coded at runtime
- **No project system** — game projects are just raw CMake subdirectories
- **No scripting** — all game logic must be compiled C++
- **Asset pipeline is minimal** — loader exists but no build-time compilation, no streaming, no hot-reload
- **Window is GLFW-only** — `Window::handle()` leaks `GLFWwindow*` into calling code

---

## Roadmap to a Production-Quality 3D Engine

Each phase below builds on the previous. Estimated logical order; phases 1–3 unlock
the most visible improvements.

---

### Phase 1 — Solid Forward Renderer (foundation for everything else)

The current single-pass Phong shading needs to grow before deferred or GI are
possible.

**Shadowmaps**
- Add a shadow pass: render scene depth from light POV into a depth texture
- PCF (Percentage Closer Filtering) for soft edges
- Cascaded Shadow Maps (CSM) for directional lights to cover large scenes

**Point & Spot Lights**
- Uniform array of lights passed to the shader (`u_lights[16]`)
- Attenuation, spot cone angle, colour + intensity

**PBR Materials**
- Replace Phong with Cook-Torrance BRDF (metallic/roughness workflow)
- Albedo, Normal, Metallic/Roughness, AO, Emissive texture slots
- Image-Based Lighting (IBL): diffuse irradiance cube + pre-filtered specular cube

**HDR Pipeline & Post-Processing**
- Render to an HDR float framebuffer (RGBA16F)
- Tonemapping pass (ACES or Uncharted 2 filmic)
- Bloom (dual Kawase or compute-based hierarchical blur)
- Colour grading via LUT

**Skybox**
- Equirectangular HDR panorama → cube map at load time
- Rendered last (infinite sphere, reversed depth)
- Feed into IBL diffuse/specular probes

---

### Phase 2 — Deferred Rendering Pipeline

Forward+ works for moderate light counts; deferred unlocks hundreds of lights and
is required for SSAO and SSR.

**G-Buffer layout (4 render targets)**

| RT | Format   | Contents                       |
|----|----------|--------------------------------|
| 0  | RGBA8    | Albedo (RGB) + unused          |
| 1  | RGBA16F  | World-space normal (XY packed) |
| 2  | RGBA8    | Metallic (R) + Roughness (G)   |
| 3  | R32F     | Depth (or reuse the depth buffer) |

**Lighting pass**
- Full-screen quad reads G-Buffer, applies every light in one pass
- Tiled or clustered culling for large light counts

**Transparency**
- Deferred handles only opaque geometry; transparent objects rendered in a
  separate forward pass on top

---

### Phase 3 — Screen-Space Effects

Requires the deferred G-Buffer from Phase 2.

**SSAO / GTAO (Ambient Occlusion)**
- Screen-Space Ambient Occlusion: sample depth hemisphere, compare depths
- Ground-Truth Ambient Occlusion (GTAO) for higher quality
- Bilateral blur to denoise; result multiplied into the ambient lighting term

**SSR (Screen-Space Reflections)**
- Ray-march in screen space using the depth buffer and normals
- Fade out near screen edges; blend with IBL where rays leave the screen
- Optional temporal accumulation for stability

**TAA (Temporal Anti-Aliasing)**
- Sub-pixel jitter in the projection matrix each frame
- Reproject previous frame with motion vectors
- Exponential moving average blend with neighbourhood clipping

---

### Phase 4 — Global Illumination

GI makes scenes feel physically real; it is the most complex phase.

**DDGI — Dynamic Diffuse GI (recommended first)**
- Place a 3D grid of irradiance probes throughout the scene
- Each probe accumulates radiance from ray-traced or rasterised rays
- Scene geometry reads the nearest probe cluster at runtime
- Can be approximated without ray-tracing hardware using multiple shadow-map
  cascades

**LPV — Light Propagation Volumes** (lower quality, easier to implement)
- Inject direct light into a 3D volume, propagate it through neighbouring voxels
- Fast but leaks light at thin geometry

**Path-traced reference mode** (long-term)
- Use bgfx Vulkan backend + VK_KHR_ray_tracing_pipeline
- Software fallback via SPIRV-Cross for non-RT hardware

---

### Phase 5 — In-Engine Editor

The editor lives inside `sol.exe`, enabled with a `--editor` flag or via an
`editor.set_active(true)` call. No separate executable needed.

**Editor layout**
```
┌─ Toolbar (Play/Stop/Pause) ────────────────────────────────────────────────┐
├─ Scene Hierarchy ──┬─ Viewport (3D view) ─────────────────┬─ Inspector ───┤
│  World             │  [rendered scene with gizmos]         │  Entity Name  │
│  ├── Sun           │                                       │  Transform    │
│  ├── Planet_0      │                                       │  Mesh         │
│  └── Ship          │                                       │  Material     │
├────────────────────┴──────────────────────────────────────┴───────────────┤
│  Content Browser  [assets/]  ·  Log  ·  Profiler                          │
└───────────────────────────────────────────────────────────────────────────┘
```

**Key panels**
- **Scene Hierarchy** — tree of entities, drag-to-reparent, right-click create/delete
- **Inspector** — EnTT `meta_any` reflection to display and edit any component
- **Content Browser** — file-system view of the project's `assets/` folder,
  drag meshes/textures onto entities
- **Viewport** — ImGuizmo gizmos (translate / rotate / scale), camera orbit,
  pick entities with mouse
- **Console / Log** — capture `SOL_INFO/WARN/ERROR` output with colour coding

**Editor modes**
- *Edit mode*: time frozen, entities inspectable, gizmos visible
- *Play mode*: full game loop runs inside the editor viewport
- *Pause mode*: single-step frame-by-frame

**Implementation tips**
- ImGuizmo for viewport gizmos (header-only, MIT licence)
- `entt::meta` for zero-overhead runtime reflection of components
- Undo/redo: command pattern, store diffs of changed components

---

### Phase 6 — Lua Scripting

Lua is ideal for game logic: fast, small, easy to sandbox, hot-reloadable.

**Integration via sol2 (header-only C++ binding)**
```cpp
// In engine init:
sol2::state lua;
lua.open_libraries(sol2::lib::base, sol2::lib::math);

// Bind engine types:
lua.new_usertype<sol::Entity>("Entity",
    "get_transform", &sol::Entity::get_transform, ...);
```

**Script component**
```cpp
struct LuaScript {
    std::string path;          // "assets/scripts/enemy.lua"
    sol2::table  env;          // per-entity sandboxed environment
};
```

**Hot-reload**
- Watch script files for changes (`ReadDirectoryChangesW` on Windows, `inotify` on Linux)
- Re-run the script file into its environment table on change
- `on_init`, `on_update(dt)`, `on_destroy` lifecycle callbacks

**Exposed APIs to Lua**
- `Entity` (create, destroy, get/set components)
- `Transform` (position, rotation, scale)
- `Physics` (apply_force, set_velocity, raycast)
- `Input` (key_down, mouse_delta, gamepad_axis)
- `Audio` (play, stop, set_volume)
- `Debug` (draw_line, draw_sphere, log)

---

### Phase 7 — Audio Pipeline

**Library: miniaudio** (single-header, no dependencies, MIT licence)

```
AudioClip asset (.ogg / .wav / .flac)
    └── decoded to PCM at load time
    └── AudioSource component (3D position, velocity, pitch, volume)
    └── AudioListener component (one per scene, follows camera)
    └── miniaudio spatial engine (distance attenuation, doppler)
```

**Asset types**
- `AudioClip` — loaded from OGG or WAV, holds decoded PCM buffer
- `AudioSource` — component: clip reference, play/pause/stop, loop, volume, range
- `AudioListener` — component: defines the listener position/orientation

**Effects**
- Per-source low-pass filter (distance muffling)
- Reverb sends (DSP node graph in miniaudio)
- Music playlist / cross-fade

---

### Phase 8 — Proper Input System

Replace the raw `glfwGetKey` calls in `Engine` with a first-class `InputSystem`.

**Action mapping**
```cpp
input.bind("Jump",   Key::Space, GamepadButton::A);
input.bind("Fire",   MouseButton::Left);
input.bind("MoveX",  Axis::GamepadLeftStickX, Key::D, Key::A); // composite
```

**Query API**
```cpp
bool     input.pressed ("Jump")   // true for one frame
bool     input.held    ("Fire")   // true while held
float    input.axis    ("MoveX")  // -1 to +1
glm::vec2 input.mouse_delta()
```

**Input contexts** (e.g. "gameplay" vs "UI" vs "editor")
- Stack of active contexts; each context has its own bindings
- Push/pop on mode transitions so UI doesn't bleed into gameplay

**Gamepad**
- GLFW joystick API; remap raw axes to standard layout
- Vibration via `glfwSetJoystickUserPointer` + platform extension (Windows `XInput`)

**Bindings file**
- `assets/input.toml` (or per-project) — persisted between runs, editable in the editor

---

### Phase 9 — Window Management

Current `Window` class wraps GLFW and leaks `GLFWwindow*`. Abstract it.

**`IWindow` interface**
```cpp
class IWindow {
public:
    virtual bool  init  (const WindowConfig&) = 0;
    virtual void  shutdown() = 0;
    virtual void  poll  () = 0;
    virtual bool  should_close() const = 0;
    virtual void  set_title(std::string_view) = 0;
    virtual void  set_fullscreen(bool) = 0;
    virtual int   width () const = 0;
    virtual int   height() const = 0;
    virtual void* native_handle  () const = 0; // HWND / NSWindow / X11 Window
    virtual void* native_display () const = 0; // nullptr on Win32/macOS
};
```

**Features to add**
- Fullscreen / windowed / borderless toggle at runtime
- Window resize callback → notifies Renderer::resize + Input::on_resize
- Multiple windows (needed for editor: scene + game-in-editor split)
- DPI awareness (`glfwSetWindowContentScaleCallback`)
- Display enumeration (list monitors, pick refresh rate)

**Implementation**: keep `GlfwWindow : IWindow`; add `Win32Window` later if needed
for raw Win32 features (raw input, custom borders).

---

### Phase 10 — Project System

Every game is a *Sol project* rooted at a directory containing `project.sol`.

**Directory layout**
```
MyGame/
  project.sol          ← project manifest (TOML format)
  assets/
    meshes/   .glb / .fbx  → compiled to .solmesh
    textures/ .png / .hdr  → compiled to .soltex
    audio/    .ogg / .wav
    scripts/  .lua
    scenes/   .solscene
  src/
    game.cpp   game.h
  build/       ← generated, gitignored
```

**`project.sol` format (TOML)**
```toml
[project]
name    = "My Game"
version = "0.1.0"
engine  = "1.0"

[window]
title  = "My Game"
width  = 1920
height = 1080
vsync  = true

[build]
sources = ["src/**/*.cpp"]
output  = "game.dll"

[assets]
auto_import = true   # watch assets/ and reimport on change

[physics]
gravity = [0.0, -9.81, 0.0]
```

**Scene format (`.solscene` — binary MessagePack or JSON)**
- Serialised list of entities with their EnTT components
- References assets by UUID (stable across renames)
- Loaded/saved by the editor and at runtime

**Asset database (`assets.db` — SQLite)**
- Maps source file path → UUID → compiled artefact path
- Tracks import settings per asset (texture compression, mesh LOD target, etc.)
- Invalidates stale compiled assets when source changes

---

### Phase 11 — Bundled Engine Tools

All tools are sub-commands of `sol.exe`. No extra executables to install.

```
sol new  <name>          Create a new project scaffold
sol run  <project>       Build (if needed) and run the game
sol editor <project>     Launch the in-engine editor
sol build <project>      Compile the game DLL without running
sol import <file>        Reimport a single asset into the project database
sol bake <project>       Pre-bake lightmaps, probes, nav-meshes
sol pack <project>       Bundle all assets into a shipping archive
sol profile              Launch the standalone profiler UI (reads .solperf files)
```

**Shader toolchain (already partially present)**
- `sol shader compile <src>` — wraps bgfx shaderc, outputs .bin headers
- Live shader reload in editor mode (file-watcher + hot-swap program handle)
- Shader validation (SPIRV-Cross reflection, detect missing uniforms)

**Texture pipeline**
- Import: PNG/JPG/TGA/HDR → `sol import texture.png`
- Compress to BCn (DXT1/5, BC7) via `bimg` (already in the dependency tree)
- Generate mip-maps at import time
- Store in `.soltex` (custom header + raw BCn data)

**Mesh pipeline**
- Import: GLB/FBX → `sol import model.glb`
- Optional mesh optimisation (meshoptimizer library, ~5 min integration)
- LOD generation (simplygon-free: simplify by quadric error)
- Store in `.solmesh` (custom binary: vertex streams + index buffer + LODs)

**Profiler**
- CPU: `SOL_PROFILE_BEGIN/END` macros wrapping a ring buffer
- GPU: bgfx timer queries
- Writes `.solperf` binary timeline files
- `sol profile` opens a Dear ImGui viewer that plays back captured frames

---

### Phase 12 — Stretch Goals

Once the above is solid:

| Feature | Library / Approach |
|---------|-------------------|
| Navigation mesh | Recast/Detour (MIT) |
| Skinned animation | glTF 2.0 skins, linear blend skinning in VS |
| Networking | enet or GameNetworkingSockets |
| Localisation | gettext `.po` files, `sol.translate("key")` |
| VR support | OpenXR (bgfx has XR path) |
| Console export | Middleware renderer (GNM/NX) swap via `Engine::set_renderer` |

---

## Architecture Principles Going Forward

1. **Backend swappability first** — every platform-specific subsystem hides behind
   a pure-virtual interface (`IRenderer`, `IWindow`, `IInputSystem`, `IAudio`).
   Swapping a backend is a one-line change in `engine.cpp`.

2. **No engine headers in game DLLs** (long-term) — the game DLL should depend
   only on `sol/api.h` and a minimal POD types header.  All engine types are
   accessed through opaque handles.

3. **Data-oriented ECS** — keep components plain data (no virtual methods).
   Systems are free functions or stateless lambdas over EnTT views.  Avoids
   cache-miss chains common in OOP entity hierarchies.

4. **Deterministic asset UUIDs** — assets are referenced by UUID, never by path.
   Paths are editor-time only.  This makes scene serialisation stable across
   directory restructuring.

5. **Batteries included, no forced dependencies** — every tool ships inside
   `sol.exe`.  A developer clones the engine, runs `sol new MyGame`, and has a
   working project with zero additional installs.
