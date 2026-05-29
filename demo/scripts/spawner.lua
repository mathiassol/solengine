-- spawner.lua — demonstrates Node Instantiation from Lua
-- Attach as script_path on a ScriptNode. Press F to spawn a physics crate.
-- Crates tagged "spawned" are auto-despawned after 8 seconds.

local M = {}
M.__index = M

local SPAWN_RANGE    = 5.0
local SPAWN_HEIGHT   = 3.0
local CRATE_LIFETIME = 8.0

function M:on_ready()
    self._spawn_count = 0
    engine:log("Spawner ready. Press F to spawn a crate.")
end

function M:on_update(dt)
    if Input.is_just_pressed("spawn_crate") then
        self:spawn_crate()
    end
end

function M:spawn_crate()
    local rb = engine:create_node("RigidBody3D")
    if not rb then
        engine:log("ERROR: create_node returned nil")
        return
    end
    rb.name = "SpawnedCrate_" .. self._spawn_count
    rb.mass = 5.0

    local px = (math.random() - 0.5) * SPAWN_RANGE * 2
    local pz = (math.random() - 0.5) * SPAWN_RANGE * 2
    rb.position = sol.vec3(px, SPAWN_HEIGHT, pz)
    rb:add_tag("spawned")

    local root = engine:get_root_node()
    engine:add_node(root, rb)

    local node_name = rb.name
    engine:create_timer(CRATE_LIFETIME, function()
        local found = engine:find_node(node_name)
        if found then
            engine:destroy_node(found)
            engine:log("Despawned crate: " .. node_name)
        end
    end)

    self._spawn_count = self._spawn_count + 1
    engine:log("Spawned crate #" .. self._spawn_count)
end

return M
