-- trigger_zone.lua — Area3D callback demo
--
-- Usage (two patterns):
--
-- PATTERN A — script directly on the Area3D node:
--   1. Create an Area3D in the scene
--   2. Add a CollisionShape3D child to define the volume
--   3. Set Area3D.script_path = "scripts/trigger_zone.lua" in the inspector
--
-- PATTERN B — script on a parent ScriptNode (callbacks bubble up one level):
--   1. Create a ScriptNode and set its script_path to this file
--   2. Add an Area3D child with a CollisionShape3D grandchild
--   The parent ScriptNode will receive on_body_entered/on_body_exited automatically.

local M = {}
M.__index = M

function M:on_ready()
    self._enter_count = 0
    engine:log("[TriggerZone] Ready. Waiting for bodies...")
end

-- Fired when a physics body enters the Area3D volume.
-- 'other' is the Node that entered (RigidBody3D, CharacterBody3D, etc.)
function M:on_body_entered(other)
    self._enter_count = self._enter_count + 1
    local name = other and other.name or "???"
    engine:log("[TriggerZone] ENTERED by: " .. name ..
               "  (total inside: " .. self._enter_count .. ")")
end

-- Fired when a body exits.
function M:on_body_exited(other)
    self._enter_count = math.max(0, self._enter_count - 1)
    local name = other and other.name or "???"
    engine:log("[TriggerZone] EXITED by: " .. name ..
               "  (total inside: " .. self._enter_count .. ")")
end

function M:on_update(dt)
    -- Demonstrate polling: log overlap count every 5 seconds.
    self._poll_timer = (self._poll_timer or 0) + dt
    if self._poll_timer >= 5.0 then
        self._poll_timer = 0.0

        -- self.node is the node this script is attached to.
        -- If it's an Area3D we can call overlap methods directly.
        local area = self.node
        if area then
            local count = area:overlap_count()
            if count > 0 then
                engine:log("[TriggerZone] Currently overlapping: " .. count)
                local bodies = area:get_overlapping_bodies()
                for i, body in ipairs(bodies) do
                    engine:log("  [" .. i .. "] " .. body.name)
                end
            end
        end
    end
end

return M
