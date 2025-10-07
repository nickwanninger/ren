local M = {}

local ffi = require 'ffi'
local reflect = require "assets.reflect"

ffi.cdef(require 'assets.flecs_cdef')
ffi.cdef[[
    ecs_world_t *__ren_get_world();
    ecs_entity_t __ren_register_component(const char *name, size_t size, size_t alignment, const char *desc);
    ecs_entity_t __ren_register_lua_component(const char *name);
]]

M.world = function()
    return ffi.C.__ren_get_world()
end
local dump = require 'jit.dump'
dump.start('v')

local function struct_tostring(self)
    local ref = reflect.typeof(self)

    -- start out empty
    local output = ""

    if ref.what == 'ref' then
        -- This element is a struct.
        ref = ref.element_type
        output = ref.name
    elseif ref.what == 'ptr' then
        -- pointer type (can be nil)
        ref = ref.element_type
        if self == nil then
            return "nil"
        end
        output = "&" .. ref.name
    else
        -- Regular value (not a struct field, but top-level)
        output = ref.name
    end

    -- finally, construct the struct portion of the output.
    output = output .. "({"
    local first = true
    for refct in ref:members() do
        output = output .. (first and "" or ",") .. refct.name .. "=" .. tostring(self[refct.name])
        first = false;
    end
    output = output .. "})"
    return output
end

-- A struct is a component type in the ECS world backed by an FFI struct. As such, it is
-- restricted to only having fields which are FFI-compatible. You also have to provide the
-- fields as a string (C syntax) so we can construct the FFI struct and register it with flecs.
function M.struct(name, fields, user_methods, user_metatype)

    -- remove c-style comments from fields
    fields = fields:gsub("/%*.-%*/", "")
    -- remove c++-style comments from fields
    fields = fields:gsub("//.-\n", "\n")

    local cdef = string.format([[
        typedef struct %s {
            %s
        } %s;
    ]], name, fields, name)

    ffi.cdef(cdef)

    local component_id = ffi.C.__ren_register_component("ren::comp::" .. name, ffi.sizeof(name), ffi.alignof(name),
        "{ " .. fields .. " }")

    local metatype = {
        __index = user_methods,
        __tostring = struct_tostring
    }

    if user_metatype ~= nil then
        for k, v in pairs(user_metatype) do
            metatype[k] = v
        end
    end

    -- Set the metatype for these FFI classes. This should allow us to do things like 
    ffi.metatype(name, metatype)

    -- The value we return here is actually a table which has a __call method. This might not get
    -- optimized that well, but we'll see.
    local info = {
        -- The name of the struct
        name = name,
        -- The C definition of the struct as the user typed.
        fields = fields,
        -- The ID of the component in the ECS world.
        cid = component_id,
        -- The size of the struct in bytes.
        size = ffi.sizeof(name)
    }

    print("Defined struct " .. name .. " size " .. info.size)
    local thestruct = setmetatable(info, {
        -- call is the constructor
        __call = function(_, ...)
            return ffi.new(name, ...)
        end,
        __index = {
            set = function(entity, ...)
                return M.set(entity, info, ...)
            end,
            get_mut = function(entity)
                return ffi.cast(info.name .. "*", ffi.C.ecs_get_id(M.world(), entity, info.cid))
            end,
            get = function(entity)
                local comp = info.get_mut(entity)
                -- return a copy of the component
                if comp == nil then
                    return nil
                end
                local copy = ffi.new(name)
                ffi.copy(copy, comp, info.size)
                return copy
            end
        }
    })

    -- The names of structs in the FFI are in a global C namespace. As a result,
    -- we will put the struct in a global namespace as well to mirror that (If
    -- something is a vec3, just use vec3()).  This might change later, though.
    _G[name] = thestruct

    return thestruct
end

function M.set(entity, component, ...)
    return ffi.C.ecs_set_id(M.world(), entity, component.cid, component.size, component(...))
end

function M.get_mut(entity, component)
end

function M.spawn(name)
    local world = M.world()
    e = ffi.C.ecs_new(world)
    if name ~= nil then
        ffi.C.ecs_set_name(world, e, name)
    end

    return e
end

-- A component is much like a struct, but it is backed by any arbitrary Lua value.
function M.component(name)
    local component_id = ffi.C.__ren_register_lua_component("ren::lua_comp::" .. name)
    print(name, component_id)
    return component_id
end

return M
