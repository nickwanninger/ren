local ffi = require 'ffi'

ffi.cdef [[
    ecs_world_t *__ren_get_world();
]]

--- Get the ren world.
local function world()
  return ffi.C.__ren_get_world()
end

return world
