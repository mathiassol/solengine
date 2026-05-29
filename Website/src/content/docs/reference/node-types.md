---
title: Node Types
description: Complete reference for all 17 node types in SolEngine.
---

# Node Types

SolEngine has 17 built-in node types organised into 4 categories.
Every node type can be added via **Add Node** in the editor hierarchy panel,
or created from Lua with `Scene.create_node("TypeName")`.

---

## Spatial Nodes

### `Node3D`
Base spatial node. Has a 3D transform (position, rotation, scale). Parent of all other 3D nodes. Use it as an empty transform grouping.

**Properties:** `position` (vec3), `rotation` (vec3 Euler degrees), `scale` (vec3), `name`

---

### `MeshNode`
A single primitive mesh (box, sphere, capsule, cylinder, plane).

**Properties:**
- `mesh_type` — `box`, `sphere`, `capsule`, `cylinder`, `plane`
- `material.base_color` (vec4), `material.metallic` (float 0–1), `material.roughness` (float 0–1)
- `material.emissive` (vec3), `material.alpha_mode` (Opaque / Mask / Blend), `material.alpha_cutoff`
- `mat_albedo_path`, `mat_normal_path`, `mat_mr_path`, `mat_emissive_path` — texture paths

---

### `ModelNode`
Loads a GLB/GLTF model. Supports multi-submesh materials.

**Properties:**
- `model_path` — path to `.glb` / `.gltf`
- Per-submesh material override (accessible in Material Editor)

---

### `Camera3D`
Perspective or orthographic camera. The editor viewport uses the first active camera.

**Properties:** `fov`, `near`, `far`, `is_orthographic`, `ortho_size`

---

## Lighting Nodes

### `DirectionalLight`
An infinitely distant directional light (sun). Generates Cascaded Shadow Maps.

**Properties:** `color`, `intensity`, `direction` (inherited from Node3D rotation), `cast_shadows`, `shadow_bias`

---

### `PointLight`
A local omni-directional point light.

**Properties:** `color`, `intensity`, `radius` (attenuation range)

---

## Physics Nodes

### `RigidBody3D`
A dynamic physics body. Can be moved by forces, impulses, or gravity.

**Properties:** `mass`, `linear_damping`, `angular_damping`, `gravity_scale`, `is_kinematic`

**Lua callbacks:** `on_collision_enter(other_node)`, `on_collision_exit(other_node)`

---

### `StaticBody3D`
An immovable physics body. Used for floors, walls, and fixed obstacles.

---

### `CharacterBody3D`
A physics body optimised for player/NPC movement. Does not tumble from physics forces.

**Lua API:**
```lua
character:move_and_slide(velocity, dt)
character:is_on_floor()
character:get_velocity()
```

---

### `CollisionShape3D`
Defines the collision geometry for a parent physics body or Area3D.

**Properties:** `shape_type` (box / sphere / capsule), `extents` / `radius` / `height`

---

### `Area3D`
A trigger volume. Detects when rigid bodies enter or exit.

**Lua callbacks:**
```lua
function node.on_body_entered(self, other) end
function node.on_body_exited(self, other)  end
```

**Lua query:**
```lua
local bodies = area:get_overlapping_bodies()  -- returns list of nodes
```

---

## Logic / Audio / Environment Nodes

### `ScriptNode`
A pure-logic node with a Lua script. No visual representation.

**Lua lifecycle:** `on_ready(self)`, `on_update(self, dt)`, `on_destroy(self)`

---

### `LuaComponent`
Attaches a Lua script to any node as a reusable behaviour component.

---

### `AudioStreamPlayer`
Plays audio non-spatially (music, UI sounds).

**Properties:** `audio_path`, `volume_db`, `autoplay`, `loop`

**Lua API:** `player:play()`, `player:stop()`, `player:is_playing()`

---

### `AudioStreamPlayer3D`
Plays audio with 3D positional attenuation.

**Properties:** `audio_path`, `volume_db`, `max_distance`, `autoplay`, `loop`

---

### `WorldEnvironment`
Sets global rendering parameters: sky, IBL, tone-mapping, fog, SSAO/SSR/TAA/bloom toggles.

---

### `SceneInstance`
Embeds another `.solscene` as a child. Enables scene composition and prefab-like reuse.

**Properties:** `scene_path`

---

## Creating Nodes from Lua

```lua
-- Create a primitive box
local box = Scene.create_node("MeshNode")
box:set_name("SpawnedBox")
box:set_position(Vec3(0, 5, 0))
box:set_mesh_type("box")

-- Load a GLB model
local model = Scene.instantiate_model("assets/models/crate.glb")
model:set_position(Vec3(2, 0, 0))

-- Attach a rigid body
local rb = Scene.create_node("RigidBody3D")
rb:set_parent(box)
```
