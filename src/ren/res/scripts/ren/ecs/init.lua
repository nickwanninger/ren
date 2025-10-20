return {
  -- Spawn an entity
  spawn = require 'ren.ecs.spawn',

  -- Get the world.
  world = require 'ren.ecs.world',

  -- Define a component which holds a lua value.
  component = require 'ren.ecs.component',

  -- Query interface
  query = require 'ren.ecs.query'
}
