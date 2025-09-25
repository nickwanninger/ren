print[[

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

local ffi = require("ffi")

-- Load up Fennel, the lisp that we use for scripting.
-- The main reason we use fennel is that it features
-- lisp macros.
_G.fnl = require('assets.fennel')
local fennel = _G.fnl.install({uesMetadata=true}).dofile("assets/test.fnl")

