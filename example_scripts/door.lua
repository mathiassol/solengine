-- door.lua
-- Slides a door open when a body enters an Area3D trigger, closes on exit.
-- Attach to the Area3D node (or a parent ScriptNode with Area3D child).

local M = {}
M.__index = M

local OPEN_Y    = 3.0   -- raised Y when open
local SPEED     = 3.0   -- units per second

function M:on_ready()
    self._closed_y = self.node.position_y
    self._open_y   = self._closed_y + OPEN_Y
    self._target_y = self._closed_y
    engine:log("[Door] ready at y=" .. self._closed_y)
end

function M:on_body_entered(other)
    engine:log("[Door] opened by " .. other.name)
    self._target_y = self._open_y
end

function M:on_body_exited(other)
    engine:log("[Door] closing")
    self._target_y = self._closed_y
end

function M:on_update(dt)
    local cy = self.node.position_y
    if math.abs(cy - self._target_y) > 0.01 then
        local dir   = self._target_y > cy and 1 or -1
        local delta = dir * SPEED * dt
        -- don't overshoot
        if math.abs(delta) > math.abs(self._target_y - cy) then
            delta = self._target_y - cy
        end
        self.node.position_y = cy + delta
    end
end

return M
