local os = require 'os'

local M = {}

function M.duration(fn)
  local start = os.clock()
  local ret_value = fn()
  local finish = os.clock()

  return finish - start, ret_value
end


return M