#pragma once


namespace ren {

  template <typename T, typename S>
  class Watcher : public std::enable_shared_from_this<Watcher<T, S>> {
   public:
    using State = S;
    using Event = T;

    virtual ~Watcher() = default;

    virtual void onEvent(State &s, Event &e) = 0;
  };
}  // namespace ren