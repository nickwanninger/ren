--- This file bootstraps the lua state for the REN interface.
--- I do some things here that usually, lua programmers would hate (such as populating _G).
-- Expose lua fun (functional programming library) globally.
require 'fun'()
-- expose ren globally. It is the core of the engine, and requiring it is needed everywhere.
_G.ren = require 'ren'
