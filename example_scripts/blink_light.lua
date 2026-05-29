local M = {}
M.__index = M

function M:on_ready()
    self._time = 0.0
    self._base_intensity = self.node.intensity
    print("[blink_light.lua] attached to: " .. self.node.name)
end

function M:on_update(dt)
    self._time = self._time + dt
    local pulse = 0.5 + 0.5 * math.sin(self._time * 3.14159 * 2.0)
    self.node.intensity = self._base_intensity * pulse
end

return M
