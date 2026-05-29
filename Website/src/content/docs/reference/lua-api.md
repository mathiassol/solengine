---
title: Lua API Reference
description: Complete Lua scripting API for SolEngine — physics, input, scene, audio, and more.
---

# Lua API Reference

Scripts are plain Lua 5.4 files. Attach them to any node via the `script_path` property
in the Inspector or by using a `ScriptNode` / `LuaComponent`.

---

## Script Lifecycle

```lua
local node = {}

function node.on_ready(self)
    -- Called once when the node enters the scene tree
end

function node.on_update(self, dt)
    -- Called every frame; dt is elapsed time in seconds
end

function node.on_destroy(self)
    -- Called when the node is removed from the scene
end

return node
```

---

## Node (self)

Methods available on any node reference (`self` in lifecycle hooks, or returned by scene queries):

```lua
-- Identity
self:get_name()           -- string
self:set_name("Name")
self:get_type()           -- e.g. "MeshNode"

-- Transform
self:get_position()       -- Vec3
self:set_position(Vec3(x, y, z))
self:get_rotation()       -- Vec3 (Euler degrees)
self:set_rotation(Vec3(x, y, z))
self:get_scale()          -- Vec3
self:set_scale(Vec3(x, y, z))

-- Hierarchy
self:get_parent()         -- Node or nil
self:get_child(index)     -- Node
self:get_child_count()    -- int
self:set_parent(node)     -- reparent
self:add_child(node)

-- Enable / disable
self:set_visible(bool)
self:is_visible()         -- bool

-- Log helper
self:get_scene()          -- Scene handle
```

---

## Vec3

```lua
local v = Vec3(1, 2, 3)
v.x, v.y, v.z
Vec3(0, 0, 0)   -- zero constructor
v + other, v - other, v * scalar
v:length()
v:normalized()
Vec3.dot(a, b)
Vec3.cross(a, b)
Vec3.lerp(a, b, t)
```

---

## Input

```lua
-- Action names are defined in the project's input map
Input.is_action_pressed("move_forward")       -- held
Input.is_action_just_pressed("jump")          -- pressed this frame
Input.is_action_just_released("attack")       -- released this frame

Input.get_axis("move_left", "move_right")     -- returns -1..1 analogue value
```

---

## Physics

### Raycast

```lua
local hit = Physics.raycast(origin, direction, max_distance)
-- Returns nil if no hit, or:
-- hit.position   Vec3   world-space hit point
-- hit.normal     Vec3   surface normal
-- hit.node       Node   the node that was hit
-- hit.distance   float  distance from origin

-- Example
local hit = Physics.raycast(
    self:get_position(),
    Vec3(0, -1, 0),
    100.0
)
if hit then
    print("Hit: " .. hit.node:get_name() .. " at " .. hit.distance)
end
```

### Collision callbacks on RigidBody3D

```lua
function node.on_collision_enter(self, other)
    print("Collided with " .. other:get_name())
end

function node.on_collision_exit(self, other)
    print("Separated from " .. other:get_name())
end
```

### CharacterBody3D

```lua
character:move_and_slide(velocity, dt)   -- returns actual velocity after slide
character:is_on_floor()                  -- bool
character:get_velocity()                 -- Vec3
character:set_velocity(Vec3(x, y, z))
```

---

## Area3D Callbacks

```lua
function node.on_body_entered(self, other)
    print(other:get_name() .. " entered the trigger")
end

function node.on_body_exited(self, other)
    print(other:get_name() .. " left the trigger")
end

-- Query overlapping bodies at any time
local bodies = area_node:get_overlapping_bodies()
for i, body in ipairs(bodies) do
    print(body:get_name())
end
```

---

## Scene

### Querying nodes

```lua
local node = Scene.get_node("NodeName")       -- by name (first match)
local node = Scene.get_node_by_path("/Root/Child/NodeName")
```

### Creating nodes

```lua
-- Primitive node
local box = Scene.create_node("MeshNode")
box:set_name("Crate")
box:set_position(Vec3(0, 5, 0))
box:set_mesh_type("box")   -- "box" | "sphere" | "capsule" | "cylinder" | "plane"

-- Load a GLB as a ModelNode
local model = Scene.instantiate_model("assets/models/barrel.glb")
model:set_position(Vec3(2, 0, 0))

-- Attach physics
local rb = Scene.create_node("RigidBody3D")
rb:set_parent(box)

-- Destroy a node
Scene.destroy_node(box)
```

---

## Audio

### AudioStreamPlayer / AudioStreamPlayer3D

```lua
local player = Scene.get_node("MyAudioPlayer")
player:play()
player:stop()
player:is_playing()       -- bool
player:set_volume_db(-6)
player:get_volume_db()
```

### One-shot fire-and-forget

```lua
Audio.play_oneshot("assets/audio/explosion.ogg")
Audio.play_oneshot_3d("assets/audio/footstep.ogg", Vec3(x, y, z))
```

---

## Log / Debug

```lua
print("hello")                          -- [Lua] hello in engine log
Log.info("message")
Log.warn("message")
Log.error("message")
```

---

## Full Example — Spawner Script

```lua
local spawner = {}
local count = 0

function spawner.on_ready(self)
    print("Spawner ready. Press F to spawn a crate.")
end

function spawner.on_update(self, dt)
    if Input.is_action_just_pressed("spawn") then
        count = count + 1
        local crate = Scene.instantiate_model("assets/models/crate.glb")
        local pos = self:get_position() + Vec3(math.random(-3, 3), 4, math.random(-3, 3))
        crate:set_position(pos)
        crate:set_name("Crate_" .. count)

        local rb = Scene.create_node("RigidBody3D")
        rb:set_parent(crate)

        print("Spawned crate #" .. count)
    end
end

return spawner
```
