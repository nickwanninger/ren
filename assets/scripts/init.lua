print [[

   ::::::::::::::::::::::::::::::::::
   ::'########::'########:'##::: ##::
   :: ##.... ##: ##.....:: ###:: ##::
   :: ##:::: ##: ##::::::: ####: ##::
   :: ########:: ######::: ## ## ##::
   :: ##.. ##::: ##...:::: ##. ####::
   :: ##::. ##:: ##::::::: ##:. ###::
   :: ##:::. ##: ########: ##::. ##::
   ::..:::::..::........::..::::..:::
   ::::::::::::::::::::::::::::::::::

          -- Welcome to REN --

]]

local ren = require 'ren'
local ffi = require 'ffi'

ren.struct("vec2", [[
    float x;
    float y;
]], {
  len = function(a)
    return math.sqrt(a.x * a.x + a.y * a.y)
  end,
  norm = function(a)
    local l = a:len()
    if l == 0 then
      return vec2(0, 0)
    end
    return vec2(a.x / l, a.y / l)
  end,
  dot = function(a, b)
    return a.x * b.x + a.y * b.y
  end,
  unpack = function(a)
    return a.x, a.y
  end
}, {
  __add = function(a, b)
    if type(b) == "number" then
      return vec2(a.x + b, a.y + b)
    end
    return vec2(a.x + b.x, a.y + b.y)
  end,
  __sub = function(a, b)
    if type(b) == "number" then
      return vec2(a.x - b, a.y - b)
    end
    return vec2(a.x - b.x, a.y - b.y)
  end,
  __mul = function(a, b)
    -- scalar multiply on either side
    if type(a) == "number" then
      return vec2(a * b.x, a * b.y)
    end
    if type(b) == "number" then
      return vec2(a.x * b, a.y * b)
    end
    -- dot if both vec2
    return a:dot(b)
  end,
  __eq = function(a, b)
    return a.x == b.x and a.y == b.y
  end,
})

vec2.up = vec2(0, 1)
vec2.right = vec2(1, 0)
vec2.left = vec2(-1, 0)
vec2.down = vec2(0, -1)
vec2.zero = vec2(0, 0)
vec2.one = vec2(1, 1)

ren.struct("vec3", [[
    float x;
    float y;
    float z;
]], {
  len = function(a)
    return math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z)
  end,
  norm = function(a)
    local l = a:len()
    if l == 0 then
      return vec3(0, 0, 0)
    end
    return vec3(a.x / l, a.y / l, a.z / l)
  end,
  dot = function(a, b)
    return a.x * b.x + a.y * b.y + a.z * b.z
  end,
  cross = function(a, b)
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)
  end,
  unpack = function(a)
    return a.x, a.y, a.z
  end
}, {
  __add = function(a, b)
    if type(b) == "number" then
      return vec3(a.x + b, a.y + b, a.z + b)
    end
    return vec3(a.x + b.x, a.y + b.y, a.z + b.z)
  end,
  __sub = function(a, b)
    if type(b) == "number" then
      return vec3(a.x - b, a.y - b, a.z - b)
    end
    return vec3(a.x - b.x, a.y - b.y, a.z - b.z)
  end,
  __mul = function(a, b)
    -- scalar multiply on either side
    if type(a) == "number" then
      return vec3(a * b.x, a * b.y, a * b.z)
    end
    if type(b) == "number" then
      return vec3(a.x * b, a.y * b, a.z * b)
    end
    -- dot if both vec3
    return a:dot(b)
  end,
  __eq = function(a, b)
    return a.x == b.x and a.y == b.y and a.z == b.z
  end
})

vec3.forward = vec3(0, 0, 1)
vec3.back = vec3(0, 0, -1)

vec3.up = vec3(0, 1, 0)
vec3.down = vec3(0, -1, 0)

vec3.left = vec3(-1, 0, 0)
vec3.right = vec3(1, 0, 0)

vec3.zero = vec3(0, 0, 0)
vec3.one = vec3(1, 1, 1)

ren.struct("vec4", [[
    float x;
    float y;
    float z;
    float w;
]])

ren.struct("quat", [[
    float x;
    float y;
    float z;
    float w;
]])

ren.struct("Transform", [[
    vec3 pos;
    quat rot;
    vec3 scale;

    float __tm[16]; // transformation matrix (4x4) (IGNORE)
]])

ffi.cdef [[
    void __lua_draw_debug_line(vec3 a, vec3 b, vec3 color, float thickness);
]]

function debug_line(a, b, color, thickness)
  ffi.C.__lua_draw_debug_line(a, b, color, thickness)
end


function update()
end

-- local ecs = require 'ren.ecs'
-- local e = ecs.spawn("LazerBeam")
-- local other = ecs.spawn("OtherEntity")
-- 
-- -- local BeamLines = ecs.component "BeamLines"
-- ren.struct("BeamLines", [[
--     u32 line_index;
--     vec3 pos;
--     u32 line_count;
--     u32 line_progress;
-- ]])
-- 
-- local q = ecs.query.new(Transform)
-- -- local q = ecs.query.new(BeamLines, Transform)
-- print('query:', q)
-- 
-- ecs.query.run(q)
-- 
-- BeamLines.set(e, {
--   line_count = 32,
--   line_progress = 0,
--   line_index = 0
-- })
-- 
-- Transform.set(e, {})
-- 
-- local function rand()
--   return math.random() * 2 - 1
-- end
-- 
-- local function randomvec3()
--   local scale = 1
--   return vec3(rand() * scale, rand() * scale, rand() * scale)
-- end
-- 
-- local function jitter(scale)
--   return vec3(rand() * scale, rand() * scale, rand() * scale)
-- end
-- 
-- local perf = require 'ren.perf'
-- 
-- function update_old()
--   local v = vec2(fps, delta_time)
--   -- local count = 0
--   -- local dur = perf.duration(function()
--   --   ecs.query.map(q, function(it)
--   --     local ts = ffi.cast("Transform*", ffi.C.ecs_field_w_size(it, ffi.sizeof("Transform"), 0))
--   --     for i = 0, it.count - 1 do
--   --       count = count + 1
--   --       local entity = it.entities[i]
--   --       local t = ts[i]
--   --       t.pos = t.pos + jitter(0.01)
--   --       -- debug_line(vec3.zero, t.pos, vec3(1, 0, 0), 0.1)
--   --     end
--   --   end)
--   -- end)
-- 
--   -- ecs.query.map(q, function(it)
--   --   local bs = ffi.cast("BeamLines*", ffi.C.ecs_field_w_size(it, ffi.sizeof("BeamLines"), 0))
--   --   local ts = ffi.cast("Transform*", ffi.C.ecs_field_w_size(it, ffi.sizeof("Transform"), 1))
--   --   for i = 0, it.count - 1 do
--   --     local entity = it.entities[i]
-- 
--   --     local beam = bs[i]
--   --     local pos = ts[i].pos
-- 
--   --     ts[i].pos = pos + jitter(0.01)
-- 
--   --     local start = pos
-- 
--   --     if beam.line_progress == beam.line_count then
--   --       beam.line_progress = 0
--   --       beam.line_index = beam.line_index + 1
--   --     end
-- 
--   --     -- set the seed
--   --     math.randomseed(beam.line_index)
-- 
--   --     local count = beam.line_count
--   --     local color = vec3(1, 1, 1)
--   --     for i = 1, beam.line_progress do
--   --       local next = start + randomvec3()
--   --       debug_line(start, next, color, 0.1)
--   --       start = next
--   --     end
--   --     beam.line_progress = beam.line_progress + 1
--   --   end
--   -- end)
-- 
--   -- print("Updated entities count:", count, "in", dur * 1000, "ms")
-- end