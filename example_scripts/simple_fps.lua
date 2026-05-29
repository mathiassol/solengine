-- simple_fps.lua
-- Basic first-person controller for a CharacterBody3D.
-- Requires input actions: move_forward, move_back, move_left, move_right, jump.
-- Place a Camera3D child named "Camera" under the CharacterBody3D.

local M = {}
M.__index = M

local WALK_SPEED  = 5.0
local JUMP_VEL    = 6.0
local GRAVITY     = -20.0
local MOUSE_SENS  = 0.12

function M:on_ready()
    self._camera    = self.node:find("Camera")
    self._yaw       = self.node.rotation_y
    self._pitch     = 0.0
    self._vel_y     = 0.0
    self._skip      = true

    engine:set_cursor_captured(true)
    Input.push_context("gameplay")
end

function M:on_update(dt)
    -- skip first frame (avoids mouse delta spike)
    if self._skip then
        Input.get_mouse_delta()
        self._skip = false
        return
    end

    -- mouse look
    local dx, dy = Input.get_mouse_delta()
    self._yaw   = self._yaw   - dx * MOUSE_SENS
    self._pitch = math.max(-89, math.min(89, self._pitch - dy * MOUSE_SENS))

    self.node.rotation_y = self._yaw
    if self._camera then self._camera.rotation_x = self._pitch end

    -- movement
    local mz  = Input.get_axis("move_back",  "move_forward")
    local mx  = Input.get_axis("move_left",  "move_right")
    local fwd = self.node:forward()
    local rgt = self.node:right()
    local vx  = fwd.x * mz + rgt.x * mx
    local vz  = fwd.z * mz + rgt.z * mx
    local len = math.sqrt(vx * vx + vz * vz)
    if len > 1.0 then vx = vx / len; vz = vz / len end

    -- gravity + jump
    if self.node:is_on_ground() then
        if self._vel_y < 0 then self._vel_y = 0 end
        if Input.is_just_pressed("jump") then self._vel_y = JUMP_VEL end
    end
    self._vel_y = self._vel_y + GRAVITY * dt

    self.node:move_and_slide(vec3(vx * WALK_SPEED, self._vel_y, vz * WALK_SPEED))
end

return M
