local M = {}
M.__index = M

function M:on_ready()
    print("Hello from Lua! Node: " .. self.node.name)
    engine:log("ScriptEngine working correctly")
end

function M:on_update(dt)
    -- nothing
end

return M
