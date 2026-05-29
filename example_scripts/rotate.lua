local M = {}
M.__index = M

function M:on_ready()
    print("[rotate.lua] on_ready: " .. self.node.name)
end

function M:on_update(dt)
    self.node.rotation_y = self.node.rotation_y + 45.0 * dt
end

return M
