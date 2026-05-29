-- spawner.lua
-- Spawns physics crates on a key press. Attach to any ScriptNode.
-- Requires an input action "spawn_crate" mapped to a key (e.g. F).

local M = {}
M.__index = M

local SPAWN_RANGE    = 5.0
local SPAWN_HEIGHT   = 4.0
local CRATE_LIFETIME = 8.0

function M:on_ready()
    self._count = 0
    engine:log("Spawner ready. Press the spawn_crate key.")
end

function M:on_update(dt)
    if Input.is_just_pressed("spawn_crate") then
        self:_spawn()
    end
end

function M:_spawn()
    self._count = self._count + 1

    local rb = engine:create_node("RigidBody3D")
    rb.name     = "Crate_" .. self._count
    rb.mass     = 5.0
    rb.position = vec3(
        (math.random() - 0.5) * SPAWN_RANGE * 2,
        SPAWN_HEIGHT,
        (math.random() - 0.5) * SPAWN_RANGE * 2
    )
    rb:add_tag("spawned")

    engine:add_node(engine:get_root_node(), rb)

    -- auto-despawn after CRATE_LIFETIME seconds
    local crate_name = rb.name
    engine:create_timer(CRATE_LIFETIME, function()
        local node = engine:find_node(crate_name)
        if node then engine:destroy_node(node) end
    end)

    engine:log("Spawned " .. rb.name)
end

return M
