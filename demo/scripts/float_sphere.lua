-- float_sphere.lua
-- Bobs the node up and down in a sine wave, preserving the original Y as baseline.

local M = {}
M.__index = M

function M:on_ready()
    self._time   = 0.0
    self._base_y = self.node.position_y
end

function M:on_update(dt)
    self._time = self._time + dt
    self.node.position_y = self._base_y + math.sin(self._time * 0.7) * 2.0
end

return M
