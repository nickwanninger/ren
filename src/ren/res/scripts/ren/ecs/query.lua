require 'ren.ecs.ffidef'
local ffi = require 'ffi'

local query = {}
local ecs_world = require 'ren.ecs.world'

ffi.cdef [[
ecs_query_t *__ren_ecs_query_from_expr(const char *expr);
]]

-- For now, we only care about "All" queries. Later on, we will come up with something more fancy.

-- setup a :run method on ecs_query_t
local query_mt = {
  __index = {
    run = function(self)
      local world = ecs_world()

      local it = ffi.C.ecs_query_iter(world, self)
      print('iterator:', it)
      while ffi.C.ecs_query_next(it) do
        print("Got iteration with count:", it.count)
      end
    end
  }
}
ffi.metatype('ecs_query_t', query_mt)

local query_from_expr = ffi.C.__ren_ecs_query_from_expr

local function to_expr(c)
  -- if c is already a string, return it.
  if type(c) == 'string' then
    return c
  end

  -- if you pass nil, match wildcard. This is probably not a good idea, but lets go with it.
  if c == nil then
    return '*'
  end

  -- otherwise, assume it is a component and return its name.
  -- TODO: error mechanism!
  return c.name
end

-- used for arguments to query.all, if you want to specify 
function query.RO(c)
  return '[in] ' .. to_expr(c)
end

function query.RW(c)
  return '[inout] ' .. to_expr(c)
end

function query.NONE(c)
  return '[none] ' .. to_expr(c)
end

function query.FILTER(c)
  return '[filter] ' .. to_expr(c)
end

function query.OR(c1, c2)
  return to_expr(c1) .. ' || ' .. to_expr(c2)
end

query.ANY = '*'

function query.PAIR(c1, c2)
  return '(' .. to_expr(c1) .. ', ' .. to_expr(c2) .. ')'
end

function query.new(...)

  local expr_parts = {}

  for i, c in ipairs({...}) do
    table.insert(expr_parts, to_expr(c))
  end

  local expr = table.concat(expr_parts, ', ')

  print('query expression:', expr)

  return query_from_expr(expr)
end

function query.run(q)
  local world = ecs_world()

  local it = ffi.C.ecs_query_iter(world, q)
  print('iterator:', it)
  while ffi.C.ecs_query_next(it) do
    print("Got iteration with count:", it.count)
  end
end

function query.map(q, fn)
  local world = ecs_world()

  local it = ffi.C.ecs_query_iter(world, q)
  while ffi.C.ecs_query_next(it) do
    fn(it)
  end
end

function query.iter()
  local world = ecs_world()

  local it = ffi.C.ecs_query_iter(world, q)
  return it
end

return query
