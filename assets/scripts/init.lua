print [[

    '########::'########:'##::: ##:
     ##.... ##: ##.....:: ###:: ##:
     ##:::: ##: ##::::::: ####: ##:
     ########:: ######::: ## ## ##:
     ##.. ##::: ##...:::: ##. ####:
     ##::. ##:: ##::::::: ##:. ###:
     ##:::. ##: ########: ##::. ##:
    ..:::::..::........::..::::..::

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
    end
})

ren.struct("vec3", [[
    float x;
    float y;
    float z;
]], {}, {
    __add = function(a, b)
        if type(b) == "number" then
            return vec3(a.x + b, a.y + b, a.z + b)
        end
        return vec3(a.x + b.x, a.y + b.y, a.z + b.z)
    end,
})

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




e = ren.spawn("LazerBeam")

ren.struct("BeamLines", [[
    u32 line_index;
    vec3 pos;
    u32 line_count;
    u32 line_progress;
]])


BeamLines.set(e, {
    pos = vec3(0, 0, 0),
    line_count = 500,
    line_progress = 0,
    line_index = 0,
})








local ren_comps = require 'ren_components'

print('now', ren_comps.test(e))
luacomp = ren.component("MyLuaComp")

function update()
    local beam = BeamLines.get_mut(e)
    local start = beam.pos

    if beam.line_progress == beam.line_count then
        beam.line_progress = 0
        beam.line_index = beam.line_index + 1
    end

    -- set the seed
    math.randomseed(beam.line_index)

    local function rand()
        return math.random() * 2 - 1
    end

    local function randomvec3()
        local scale = 1
        return vec3(rand() * scale, rand() * scale, rand() * scale)
    end

    local count = beam.line_count
    local color = vec3(1, 1, 1)
    for i = 1, beam.line_progress do
        local next = start + randomvec3()
        debug_line(start, next, color, 0.1)
        start = next
    end
    beam.line_progress = beam.line_progress + 1
end








