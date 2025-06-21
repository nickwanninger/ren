#pragma once

#include <ren/types.h>
#include <SDL2/SDL.h>

namespace ren {


  enum class EventType : u8 {
    None = 0,
    SDLEvent,
  };

  struct Event {
    bool handled = false;
    EventType type = EventType::None;

    union {
      SDL_Event sdlEvent;  // SDL event
    };


    Event(SDL_Event e)
        : type(EventType::SDLEvent)
        , sdlEvent(e) {}
  };
}  // namespace ren