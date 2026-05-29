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
local M = {}
M.__index = M

function M:on_ready()
    -- Called once when the node enters the scene tree
end

function M:on_update(dt)
    -- Called every frame; dt is elapsed time in seconds
end

function M:on_destroy()
    -- Called when the node is removed from the scene
end

return M
```

---

## Node (`self`)

Every lifecycle method receives `self` — the node the script is attached to.

```lua
-- Identity
self.name                        -- get/set node name (string)
self:type_name()                 -- e.g. "MeshNode"

-- Transform (Node3D and subclasses)
self.position                    -- Vec3 get/set
self.rotation                    -- Vec3 get/set (Euler degrees)
self.scale                       -- Vec3 get/set
self.position_x / _y / _z       -- float get/set individual axes
self.rotation_x / _y / _z

self:get_world_position()        -- Vec3 world-space position
self:forward() / :right() / :up() -- Vec3 basis vectors

-- Hierarchy
self:find("ChildName")           -- Node3D or nil
self:parent()                    -- parent Node3D
self:child_count()               -- int
self:get_child(index)            -- Node by zero-based index

-- Tags
self:add_tag("enemy")
self:has_tag("enemy")            -- bool
self:remove_tag("enemy")

-- Visibility
self.visible                     -- bool get/set

-- Components
self:add_lua_component("scripts/my_comp.lua")
```

---

## Vec3

```lua
local v = vec3(1, 2, 3)   -- or: Vec3(1, 2, 3)  — both work
v.x, v.y, v.z
v + other, v - other, v * scalar
v:length()
v:normalized()
v:dot(other)
v:cross(other)
tostring(v)               -- "Vec3(1.000, 2.000, 3.000)"
```

---

## Input

```lua
-- Action names come from the project input map
Input.is_pressed("move_forward")          -- held this frame
Input.is_just_pressed("jump")             -- became pressed this frame
Input.is_just_released("attack")          -- became released this frame
-- Godot-style aliases also work:
Input.is_action_pressed("move_forward")
Input.is_action_just_pressed("jump")
Input.is_action_just_released("attack")

Input.get_axis("move_left", "move_right") -- -1..1
Input.get_vector("left","right","up","down") -- Vec2-like table {x,y}
Input.get_mouse_delta()                   -- {x, y} pixels
Input.get_scroll()                        -- float
Input.get_strength("move_forward")        -- 0..1 analogue

-- Key / mouse button raw queries (use engine: directly)
engine:key_down(KEY_SPACE)    -- bool; KEY_* constants available
engine:mouse_down(MOUSE_LEFT) -- bool; MOUSE_LEFT / MOUSE_RIGHT / MOUSE_MIDDLE
```

---

## Physics

### Raycast

```lua
local hit = Physics.raycast(origin, direction, max_distance)
-- Returns a table (hit may be false if nothing was struck):
-- hit.hit        bool
-- hit.position   Vec3  world-space hit point
-- hit.normal     Vec3  surface normal
-- hit.node       Node  the node that was hit (nil if none)
-- hit.distance   float distance from origin

local hit = Physics.raycast(self:get_world_position(), self:forward(), 50.0)
if hit and hit.hit then
    engine:log("Hit: " .. hit.node.name)
end

-- Optional fifth argument: node to ignore
local hit = Physics.raycast(origin, dir, 100, self)
```

### Sphere overlap

```lua
local bodies = Physics.overlap_sphere(center_vec3, radius)
for i, body in ipairs(bodies) do
    print(body.name)
end
```

### RigidBody3D

```lua
rb:get_velocity()              -- Vec3
rb:set_velocity(vec3(0,5,0))
rb:get_angular_velocity()
rb:set_angular_velocity(v)
rb:apply_impulse(vec3(0,10,0))
rb:apply_force(v)
rb:apply_torque_impulse(v)
rb:freeze_rotation(true)
rb:set_kinematic(true)
rb.mass             -- float get/set
rb.gravity_scale    -- float get/set
```

### CharacterBody3D

```lua
cb:move_and_slide(velocity_vec3, dt)   -- returns clamped velocity
cb:is_on_ground()                      -- bool
cb:get_velocity()                      -- Vec3
```

### Collision callbacks

```lua
function M:on_collision_enter(other)
    print("Collided with " .. other.name)
end
function M:on_collision_exit(other) end
```

---

## Area3D Callbacks

```lua
function M:on_body_entered(other)
    print(other.name .. " entered the trigger")
end
function M:on_body_exited(other)
    print(other.name .. " left the trigger")
end

-- Query overlapping bodies at any time
local bodies = area_node:get_overlapping_bodies()
area_node:is_overlapping_with(other_node)  -- bool
area_node:overlap_count()                  -- int
```

---

## Scene

```lua
-- Find / query
Scene.get_node("NodeName")              -- first match by name
Scene.get_root()                        -- root Node
Scene.find_by_tag("enemy")             -- table of nodes

-- Create primitives
local box = Scene.create_node("MeshNode")
box.name = "Crate"
box.position = vec3(0, 5, 0)

-- Load a GLB as a ModelNode (auto-spawned at root)
local model = Scene.instantiate_model("assets/models/barrel.glb")
model.position = vec3(2, 0, 0)

-- Manually add a pending node to a parent
local rb = engine:create_node("RigidBody3D")   -- held in pending pool
engine:add_node(box, rb)                        -- spawn under box

-- Destroy
Scene.destroy_node(box)

-- Load another scene
Scene.load("scenes/level2.solscene")
```

---

## Audio

```lua
-- Attached AudioStreamPlayer
local player = Scene.get_node("Music")
player:play()
player:stop()
player:is_playing()          -- bool
player.volume                -- float (linear)
player.pitch                 -- float
player.loop                  -- bool

-- Fire-and-forget
Audio.play_oneshot("assets/audio/explosion.ogg")
Audio.play_oneshot_bus("assets/audio/footstep.ogg", "sfx")

-- One-shot via engine
engine:play_sound("assets/audio/ping.ogg")
engine:play_sound_bus("assets/audio/ping.ogg", "sfx")

-- Master / bus volume
engine:set_master_volume(0.8)
engine:set_bus_volume("sfx", 0.5)
```

---

## Log / Debug

```lua
print("hello")          -- appears in the Console panel
engine:log("message")   -- engine log (same output)
Log.info("message")
Log.warn("message")
Log.error("message")
```

---

## Timer

```lua
-- One-shot
engine:create_timer(2.0, function()
    print("two seconds later")
end)

-- Repeating
local id = engine:create_timer(1.0, function()
    print("every second")
end, true)

engine:cancel_timer(id)
```

---

## Engine globals

```lua
engine.delta_time    -- float, seconds since last frame
engine.elapsed_time  -- float, total seconds since start
engine:screen_width()
engine:screen_height()
engine:set_cursor_captured(true)   -- FPS mouse capture
engine:cursor_x() / :cursor_y()   -- screen-space position
engine:quit()
```

---

## Full Example — Spawner Script

```lua
local M = {}
M.__index = M

local COUNT = 0

function M:on_ready()
    engine:log("Spawner ready. Press F to spawn a crate.")
end

function M:on_update(dt)
    if Input.is_just_pressed("spawn_crate") then
        self:spawn()
    end
end

function M:spawn()
    COUNT = COUNT + 1
    local rb = engine:create_node("RigidBody3D")
    rb.name     = "Crate_" .. COUNT
    rb.mass     = 5.0
    rb.position = vec3(
        (math.random() - 0.5) * 10,
        4.0,
        (math.random() - 0.5) * 10
    )
    rb:add_tag("spawned")

    engine:add_node(engine:get_root_node(), rb)

    engine:create_timer(8.0, function()
        local found = engine:find_node(rb.name)
        if found then engine:destroy_node(found) end
    end)

    engine:log("Spawned " .. rb.name)
end

return M
```


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
