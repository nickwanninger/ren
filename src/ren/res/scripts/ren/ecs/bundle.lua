-- A bundle is a collection of component values that can be added to an entity at once.
-- You can think of it as a 'prefab' of components.
function bundle(...)
    local component_values = {...}

    local info = {
        __is_bundle = true,
        components = component_values
    }

    function info:apply(e)
        for _, comp in ipairs(self.components) do
            comp.set(e, comp())
        end
    end

    return info
end

return bundle
