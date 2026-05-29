-- pulse_light.lua
-- Pulses a PointLight intensity in a gentle sine wave.

local M = {}
M.__index = M

function M:on_ready()
    self._time           = 0.0
    self._base_intensity = self.node.intensity
end

function M:on_update(dt)
    self._time = self._time + dt
    -- Pulse between 60% and 140% of base intensity
    self.node.intensity = self._base_intensity * (1.0 + 0.4 * math.sin(self._time * 1.8))
end

return M
