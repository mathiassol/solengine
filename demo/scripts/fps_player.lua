-- FPS Player Controller
-- Input actions + zone-based footstep sounds + raycast push interaction.

local M = {}
M.__index = M

local WALK_SPEED          = 5.0
local SPRINT_SPEED        = 9.0
local JUMP_VEL            = 6.0
local GRAVITY             = -20.0
local MOUSE_SENSITIVITY   = 0.12   -- degrees per pixel
local ARROW_LOOK_SPEED    = 90.0   -- degrees per second
local STEP_WALK_INTERVAL  = 0.50
local STEP_SPRINT_INTERVAL = 0.30
local INTERACT_REACH      = 4.0    -- metres
local PUSH_IMPULSE        = 14.0   -- N·s

-- Surface variant counts per zone
local SURFACE_COUNTS = {
  Gravel = 21, Dirt = 21, Wood = 21, Tiles = 22,
}

local function get_surface(x, z)
  if x < 0 and z < 0 then return "Gravel"
  elseif x >= 0 and z < 0 then return "Dirt"
  elseif x < 0 and z >= 0 then return "Wood"
  else return "Tiles"
  end
end

local function surface_path(surface, index)
  return string.format("assets/audio/%s/Steps_%s-%03d.ogg",
    surface, string.lower(surface), index)
end

-- ---------------------------------------------------------------------------

function M:on_ready()
  self._camera    = self.node:find("Camera")
  self._yaw       = self.node.rotation_y
  self._pitch     = self._camera and self._camera.rotation_x or 0.0
  self._vel_y     = 0.0
  self._step_timer = 0.0
  self._captured  = false
  self._skip_frame = true
  self._look_target = nil   -- Node currently aimed at (or nil)

  math.randomseed(os.time())
  engine:set_cursor_captured(true)
  self._captured = true
  Input.push_context("gameplay")
end

function M:on_update(dt)
  -- Skip first frame to avoid mouse delta spike
  if self._skip_frame then
    Input.get_mouse_delta()
    self._skip_frame = false
    return
  end

  -- Cursor toggle
  if Input.is_just_pressed("escape") then
    self._captured = false
    engine:set_cursor_captured(false)
  end
  if Input.is_just_pressed("recapture") and not self._captured then
    self._captured = true
    engine:set_cursor_captured(true)
  end

  -- Mouse look
  if self._captured then
    local dx, dy = Input.get_mouse_delta()
    self._yaw   = self._yaw   - dx * MOUSE_SENSITIVITY
    self._pitch = self._pitch - dy * MOUSE_SENSITIVITY
  end

  -- Arrow key look
  local ah = Input.get_axis("look_left", "look_right")
  local av = Input.get_axis("look_up",   "look_down")
  self._yaw   = self._yaw   + ah * ARROW_LOOK_SPEED * dt
  self._pitch = self._pitch - av * ARROW_LOOK_SPEED * dt
  self._pitch = math.max(-89.0, math.min(89.0, self._pitch))

  self.node.rotation_y = self._yaw
  if self._camera then self._camera.rotation_x = self._pitch end

  -- Movement
  local sprinting = Input.is_pressed("sprint")
  local speed     = sprinting and SPRINT_SPEED or WALK_SPEED
  local mz = Input.get_axis("move_back",  "move_forward")
  local mx = Input.get_axis("move_left",  "move_right")

  local fwd   = self.node:forward()
  local right = self.node:right()
  local wx = fwd.x * mz + right.x * mx
  local wz = fwd.z * mz + right.z * mx

  local len = math.sqrt(wx * wx + wz * wz)
  if len > 1.0 then wx = wx / len; wz = wz / len end

  -- Gravity + jump
  local grounded = self.node:is_on_ground()
  if grounded and self._vel_y < 0.0 then self._vel_y = 0.0 end
  if grounded and Input.is_just_pressed("jump") then self._vel_y = JUMP_VEL end
  self._vel_y = self._vel_y + GRAVITY * dt

  self.node:move_and_slide(sol.vec3(wx * speed, self._vel_y, wz * speed))

  -- Footsteps
  local moving = len > 0.05
  if grounded and moving then
    self._step_timer = self._step_timer - dt
    if self._step_timer <= 0.0 then
      self._step_timer = sprinting and STEP_SPRINT_INTERVAL or STEP_WALK_INTERVAL
      local surface = get_surface(self.node.position_x, self.node.position_z)
      local count   = SURFACE_COUNTS[surface]
      local path    = surface_path(surface, math.random(1, count))
      engine:play_sound(path)
    end
  else
    if self._step_timer > STEP_WALK_INTERVAL then
      self._step_timer = STEP_WALK_INTERVAL
    end
  end

  -- ---- Raycast interaction ------------------------------------------------
  self:_update_interact(dt)
end

function M:_update_interact(dt)
  local cam = self._camera
  if not cam then return end

  -- Ray origin = camera world position (approx player eye)
  local eye = sol.vec3(
    self.node.position_x,
    self.node.position_y + 0.7,
    self.node.position_z
  )

  -- Camera forward in world space
  local cf = cam:forward()

  local hit = engine:raycast(eye, cf, INTERACT_REACH, self.node)

  if hit.hit and hit.node and hit.node.name == "PushCrate" then
    self._look_target = hit.node

    -- Press E → launch the crate in the look direction + a bit upward
    if Input.is_just_pressed("interact") then
      local impulse = sol.vec3(
        cf.x * PUSH_IMPULSE,
        cf.y * PUSH_IMPULSE + 3.0,   -- slight upward kick
        cf.z * PUSH_IMPULSE
      )
      hit.node:apply_impulse(impulse)
    end
  else
    self._look_target = nil
  end
end

return M
