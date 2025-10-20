local ffi = require 'ffi'
local lua_comp = require 'ren_lua_component'

-- define the interface to define new Lua components in the ECS
ffi.cdef [[
ecs_entity_t __ren_lua_component_create(const char *name);
]]

--- Define a component in the ECS.
--- In the lua side, a component is a lua value. use `ecs.struct` to define a struct
--- component (C/C++ FFI value.)
local function component(name)

    local component_name = 'ren.lua.' .. name
    local cid = ffi.C.__ren_lua_component_create(component_name)

    local info = {
        -- The name of the entity.
        name = component_name,
        -- The ID of the component in the ECS world.
        cid = cid
    }


    local component = setmetatable(info, {
        __index = {
            -- Add this component to an entity with an initial value.
            -- component.add(e, initial_value)
            set = function(entity, initial_value)
                lua_comp.write(entity, cid, initial_value)
            end,

            -- Remove this component from an entity.
            -- component.remove(e)
            remove = function(entity)
                lua_comp.remove(entity, cid)
            end,

            -- Get a mutable reference to the component's current state on an entity
            -- component.get_mut(e) -> value or nil
            get_mut = function(entity)
                return lua_comp.read(entity, cid)
            end,

            -- Get a copy of the component's current state on an entity
            -- component.get(e) -> value or nil
            get = function(entity)
                return lua_comp.read(entity, cid)
            end,

            -- Check if an entity has this component.
            -- component.on(e) -> bool
            on = function(entity)
                return lua_comp.has(entity, cid)
            end
            
        }
    })

    return component

end

return component
