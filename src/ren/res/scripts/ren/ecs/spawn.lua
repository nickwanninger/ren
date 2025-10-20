
local ffi = require 'ffi'

local ecs_world = require 'ren.ecs.world'




return function(name)
  local world = ecs_world()
  local e = ffi.C.ecs_new(world)

  if name ~= nil then
    ffi.C.ecs_set_name(world, e, name)
  end

  return e
end