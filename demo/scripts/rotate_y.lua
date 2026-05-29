-- rotate_y.lua
-- Continuously rotates the node around the Y axis.

local M = {}
M.__index = M

function M:on_ready()
    self._speed = 20.0  -- degrees per second
end

function M:on_update(dt)
    self.node.rotation_y = self.node.rotation_y + self._speed * dt
end

return M
